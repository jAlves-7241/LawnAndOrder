#include "i18n.h"
#include "Storage.h"
#include <Preferences.h>
#include <stddef.h>
#include "log.h"


// ── NVS namespace (max 15 chars) ──────────────────────────
static const char* NVS_NS       = "rega";
static const char* KEY_CFG      = "cfg";
static const char* KEY_HBLOB    = "hblob";
static const char* KEY_SCHED_TR = "sched_tr";
static const char* KEY_RECV     = "recv";
static const size_t HIST_CACHE_MAX_BYTES = 256;

struct __attribute__((packed)) HistoryNvsHeader {
    uint8_t  version;
    uint16_t lineCount;
    uint32_t fileSize;
    uint16_t dataSize;
};

struct __attribute__((packed)) SchedTriggersBlob {
    uint8_t  version;
    uint8_t  count;
    uint32_t ts[MAX_SLOTS_PER_MODE];
    uint16_t crc16;
};

static Preferences prefs;
Storage storage;

// ─────────────────────────────────────────────────────────
void Storage::begin() {
    // Open in read-write mode; creates namespace if it doesn't exist.
    _ready = prefs.begin(NVS_NS, false);
    if (!_ready) {
        LOG_E(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_NS_FAIL);
    } else {
        LOG_D(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_READY);
    }
}

uint16_t Storage::_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// AppConfigBlob is a packed wire/storage format. Validate bool bytes before
// reading them as bools so malformed NVS/import data cannot create invalid
// C++ bool representations.
static bool _blobHasValidBooleans(const AppConfigBlob& blob) {
    static_assert(sizeof(bool) == 1, "AppConfigBlob expects 1-byte bools");
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&blob);

    if (raw[offsetof(AppConfigBlob, auto_dst)] > 1 ||
        raw[offsetof(AppConfigBlob, setup_done)] > 1) {
        return false;
    }

    const size_t zonesOffset = offsetof(AppConfigBlob, zones);
    for (size_t i = 0; i < NUM_ZONES; ++i) {
        const size_t zoneOffset = zonesOffset + i * sizeof(blob.zones[0]);
        if (raw[zoneOffset] > 1) return false;
    }
    return true;
}

static bool _validateImportBlob(const AppConfigBlob& blob) {
    if (blob.mode >= (uint8_t)AppMode::_COUNT) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_MODE, (unsigned)blob.mode);
        return false;
    }
    if (blob.backlight_timeout_ms != BACKLIGHT_TIMEOUT_NEVER &&
        blob.backlight_timeout_ms < 30000UL) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_FIELD, TXT_IMP_FLD_BACKLIGHT);
        return false;
    }
    if (blob.suspended_until > 4102444799UL) { // > 2099-12-31 23:59:59 UTC
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_FIELD, TXT_IMP_FLD_SUSP_END);
        return false;
    }
    if (blob.custom_ref_day != 0xFFFFFFFFUL && blob.custom_ref_day > 47482UL) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_FIELD, TXT_IMP_FLD_REF_DAY);
        return false;
    }
    for (int i = 0; i < NUM_ZONES; i++) {
        if (blob.zones[i].duration_min > 20 ||
            (blob.zones[i].enabled && blob.zones[i].duration_min == 0)) {
            LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_FIELD, TXT_IMP_FLD_ZONE_DUR);
            return false;
        }
    }
    if (blob.custom_interval_days == 0 || blob.custom_interval_days > 14) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_FIELD, TXT_IMP_FLD_INTERVAL);
        return false;
    }
    if (blob.custom_slot_count == 0 || blob.custom_slot_count > MAX_SLOTS_PER_MODE) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_FIELD, TXT_IMP_FLD_SLOTS);
        return false;
    }
    for (int i = 0; i < MAX_SLOTS_PER_MODE; i++) {
        if (blob.custom_slots[i].hour > 23 || blob.custom_slots[i].minute > 59) {
            LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_FIELD, TXT_IMP_FLD_TIME);
            return false;
        }
    }
    return true;
}

static int _hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}


