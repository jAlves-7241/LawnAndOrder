#include "Scheduler.h"
#include "WateringController.h"
#include "Storage.h"
#include <RTClib.h>

Scheduler scheduler;

// ─────────────────────────────────────────────────────────
Scheduler::Scheduler()
    : _triggered(false), _lastMin(0xFF), _wasWatering(false)
{}

// ─────────────────────────────────────────────────────────
void Scheduler::begin() {
    if (!gState.rtc_valid) return;
    computeNext(gState.mode, gState.now,
                gState.next_hour, gState.next_min);
    Serial.printf("[SCHED] Proxima rega: %02d:%02d\n",
                  gState.next_hour, gState.next_min);
}

// ─────────────────────────────────────────────────────────
void Scheduler::update() {
    if (!gState.rtc_valid)  return;

    const SystemTime& t = gState.now;

    // Auto-wake from suspension
    if (gState.suspended) {
        if (t.unix >= gState.suspended_until) {
            gState.suspended = false;
            gState.suspended_until = 0;
            storage.save();
            Serial.println("[SCHED] Suspensao expirou — rega reativada");
        } else {
            return; // Still suspended
        }
    }

    // Detect watering completion so we can advance next_*
    bool isWatering = gState.watering.active;
    if (_wasWatering && !isWatering) {
        onWateringDone();
    }
    _wasWatering = isWatering;

    // Don't trigger while a cycle is already running
    if (isWatering) return;

    // On minute rollover, reset trigger guard and recompute next_*
    if (t.min != _lastMin) {
        _triggered = false;
        _lastMin   = t.min;
        // Recompute so next_* is always fresh (catches mode changes too)
        computeNext(gState.mode, t,
                    gState.next_hour, gState.next_min);
    }

    // Check every slot of the active mode
    if (_triggered) return;

    const ModeSchedule& sched = MODE_SCHEDULES[(uint8_t)gState.mode];
    if (sched.slot_count == 0) return;
    if (!_dayMatches(sched, t)) return;

    for (uint8_t i = 0; i < sched.slot_count; i++) {
        if (t.hour == sched.slots[i].hour &&
            t.min  == sched.slots[i].minute) {
            _triggered = true;
            Serial.printf("[SCHED] Disparar rega automatica %02d:%02d\n",
                          t.hour, t.min);
            wateringCtrl.startGeneral(WaterTrigger::AUTO);
            return;
        }
    }
}

// ─────────────────────────────────────────────────────────
void Scheduler::onModeChanged() {
    _triggered = false;
    if (!gState.rtc_valid) {
        // RTC not ready — seed next_* from the first slot of the new mode
        const ModeSchedule& sched = MODE_SCHEDULES[(uint8_t)gState.mode];
        if (sched.slot_count > 0) {
            gState.next_hour = sched.slots[0].hour;
            gState.next_min  = sched.slots[0].minute;
        } else {
            gState.next_hour = 0;
            gState.next_min  = 0;
        }
        return;
    }
    computeNext(gState.mode, gState.now,
                gState.next_hour, gState.next_min);
    Serial.printf("[SCHED] Modo alterado: Proxima rega %02d:%02d\n",
                  gState.next_hour, gState.next_min);
}

// ─────────────────────────────────────────────────────────
void Scheduler::onWateringDone() {
    if (!gState.rtc_valid) return;
    // Advance time by 1 minute past the just-fired slot so computeNext
    // doesn't return the same slot again.
    DateTime next(gState.now.unix + 60);

    SystemTime advanced;
    advanced.unix = next.unixtime();
    advanced.hour = next.hour();
    advanced.min  = next.minute();
    advanced.day  = next.day();
    advanced.dow  = next.dayOfTheWeek();

    computeNext(gState.mode, advanced,
                gState.next_hour, gState.next_min);
    Serial.printf("[SCHED] Ciclo concluido: Proxima rega %02d:%02d\n",
                  gState.next_hour, gState.next_min);
}

// ─────────────────────────────────────────────────────────
// Static: compute the next fire time for a given mode + current time.
// Returns false if mode has no schedule.
// ─────────────────────────────────────────────────────────
bool Scheduler::computeNext(AppMode mode, const SystemTime& now,
                             uint8_t& out_hour, uint8_t& out_min) {
    const ModeSchedule& sched = MODE_SCHEDULES[(uint8_t)mode];

    if (sched.slot_count == 0) {
        out_hour = 0;
        out_min  = 0;
        return false;
    }

    // Use RTClib's DateTime for proper date arithmetic (month ends, leap years)
    DateTime current(now.unix);

    // We search up to 15 days ahead to accommodate EVERY_X_DAYS (max 14)
    for (uint8_t dayOffset = 0; dayOffset <= 15; dayOffset++) {
        DateTime candidateDt = current + TimeSpan(dayOffset, 0, 0, 0);
        
        // Convert back to SystemTime-like fields for _dayMatches
        SystemTime candidate;
        candidate.day  = candidateDt.day();
        candidate.dow  = candidateDt.dayOfTheWeek();
        candidate.unix = candidateDt.unixtime();

        if (!_dayMatches(sched, candidate)) continue;

        // Scan slots in order
        for (uint8_t i = 0; i < sched.slot_count; i++) {
            const ScheduleSlot& sl = sched.slots[i];
            
            // For today (dayOffset==0) skip slots already past
            if (dayOffset == 0) {
                if (sl.hour < now.hour) continue;
                if (sl.hour == now.hour && sl.minute <= now.min) continue;
            }
            out_hour = sl.hour;
            out_min  = sl.minute;
            return true;
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────
// Static: returns true if the schedule fires on the given day
// ─────────────────────────────────────────────────────────
bool Scheduler::_dayMatches(const ModeSchedule& sched, const SystemTime& now) {
    switch (sched.day_pattern) {
        case DayPattern::DAILY:
            return true;
        case DayPattern::DOW_MASK:
            return (sched.dow_mask & (1 << now.dow)) != 0;
        case DayPattern::EVERY_X_DAYS: {
            uint32_t current_day = now.unix / 86400UL;
            uint32_t ref_day     = gState.custom_ref_day;
            // Calculate absolute difference in days
            uint32_t diff = (current_day >= ref_day) ? (current_day - ref_day) : (ref_day - current_day);
            return (diff % sched.interval_days) == 0;
        }
        default:
            return false;
    }
}
