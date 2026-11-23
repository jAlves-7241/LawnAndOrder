#pragma once
#include <Arduino.h>
#include <RTClib.h>
#include "AppState.h"

// ─────────────────────────────────────────────────────────
// Scheduler
//
// Responsible for:
//   1. Computing and keeping gState.next_hour / next_min up to date
//   2. Triggering wateringCtrl.startGeneral() at the right moment
//
// Depends on:
//   - gState.now         (written by RTClock)
//   - gState.mode        (written by UI)
//   - gState.suspended   (written by UI)
//   - gState.watering    (written by WateringController)
//   - MODE_SCHEDULES[]   (const table in AppState.cpp)
//
// Call order in setup():
//   1. initAppState()
//   2. rtclock.begin()
//   3. scheduler.begin()   ← computes first next_* from live RTC time
//
// Call in loop():
//   scheduler.update()
// ─────────────────────────────────────────────────────────
class Scheduler {
public:
    Scheduler();

    // Call once after RTC is initialised.
    // Computes the first gState.next_hour/min.
    void begin();

    // Load persisted DST fall-back trigger guards from NVS (safe to call before wateringCtrl.begin()).
    void loadTriggers();

    // Call every loop(). Checks if a trigger is due and fires watering.
    void update();

    // Call this whenever the active mode changes (from UI dispatch) or schedule must recompute.
    // Clears persisted DST trigger guards and recomputes next_* immediately so the IDLE screen updates.
    void onModeChanged(bool announceMode = true);

    // Recompute next_* immediately without clearing persisted DST fall-back trigger guards
    // (used for non-schedule actions such as cancelling a pause or skipping initial clock setup).
    bool refreshNext();

    // Clear persisted/cached DST fall-back trigger guards (on mode change or manual set_time).
    void clearLastTriggers();

    // Call this when a watering cycle finishes (from WateringController
    // or from the loop after noticing watering went inactive).
    // Advances next_* past the just-fired slot.
    void onWateringDone();

    // Returns true if an interrupted cycle shouldn't be resumed because
    // too much time has passed, or it overlaps with the next cycle.
    bool isCycleExpired(uint32_t start_unix, const SystemTime& current_time, uint32_t remaining_duration_sec);

    // Helper to get the exact unix timestamp of the next scheduled cycle
    uint32_t getNextCycleUnix(SystemTime now);

    // Public utility: compute the next fire time for a given mode and
    // current time. Writes result into out_hour / out_min.
    // Returns false if the mode has no schedule (e.g. DESATIVADO).
    bool computeNext(AppMode mode, const SystemTime& now,
                            uint8_t& out_hour, uint8_t& out_min, uint32_t* out_day_1970 = nullptr, bool writeBack = true);

    // Returns true if at least one zone is enabled with duration_min > 0.
    static bool hasEnabledZones();

    // True when a scheduled slot was skipped because another watering run was active.
    bool    hasSkippedSlot() const { return _hasSkippedSlot; }
    uint8_t skippedHour()    const { return _skippedHour; }
    uint8_t skippedMin()     const { return _skippedMin; }

private:
    UNIT_TEST_FRIEND
    // Returns true if the schedule is active on the given calendar day (since 1970).
    bool _dayMatches(const ModeSchedule& sched, uint32_t candidate_day_1970, bool writeBack = true);
    bool _tryCatchUp(const SystemTime& t, bool allowExactSecond = false);
    static uint32_t _resolveSlotUtc(const DateTime& slotLocal, uint32_t afterUtc, bool* outDstSkipped = nullptr);

    // Last minute we checked - used to detect minute rollover.
    uint8_t _lastMin;
    uint8_t _lastSec;
    uint32_t _lastClockRev;
    uint32_t _lastManualClockRev;
    bool _catchUpPending;
    bool _collisionCatchUpPending;
    uint8_t _collisionHour;
    uint8_t _collisionMin;
    bool _hasSkippedSlot;
    uint8_t _skippedHour;
    uint8_t _skippedMin;
    bool _triggersLoaded;
    uint32_t _prevLocalWallUnix;

    // UTC unix timestamp of the last successful auto-trigger for each slot (persisted to NVS).
    // Prevents re-triggering the same slot when local time repeats (DST fall-back).
    // Uses a 2h (7200s) window: the same slot on consecutive days is ≥24h apart,
    // while a DST duplicate is exactly 1h apart. Different slots use different indices.
    uint32_t _lastTriggerUnix[MAX_SLOTS_PER_MODE];
};

extern Scheduler scheduler;
