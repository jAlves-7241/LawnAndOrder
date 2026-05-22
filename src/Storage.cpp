#include "Storage.h"
#include <Preferences.h>
#include "log.h"

// ── NVS namespace (max 15 chars) ──────────────────────────
static const char* NVS_NS = "rega";

// ── Key names — kept short to minimise NVS overhead ───────
static const char* KEY_VER      = "ver";
static const char* KEY_MODE     = "mode";
static const char* KEY_BL       = "bl";
static const char* KEY_SUNT     = "sunt";
static const char* KEY_CRD      = "crd";
static const char* KEY_CIF      = "cif";
static const char* KEY_CSC      = "csc";
// Zone keys are built dynamically: "z0e","z0d","z1e","z1d"…
// Custom slot keys: "c0h","c0m"…

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

// ─────────────────────────────────────────────────────────
bool Storage::load() {
    if (!_ready) return false;

    // Version check — if missing or wrong version, bail out and keep defaults
    uint8_t ver = prefs.getUChar(KEY_VER, 0xFF);
    if (ver != NVS_VERSION) {
        LOG_W("NVS", "Versao incompativel (%d != %d) — usar defaults", ver, NVS_VERSION);
        return false;
    }

    // ── Mode ─────────────────────────────────────────────
    uint8_t mode = prefs.getUChar(KEY_MODE, (uint8_t)gState.mode);
    if (mode < (uint8_t)AppMode::_COUNT)
        gState.mode = (AppMode)mode;

    // ── Zones ─────────────────────────────────────────────
    char key[5];
    for (int i = 0; i < NUM_ZONES; i++) {
        snprintf(key, sizeof(key), "z%de", i);
        gState.zones[i].enabled =
            (bool)prefs.getUChar(key, gState.zones[i].enabled ? 1 : 0);

        snprintf(key, sizeof(key), "z%dd", i);
        uint8_t dur = prefs.getUChar(key, gState.zones[i].duration_min);
        // Clamp to valid range in case of flash corruption
        gState.zones[i].duration_min = (dur <= 20) ? dur : gState.zones[i].duration_min;
    }

    // ── Backlight timeout ─────────────────────────────────
    gState.backlight_timeout_ms =
        prefs.getULong(KEY_BL, gState.backlight_timeout_ms);

    // ── DST ───────────────────────────────────────────────
    gState.auto_dst = prefs.getBool("dst", gState.auto_dst);

    // ── Setup Wizard ──────────────────────────────────────
    gState.setup_done = (bool)prefs.getUChar("sdone", 0);

    // ── Suspended ─────────────────────────────────────────
    gState.suspended_until =
        prefs.getUInt(KEY_SUNT, gState.suspended_until);
    // Restore suspended flag — the Scheduler will auto-clear it once
    // gState.now.unix >= suspended_until (checked every second in update()).
    gState.suspended = (gState.suspended_until > 0);

    // ── Custom Schedule ───────────────────────────────────
    gState.custom_ref_day = prefs.getUInt(KEY_CRD, gState.custom_ref_day);
    
    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.interval_days = prefs.getUChar(KEY_CIF, cs.interval_days);
    if (cs.interval_days == 0 || cs.interval_days > 14) cs.interval_days = 1;
    cs.slot_count    = prefs.getUChar(KEY_CSC, cs.slot_count);
    if (cs.slot_count == 0 || cs.slot_count > MAX_SLOTS_PER_MODE) cs.slot_count = 1;

    for (int i = 0; i < MAX_SLOTS_PER_MODE; i++) {
        char kh[5], km[5];
        snprintf(kh, sizeof(kh), "c%dh", i);
        snprintf(km, sizeof(km), "c%dm", i);
        cs.slots[i].hour   = prefs.getUChar(kh, cs.slots[i].hour);
        if (cs.slots[i].hour > 23) cs.slots[i].hour = 0;
        cs.slots[i].minute = prefs.getUChar(km, cs.slots[i].minute);
        if (cs.slots[i].minute > 59) cs.slots[i].minute = 0;
    }

    LOG_I("NVS", "Dados carregados (Modo: %d, Susp: %d)", (uint8_t)gState.mode, gState.suspended);
    return true;
}


// ─────────────────────────────────────────────────────────
void Storage::save() {
    if (!_ready) return;

    bool changed = false;

    auto updateUChar = [&](const char* k, uint8_t v) {
        if (prefs.getUChar(k, 0xFF) != v) { prefs.putUChar(k, v); changed = true; }
    };
    auto updateUInt = [&](const char* k, uint32_t v) {
        if (prefs.getUInt(k, 0) != v) { prefs.putUInt(k, v); changed = true; }
    };
    auto updateULong = [&](const char* k, uint32_t v) {
        if (prefs.getULong(k, 0) != v) { prefs.putULong(k, v); changed = true; }
    };

    updateUChar(KEY_VER,  NVS_VERSION);
    updateUChar(KEY_MODE, (uint8_t)gState.mode);
    updateULong(KEY_BL,   gState.backlight_timeout_ms);
    updateUInt(KEY_SUNT,  gState.suspended_until);
    updateUInt(KEY_CRD,   gState.custom_ref_day);

    if (prefs.getBool("dst", true) != gState.auto_dst) {
        prefs.putBool("dst", gState.auto_dst);
        changed = true;
    }

    updateUChar("sdone", gState.setup_done ? 1 : 0);

    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    updateUChar(KEY_CIF, cs.interval_days);
    updateUChar(KEY_CSC, cs.slot_count);

    for (int i = 0; i < MAX_SLOTS_PER_MODE; i++) {
        char kh[5], km[5];
        snprintf(kh, sizeof(kh), "c%dh", i);
        snprintf(km, sizeof(km), "c%dm", i);
        updateUChar(kh, cs.slots[i].hour);
        updateUChar(km, cs.slots[i].minute);
    }

    char key[5];
    for (int i = 0; i < NUM_ZONES; i++) {
        snprintf(key, sizeof(key), "z%de", i);
        updateUChar(key, gState.zones[i].enabled ? 1 : 0);

        snprintf(key, sizeof(key), "z%dd", i);
        updateUChar(key, gState.zones[i].duration_min);
    }

    if (changed) {
        LOG_I("NVS", "Dados actualizados");
    }
}

// ─────────────────────────────────────────────────────────
void Storage::clear() {
    if (!_ready) return;
    prefs.clear();   // erases all keys in the namespace
    LOG_I("NVS", "Memoria limpa (Reset)");
}
