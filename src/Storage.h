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

#define NVS_VERSION 4   // increment when key schema changes

struct __attribute__((packed)) RecoveryState {
    bool active;
    uint32_t start_unix_time;
    uint8_t queuePos;
    uint8_t queueLen;
    struct QueueEntry {
        uint8_t  zone_idx;
        uint32_t duration_ms;
    } queue[NUM_ZONES];
    uint8_t zone_dur_min[NUM_ZONES];
    WaterTrigger trigger;
};

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

    // Persist recovery state to NVS
    bool loadRecoveryState(RecoveryState& rs);
    void saveRecoveryState(const RecoveryState& rs);

    // Export current state as a 68-character hexadecimal string
    void exportConfigHex(char* hexOut);

    // Import a 68-character hexadecimal string, validate and save to NVS
    bool importConfigHex(const char* hexIn);

private:
    bool _ready = false;  // true after begin() succeeds
};

extern Storage storage;
