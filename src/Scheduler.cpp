#include "Scheduler.h"
#include "WateringController.h"

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
    if (gState.suspended)   return;

    const SystemTime& t = gState.now;

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
            wateringCtrl.startGeneral();
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
    Serial.printf("[SCHED] Modo alterado — proxima rega: %02d:%02d\n",
                  gState.next_hour, gState.next_min);
}

// ─────────────────────────────────────────────────────────
void Scheduler::onWateringDone() {
    if (!gState.rtc_valid) return;
    // Advance time by 1 minute past the just-fired slot so computeNext
    // doesn't return the same slot again.
    SystemTime advanced = gState.now;
    advanced.min++;
    if (advanced.min >= 60) { advanced.min = 0; advanced.hour++; }
    if (advanced.hour >= 24) { advanced.hour = 0; advanced.day++; }

    computeNext(gState.mode, advanced,
                gState.next_hour, gState.next_min);
    Serial.printf("[SCHED] Rega concluida — proxima: %02d:%02d\n",
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

    // We search up to 2 days ahead (today + tomorrow) to handle
    // FRACO (every other day) without looping indefinitely.
    for (uint8_t dayOffset = 0; dayOffset <= 2; dayOffset++) {
        // Build a hypothetical "day" to check
        SystemTime candidate = now;
        if (dayOffset > 0) {
            // Advance by dayOffset days (simple, ignores month boundaries —
            // acceptable since we only look 2 days ahead)
            candidate.day  += dayOffset;
            candidate.hour  = 0;
            candidate.min   = 0;
            candidate.sec   = 0;
            // Advance dow
            candidate.dow = (candidate.dow + dayOffset) % 7;
        }

        if (!_dayMatches(sched, candidate)) continue;

        // Scan slots in order (they are defined earliest→latest in config.h)
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

    // All slots today and tomorrow are in the past (shouldn't happen normally)
    // Fall back to first slot of next matching day
    const ScheduleSlot& first = sched.slots[0];
    out_hour = first.hour;
    out_min  = first.minute;
    return true;
}

// ─────────────────────────────────────────────────────────
// Static: returns true if the schedule fires on the given day
// ─────────────────────────────────────────────────────────
bool Scheduler::_dayMatches(const ModeSchedule& sched, const SystemTime& now) {
    switch (sched.day_pattern) {
        case DayPattern::DAILY:
            return true;
        case DayPattern::ODD_DAYS:
            return (now.day % 2) != 0;
        case DayPattern::EVEN_DAYS:
            return (now.day % 2) == 0;
        case DayPattern::DOW_MASK:
            return (sched.dow_mask & (1 << now.dow)) != 0;
        default:
            return false;
    }
}