// ─────────────────────────────────────────────────────────
bool Storage::load() {
    if (!_ready) return false;

    AppConfigBlob blob = {};
    size_t readBytes = prefs.getBytes(KEY_CFG, &blob, sizeof(blob));
    if (readBytes == 0) {
        LOG_I(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_EMPTY);
        return false;
    }
    if (readBytes != sizeof(blob)) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_SIZE_MISMATCH);
        return false;
    }
    if (blob.version != NVS_VERSION) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_VER_MISMATCH,
              (unsigned)blob.version, (unsigned)NVS_VERSION);
        return false;
    }
    if (!_blobHasValidBooleans(blob)) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_BOOL_INV);
        return false;
    }

    uint16_t expectedCrc = _crc16(reinterpret_cast<const uint8_t*>(&blob),
                                  offsetof(AppConfigBlob, crc16));
    if (blob.crc16 != expectedCrc) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_CRC_FAIL, TXT_LOG_NVS_PART_CFG);
        return false;
    }

    if (!_blobToState(blob)) {
        return false;
    }

    LOG_I(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_LOADED,
          serialModeName(gState.mode),
          gState.suspended ? TXT_SER_YES : TXT_SER_NO);
    return true;
}

// ─────────────────────────────────────────────────────────
bool Storage::save() {
    if (!_ready) {
        LOG_E(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_WRITE_FAIL, TXT_LOG_NVS_PART_CFG);
        return false;
    }

    for (int i = 0; i < NUM_ZONES; i++) {
        if (gState.zones[i].duration_min == 0) {
            gState.zones[i].enabled = false;
        }
    }

    AppConfigBlob blob = {};
    _stateToBlob(blob);

    // Read current to avoid unnecessary write
    AppConfigBlob current = {};
    if (prefs.getBytes(KEY_CFG, &current, sizeof(current)) == sizeof(current) &&
        memcmp(&blob, &current, sizeof(blob)) == 0) {
        return true;
    }

    size_t written = prefs.putBytes(KEY_CFG, &blob, sizeof(blob));
    if (written != sizeof(blob)) {
        LOG_E(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_WRITE_FAIL, TXT_LOG_NVS_PART_CFG);
        return false;
    }

    LOG_D(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_UPDATED, serialModeName(gState.mode));
    return true;
}


// ─────────────────────────────────────────────────────────
bool Storage::clear() {
    if (!_ready) {
        LOG_E(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_NS_FAIL);
        return false;
    }
    if (!prefs.clear()) {   // erases all keys in the namespace
        LOG_E(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_WRITE_FAIL, TXT_LOG_NVS_PART_CFG);
        return false;
    }
    LOG_I(TXT_LOG_TAG_STORAGE, TXT_LOG_MEM_CLEARED);
    return true;
}

// ─────────────────────────────────────────────────────────
bool Storage::loadHistoryCache(void* dest, size_t size, uint16_t& lineCount, uint32_t* outFileSize) {
    lineCount = 0;
    if (outFileSize) *outFileSize = 0;
    if (!_ready || !dest || size == 0 || size > HIST_CACHE_MAX_BYTES) return false;

    const size_t expectedLen = sizeof(HistoryNvsHeader) + size + sizeof(uint16_t);
    uint8_t buf[sizeof(HistoryNvsHeader) + HIST_CACHE_MAX_BYTES + sizeof(uint16_t)];
    size_t readBytes = prefs.getBytes(KEY_HBLOB, buf, sizeof(buf));
    if (readBytes == 0) return false;
    if (readBytes != expectedLen) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_HIST_INV);
        return false;
    }

    HistoryNvsHeader hdr = {};
    memcpy(&hdr, buf, sizeof(hdr));
    if (hdr.version != NVS_VERSION || hdr.dataSize != size) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_HIST_INV);
        return false;
    }

    uint16_t storedCrc = 0;
    memcpy(&storedCrc, buf + sizeof(hdr) + size, sizeof(storedCrc));
    uint16_t calcCrc = _crc16(buf, sizeof(hdr) + size);
    if (storedCrc != calcCrc) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_CRC_FAIL, TXT_LOG_NVS_PART_HIST);
        return false;
    }

    memcpy(dest, buf + sizeof(hdr), size);
    lineCount = hdr.lineCount;
    if (outFileSize) *outFileSize = hdr.fileSize;
    return true;
}

