#include "i18n.h"
#include "Storage.h"
#include <Preferences.h>
#include "log.h"

// ── NVS namespace (max 15 chars) ──────────────────────────
static const char* NVS_NS = "rega";

static Preferences prefs;
Storage storage;

// ─────────────────────────────────────────────────────────
void Storage::begin() {
    // Open in read-write mode; creates namespace if it doesn't exist.
    _ready = prefs.begin(NVS_NS, false);
    if (!_ready) {
        LOG_E("NVS", TXT_LOG_NVS_NS_FAIL);
    } else {
        LOG_I("NVS", TXT_LOG_NVS_READY);
    }
}

static const char* KEY_CFG = "cfg";

// ─────────────────────────────────────────────────────────
bool Storage::load() {
    if (!_ready) return false;

    AppConfigBlob blob = {};
    size_t readBytes = prefs.getBytes(KEY_CFG, &blob, sizeof(blob));
    if (readBytes != sizeof(blob) || blob.version != NVS_VERSION) {
        LOG_W("NVS", TXT_LOG_NVS_BLOB_INV, blob.version);
        return false;
    }

    if (!_blobToState(blob)) {
        LOG_W("NVS", TXT_LOG_NVS_VAL_FAIL);
        return false;
    }

    LOG_I("NVS", TXT_LOG_NVS_LOADED, (uint8_t)gState.mode, gState.suspended);
    return true;
}

// ─────────────────────────────────────────────────────────
void Storage::save() {
    if (!_ready) return;

    AppConfigBlob blob = {};
    _stateToBlob(blob);

    // Read current to avoid unnecessary write
    AppConfigBlob current = {};
    if (prefs.getBytes(KEY_CFG, &current, sizeof(current)) != sizeof(current) ||
        memcmp(&blob, &current, sizeof(blob)) != 0) {
        
        prefs.putBytes(KEY_CFG, &blob, sizeof(blob));
        LOG_I("NVS", TXT_LOG_NVS_UPDATED);
    }
}

// ─────────────────────────────────────────────────────────
void Storage::clear() {
    if (!_ready) return;
    prefs.clear();   // erases all keys in the namespace
    LOG_I("NVS", TXT_LOG_MEM_CLEARED);
}

// ─────────────────────────────────────────────────────────
bool Storage::loadHistoryCache(void* dest, size_t size, uint16_t& lineCount) {
    if (!_ready) { lineCount = 0; return false; }
    size_t readBytes = prefs.getBytes("hcache", dest, size);
    if (readBytes != size) { lineCount = 0; return false; }
    lineCount = prefs.getUShort("hcnt", 0);
    return true;
}

void Storage::saveHistoryCache(const void* src, size_t size, uint16_t lineCount) {
    if (!_ready) return;
    
    uint16_t currentCnt = prefs.getUShort("hcnt", 0);
    if (currentCnt != lineCount) {
        prefs.putUShort("hcnt", lineCount);
    }

    uint8_t currentCache[128];
    if (size <= sizeof(currentCache)) {
        size_t readBytes = prefs.getBytes("hcache", currentCache, size);
        if (readBytes != size || memcmp(src, currentCache, size) != 0) {
            prefs.putBytes("hcache", src, size);
        }
    } else {
        prefs.putBytes("hcache", src, size);
    }
}

// ─────────────────────────────────────────────────────────
bool Storage::loadRecoveryState(RecoveryState& rs) {
    if (!_ready) return false;
    size_t readBytes = prefs.getBytes("recv", &rs, sizeof(rs));
    if (readBytes != sizeof(rs)) return false;
    if (rs.version != NVS_VERSION) return false;
    return true;
}

void Storage::saveRecoveryState(const RecoveryState& rs) {
    if (!_ready) return;
    RecoveryState copy;
    memset(&copy, 0, sizeof(RecoveryState));
    copy = rs;
    copy.version = NVS_VERSION;
    
    RecoveryState current = {};
    if (prefs.getBytes("recv", &current, sizeof(current)) != sizeof(current) ||
        memcmp(&copy, &current, sizeof(copy)) != 0) {
        prefs.putBytes("recv", &copy, sizeof(copy));
    }
}

// ─────────────────────────────────────────────────────────
void Storage::exportConfigHex(char* hexOut, size_t maxLen) {
    AppConfigBlob blob = {};
    _stateToBlob(blob);

    // Convert to hex string
    if (maxLen < sizeof(AppConfigBlob) * 2 + 1) return;
    uint8_t* ptr = (uint8_t*)&blob;
    for (size_t i = 0; i < sizeof(AppConfigBlob); i++) {
        snprintf(hexOut + (i * 2), maxLen - (i * 2), "%02X", ptr[i]);
    }
}

