#pragma once
#include <Arduino.h>
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

    // Call every loop(). Checks if a trigger is due and fires watering.
    void update();

    // Call this whenever the active mode changes (from UI dispatch).
    // Recomputes next_* immediately so the IDLE screen updates.
    void onModeChanged();

    // Call this when a watering cycle finishes (from WateringController
    // or from the loop after noticing watering went inactive).
    // Advances next_* past the just-fired slot.
    void onWateringDone();

    // Public utility: compute the next fire time for a given mode and
    // current time. Writes result into out_hour / out_min.
    // Returns false if the mode has no schedule (DESATIVADO / PERSONALIZADO).
    static bool computeNext(AppMode mode, const SystemTime& now,
                            uint8_t& out_hour, uint8_t& out_min);

private:
    // Returns true if the schedule is active on the given calendar day.
    static bool _dayMatches(const ModeSchedule& sched, const SystemTime& now);

    // True while we're inside the trigger minute to prevent double-fire.
    bool _triggered;

    // Last minute we checked — used to detect minute rollover.
    uint8_t _lastMin;

    // Track watering state to detect completion without a callback.
    bool _wasWatering;
};

extern Scheduler scheduler;