void Storage::saveHistoryCache(const void* src, size_t size, uint16_t lineCount, uint32_t fileSize) {
    if (!_ready || !src || size == 0 || size > HIST_CACHE_MAX_BYTES) return;

    const size_t totalLen = sizeof(HistoryNvsHeader) + size + sizeof(uint16_t);
    uint8_t buf[sizeof(HistoryNvsHeader) + HIST_CACHE_MAX_BYTES + sizeof(uint16_t)];
    HistoryNvsHeader hdr = {};
    hdr.version   = NVS_VERSION;
    hdr.lineCount = lineCount;
    hdr.fileSize  = fileSize;
    hdr.dataSize  = (uint16_t)size;

    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), src, size);
    uint16_t crc = _crc16(buf, sizeof(hdr) + size);
    memcpy(buf + sizeof(hdr) + size, &crc, sizeof(crc));

    uint8_t existing[sizeof(HistoryNvsHeader) + HIST_CACHE_MAX_BYTES + sizeof(uint16_t)];
    if (prefs.getBytes(KEY_HBLOB, existing, sizeof(existing)) == totalLen &&
        memcmp(buf, existing, totalLen) == 0) {
        return;
    }

    if (prefs.putBytes(KEY_HBLOB, buf, totalLen) != totalLen) {
        LOG_E(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_WRITE_FAIL, TXT_LOG_NVS_PART_HIST);
    }
}

bool Storage::hasHistoryData() {
    if (!_ready) return false;
    uint8_t buf[sizeof(HistoryNvsHeader) + HIST_CACHE_MAX_BYTES + sizeof(uint16_t)];
    size_t readBytes = prefs.getBytes(KEY_HBLOB, buf, sizeof(buf));
    if (readBytes < sizeof(HistoryNvsHeader) + sizeof(uint16_t)) return false;

    HistoryNvsHeader hdr = {};
    memcpy(&hdr, buf, sizeof(hdr));
    if (hdr.version != NVS_VERSION || hdr.dataSize > HIST_CACHE_MAX_BYTES) return false;
    size_t expectedLen = sizeof(HistoryNvsHeader) + hdr.dataSize + sizeof(uint16_t);
    if (readBytes != expectedLen) return false;

    uint16_t storedCrc = 0;
    memcpy(&storedCrc, buf + sizeof(hdr) + hdr.dataSize, sizeof(storedCrc));
    if (storedCrc != _crc16(buf, sizeof(hdr) + hdr.dataSize)) return false;

    return (hdr.lineCount > 0 || hdr.fileSize > 0);
}

// ─────────────────────────────────────────────────────────
bool Storage::loadLastTriggers(uint32_t* out, size_t count) {
    if (!out || count != MAX_SLOTS_PER_MODE) return false;
    for (size_t i = 0; i < count; i++) out[i] = 0;
    if (!_ready) return false;

    SchedTriggersBlob blob = {};
    size_t readBytes = prefs.getBytes(KEY_SCHED_TR, &blob, sizeof(blob));
    if (readBytes == 0) return false;
    if (readBytes != sizeof(blob) || blob.version != NVS_VERSION ||
        blob.count != MAX_SLOTS_PER_MODE) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_TRIG_CORRUPT);
        return false;
    }

    uint16_t expectedCrc = _crc16(reinterpret_cast<const uint8_t*>(&blob),
                                  offsetof(SchedTriggersBlob, crc16));
    if (blob.crc16 != expectedCrc) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_CRC_FAIL, TXT_LOG_NVS_PART_TRIG);
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        out[i] = blob.ts[i];
    }
    return true;
}

