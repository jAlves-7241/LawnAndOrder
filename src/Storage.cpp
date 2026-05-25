#include "Storage.h"
#include <Preferences.h>
#include "log.h"

// ── NVS namespace (max 15 chars) ──────────────────────────
static const char* NVS_NS = "rega";

struct __attribute__((packed)) AppConfigBlob {
    uint8_t  version;
    uint8_t  mode;
    uint32_t backlight_timeout_ms;
    uint32_t suspended_until;
    uint32_t custom_ref_day;
    bool     auto_dst;
    bool     setup_done;
    
    struct {
        bool    enabled;
        uint8_t duration_min;
    } zones[NUM_ZONES];
    
    uint8_t  custom_interval_days;
    uint8_t  custom_slot_count;
    struct {
        uint8_t hour;
        uint8_t minute;
    } custom_slots[MAX_SLOTS_PER_MODE];
};

static Preferences prefs;
Storage storage;

// ─────────────────────────────────────────────────────────
void Storage::begin() {
    // Open in read-write mode; creates namespace if it doesn't exist.
    _ready = prefs.begin(NVS_NS, false);
    if (!_ready) {
        LOG_E("NVS", "Falha ao abrir namespace");
    } else {
        LOG_I("NVS", "Pronto");
    }
}

static const char* KEY_CFG = "cfg";

// ─────────────────────────────────────────────────────────
bool Storage::load() {
    if (!_ready) return false;

    AppConfigBlob blob;
    size_t readBytes = prefs.getBytes(KEY_CFG, &blob, sizeof(blob));
    if (readBytes != sizeof(blob) || blob.version != NVS_VERSION) {
        LOG_W("NVS", "Blob invalido ou versao incorreta (%d). A usar defaults.", blob.version);
        return false;
    }

    if (blob.mode < (uint8_t)AppMode::_COUNT)
        gState.mode = (AppMode)blob.mode;

    gState.backlight_timeout_ms = blob.backlight_timeout_ms;
    gState.suspended_until      = blob.suspended_until;
    gState.custom_ref_day       = blob.custom_ref_day;
    gState.auto_dst             = blob.auto_dst;
    gState.setup_done           = blob.setup_done;
    gState.suspended            = (gState.suspended_until > 0);

    for (int i = 0; i < NUM_ZONES; i++) {
        gState.zones[i].enabled = blob.zones[i].enabled;
        gState.zones[i].duration_min = (blob.zones[i].duration_min <= 20) ? blob.zones[i].duration_min : gState.zones[i].duration_min;
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

    LOG_I("NVS", "Dados blob carregados (Modo: %d, Susp: %d)", (uint8_t)gState.mode, gState.suspended);
    return true;
}

// ─────────────────────────────────────────────────────────
void Storage::save() {
    if (!_ready) return;

    AppConfigBlob blob = {};
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

    // Read current to avoid unnecessary write
    AppConfigBlob current;
    if (prefs.getBytes(KEY_CFG, &current, sizeof(current)) != sizeof(current) ||
        memcmp(&blob, &current, sizeof(blob)) != 0) {
        
        prefs.putBytes(KEY_CFG, &blob, sizeof(blob));
        LOG_I("NVS", "Dados blob actualizados");
    }
}

// ─────────────────────────────────────────────────────────
void Storage::clear() {
    if (!_ready) return;
    prefs.clear();   // erases all keys in the namespace
    LOG_I("NVS", "Memoria limpa (Reset)");
}

// ─────────────────────────────────────────────────────────
bool Storage::loadHistoryCache(void* dest, size_t size, uint16_t& lineCount) {
    if (!_ready) return false;
    lineCount = prefs.getUInt("hcnt", 0);
    size_t readBytes = prefs.getBytes("hcache", dest, size);
    return (readBytes == size);
}

void Storage::saveHistoryCache(const void* src, size_t size, uint16_t lineCount) {
    if (!_ready) return;
    prefs.putUInt("hcnt", lineCount);
    prefs.putBytes("hcache", src, size);
}

// ─────────────────────────────────────────────────────────
bool Storage::loadRecoveryState(RecoveryState& rs) {
    if (!_ready) return false;
    size_t readBytes = prefs.getBytes("recv", &rs, sizeof(rs));
    return (readBytes == sizeof(rs));
}

void Storage::saveRecoveryState(const RecoveryState& rs) {
    if (!_ready) return;
    prefs.putBytes("recv", &rs, sizeof(rs));
}

