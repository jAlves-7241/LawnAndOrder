#pragma once
#include <Arduino.h>
#include "config.h"
#include "AppState.h"
#include "History.h"   // WateringType, HistoryEntry

// ─────────────────────────────────────────────────────────
// WateringController
// ─────────────────────────────────────────────────────────

class WateringController {
public:
    WateringController();

    void begin();

    void startGeneral();
    void startCustom(const bool zones[NUM_ZONES], uint8_t dur_min);
    void startTest(int8_t zone_idx);
    void stop();
    void update();

    bool    isActive()    const { return _active; }
    uint8_t currentZone() const { return _zoneIdx; }

private:
    struct QueueEntry {
        uint8_t  zone_idx;
        uint32_t duration_ms;
    };

    QueueEntry _queue[NUM_ZONES];
    uint8_t    _queueLen;
    uint8_t    _queuePos;

    bool     _active;
    uint8_t  _zoneIdx;
    uint32_t _zoneStartMs;
    uint32_t _zoneDurationMs;

    // ── History tracking ──────────────────────────────────
    WateringType _runType;                 // type of the current cycle
    SystemTime   _cycleStart;             // gState.now snapshot at cycle start
    uint8_t      _zoneDurMin[NUM_ZONES];  // actual minutes per zone (0 = not run)

    void _buildQueue(const bool zones[NUM_ZONES], uint32_t dur_ms,
                     WateringType type);
    void _startNextZone();
    void _finishCycle();     // record to history and notify scheduler
    void _activateRelay(uint8_t zone_idx);
    void _deactivateAll();
    void _syncState();

    static const uint8_t _relayPins[NUM_ZONES];
};

extern WateringController wateringCtrl;
