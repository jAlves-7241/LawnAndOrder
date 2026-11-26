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
    : _lastMin(0xFF), _lastSec(0xFF), _lastClockRev(0),
      _lastManualClockRev(0), _catchUpPending(false),
      _collisionCatchUpPending(false), _collisionHour(0), _collisionMin(0),
      _hasSkippedSlot(false), _skippedHour(0), _skippedMin(0),
      _triggersLoaded(false), _prevLocalWallUnix(0)
{
    memset(_lastTriggerUnix, 0, sizeof(_lastTriggerUnix));
}

void Scheduler::loadTriggers() {
    storage.loadLastTriggers(_lastTriggerUnix, MAX_SLOTS_PER_MODE);
    _triggersLoaded = true;
}

// ─────────────────────────────────────────────────────────
void Scheduler::begin() {
    _lastSec = 0xFF;
    _lastMin = 0xFF;
    _lastClockRev = rtclock.revision();
    _lastManualClockRev = rtclock.manualRevision();
    _prevLocalWallUnix = 0;
    _catchUpPending = gState.setup_done && !gState.suspended && !wateringCtrl.isActive();
    _collisionCatchUpPending = false;
    _collisionHour = 0;
    _collisionMin = 0;
    _hasSkippedSlot = false;
    _skippedHour = 0;
    _skippedMin = 0;
    if (!_triggersLoaded) {
        loadTriggers();
    }
    if (!gState.rtc_valid) return;
    uint32_t old_ref = gState.custom_ref_day;
    bool hasNext = computeNext(gState.mode, gState.now,
                               gState.next_hour, gState.next_min);
    if (gState.custom_ref_day != old_ref) storage.save();
    if (hasNext) {
        if (gState.suspended) {
            LOG_I(TXT_LOG_TAG_SCHEDULE, TXT_LOG_NEXT_WATERING_SUSP,
                  (int)gState.next_hour, (int)gState.next_min,
                  serialModeName(gState.mode));
        } else {
            LOG_I(TXT_LOG_TAG_SCHEDULE, TXT_LOG_NEXT_WATERING,
                  (int)gState.next_hour, (int)gState.next_min,
                  serialModeName(gState.mode));
        }
    } else {
        LOG_I(TXT_LOG_TAG_SCHEDULE, TXT_LOG_NEXT_WATERING_OFF);
    }
}

void Scheduler::clearLastTriggers() {
    memset(_lastTriggerUnix, 0, sizeof(_lastTriggerUnix));
    _triggersLoaded = true;
    storage.saveLastTriggers(_lastTriggerUnix, MAX_SLOTS_PER_MODE);
}