void Storage::saveLastTriggers(const uint32_t* in, size_t count) {
    if (!_ready || !in || count != MAX_SLOTS_PER_MODE) return;

    SchedTriggersBlob blob = {};
    blob.version = NVS_VERSION;
    blob.count   = MAX_SLOTS_PER_MODE;
    for (size_t i = 0; i < count; i++) {
        blob.ts[i] = in[i];
    }
    blob.crc16 = _crc16(reinterpret_cast<const uint8_t*>(&blob),
                        offsetof(SchedTriggersBlob, crc16));

    SchedTriggersBlob current = {};
    if (prefs.getBytes(KEY_SCHED_TR, &current, sizeof(current)) == sizeof(current) &&
        memcmp(&blob, &current, sizeof(blob)) == 0) {
        return;
    }

    if (prefs.putBytes(KEY_SCHED_TR, &blob, sizeof(blob)) != sizeof(blob)) {
        LOG_E(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_WRITE_FAIL, TXT_LOG_NVS_PART_TRIG);
    }
}

// ─────────────────────────────────────────────────────────
bool Storage::loadRecoveryState(RecoveryState& rs) {
    static_assert(sizeof(RecoveryState) == 36, "RecoveryState must be 36 bytes");
    static_assert(offsetof(RecoveryState, crc16) == 34,
                  "RecoveryState must have no padding before crc16");
    if (!_ready) return false;
    RecoveryState tmp = {};
    size_t readBytes = prefs.getBytes(KEY_RECV, &tmp, sizeof(tmp));
    if (readBytes == 0) return false;
    if (readBytes != sizeof(tmp)) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_RECV_CORRUPT, TXT_NVS_RSN_SIZE);
        return false;
    }
    if (tmp.version != NVS_VERSION) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_RECV_CORRUPT, TXT_NVS_RSN_VER);
        return false;
    }
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&tmp);
    if (raw[offsetof(RecoveryState, active)] > 1) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_RECV_CORRUPT, TXT_NVS_RSN_BOOL);
        return false;
    }
    uint16_t expectedCrc = _crc16(raw, offsetof(RecoveryState, crc16));
    if (tmp.crc16 != expectedCrc) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_CRC_FAIL, TXT_LOG_NVS_PART_RECV);
        return false;
    }
    rs = tmp;
    return true;
}

void Storage::saveRecoveryState(const RecoveryState& rs) {
    if (!_ready) return;
    RecoveryState copy;
    memset(&copy, 0, sizeof(RecoveryState));
    copy = rs;
    copy.version = NVS_VERSION;
    copy.crc16 = _crc16(reinterpret_cast<const uint8_t*>(&copy),
                        offsetof(RecoveryState, crc16));
    
    RecoveryState current = {};
    if (prefs.getBytes(KEY_RECV, &current, sizeof(current)) != sizeof(current) ||
        memcmp(&copy, &current, sizeof(copy)) != 0) {
        if (prefs.putBytes(KEY_RECV, &copy, sizeof(copy)) != sizeof(copy)) {
            LOG_E(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_WRITE_FAIL, TXT_LOG_NVS_PART_RECV);
        }
    }
}

// ─────────────────────────────────────────────────────────
void Storage::exportConfigHex(char* hexOut, size_t maxLen) {
    if (!hexOut || maxLen == 0) return;
    if (maxLen < sizeof(AppConfigBlob) * 2 + 1) {
        hexOut[0] = '\0';
        return;
    }

    AppConfigBlob blob = {};
    _stateToBlob(blob);

    // Convert to hex string
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&blob);
    static const char hexChars[] = "0123456789ABCDEF";
    for (size_t i = 0; i < sizeof(AppConfigBlob); i++) {
        hexOut[i * 2]     = hexChars[ptr[i] >> 4];
        hexOut[i * 2 + 1] = hexChars[ptr[i] & 0x0F];
    }
    hexOut[sizeof(AppConfigBlob) * 2] = '\0';
}

