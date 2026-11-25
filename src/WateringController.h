#pragma once
#include <Arduino.h>
#include "config.h"
#include "AppState.h"   // WaterTrigger, HistoryEntry
#include "History.h"

// ─────────────────────────────────────────────────────────
// WateringController
// ─────────────────────────────────────────────────────────

class WateringController {
public:
    WateringController();

    void begin();

    bool startGeneral(WaterTrigger trigger = WaterTrigger::MANUAL);
    bool startCustom(const bool zones[NUM_ZONES], uint8_t dur_min);
    bool startTest(int8_t zone_idx);
    void stop();
    void deenergizeRelays();
    void update();

    bool         isActive()      const { return _active; }
    uint8_t      currentZone()   const { return _zoneIdx; }
    WaterTrigger activeTrigger() const { return _runTrigger; }

private:
    UNIT_TEST_FRIEND
    struct QueueEntry {
        uint8_t  zone_idx;
        uint32_t duration_ms;
    };

    QueueEntry _queue[NUM_ZONES];
    uint8_t    _queueLen;
    uint8_t    _queuePos;

    bool     _active;
    bool     _isWaiting;
    bool     _isRelayDeadTimeWaiting;
    bool     _cycleStartValid;
    bool     _recoveryClearPending;
    uint8_t  _recoveryRetryCount;
    uint8_t  _zoneIdx;
    uint32_t _zoneStartMs;
    uint32_t _zoneDurationMs;
    uint32_t _waitStartMs;
    uint32_t _relayWaitStartMs;
    uint32_t _lastPinRefreshMs;
    uint32_t _lastRecoveryClearRetryMs;

    // ── History tracking ──────────────────────────────────
    WaterTrigger _runTrigger;              // trigger type of current cycle
    SystemTime   _cycleStart;             // gState.now snapshot at cycle start
    uint8_t      _zoneDurMin[NUM_ZONES];  // actual minutes per zone (0 = not run)
    uint8_t      _checkpointedMin;        // minutes persisted mid-zone on current segment

    bool _buildQueue(const bool zones[NUM_ZONES], uint32_t dur_ms,
                     WaterTrigger trigger);
    uint32_t _maxZoneMs() const;
    uint32_t _effectiveZoneDurationMs() const;
    void _startNextZone();
    void _finishCycle();     // record to history and notify scheduler
    void _activateRelay();
    void _deactivateAll();
    void _syncState();
    void _saveRecoveryState(bool active);
    bool _clearRecoveryState();

    static const uint8_t _relayPins[NUM_ZONES];
};

extern WateringController wateringCtrl;