// ─────────────────────────────────────────────────────────
void Scheduler::update() {
    bool clockChanged = false;
    if (rtclock.revision() != _lastClockRev) {
        _lastClockRev = rtclock.revision();
        clockChanged = true;
    }
    if (rtclock.manualRevision() != _lastManualClockRev) {
        _lastManualClockRev = rtclock.manualRevision();
        clearLastTriggers();
        _catchUpPending = false;
        _collisionCatchUpPending = false;
        _hasSkippedSlot = false;
        _prevLocalWallUnix = 0;
        clockChanged = true;
    }

    if (!gState.setup_done) {
        _catchUpPending = false;
        _collisionCatchUpPending = false;
        _prevLocalWallUnix = 0;
        return;
    }

    if (!gState.rtc_valid)  return;

    // Apenas processar se o segundo do relogio tiver mudado ou apos salto/acerto de relogio
    if (!clockChanged && gState.now.sec == _lastSec) return;
    _lastSec = gState.now.sec;
    if (clockChanged) {
        _lastMin = 0xFF;
    }

    const SystemTime& t = gState.now;

    // Auto-wake from suspension
    if (gState.suspended) {
        _catchUpPending = false;
        _collisionCatchUpPending = false;
        _prevLocalWallUnix = 0;
        if (t.unix >= gState.suspended_until) {
            gState.suspended = false;
            gState.suspended_until = 0;
            _lastMin = 0xFF; // Force schedule evaluation to avoid skip if cleared on same minute
            storage.save();
            LOG_I(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_SUSP_EXP);
        } else {
            return; // Still suspended
        }
    }

    if ((uint8_t)gState.mode >= (uint8_t)AppMode::_COUNT) return;

    // On minute rollover, reset trigger guard and recompute next_*
    if (t.min != _lastMin) {
        _lastMin   = t.min;
        uint32_t localWallUnix = DateTime(t.year, t.month, t.day, t.hour, t.min, 0).unixtime();
        if (_prevLocalWallUnix > 0 && localWallUnix > _prevLocalWallUnix) {
            uint32_t wallJump = localWallUnix - _prevLocalWallUnix;
            if (wallJump > 60UL && wallJump <= SCHED_WALL_JUMP_MAX_S) {
                _catchUpPending = true;
            }
        }
        _prevLocalWallUnix = localWallUnix;

        // Recompute so next_* is always fresh (catches mode changes too)
        uint32_t old_ref = gState.custom_ref_day;
        computeNext(gState.mode, t,
                    gState.next_hour, gState.next_min);
        if (gState.custom_ref_day != old_ref) storage.save();

        const ModeSchedule& sched = MODE_SCHEDULES[(uint8_t)gState.mode];
        if (sched.slot_count > 0) {
            DateTime localDate(t.year, t.month, t.day, 0, 0, 0);
            uint32_t current_day_1970 = localDate.unixtime() / 86400UL;

            if (_dayMatches(sched, current_day_1970)) {
                for (uint8_t i = 0; i < sched.slot_count; i++) {
                    if (t.hour == sched.slots[i].hour &&
                        t.min  == sched.slots[i].minute) {
                        _catchUpPending = false;
                        // DST fall-back guard: if THIS SPECIFIC SLOT fired less than
                        // DST_DUP_WINDOW_S (2h UTC) ago, this is a duplicate from the repeated hour.
                        // Same slot on consecutive days is >=24h apart, so 2h is safe.
                        if (_lastTriggerUnix[i] > 0 && t.unix >= _lastTriggerUnix[i] &&
                            (t.unix - _lastTriggerUnix[i]) < DST_DUP_WINDOW_S) {
                            LOG_W(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_DUP_BLOCK,
                                  (int)sched.slots[i].hour, (int)sched.slots[i].minute);
                            break;
                        }
                        if (!hasEnabledZones()) {
                            LOG_W(TXT_LOG_TAG_WATER, TXT_LOG_START_NO_ZONES);
                            break;
                        }
                        if (gState.watering.active) {
                            _handleSlotCollision(i, sched.slots[i].hour, sched.slots[i].minute, t.unix);
                            break;
                        }
                        if (!wateringCtrl.startGeneral(WaterTrigger::AUTO)) {
                            break;
                        }
                        if (_collisionCatchUpPending &&
                            (_collisionHour != sched.slots[i].hour || _collisionMin != sched.slots[i].minute)) {
                            LOG_W(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_COLLISION_SKIP,
                                  (int)_collisionHour, (int)_collisionMin);
                        }
                        _collisionCatchUpPending = false;
                        _hasSkippedSlot = false;
                        _lastTriggerUnix[i] = t.unix;
                        storage.saveLastTriggers(_lastTriggerUnix, MAX_SLOTS_PER_MODE);
                        LOG_I(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_AUTO_START,
                              (int)t.hour, (int)t.min);
                        return;
                    }
                }
            }
        }

        if (_catchUpPending) {
            _catchUpPending = false;
            if (sched.slot_count > 0) {
                if (!gState.watering.active) {
                    _tryCatchUp(t);
                } else {
                    uint8_t cIdx = 0, cHour = 0, cMin = 0;
                    if (_findCatchUpSlot(t, false, cIdx, cHour, cMin) &&
                        !(_lastTriggerUnix[cIdx] > 0 && t.unix >= _lastTriggerUnix[cIdx] &&
                          (t.unix - _lastTriggerUnix[cIdx]) < DST_DUP_WINDOW_S) &&
                        hasEnabledZones()) {
                        _handleSlotCollision(cIdx, cHour, cMin, t.unix);
                    }
                }
            }
        }
    }

    // Retry a slot that collided with a TEST or CUSTOM run once watering ends
    if (_collisionCatchUpPending && !gState.watering.active) {
        _collisionCatchUpPending = false;
        if (!_tryCatchUp(t, true)) {
            _hasSkippedSlot = true;
            _skippedHour = _collisionHour;
            _skippedMin  = _collisionMin;
            LOG_W(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_COLLISION_SKIP,
                  (int)_skippedHour, (int)_skippedMin);
        }
    }
}

