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
//   - gState.mode, custom_interval_days, custom_slot_count, custom_slots, custom_ref_day
//   - gState.zones[i].enabled + duration_min  (for all NUM_ZONES)
//   - gState.backlight_timeout_ms
//   - gState.suspended_until (monotonic UTC boundary)
//   - gState.auto_dst, gState.setup_done
//   - NVS v5 blobs (`cfg`, `hblob`, `sched_tr`, `recv`) include CRC-16/CCITT integrity verification
//   - RecoveryState (`recv`) includes `retry_count` for bounded blackout recovery
//   - Scheduler slot trigger guards (`sched_tr`) persist across reboots
//
// What is NOT persisted (intentionally):
//   - gState.now          → always comes from RTC
//   - gState.watering     → transient runtime state
//   - gState.custom_sel   → per-session selection, not worth persisting
//   - gState.next_hour/min → recomputed by Scheduler on boot
//
// Versioning:
//   NVS_VERSION is stored alongside the data and protected by CRC-16.  If the
//   version in flash doesn't match NVS_VERSION or CRC fails, stored data is
//   rejected and defaults are kept.  Increment NVS_VERSION whenever the key
//   schema changes to avoid loading corrupt/stale data after a firmware update.
//
// Usage:
//   setup()  → initAppState() → storage.load() → rest of init
//   On every user config change → storage.save()
//   On factory reset            → storage.clear() + initAppState()
// ─────────────────────────────────────────────────────────

#define NVS_VERSION 5   // increment when key schema changes

struct __attribute__((packed)) RecoveryState {
    uint8_t version;
    bool active;
    uint32_t start_unix_time;
    uint8_t queuePos;
    uint8_t queueLen;
    struct __attribute__((packed)) QueueEntry {
        uint8_t  zone_idx;
        uint32_t duration_ms;
    } queue[NUM_ZONES];
    uint8_t zone_dur_min[NUM_ZONES];
    WaterTrigger trigger;
    uint8_t retry_count;
    uint16_t crc16;
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
    uint16_t crc16;
};

// Redundant NVS ping-pong slot envelope (cfg / cfg_b).
// Wraps AppConfigBlob with a monotonic sequence counter and outer CRC-16 so
// export_config / import_config stays an unchanged 36-byte (72-hex) format.
struct __attribute__((packed)) AppConfigSlot {
    uint8_t       magic;      // 0xA5
    uint8_t       version;    // NVS_VERSION
    uint32_t      seq;        // monotonically increasing write sequence (>= 1)
    AppConfigBlob blob;
    uint16_t      slot_crc16;
};

enum class ConfigLoadResult : uint8_t {
    OK = 0,
    EMPTY,
    CORRUPT,
    VERSION_MISMATCH,
    NOT_READY,
};


class Storage {
public:
    // Open NVS namespace. Call once in setup(), before load().
    void begin();

    // Overwrite gState fields with values from NVS (reads cfg and cfg_b, picking newer valid copy).
    // If NVS is empty or both copies fail validation, gState is left unchanged (defaults).
    // Returns true if data was successfully loaded. Inspect lastLoadResult() / configLoadFailed()
    // to distinguish first-boot EMPTY from corruption or version mismatch.
    bool load();
    ConfigLoadResult lastLoadResult() const { return _lastLoadResult; }
    bool configLoadFailed() const {
        return _lastLoadResult != ConfigLoadResult::OK &&
               _lastLoadResult != ConfigLoadResult::EMPTY;
    }

    // Write all persisted fields from gState to NVS (alternating cfg / cfg_b slots).
    // Returns false if NVS is unavailable or the write fails (arms periodic retry).
    bool save();

    // Retry a previously failed config save on a 5 s throttle. Call from loop().
    void update();
    bool hasPendingSave() const { return _savePending; }

    // Erase the entire NVS namespace (factory reset).
    // Returns false if NVS is unavailable or the erase fails.
    bool clear();

    // Persist atomic CRC-protected history cache (`hblob`) to NVS
    bool loadHistoryCache(void* dest, size_t size, uint16_t& lineCount, uint32_t* outFileSize = nullptr);
    void saveHistoryCache(const void* src, size_t size, uint16_t lineCount, uint32_t fileSize = 0);
    bool hasHistoryData();

    // Persist scheduler slot last-trigger Unix timestamps (`sched_tr`)
    bool loadLastTriggers(uint32_t* out, size_t count);
    void saveLastTriggers(const uint32_t* in, size_t count);

    // Persist recovery state to NVS. Returns true if persisted or already up to date.
    bool loadRecoveryState(RecoveryState& rs);
    bool saveRecoveryState(const RecoveryState& rs);

    // Export current state as a 72-character hexadecimal string (v5 CRC-protected)
    void exportConfigHex(char* hexOut, size_t maxLen);

    // Import a 72-character hexadecimal string (v5 CRC-protected), validate strictly and save to NVS
    bool importConfigHex(const char* hexIn);

private:
    bool _ready = false;  // true after begin() succeeds
    bool _savePending = false;
    uint32_t _lastSaveRetryMs = 0;
    ConfigLoadResult _lastLoadResult = ConfigLoadResult::EMPTY;
    uint32_t _configSeq = 0;
    uint8_t  _activeSlot = 0;
    bool     _backupSeeded = false;

    static uint16_t _crc16(const uint8_t* data, size_t len);
    bool _readConfigSlot(const char* key, AppConfigBlob& outBlob,
                         uint32_t& outSeq, bool& outEnveloped,
                         ConfigLoadResult& outErr);
    static bool _migratePrevVersionBlob(const uint8_t* raw, size_t len, AppConfigBlob& outBlob);
    void _stateToBlob(AppConfigBlob& blob);
    bool _blobToState(const AppConfigBlob& blob);
};

extern Storage storage;