// ─────────────────────────────────────────────────────────
bool Storage::importConfigHex(const char* hexIn) {
    if (!hexIn) return false;
    size_t len = strlen(hexIn);
    if (len != sizeof(AppConfigBlob) * 2) {
        LOG_W("NVS", TXT_LOG_NVS_IMP_INV_LEN, len, sizeof(AppConfigBlob) * 2);
        return false;
    }

    AppConfigBlob blob = {};
    uint8_t* ptr = (uint8_t*)&blob;
    for (size_t i = 0; i < sizeof(AppConfigBlob); i++) {
        char tmp[3] = { hexIn[i * 2], hexIn[i * 2 + 1], '\0' };
        char* endptr;
        ptr[i] = (uint8_t)strtol(tmp, &endptr, 16);
        if (*endptr != '\0') return false;
    }

    if (blob.version != NVS_VERSION) {
        LOG_W("NVS", TXT_LOG_NVS_IMP_INV_VER, blob.version, NVS_VERSION);
        return false;
    }

    if (!_blobToState(blob)) {
        return false;
    }

    LOG_I("NVS", TXT_LOG_NVS_IMP_OK);
    save(); // Grava no NVS flash com memcmp de segurança incorporado
    return true;
}


// ─────────────────────────────────────────────────────────
void Storage::_stateToBlob(AppConfigBlob& blob) {
    blob.version = NVS_VERSION;
    blob.mode    = (uint8_t)gState.mode;
    blob.backlight_timeout_ms = gState.backlight_timeout_ms;
    blob.suspended_until      = gState.suspended_until;
    blob.custom_ref_day       = gState.custom_ref_day;
    blob.auto_dst             = gState.auto_dst;
    blob.setup_done           = gState.setup_done;

    for (int i = 0; i < NUM_ZONES; i++) {
        blob.zones[i].enabled      = gState.zones[i].enabled;
        blob.zones[i].duration_min = gState.zones[i].duration_min;
    }

    const ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    blob.custom_interval_days = cs.interval_days;
    blob.custom_slot_count    = cs.slot_count;
    for (int i = 0; i < MAX_SLOTS_PER_MODE; i++) {
        blob.custom_slots[i].hour   = cs.slots[i].hour;
        blob.custom_slots[i].minute = cs.slots[i].minute;
    }
}

bool Storage::_blobToState(const AppConfigBlob& blob) {
    if (blob.mode >= (uint8_t)AppMode::_COUNT) {
        LOG_W("NVS", TXT_LOG_NVS_IMP_INV_MODE, blob.mode);
        return false;
    }

    gState.mode = (AppMode)blob.mode;

    if (blob.backlight_timeout_ms != BACKLIGHT_TIMEOUT_NEVER && blob.backlight_timeout_ms < 30000UL) {
        gState.backlight_timeout_ms = 120000UL; // Fallback para 2 minutos
    } else {
        gState.backlight_timeout_ms = blob.backlight_timeout_ms;
    }
    gState.suspended_until      = blob.suspended_until;
    gState.custom_ref_day       = blob.custom_ref_day;
    gState.auto_dst             = !!blob.auto_dst;
    gState.setup_done           = !!blob.setup_done;
    gState.suspended            = (gState.suspended_until > 0);

    for (int i = 0; i < NUM_ZONES; i++) {
        gState.zones[i].enabled = !!blob.zones[i].enabled;
        gState.zones[i].duration_min = (blob.zones[i].duration_min <= 20) ? blob.zones[i].duration_min : 8;
    }

    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.interval_days = blob.custom_interval_days;
    if (cs.interval_days == 0 || cs.interval_days > 14) cs.interval_days = 1;
    
    cs.slot_count = blob.custom_slot_count;
    if (cs.slot_count == 0 || cs.slot_count > MAX_SLOTS_PER_MODE) cs.slot_count = 1;

    for (int i = 0; i < MAX_SLOTS_PER_MODE; i++) {
        cs.slots[i].hour   = (blob.custom_slots[i].hour > 23) ? 0 : blob.custom_slots[i].hour;
        cs.slots[i].minute = (blob.custom_slots[i].minute > 59) ? 0 : blob.custom_slots[i].minute;
    }
    
    // Garantir que os slots ativos estão sempre ordenados cronologicamente (Bubble Sort simples)
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
    
    return true;
}