bool Scheduler::hasEnabledZones() {
    for (int z = 0; z < NUM_ZONES; z++) {
        if (gState.zones[z].enabled && gState.zones[z].duration_min > 0) {
            return true;
        }
    }
    return false;
}

uint32_t Scheduler::_resolveSlotUtc(const DateTime& slotLocal, uint32_t afterUtc, bool* outDstSkipped) {
    DateTime slotUtc = RTClock::localToUtc(slotLocal, afterUtc);
    DateTime roundTrip = RTClock::utcToLocal(slotUtc);
    uint32_t effSlotUtc = slotUtc.unixtime();
    const bool dstSkipped = (roundTrip.hour() != slotLocal.hour() ||
                             roundTrip.minute() != slotLocal.minute());
    if (dstSkipped) {
        const uint32_t minSec = (uint32_t)slotLocal.minute() * 60UL;
        if (effSlotUtc >= minSec) effSlotUtc -= minSec;
    }
    if (outDstSkipped) *outDstSkipped = dstSkipped;
    return effSlotUtc;
}

void Scheduler::_handleSlotCollision(uint8_t slotIdx, uint8_t hour, uint8_t min, uint32_t nowUnix) {
    WaterTrigger activeTrig = wateringCtrl.activeTrigger();
    if (activeTrig == WaterTrigger::TEST || activeTrig == WaterTrigger::CUSTOM) {
        if (_collisionCatchUpPending && (_collisionHour != hour || _collisionMin != min)) {
            LOG_W(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_COLLISION_SKIP,
                  (int)_collisionHour, (int)_collisionMin);
        }
        _collisionCatchUpPending = true;
        _collisionHour = hour;
        _collisionMin  = min;
        LOG_W(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_COLLISION_DEFER,
              (int)_collisionHour, (int)_collisionMin);
    } else {
        _collisionCatchUpPending = false;
        _hasSkippedSlot = true;
        _skippedHour = hour;
        _skippedMin  = min;
        if (slotIdx < MAX_SLOTS_PER_MODE) {
            _lastTriggerUnix[slotIdx] = nowUnix;
            storage.saveLastTriggers(_lastTriggerUnix, MAX_SLOTS_PER_MODE);
        }
        LOG_W(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_COLLISION_SKIP,
              (int)_skippedHour, (int)_skippedMin);
    }
}

bool Scheduler::_findCatchUpSlot(const SystemTime& t, bool allowExactSecond,
                                 uint8_t& outIdx, uint8_t& outHour, uint8_t& outMin) {
    if ((uint8_t)gState.mode >= (uint8_t)AppMode::_COUNT) return false;
    const ModeSchedule& sched = MODE_SCHEDULES[(uint8_t)gState.mode];
    if (sched.slot_count == 0) return false;

    DateTime localDate(t.year, t.month, t.day, 0, 0, 0);
    uint32_t currentDay1970 = localDate.unixtime() / 86400UL;

    bool found = false;
    uint8_t bestIdx = 0;
    uint8_t bestHour = 0;
    uint8_t bestMin = 0;
    uint32_t bestAge = 0xFFFFFFFFUL;

    for (int dayOff = 0; dayOff >= -1; dayOff--) {
        if (dayOff < 0 && currentDay1970 == 0) continue;
        if (dayOff < 0 && sched.day_pattern == DayPattern::EVERY_X_DAYS &&
            gState.custom_ref_day == 0xFFFFFFFFUL) {
            continue;
        }
        uint32_t candDay = (uint32_t)((int32_t)currentDay1970 + dayOff);
        if (!_dayMatches(sched, candDay, false)) continue;

        DateTime candDate(candDay * 86400UL);
        for (uint8_t i = 0; i < sched.slot_count; i++) {
            const ScheduleSlot& sl = sched.slots[i];
            DateTime slotLocal(candDate.year(), candDate.month(), candDate.day(),
                               sl.hour, sl.minute, 0);
            const uint32_t afterUtc = (t.unix > SCHED_CATCHUP_WINDOW_S + 1UL)
                ? (t.unix - SCHED_CATCHUP_WINDOW_S - 1UL)
                : 0UL;
            bool dstSkipped = false;
            uint32_t effSlotUtc = _resolveSlotUtc(slotLocal, afterUtc, &dstSkipped);
            if (t.unix < effSlotUtc) continue;
            if (!dstSkipped && !allowExactSecond && t.unix == effSlotUtc) continue;

            uint32_t age = t.unix - effSlotUtc;
            if (age <= SCHED_CATCHUP_WINDOW_S && (!found || age < bestAge)) {
                found = true;
                bestIdx = i;
                bestHour = sl.hour;
                bestMin = sl.minute;
                bestAge = age;
            }
        }
    }

    if (!found) return false;
    outIdx  = bestIdx;
    outHour = bestHour;
    outMin  = bestMin;
    return true;
}

