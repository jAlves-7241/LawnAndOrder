#pragma once
#include <Arduino.h>
#include "config.h"
#include "AppState.h"

// ─────────────────────────────────────────────────────────
// WateringController
//
// Runs one zone at a time, sequentially.
// Drives GPIO relay pins and keeps gState.watering in sync
// so the IDLE screen always reflects reality.
//
// Usage in loop():
//   wateringCtrl.update();
//
// Triggering from UI:
//   wateringCtrl.startGeneral();
//   wateringCtrl.startCustom(zones_bool_array, duration_min);
//   wateringCtrl.startTest(zone_idx);   // zone_idx = -1 → all zones
//   wateringCtrl.stop();
// ─────────────────────────────────────────────────────────

// Internal run-mode — determines duration source
enum class RunMode : uint8_t {
    GENERAL,   // uses gState.zones[i].duration_min per zone
    CUSTOM,    // uses a fixed custom duration for all selected zones
    TEST,      // uses ZONE_TEST_DURATION_S for selected zones
};

class WateringController {
public:
    WateringController();

    void begin();   // configure relay pins

    // ── Start commands ────────────────────────────────────
    // Runs all enabled zones in order, each for its configured duration.
    void startGeneral();

    // Runs the selected zones for the given duration (minutes).
    void startCustom(const bool zones[NUM_ZONES], uint8_t dur_min);

    // Runs a single zone (zone_idx 0-3) or all zones (zone_idx = -1)
    // each for ZONE_TEST_DURATION_S seconds.
    void startTest(int8_t zone_idx);

    // Immediately stops the current cycle and turns all relays off.
    void stop();

    // ── Loop ─────────────────────────────────────────────
    // Must be called every loop() iteration.
    void update();

    // ── Accessors ────────────────────────────────────────
    bool    isActive()    const { return _active; }
    uint8_t currentZone() const { return _zoneIdx; }

private:
    // ── Queue of zones to run ─────────────────────────────
    // Simple static array — no heap allocation.
    struct QueueEntry {
        uint8_t zone_idx;
        uint32_t duration_ms;
    };

    QueueEntry _queue[NUM_ZONES];
    uint8_t    _queueLen;
    uint8_t    _queuePos;

    // ── Runtime state ─────────────────────────────────────
    bool     _active;
    uint8_t  _zoneIdx;        // physical index into gState.zones
    uint32_t _zoneStartMs;
    uint32_t _zoneDurationMs;

    // ── Internal ──────────────────────────────────────────
    void     _buildQueue(const bool zones[NUM_ZONES], uint32_t dur_ms);
    void     _startNextZone();
    void     _activateRelay(uint8_t zone_idx);
    void     _deactivateAll();
    void     _syncState();     // push current status into gState.watering

    static const uint8_t _relayPins[NUM_ZONES];
};

extern WateringController wateringCtrl;
