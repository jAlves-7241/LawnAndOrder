#include "i18n.h"
#include "Scheduler.h"
#include "WateringController.h"
#include "Storage.h"
#include "RTClock.h"
#include <RTClib.h>
#include "log.h"

Scheduler scheduler;

// ─────────────────────────────────────────────────────────
Scheduler::Scheduler()
    : _lastMin(0xFF)
{
    memset(_lastTriggerUnix, 0, sizeof(_lastTriggerUnix));
}

// ─────────────────────────────────────────────────────────
void Scheduler::begin() {
    if (!gState.rtc_valid) return;
    uint32_t old_ref = gState.custom_ref_day;
    computeNext(gState.mode, gState.now,
                gState.next_hour, gState.next_min);
    if (gState.custom_ref_day != old_ref) storage.save();
    LOG_I("SCHED", TXT_LOG_NEXT_WATERING,
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
            _lastMin = 0xFF; // Force schedule evaluation to avoid skip if cleared on same minute
            storage.save();
            LOG_I("SCHED", "Suspensao expirada - rega reativada");
        } else {
            return; // Still suspended
        }
    }

    if ((uint8_t)gState.mode >= (uint8_t)AppMode::_COUNT) return;

    // On minute rollover, reset trigger guard and recompute next_*
    if (t.min != _lastMin) {
        _lastMin   = t.min;
        // Recompute so next_* is always fresh (catches mode changes too)
        uint32_t old_ref = gState.custom_ref_day;
        computeNext(gState.mode, t,
                    gState.next_hour, gState.next_min);
        if (gState.custom_ref_day != old_ref) storage.save();

        // Don't trigger while a cycle is already running
        if (gState.watering.active) return;

        const ModeSchedule& sched = MODE_SCHEDULES[(uint8_t)gState.mode];
        if (sched.slot_count > 0) {
            DateTime localDate(t.year, t.month, t.day, 0, 0, 0);
            uint32_t current_day_1970 = localDate.unixtime() / 86400UL;

            if (_dayMatches(sched, current_day_1970)) {
                for (uint8_t i = 0; i < sched.slot_count; i++) {
                    if (t.hour == sched.slots[i].hour &&
                        t.min  == sched.slots[i].minute) {
                        // DST fall-back guard: if THIS SPECIFIC SLOT fired less than
                        // 2 hours ago (UTC), this is a duplicate from the repeated hour.
                        // Same slot on consecutive days is ≥24h apart, so 7200s is safe.
                        if (_lastTriggerUnix[i] > 0 && t.unix >= _lastTriggerUnix[i] &&
                            (t.unix - _lastTriggerUnix[i]) < 7200) {
                            LOG_W("SCHED", TXT_LOG_SCHED_DUP_BLOCK,
                                  sched.slots[i].hour, sched.slots[i].minute);
                            break;
                        }
                        _lastTriggerUnix[i] = t.unix;
                        LOG_I("SCHED", TXT_LOG_SCHED_AUTO_START,
                                      t.hour, t.min);
                        wateringCtrl.startGeneral(WaterTrigger::AUTO);
                        break;
                    }
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────
void Scheduler::onModeChanged() {
    memset(_lastTriggerUnix, 0, sizeof(_lastTriggerUnix));
    _lastMin = 0xFF; // Force schedule evaluation
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
    LOG_I("SCHED", TXT_LOG_SCHED_MODE_CHG,
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
    LOG_I("SCHED", TXT_LOG_SCHED_CYCLE_DONE,
                  gState.next_hour, gState.next_min);
}

// ─────────────────────────────────────────────────────────
uint32_t Scheduler::getNextCycleUnix(SystemTime now) {
    uint8_t nextH = 0, nextM = 0;
    uint32_t nextDay1970 = 0;
    if (!computeNext(gState.mode, now, nextH, nextM, &nextDay1970, false)) {
        return 0xFFFFFFFF; // No next cycle
    }
    
    DateTime dt(nextDay1970 * 86400UL);
    DateTime nextLocal(dt.year(), dt.month(), dt.day(), nextH, nextM, 0);
    uint32_t nextLocalUnix = nextLocal.unixtime();
    uint32_t nextUTCUnix = nextLocalUnix - (TIMEZONE_OFFSET * 3600);
    
    if (gState.auto_dst) {
        // Fast heuristic for Local -> UTC DST check
        // LIMITATION: During the ambiguous hour of October DST end (01:00-02:00 local),
        // we default to assuming DST is still active without sub-hour history.
        bool isDstLocal = false;
        uint16_t year = nextLocal.year();
        uint8_t month = nextLocal.month();
        uint8_t day = nextLocal.day();
        uint8_t hour = nextLocal.hour();
        
        if (month > 3 && month < 10) isDstLocal = true;
        else if (month == 3 || month == 10) {
            uint8_t ls = 31 - DateTime(year, month, 31, 0, 0, 0).dayOfTheWeek();
            if (month == 3) {
                if (day > ls || (day == ls && hour >= (1 + TIMEZONE_OFFSET))) isDstLocal = true;
            } else {
                if (day < ls || (day == ls && hour < (2 + TIMEZONE_OFFSET))) isDstLocal = true;
            }
        }
        if (isDstLocal) {
            nextUTCUnix = nextLocalUnix - 3600;
        }
    }
    return nextUTCUnix;
}

// ─────────────────────────────────────────────────────────
bool Scheduler::isCycleExpired(uint32_t start_unix, const SystemTime& current_time, uint32_t remaining_duration_sec) {
    // 1. Hard Limits
    uint32_t current_unix = current_time.unix;
    if ((uint8_t)gState.mode >= (uint8_t)AppMode::_COUNT) return true;
    const ModeSchedule& sched = MODE_SCHEDULES[(uint8_t)gState.mode];
    uint32_t max_allowed_duration = (sched.slot_count <= 1) ? 43200UL : 28800UL; // 12h or 8h
    if (current_unix < start_unix || (current_unix - start_unix) > max_allowed_duration) {
        LOG_W("SCHED", TXT_LOG_SCHED_REC_HLIMIT);
        return true;
    }

    // Overlap / Missed Cycle
    uint32_t next_cycle_unix = getNextCycleUnix(current_time);
    
    // Check if there was a scheduled slot between start and now
    // Create a mock SystemTime at start_unix to find the very next slot
    DateTime localDT = rtclock.utcToLocal(DateTime(start_unix));
    SystemTime startST = {};
    startST.year = localDT.year();
    startST.month = localDT.month();
    startST.day = localDT.day();
    startST.hour = localDT.hour();
    startST.min = localDT.minute();
    startST.sec = localDT.second();
    startST.dow = localDT.dayOfTheWeek();
    startST.unix = start_unix;
    
    uint32_t next_from_start = getNextCycleUnix(startST);
    if (next_from_start != 0xFFFFFFFF && next_from_start <= current_unix) {
         LOG_W("SCHED", TXT_LOG_SCHED_REC_GHOST);
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
                             uint8_t& out_hour, uint8_t& out_min, uint32_t* out_day_1970, bool writeBack) {
    if ((uint8_t)mode >= (uint8_t)AppMode::_COUNT) {
        out_hour = 0; out_min = 0;
        return false;
    }
    const ModeSchedule& sched = MODE_SCHEDULES[(uint8_t)mode];

    if (sched.slot_count == 0) {
        out_hour = 0;
        out_min  = 0;
        return false;
    }

    // Use RTClib's DateTime for proper date arithmetic in local time
    DateTime current(now.year, now.month, now.day, now.hour, now.min, now.sec);
    uint32_t start_day_1970 = current.unixtime() / 86400UL;

    uint16_t dayOffset = 0;
    uint16_t limit = 15;

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
        }
        limit = dayOffset + sched.interval_days;
    }

    while (dayOffset <= limit) {
        uint32_t candidate_day_1970 = start_day_1970 + dayOffset;

        if (_dayMatches(sched, candidate_day_1970, writeBack)) {
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
bool Scheduler::_dayMatches(const ModeSchedule& sched, uint32_t candidate_day_1970, bool writeBack) {
    switch (sched.day_pattern) {
        case DayPattern::DAILY:
            return true;
        case DayPattern::DOW_MASK: {
            // 1970-01-01 was Thursday (day 4). Map to 0=Sun, 1=Mon, ..., 6=Sat
            uint8_t dow = (candidate_day_1970 + 4) % 7;
            return (sched.dow_mask & (1 << dow)) != 0;
        }
        case DayPattern::EVERY_X_DAYS: {
            uint32_t ref_day = gState.custom_ref_day;
            if (ref_day == 0xFFFFFFFFUL) {
                if (writeBack) {
                    gState.custom_ref_day = candidate_day_1970;
                    LOG_D("SCHED", TXT_LOG_SCHED_CUSTOM_DAY, candidate_day_1970);
                    ref_day = candidate_day_1970;
                } else {
                    DateTime current(gState.now.year, gState.now.month, gState.now.day, 0, 0, 0);
                    ref_day = current.unixtime() / 86400UL;
                }
            }
            uint32_t diff = (candidate_day_1970 >= ref_day) ? (candidate_day_1970 - ref_day) : (ref_day - candidate_day_1970);
            if (sched.interval_days == 0) return true;  // guard: treat 0 as daily
            return (diff % sched.interval_days) == 0;
        }
        default:
            return false;
    }
}