bool Scheduler::_tryCatchUp(const SystemTime& t, bool allowExactSecond) {
    uint8_t bestIdx = 0;
    uint8_t bestHour = 0;
    uint8_t bestMin = 0;
    if (!_findCatchUpSlot(t, allowExactSecond, bestIdx, bestHour, bestMin)) {
        return false;
    }
    (void)bestHour;
    (void)bestMin;

    if (_lastTriggerUnix[bestIdx] > 0 && t.unix >= _lastTriggerUnix[bestIdx] &&
        (t.unix - _lastTriggerUnix[bestIdx]) < DST_DUP_WINDOW_S) {
        return false;
    }

    uint32_t cycleSec = 0;
    uint8_t enabledZones = 0;
    for (int z = 0; z < NUM_ZONES; z++) {
        if (gState.zones[z].enabled && gState.zones[z].duration_min > 0) {
            cycleSec += (uint32_t)gState.zones[z].duration_min * 60UL;
            enabledZones++;
        }
    }
    if (enabledZones == 0) return false;
    if (enabledZones > 1) {
        cycleSec += ((uint32_t)(enabledZones - 1) * ZONE_WAIT_DELAY_MS) / 1000UL;
    }
    cycleSec += ((uint32_t)enabledZones * RELAY_DEADTIME_MS + 999UL) / 1000UL;

    uint32_t nextCycleUnix = getNextCycleUnix(t);
    if (nextCycleUnix != 0xFFFFFFFFUL &&
        (uint64_t)t.unix + (uint64_t)cycleSec >= (uint64_t)nextCycleUnix) {
        return false;
    }

    if (!wateringCtrl.startGeneral(WaterTrigger::AUTO)) {
        return false;
    }
    _hasSkippedSlot = false;
    _lastTriggerUnix[bestIdx] = t.unix;
    storage.saveLastTriggers(_lastTriggerUnix, MAX_SLOTS_PER_MODE);
    LOG_I(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_CATCHUP, (int)bestHour, (int)bestMin);
    return true;
}

// ─────────────────────────────────────────────────────────
bool Scheduler::refreshNext() {
    if ((uint8_t)gState.mode >= (uint8_t)AppMode::_COUNT) return false;
    _lastClockRev = rtclock.revision();
    _lastManualClockRev = rtclock.manualRevision();
    _lastSec = 0xFF;
    _catchUpPending = false;
    _collisionCatchUpPending = false;
    _prevLocalWallUnix = 0;
    if (gState.rtc_valid) {
        _lastMin = gState.now.min; // Align with current minute to prevent mid-minute double trigger
    } else {
        _lastMin = 0xFF;
    }
    if (!gState.rtc_valid) {
        // RTC not ready - seed next_* from the first slot of the current mode
        const ModeSchedule& sched = MODE_SCHEDULES[(uint8_t)gState.mode];
        if (sched.slot_count > 0) {
            gState.next_hour = sched.slots[0].hour;
            gState.next_min  = sched.slots[0].minute;
            return true;
        }
        gState.next_hour = 0;
        gState.next_min  = 0;
        return false;
    }
    uint32_t old_ref = gState.custom_ref_day;
    bool hasNext = computeNext(gState.mode, gState.now,
                               gState.next_hour, gState.next_min);
    if (gState.custom_ref_day != old_ref) storage.save();
    return hasNext;
}