// ─────────────────────────────────────────────────────────
bool Storage::importConfigHex(const char* hexIn) {
    if (!hexIn) return false;
    if (gState.watering.active) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_ACTIVE);
        return false;
    }

    size_t len = strlen(hexIn);
    if (len != sizeof(AppConfigBlob) * 2) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_LEN,
              (unsigned)len, (unsigned)(sizeof(AppConfigBlob) * 2));
        return false;
    }

    AppConfigBlob blob = {};
    uint8_t* ptr = reinterpret_cast<uint8_t*>(&blob);
    for (size_t i = 0; i < sizeof(AppConfigBlob); i++) {
        const int hi = _hexNibble(hexIn[i * 2]);
        const int lo = _hexNibble(hexIn[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_BAD_HEX,
                  (unsigned)((hi < 0) ? (i * 2) : (i * 2 + 1)));
            return false;
        }
        ptr[i] = (uint8_t)((hi << 4) | lo);
    }

    if (blob.version != NVS_VERSION) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_VER,
              (unsigned)blob.version, (unsigned)NVS_VERSION);
        return false;
    }

    if (!_blobHasValidBooleans(blob)) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_BAD_BOOL);
        return false;
    }

    uint16_t expectedCrc = _crc16(reinterpret_cast<const uint8_t*>(&blob),
                                  offsetof(AppConfigBlob, crc16));
    if (blob.crc16 != expectedCrc) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_CRC_FAIL, TXT_LOG_NVS_PART_IMP);
        return false;
    }

    if (!_validateImportBlob(blob)) {
        return false;
    }

    const AppState previousState = gState;
    const uint8_t customMode = (uint8_t)AppMode::PERSONALIZADO;
    const ModeSchedule previousCustomSchedule = MODE_SCHEDULES[customMode];

    if (!_blobToState(blob)) {
        return false;
    }

    if (!save()) {
        gState = previousState;
        MODE_SCHEDULES[customMode] = previousCustomSchedule;
        return false;
    }

    LOG_I(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_OK, serialModeName(gState.mode));
    return true;
}


// ─────────────────────────────────────────────────────────
void Storage::_stateToBlob(AppConfigBlob& blob) {
    memset(&blob, 0, sizeof(blob));
    blob.version              = NVS_VERSION;
    blob.mode                 = (uint8_t)gState.mode;
    blob.backlight_timeout_ms = gState.backlight_timeout_ms;
    blob.suspended_until      = gState.suspended_until;
    blob.custom_ref_day       = gState.custom_ref_day;
    blob.auto_dst             = gState.auto_dst;
    blob.setup_done           = gState.setup_done;

    for (int i = 0; i < NUM_ZONES; i++) {
        blob.zones[i].enabled      = gState.zones[i].enabled && (gState.zones[i].duration_min > 0);
        blob.zones[i].duration_min = gState.zones[i].duration_min;
    }

    const ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    blob.custom_interval_days = cs.interval_days;
    blob.custom_slot_count    = cs.slot_count;
    for (int i = 0; i < MAX_SLOTS_PER_MODE; i++) {
        blob.custom_slots[i].hour   = cs.slots[i].hour;
        blob.custom_slots[i].minute = cs.slots[i].minute;
    }

    blob.crc16 = _crc16(reinterpret_cast<const uint8_t*>(&blob),
                        offsetof(AppConfigBlob, crc16));
}

