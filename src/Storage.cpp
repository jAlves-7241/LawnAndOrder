#include "Storage.h"
#include <Preferences.h>

// ── NVS namespace (max 15 chars) ──────────────────────────
static const char* NVS_NS = "rega";

// ── Key names — kept short to minimise NVS overhead ───────
static const char* KEY_VER      = "ver";
static const char* KEY_MODE     = "mode";
static const char* KEY_BL       = "bl";
static const char* KEY_SUSP     = "susp";
static const char* KEY_SUNT     = "sunt";
// Zone keys are built dynamically: "z0e","z0d","z1e","z1d"…

static Preferences prefs;
Storage storage;

// ─────────────────────────────────────────────────────────
void Storage::begin() {
    // Open in read-write mode; creates namespace if it doesn't exist.
    _ready = prefs.begin(NVS_NS, false);
    if (!_ready) {
        Serial.println("[NVS] Erro ao abrir namespace");
    } else {
        Serial.println("[NVS] Pronto");
    }
}

// ─────────────────────────────────────────────────────────
bool Storage::load() {
    if (!_ready) return false;

    // Version check — if missing or wrong version, bail out and keep defaults
    uint8_t ver = prefs.getUChar(KEY_VER, 0xFF);
    if (ver != NVS_VERSION) {
        Serial.printf("[NVS] Versao incompativel (%d != %d) — usar defaults\n",
                      ver, NVS_VERSION);
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

    // ── Suspended ─────────────────────────────────────────
    gState.suspended_until =
        prefs.getUInt(KEY_SUNT, gState.suspended_until);
    // Restore suspended flag — the Scheduler will auto-clear it once
    // gState.now.unix >= suspended_until (checked every second in update()).
    gState.suspended = (gState.suspended_until > 0);

    Serial.printf("[NVS] Dados carregados (Modo: %d, Susp: %d)\n",
                  (uint8_t)gState.mode, gState.suspended);
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
    updateUChar(KEY_SUSP, gState.suspended ? 1 : 0);
    updateUInt(KEY_SUNT,  gState.suspended_until);

    char key[5];
    for (int i = 0; i < NUM_ZONES; i++) {
        snprintf(key, sizeof(key), "z%de", i);
        updateUChar(key, gState.zones[i].enabled ? 1 : 0);

        snprintf(key, sizeof(key), "z%dd", i);
        updateUChar(key, gState.zones[i].duration_min);
    }

    if (changed) {
        Serial.println("[NVS] Dados actualizados");
    }
}

// ─────────────────────────────────────────────────────────
void Storage::clear() {
    if (!_ready) return;
    prefs.clear();   // erases all keys in the namespace
    Serial.println("[NVS] Memoria limpa (Reset)");
}