// ─────────────────────────────────────────────────────────
void Scheduler::onModeChanged(bool announceMode) {
    if ((uint8_t)gState.mode >= (uint8_t)AppMode::_COUNT) return;
    _hasSkippedSlot = false;
    clearLastTriggers();
    bool hasNext = refreshNext();
    if (!gState.rtc_valid) {
        if (announceMode) {
            LOG_I(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_MODE_WAIT_CLK,
                  serialModeName(gState.mode));
        }
        return;
    }
    if (announceMode) {
        if (hasNext) {
            LOG_I(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_MODE_CHG,
                  serialModeName(gState.mode),
                  (int)gState.next_hour, (int)gState.next_min);
        } else {
            LOG_I(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_MODE_CHG_OFF,
                  serialModeName(gState.mode));
        }
    }
}

// ─────────────────────────────────────────────────────────
void Scheduler::onWateringDone() {
    if (!gState.rtc_valid) return;
    // computeNext() already skips slots at or before now.hour:now.min (<=),
    // so pass gState.now directly to avoid skipping an upcoming slot on the next minute.
    uint32_t old_ref = gState.custom_ref_day;
    bool hasNext = computeNext(gState.mode, gState.now,
                               gState.next_hour, gState.next_min);
    if (gState.custom_ref_day != old_ref) storage.save();
    if (hasNext) {
        LOG_I(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_CYCLE_DONE,
              (int)gState.next_hour, (int)gState.next_min);
    } else {
        LOG_I(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_CYCLE_DONE_OFF);
    }
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
    return _resolveSlotUtc(nextLocal, now.unix);
}