bool Storage::_blobToState(const AppConfigBlob& blob) {
    if (blob.mode >= (uint8_t)AppMode::_COUNT) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_MODE, (unsigned)blob.mode);
        return false;
    }

    const AppState previousState = gState;
    const uint8_t customMode = (uint8_t)AppMode::PERSONALIZADO;
    const ModeSchedule previousCustomSchedule = MODE_SCHEDULES[customMode];

    gState.mode = (AppMode)blob.mode;

    if (blob.backlight_timeout_ms != BACKLIGHT_TIMEOUT_NEVER &&
        blob.backlight_timeout_ms < 30000UL) {
        gState.backlight_timeout_ms = 120000UL; // Fallback para 2 minutos
    } else {
        gState.backlight_timeout_ms = blob.backlight_timeout_ms;
    }
    gState.suspended_until = blob.suspended_until;
    gState.custom_ref_day = blob.custom_ref_day;
    gState.auto_dst = !!blob.auto_dst;
    gState.setup_done = !!blob.setup_done;
    gState.suspended = (gState.suspended_until > 0);

    for (int i = 0; i < NUM_ZONES; i++) {
        gState.zones[i].enabled = !!blob.zones[i].enabled;
        gState.zones[i].duration_min =
            (blob.zones[i].duration_min <= 20) ? blob.zones[i].duration_min : 8;
        if (gState.zones[i].duration_min == 0) {
            gState.zones[i].enabled = false;
        }
    }

    ModeSchedule& cs = MODE_SCHEDULES[customMode];
    cs.interval_days = blob.custom_interval_days;
    if (cs.interval_days == 0 || cs.interval_days > 14) cs.interval_days = 1;

    cs.slot_count = blob.custom_slot_count;
    if (cs.slot_count == 0 || cs.slot_count > MAX_SLOTS_PER_MODE) cs.slot_count = 1;

    for (int i = 0; i < MAX_SLOTS_PER_MODE; i++) {
        cs.slots[i].hour = (blob.custom_slots[i].hour > 23) ? 0 : blob.custom_slots[i].hour;
        cs.slots[i].minute = (blob.custom_slots[i].minute > 59) ? 0 : blob.custom_slots[i].minute;
    }

    // Keep active slots in chronological order.
    for (int i = 0; i < cs.slot_count - 1; i++) {
        for (int j = i + 1; j < cs.slot_count; j++) {
            uint16_t mins_i = cs.slots[i].hour * 60 + cs.slots[i].minute;
            uint16_t mins_j = cs.slots[j].hour * 60 + cs.slots[j].minute;
            if (mins_j < mins_i) {
                auto temp = cs.slots[i];
                cs.slots[i] = cs.slots[j];
                cs.slots[j] = temp;
            }
        }
    }

    // Reject schedules whose watering runs could overlap (enforce >= 1 min even if total_dur == 0).
    if (cs.slot_count > 1) {
        uint32_t total_dur = 0;
        uint8_t enabledZones = 0;
        for (int i = 0; i < NUM_ZONES; i++) {
            if (gState.zones[i].enabled) {
                total_dur += gState.zones[i].duration_min;
                enabledZones++;
            }
        }
        // Match the UI check: include one minute per inter-zone transition.
        if (enabledZones > 1) total_dur += enabledZones - 1;
        uint32_t min_spacing = (total_dur > 0) ? total_dur : 1;

        bool hasOverlap = false;
        for (int i = 0; i < cs.slot_count && !hasOverlap; i++) {
            for (int j = i + 1; j < cs.slot_count; j++) {
                uint16_t s1 = cs.slots[i].hour * 60 + cs.slots[i].minute;
                uint16_t s2 = cs.slots[j].hour * 60 + cs.slots[j].minute;
                int16_t diff = (int16_t)s1 - (int16_t)s2;
                if (diff < 0) diff = -diff;
                if (diff > 720) diff = 1440 - diff;
                if ((uint32_t)diff < min_spacing) {
                    hasOverlap = true;
                    break;
                }
            }
        }
        if (hasOverlap) {
            if (gState.mode == AppMode::PERSONALIZADO) {
                LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_OVERLAP);
                gState = previousState;
                MODE_SCHEDULES[customMode] = previousCustomSchedule;
                return false;
            }
            // Dormant custom schedule overlaps current zone durations: respace evenly
            // anchored at slots[0] and keep sorted so user settings are preserved.
            uint16_t baseMins = cs.slots[0].hour * 60 + cs.slots[0].minute;
            uint16_t stepMins = 1440 / cs.slot_count;
            for (int i = 1; i < cs.slot_count; i++) {
                uint16_t nextMins = (baseMins + i * stepMins) % 1440;
                cs.slots[i].hour   = (uint8_t)(nextMins / 60);
                cs.slots[i].minute = (uint8_t)(nextMins % 60);
            }
            for (int i = 0; i < cs.slot_count - 1; i++) {
                for (int j = i + 1; j < cs.slot_count; j++) {
                    uint16_t mins_i = cs.slots[i].hour * 60 + cs.slots[i].minute;
                    uint16_t mins_j = cs.slots[j].hour * 60 + cs.slots[j].minute;
                    if (mins_j < mins_i) {
                        auto temp = cs.slots[i];
                        cs.slots[i] = cs.slots[j];
                        cs.slots[j] = temp;
                    }
                }
            }
            LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_CUSTOM_RESPACED);
        }
    }

    return true;
}
