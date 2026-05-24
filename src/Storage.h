#pragma once
#include <Arduino.h>
#include "AppState.h"

// ─────────────────────────────────────────────────────────
// Storage
//
// Persists user-configurable gState fields to ESP32 NVS flash
// using the Preferences library (no extra dependencies).
//
// What is persisted:
//   - gState.mode
//   - gState.zones[i].enabled + duration_min  (for all NUM_ZONES)
//   - gState.backlight_timeout_ms
//   - gState.suspended
//   - gState.setup_done
//
// What is NOT persisted (intentionally):
//   - gState.now          → always comes from RTC
//   - gState.watering     → transient runtime state
//   - gState.custom_sel   → per-session selection, not worth persisting
//   - gState.next_hour/min → recomputed by Scheduler on boot
//
// Versioning:
//   NVS_VERSION is stored alongside the data.  If the version in flash
//   doesn't match the current NVS_VERSION, stored data is ignored and
//   defaults are kept.  Increment NVS_VERSION whenever the key schema
//   changes to avoid loading corrupt/stale data after a firmware update.
//
// Usage:
//   setup()  → initAppState() → storage.load() → rest of init
//   On every user config change → storage.save()
//   On factory reset            → storage.clear() + initAppState()
// ─────────────────────────────────────────────────────────

#define NVS_VERSION 3   // increment when key schema changes

class Storage {
public:
    // Open NVS namespace. Call once in setup(), before load().
    void begin();

    // Overwrite gState fields with values from NVS.
    // If NVS is empty or version mismatch, gState is left unchanged (defaults).
    // Returns true if data was successfully loaded.
    bool load();

    // Write all persisted fields from gState to NVS.
    void save();

    // Erase the entire NVS namespace (factory reset).
    void clear();

    // Persist history cache to NVS
    bool loadHistoryCache(void* dest, size_t size, uint16_t& lineCount);
    void saveHistoryCache(const void* src, size_t size, uint16_t lineCount);

private:
    bool _ready;  // true after begin() succeeds
};

extern Storage storage;
