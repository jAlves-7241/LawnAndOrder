#include "Scheduler.h"
#include "WateringController.h"
#include "Storage.h"
#include <RTClib.h>
#include "log.h"

Scheduler scheduler;

// ─────────────────────────────────────────────────────────
Scheduler::Scheduler()
    : _triggered(false), _lastMin(0xFF), _wasWatering(false)
{}

// ─────────────────────────────────────────────────────────
void Scheduler::begin() {
    if (!gState.rtc_valid) return;
    uint32_t old_ref = gState.custom_ref_day;
    computeNext(gState.mode, gState.now,
                gState.next_hour, gState.next_min);
    if (gState.custom_ref_day != old_ref) storage.save();
    LOG_I("SCHED", "Proxima rega: %02d:%02d",
                  gState.next_hour, gState.next_min);
}

// ─────────────────────────────────────────────────────────
void Scheduler::update() {
    if (!gState.rtc_valid)  return;

    // Apenas processar se o segundo do relógio tiver mudado (redução de carga sobre o CPU)
    static uint8_t lastSec = 0xFF;
    if (gState.now.sec == lastSec) return;
    lastSec = gState.now.sec;

    const SystemTime& t = gState.now;

    // Auto-wake from suspension
    if (gState.suspended) {
        if (t.unix >= gState.suspended_until) {
            gState.suspended = false;
            gState.suspended_until = 0;
            storage.save();
            LOG_I("SCHED", "Suspensao expirada - rega reativada");
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
        uint32_t old_ref = gState.custom_ref_day;
        computeNext(gState.mode, t,
                    gState.next_hour, gState.next_min);
        if (gState.custom_ref_day != old_ref) storage.save();
    }

    // Check every slot of the active mode
    if (_triggered) return;

    const ModeSchedule& sched = MODE_SCHEDULES[(uint8_t)gState.mode];
    if (sched.slot_count == 0) return;

    DateTime localDate(t.year, t.month, t.day, 0, 0, 0);
    uint32_t current_day_1970 = localDate.unixtime() / 86400UL;

    if (!_dayMatches(sched, current_day_1970)) return;

    for (uint8_t i = 0; i < sched.slot_count; i++) {
        if (t.hour == sched.slots[i].hour &&
            t.min  == sched.slots[i].minute) {
            _triggered = true;
            LOG_I("SCHED", "Iniciar rega automatica %02d:%02d",
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
        // RTC not ready - seed next_* from the first slot of the new mode
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
    uint32_t old_ref = gState.custom_ref_day;
    computeNext(gState.mode, gState.now,
                gState.next_hour, gState.next_min);
    if (gState.custom_ref_day != old_ref) storage.save();
    LOG_I("SCHED", "Modo alterado: Proxima rega %02d:%02d",
                  gState.next_hour, gState.next_min);
}

// ─────────────────────────────────────────────────────────
void Scheduler::onWateringDone() {
    if (!gState.rtc_valid) return;
    // Construct local DateTime from gState.now (which is local time)
    DateTime localNow(gState.now.year, gState.now.month, gState.now.day,
                      gState.now.hour, gState.now.min, gState.now.sec);
    DateTime localNext = localNow + TimeSpan(0, 0, 1, 0); // add 1 minute

    SystemTime advanced = {};
    advanced.unix   = gState.now.unix + 60;
    advanced.year   = localNext.year();
    advanced.month  = localNext.month();
    advanced.day    = localNext.day();
    advanced.hour   = localNext.hour();
    advanced.min    = localNext.minute();
    advanced.sec    = localNext.second();
    advanced.dow    = localNext.dayOfTheWeek();

    uint32_t old_ref = gState.custom_ref_day;
    computeNext(gState.mode, advanced,
                gState.next_hour, gState.next_min);
    if (gState.custom_ref_day != old_ref) storage.save();
    LOG_I("SCHED", "Ciclo concluido: Proxima rega %02d:%02d",
                  gState.next_hour, gState.next_min);
}

// ─────────────────────────────────────────────────────────
uint32_t Scheduler::getNextCycleUnix(SystemTime now) {
    uint8_t nextH = 0, nextM = 0;
    uint32_t nextDay1970 = 0;
    if (!computeNext(gState.mode, now, nextH, nextM, &nextDay1970)) {
        return 0xFFFFFFFF; // No next cycle
    }
    
    DateTime next(DateTime(nextDay1970 * 86400UL).year(),
                  DateTime(nextDay1970 * 86400UL).month(),
                  DateTime(nextDay1970 * 86400UL).day(),
                  nextH, nextM, 0);
    return next.unixtime();
}

// ─────────────────────────────────────────────────────────
bool Scheduler::isCycleExpired(uint32_t start_unix, uint32_t current_unix, uint32_t remaining_duration_sec) {
    // 1. Hard Limits
    const ModeSchedule& sched = MODE_SCHEDULES[(uint8_t)gState.mode];
    uint32_t max_allowed_duration = (sched.slot_count <= 1) ? 43200UL : 28800UL; // 12h or 8h
    if (current_unix < start_unix || (current_unix - start_unix) > max_allowed_duration) {
        LOG_W("SCHED", "Recuperacao descartada: Hard limit excedido.");
        return true;
    }

    // 2. Overlap / Missed Cycle
    DateTime currentDT(current_unix);
    SystemTime currentST = {};
    currentST.unix = current_unix;
    currentST.year = currentDT.year();
    currentST.month = currentDT.month();
    currentST.day = currentDT.day();
    currentST.hour = currentDT.hour();
    currentST.min = currentDT.minute();
    currentST.sec = currentDT.second();

    uint32_t next_cycle_unix = getNextCycleUnix(currentST);
    
    // Check if there was a scheduled slot between start and now
    // Create a mock SystemTime at start_unix to find the very next slot
    DateTime startDT(start_unix);
    SystemTime startST = {};
    startST.year = startDT.year();
    startST.month = startDT.month();
    startST.day = startDT.day();
    startST.hour = startDT.hour();
    startST.min = startDT.minute();
    startST.sec = startDT.second();
    
    uint32_t next_from_start = getNextCycleUnix(startST);
    if (next_from_start != 0xFFFFFFFF && next_from_start <= current_unix) {
         LOG_W("SCHED", "Recuperacao descartada: Atropelado por ciclo fantasma.");
         return true;
    }
    
    // 3. Safety Gap
    if (next_cycle_unix != 0xFFFFFFFF) {
        uint32_t estimated_end = current_unix + remaining_duration_sec;
        if ((uint64_t)estimated_end + 7200ULL > (uint64_t)next_cycle_unix) {
            LOG_W("SCHED", "Recuperacao descartada: Gap de segurança 2h desrespeitado.");
            return true;
        }
    }

    return false;
}

// ─────────────────────────────────────────────────────────
// Static: compute the next fire time for a given mode + current time.
// Returns false if mode has no schedule.
// ─────────────────────────────────────────────────────────
bool Scheduler::computeNext(AppMode mode, const SystemTime& now,
                             uint8_t& out_hour, uint8_t& out_min, uint32_t* out_day_1970) {
    const ModeSchedule& sched = MODE_SCHEDULES[(uint8_t)mode];

    if (sched.slot_count == 0) {
        out_hour = 0;
        out_min  = 0;
        return false;
    }

    // Use RTClib's DateTime for proper date arithmetic in local time
    DateTime current(now.year, now.month, now.day, now.hour, now.min, now.sec);
    uint32_t start_day_1970 = current.unixtime() / 86400UL;

    uint8_t dayOffset = 0;
    uint8_t limit = 15;

    // Fast-jump O(1) math for DayPattern::EVERY_X_DAYS
    if (sched.day_pattern == DayPattern::EVERY_X_DAYS && sched.interval_days > 0) {
        uint32_t ref_day = gState.custom_ref_day;
        
        if (ref_day != 0xFFFFFFFFUL) {
            if (start_day_1970 >= ref_day) {
                uint32_t diff = start_day_1970 - ref_day;
                uint32_t rem = diff % sched.interval_days;
                dayOffset = (rem > 0) ? (sched.interval_days - rem) : 0;
            } else {
                uint32_t diff = ref_day - start_day_1970;
                dayOffset = diff % sched.interval_days;
            }
            limit = dayOffset + sched.interval_days;
        }
    }

    while (dayOffset <= limit) {
        uint32_t candidate_day_1970 = start_day_1970 + dayOffset;

        if (_dayMatches(sched, candidate_day_1970)) {
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
                if (out_day_1970) *out_day_1970 = candidate_day_1970;
                return true;
            }
        }

        // Advance to next candidate day
        if (sched.day_pattern == DayPattern::EVERY_X_DAYS && sched.interval_days > 0) {
            dayOffset += sched.interval_days;
        } else {
            dayOffset++;
        }
    }
    // Fallback: show the first slot time if no exact match found in lookahead
    out_hour = sched.slots[0].hour;
    out_min  = sched.slots[0].minute;
    if (out_day_1970) *out_day_1970 = start_day_1970 + dayOffset;
    return false;
}

// ─────────────────────────────────────────────────────────
// Static: returns true if the schedule fires on the given day
// ─────────────────────────────────────────────────────────
bool Scheduler::_dayMatches(const ModeSchedule& sched, uint32_t candidate_day_1970) {
    switch (sched.day_pattern) {
        case DayPattern::DAILY:
            return true;
        case DayPattern::DOW_MASK: {
            // 1970-01-01 was Thursday (day 4). Map to 0=Sun, 1=Mon, ..., 6=Sat
            uint8_t dow = (candidate_day_1970 + 4) % 7;
            return (sched.dow_mask & (1 << dow)) != 0;
        }
        case DayPattern::EVERY_X_DAYS: {
            // First use: anchor the reference day to today and persist it
            if (gState.custom_ref_day == 0xFFFFFFFFUL || candidate_day_1970 < gState.custom_ref_day) {
                gState.custom_ref_day = candidate_day_1970;
                LOG_D("SCHED", "custom_ref_day definido/redefinido: %lu", candidate_day_1970);
            }
            uint32_t ref_day = gState.custom_ref_day;
            uint32_t diff = (candidate_day_1970 >= ref_day) ? (candidate_day_1970 - ref_day) : (ref_day - candidate_day_1970);
            if (sched.interval_days == 0) return true;  // guard: treat 0 as daily
            return (diff % sched.interval_days) == 0;
        }
        default:
            return false;
    }
}