// ─────────────────────────────────────────────────────────
bool Scheduler::isCycleExpired(uint32_t start_unix, const SystemTime& current_time, uint32_t remaining_duration_sec) {
    // 1. Limits
    const uint32_t max_cycle_sec = (uint32_t)NUM_ZONES * 20UL * 60UL;
    if (remaining_duration_sec == 0 || remaining_duration_sec > max_cycle_sec) {
#if LOG_LEVEL >= LVL_WARN
        unsigned long rem_min = (remaining_duration_sec == 0)
            ? 0UL
            : (unsigned long)((remaining_duration_sec - 1UL) / 60UL + 1UL);
        LOG_W(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_REC_DUR,
              rem_min, (unsigned long)(max_cycle_sec / 60UL));
#endif
        return true;
    }
    uint32_t current_unix = current_time.unix;
    if (start_unix < UNIX_MIN_VALID || start_unix > UNIX_MAX_VALID) {
        LOG_W(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_REC_BACKWARD);
        return true;
    }
    if ((uint8_t)gState.mode >= (uint8_t)AppMode::_COUNT) {
        LOG_W(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_REC_MODE, (unsigned)gState.mode);
        return true;
    }
    const uint32_t max_allowed_duration = RECOVERY_MAX_AGE_S;
    if (current_unix < start_unix) {
        LOG_W(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_REC_BACKWARD);
        return true;
    }
    const uint32_t outage_sec = current_unix - start_unix;
    if (outage_sec > max_allowed_duration) {
#if LOG_LEVEL >= LVL_WARN
        const uint32_t outage_min = (outage_sec / 60UL) + ((outage_sec % 60UL) ? 1UL : 0UL);
        LOG_W(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_REC_AGE,
              (unsigned long)outage_min,
              (unsigned long)(max_allowed_duration / 60UL));
#endif
        return true;
    }

    // Ensure persisted DST fall-back trigger guards are loaded even if called before begin()
    if (!_triggersLoaded) {
        bool hasAny = false;
        for (uint8_t i = 0; i < MAX_SLOTS_PER_MODE; i++) {
            if (_lastTriggerUnix[i] != 0) {
                hasAny = true;
                break;
            }
        }
        if (!hasAny) {
            loadTriggers();
        } else {
            _triggersLoaded = true;
        }
    }

    // If no zone is enabled, no automatic cycle can execute to collide with recovery
    if (!hasEnabledZones()) {
        return false;
    }

    // Overlap / Missed Cycle
    // Check if there was a scheduled slot between start and now
    // Create a mock SystemTime at start_unix to find the very next slot
    DateTime localDT = RTClock::utcToLocal(DateTime(start_unix));
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
    if (next_from_start != 0xFFFFFFFFUL && next_from_start <= current_unix) {
        // Ignore scheduled slots that were suppressed by an active suspension
        if (!(gState.suspended && gState.suspended_until > 0 &&
              (uint64_t)next_from_start + 59ULL < (uint64_t)gState.suspended_until)) {
            LOG_W(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_REC_MISSED);
            return true;
        }
    }

    // 3. Safety Gap
    uint64_t estimated_end = (uint64_t)current_unix + (uint64_t)remaining_duration_sec;
    if (gState.suspended && gState.suspended_until > 0 &&
        (uint64_t)gState.suspended_until > estimated_end + (uint64_t)SAFETY_GAP_SEC + 59ULL) {
        return false; // Suspension outlasts recovery + safety gap; no auto cycle can run
    }

    uint32_t next_cycle_unix = getNextCycleUnix(current_time);
    if (gState.suspended && gState.suspended_until > 0) {
        // Advance past any slots suppressed by the active suspension
        uint8_t guard = 0;
        while (next_cycle_unix != 0xFFFFFFFFUL &&
               (uint64_t)next_cycle_unix + 59ULL < (uint64_t)gState.suspended_until &&
               guard++ < (uint8_t)(MAX_SLOTS_PER_MODE * 2U)) {
            DateTime stepLocal = RTClock::utcToLocal(DateTime(next_cycle_unix));
            SystemTime stepST = {};
            stepST.year = stepLocal.year();
            stepST.month = stepLocal.month();
            stepST.day = stepLocal.day();
            stepST.hour = stepLocal.hour();
            stepST.min = stepLocal.minute();
            stepST.sec = stepLocal.second();
            stepST.dow = stepLocal.dayOfTheWeek();
            stepST.unix = next_cycle_unix;
            uint32_t adv = getNextCycleUnix(stepST);
            if (adv == 0xFFFFFFFFUL || adv <= next_cycle_unix) {
                next_cycle_unix = 0xFFFFFFFFUL;
                break;
            }
            next_cycle_unix = adv;
        }
        if (next_cycle_unix != 0xFFFFFFFFUL &&
            (uint64_t)next_cycle_unix + 59ULL < (uint64_t)gState.suspended_until) {
            next_cycle_unix = 0xFFFFFFFFUL;
        }
    }

    if (next_cycle_unix != 0xFFFFFFFFUL) {
        if (estimated_end + (uint64_t)SAFETY_GAP_SEC > (uint64_t)next_cycle_unix) {
            LOG_W(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_REC_GAP,
                  (unsigned long)(SAFETY_GAP_SEC / 60UL));
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
            bool found = false;
            uint16_t bestMins = 0xFFFF;
            for (uint8_t i = 0; i < sched.slot_count; i++) {
                const ScheduleSlot& sl = sched.slots[i];
                
                // For today (dayOffset==0) skip slots already past (or already triggered
                // within the 2h DST fall-back duplicate window on the active mode).
                if (dayOffset == 0) {
                    if (sl.hour < now.hour) continue;
                    if (sl.hour == now.hour && sl.minute <= now.min) continue;
                    if (mode == gState.mode && now.unix >= UNIX_MIN_VALID &&
                        _lastTriggerUnix[i] > 0) {
                        DateTime slotLocal(now.year, now.month, now.day, sl.hour, sl.minute, 0);
                        uint32_t slotUtc = _resolveSlotUtc(slotLocal, now.unix);
                        if (slotUtc >= _lastTriggerUnix[i] &&
                            (slotUtc - _lastTriggerUnix[i]) < DST_DUP_WINDOW_S) {
                            continue;
                        }
                    }
                }
                uint16_t slotMins = (uint16_t)sl.hour * 60U + sl.minute;
                if (!found || slotMins < bestMins) {
                    bestMins = slotMins;
                    out_hour = sl.hour;
                    out_min  = sl.minute;
                    found    = true;
                }
            }
            if (found) {
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
#if LOG_LEVEL >= LVL_DEBUG
                    DateTime refDt(candidate_day_1970 * 86400UL);
                    LOG_D(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SCHED_CUSTOM_DAY,
                          (int)refDt.year(), (int)refDt.month(), (int)refDt.day(),
                          (unsigned)sched.interval_days);
#endif
                    ref_day = candidate_day_1970;
                } else {
                    ref_day = candidate_day_1970;
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
