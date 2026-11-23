#include <unity.h>
#include "TestCommon.h"
#include "../../test/mocks/TestCommon.cpp"
#include "../../src/AppState.cpp"
#include "../../src/Storage.cpp"

#include "RTClock.h"
#include "History.h"
#include "WateringController.h"
#include "Scheduler.h"

#include "../../src/RTClock.cpp"
#include "../../src/History.cpp"
#include "../../src/WateringController.cpp"
#include "../../src/Scheduler.cpp"

void setUp(void) {
    resetAllTestMocks();
    wateringCtrl.begin();
    gState.rtc_valid = true;
    gState.setup_done = true;
}

void tearDown(void) {
    wateringCtrl.stop();
}

struct UnitTestAccess {
static void test_scheduler_medio_next_slot(void) {
    gState.mode = AppMode::MEDIO;

    // Morning: 05:00 -> next is slot 0 (08:00)
    SystemTime now = { 2026, 6, 15, 5, 0, 0, 1, 1781500000UL };
    uint8_t nextH = 0, nextM = 0;
    bool ok = scheduler.computeNext(AppMode::MEDIO, now, nextH, nextM);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT0_H, nextH);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT0_M, nextM);

    // Midday: 12:00 -> next is slot 1 (20:00)
    now.hour = 12;
    ok = scheduler.computeNext(AppMode::MEDIO, now, nextH, nextM);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT1_H, nextH);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT1_M, nextM);

    // Late night: 22:00 -> wraps to tomorrow slot 0 (08:00)
    now.hour = 22;
    ok = scheduler.computeNext(AppMode::MEDIO, now, nextH, nextM);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT0_H, nextH);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT0_M, nextM);
}

static void test_scheduler_intenso_next_slot(void) {
    gState.mode = AppMode::INTENSO;

    SystemTime now = { 2026, 6, 15, 4, 0, 0, 1, 1781500000UL };
    uint8_t nextH = 0, nextM = 0;

    // 04:00 -> next is slot 0 (07:00)
    bool ok = scheduler.computeNext(AppMode::INTENSO, now, nextH, nextM);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(SCHED_INTENSO_SLOT0_H, nextH);

    // 10:00 -> next is slot 1 (13:00)
    now.hour = 10;
    ok = scheduler.computeNext(AppMode::INTENSO, now, nextH, nextM);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(SCHED_INTENSO_SLOT1_H, nextH);

    // 16:00 -> next is slot 2 (21:00)
    now.hour = 16;
    ok = scheduler.computeNext(AppMode::INTENSO, now, nextH, nextM);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(SCHED_INTENSO_SLOT2_H, nextH);

    // 23:00 -> wraps to tomorrow slot 0 (07:00)
    now.hour = 23;
    ok = scheduler.computeNext(AppMode::INTENSO, now, nextH, nextM);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(SCHED_INTENSO_SLOT0_H, nextH);
}

static void test_scheduler_desativado_returns_false(void) {
    SystemTime now = { 2026, 6, 15, 12, 0, 0, 1, 1781500000UL };
    uint8_t nextH = 99, nextM = 99;
    bool ok = scheduler.computeNext(AppMode::DESATIVADO, now, nextH, nextM);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_UINT8(0, nextH);
    TEST_ASSERT_EQUAL_UINT8(0, nextM);
}

static void test_scheduler_custom_every_x_days(void) {
    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.slot_count = 1;
    cs.interval_days = 3; // Every 3 days
    cs.day_pattern = DayPattern::EVERY_X_DAYS;
    cs.slots[0].hour = 8;
    cs.slots[0].minute = 0;

    // Use day 20000 as reference day
    gState.custom_ref_day = 20000;

    DateTime dtRef(20000 * 86400UL);
    SystemTime now = { dtRef.year(), dtRef.month(), dtRef.day(), 6, 0, 0, dtRef.dayOfTheWeek(), dtRef.unixtime() };

    uint8_t nextH = 0, nextM = 0;
    uint32_t nextDay = 0;

    // On reference day before 08:00 -> fires today (day 20000) at 08:00
    bool ok = scheduler.computeNext(AppMode::PERSONALIZADO, now, nextH, nextM, &nextDay, false);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(8, nextH);
    TEST_ASSERT_EQUAL_UINT8(0, nextM);
    TEST_ASSERT_EQUAL_UINT32(20000, nextDay);

    // On reference day after 08:00 (e.g. 10:00) -> next must be day 20003!
    now.hour = 10;
    ok = scheduler.computeNext(AppMode::PERSONALIZADO, now, nextH, nextM, &nextDay, false);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(8, nextH);
    TEST_ASSERT_EQUAL_UINT32(20003, nextDay);

    // On day 20001 (1 day after reference) -> next must still be day 20003!
    DateTime dt1(20001 * 86400UL);
    now = { dt1.year(), dt1.month(), dt1.day(), 10, 0, 0, dt1.dayOfTheWeek(), dt1.unixtime() };
    ok = scheduler.computeNext(AppMode::PERSONALIZADO, now, nextH, nextM, &nextDay, false);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(20003, nextDay);
}

static void test_scheduler_cycle_expired_check(void) {
    gState.mode = AppMode::MEDIO;
    uint32_t startUnix = 1781500000UL;

    // Current time is 15 hours after start (> 8h max allowed duration) -> expired!
    SystemTime current = { 2026, 6, 15, 15, 0, 0, 1, (uint32_t)(startUnix + 15UL * 3600UL) };
    bool expired = scheduler.isCycleExpired(startUnix, current, 600);
    TEST_ASSERT_TRUE(expired);

    // Current time is before start time (clock went backwards) -> expired!
    current.unix = startUnix - 100;
    expired = scheduler.isCycleExpired(startUnix, current, 600);
    TEST_ASSERT_TRUE(expired);
}

static void test_scheduler_suspension_auto_wake(void) {
    gState.mode = AppMode::MEDIO;
    gState.suspended = true;
    gState.suspended_until = 1781510000UL;

    // Current time is before suspended_until -> stays suspended
    gState.now = { 2026, 6, 15, 10, 0, 0, 1, 1781500000UL };
    scheduler.update();
    TEST_ASSERT_TRUE(gState.suspended);

    // Advance time past suspended_until -> automatically unsuspends!
    gState.now.sec = 1; // trigger second change
    gState.now.unix = 1781510001UL;
    scheduler.update();
    TEST_ASSERT_FALSE(gState.suspended);
    TEST_ASSERT_EQUAL_UINT32(0, gState.suspended_until);
}

static void test_scheduler_month_end_and_leap_year_rollover(void) {
    gState.mode = AppMode::MEDIO;

    // 1. January 31 at 23:00 -> next is tomorrow (Feb 1) at slot 0 (08:00)
    DateTime dtJan31(2026, 1, 31, 23, 0, 0);
    SystemTime now = { dtJan31.year(), dtJan31.month(), dtJan31.day(), 23, 0, 0, dtJan31.dayOfTheWeek(), dtJan31.unixtime() };
    uint8_t nextH = 0, nextM = 0;
    uint32_t nextDay = 0;

    bool ok = scheduler.computeNext(AppMode::MEDIO, now, nextH, nextM, &nextDay, false);
    TEST_ASSERT_TRUE(ok);
    DateTime dtNext(nextDay * 86400UL);
    TEST_ASSERT_EQUAL_UINT8(2, dtNext.month()); // February
    TEST_ASSERT_EQUAL_UINT8(1, dtNext.day());   // 1st

    // 2. 2026 is NOT a leap year: Feb 28 at 23:00 -> next is March 1
    DateTime dtFeb28(2026, 2, 28, 23, 0, 0);
    now = { dtFeb28.year(), dtFeb28.month(), dtFeb28.day(), 23, 0, 0, dtFeb28.dayOfTheWeek(), dtFeb28.unixtime() };
    ok = scheduler.computeNext(AppMode::MEDIO, now, nextH, nextM, &nextDay, false);
    TEST_ASSERT_TRUE(ok);
    dtNext = DateTime(nextDay * 86400UL);
    TEST_ASSERT_EQUAL_UINT8(3, dtNext.month()); // March
    TEST_ASSERT_EQUAL_UINT8(1, dtNext.day());   // 1st

    // 3. 2028 IS a leap year: Feb 28 at 23:00 -> next is Feb 29
    DateTime dtLeap28(2028, 2, 28, 23, 0, 0);
    now = { dtLeap28.year(), dtLeap28.month(), dtLeap28.day(), 23, 0, 0, dtLeap28.dayOfTheWeek(), dtLeap28.unixtime() };
    ok = scheduler.computeNext(AppMode::MEDIO, now, nextH, nextM, &nextDay, false);
    TEST_ASSERT_TRUE(ok);
    dtNext = DateTime(nextDay * 86400UL);
    TEST_ASSERT_EQUAL_UINT8(2, dtNext.month()); // February
    TEST_ASSERT_EQUAL_UINT8(29, dtNext.day());  // 29th!
}

static void test_scheduler_on_mode_changed_immediate_update(void) {
    gState.mode = AppMode::DESATIVADO;
    gState.now = { 2026, 6, 15, 10, 0, 0, 1, 1781500000UL };
    gState.next_hour = 0;
    gState.next_min = 0;

    // Switch to INTENSO (slots at 07:00, 13:00, 19:00)
    gState.mode = AppMode::INTENSO;
    scheduler.onModeChanged();

    // Next slot should immediately become 13:00
    TEST_ASSERT_EQUAL_UINT8(SCHED_INTENSO_SLOT1_H, gState.next_hour);
    TEST_ASSERT_EQUAL_UINT8(SCHED_INTENSO_SLOT1_M, gState.next_min);
}

static void test_scheduler_fraco_next_slot(void) {
    gState.mode = AppMode::FRACO;

    // Morning: 06:00 -> next is 08:00 today
    SystemTime now = { 2026, 6, 15, 6, 0, 0, 1, 1781500000UL };
    uint8_t nextH = 0, nextM = 0;
    uint32_t nextDay = 0;
    bool ok = scheduler.computeNext(AppMode::FRACO, now, nextH, nextM, &nextDay, false);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(SCHED_FRACO_SLOT0_H, nextH);
    TEST_ASSERT_EQUAL_UINT8(SCHED_FRACO_SLOT0_M, nextM);

    // After 08:00: 10:00 -> wraps to tomorrow 08:00
    now.hour = 10;
    ok = scheduler.computeNext(AppMode::FRACO, now, nextH, nextM, &nextDay, false);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(SCHED_FRACO_SLOT0_H, nextH);
    TEST_ASSERT_EQUAL_UINT8(SCHED_FRACO_SLOT0_M, nextM);
    DateTime dtNext(nextDay * 86400UL);
    TEST_ASSERT_EQUAL_UINT8(16, dtNext.day()); // Next day
}

static void test_scheduler_dow_mask(void) {
    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.slot_count = 1;
    cs.day_pattern = DayPattern::DOW_MASK;
    // Monday (1), Wednesday (3), Friday (5)
    cs.dow_mask = (1 << 1) | (1 << 3) | (1 << 5);
    cs.slots[0].hour = 8;
    cs.slots[0].minute = 0;

    // 2026-06-15 is a Monday (dow = 1)
    DateTime dtMon(2026, 6, 15, 6, 0, 0);
    SystemTime now = { dtMon.year(), dtMon.month(), dtMon.day(), 6, 0, 0, dtMon.dayOfTheWeek(), dtMon.unixtime() };
    uint8_t nextH = 0, nextM = 0;
    uint32_t nextDay = 0;

    // Before 08:00 on Monday -> fires Monday
    bool ok = scheduler.computeNext(AppMode::PERSONALIZADO, now, nextH, nextM, &nextDay, false);
    TEST_ASSERT_TRUE(ok);
    DateTime dtNext(nextDay * 86400UL);
    TEST_ASSERT_EQUAL_UINT8(15, dtNext.day()); // Monday June 15

    // After 08:00 on Monday -> next must be Wednesday June 17
    now.hour = 10;
    ok = scheduler.computeNext(AppMode::PERSONALIZADO, now, nextH, nextM, &nextDay, false);
    TEST_ASSERT_TRUE(ok);
    dtNext = DateTime(nextDay * 86400UL);
    TEST_ASSERT_EQUAL_UINT8(17, dtNext.day()); // Wednesday June 17

    // After 08:00 on Wednesday -> next must be Friday June 19
    DateTime dtWed(2026, 6, 17, 10, 0, 0);
    now = { dtWed.year(), dtWed.month(), dtWed.day(), 10, 0, 0, dtWed.dayOfTheWeek(), dtWed.unixtime() };
    ok = scheduler.computeNext(AppMode::PERSONALIZADO, now, nextH, nextM, &nextDay, false);
    TEST_ASSERT_TRUE(ok);
    dtNext = DateTime(nextDay * 86400UL);
    TEST_ASSERT_EQUAL_UINT8(19, dtNext.day()); // Friday June 19

    // After 08:00 on Friday -> next wraps to next Monday June 22
    DateTime dtFri(2026, 6, 19, 10, 0, 0);
    now = { dtFri.year(), dtFri.month(), dtFri.day(), 10, 0, 0, dtFri.dayOfTheWeek(), dtFri.unixtime() };
    ok = scheduler.computeNext(AppMode::PERSONALIZADO, now, nextH, nextM, &nextDay, false);
    TEST_ASSERT_TRUE(ok);
    dtNext = DateTime(nextDay * 86400UL);
    TEST_ASSERT_EQUAL_UINT8(22, dtNext.day()); // Monday June 22
}

static void test_scheduler_dst_fallback_duplicate_block(void) {
    gState.mode = AppMode::MEDIO;
    uint32_t t1 = 1781500000UL;
    gState.now = { 2026, 10, 25, 8, 0, 0, 0, t1 };
    scheduler._lastMin = 0xFF;
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    wateringCtrl.stop();
    TEST_ASSERT_FALSE(wateringCtrl._active);

    // If autumn DST causes hour 8 to repeat 1 hour later (unix + 3600),
    // update() should detect (t.unix - _lastTriggerUnix[0]) < 7200 and block auto-start
    gState.now = { 2026, 10, 25, 8, 0, 1, 0, (uint32_t)(t1 + 3600UL) };
    scheduler._lastMin = 0xFF;
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);
}

static void test_scheduler_on_watering_done_advances_slot(void) {
    gState.mode = AppMode::MEDIO;
    gState.now = { 2026, 6, 15, 8, 15, 0, 1, 1781500900UL };
    gState.next_hour = 8;
    gState.next_min = 0;

    scheduler.onWateringDone();

    // After slot 0 completes, next slot must advance to 20:00 (slot 1)
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT1_H, gState.next_hour);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT1_M, gState.next_min);
}

static void test_scheduler_exact_minute_boundary(void) {
    gState.mode = AppMode::MEDIO;
    SystemTime now = { 2026, 6, 15, 8, 0, 0, 1, 1781500000UL };
    uint8_t nextH = 0, nextM = 0;

    bool ok = scheduler.computeNext(AppMode::MEDIO, now, nextH, nextM);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT1_H, nextH);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT1_M, nextM);
}

static void test_scheduler_on_watering_done_does_not_skip_adjacent_minute_slot(void) {
    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.slot_count = 2;
    cs.day_pattern = DayPattern::DAILY;
    cs.slots[0] = { 7, 58 };
    cs.slots[1] = { 8, 0 };

    gState.mode = AppMode::PERSONALIZADO;
    // Slot 0 finishes at 07:59:15; next slot at 08:00 MUST NOT be skipped!
    gState.now = { 2026, 6, 15, 7, 59, 15, 1, 1781500755UL };
    scheduler.onWateringDone();

    TEST_ASSERT_EQUAL_UINT8(8, gState.next_hour);
    TEST_ASSERT_EQUAL_UINT8(0, gState.next_min);
}

static void test_scheduler_cycle_expired_bounds_and_overflow_safety(void) {
    gState.mode = AppMode::MEDIO;
    uint32_t startUnix = 1781500000UL;
    SystemTime current = { 2026, 6, 15, 8, 5, 0, 1, (uint32_t)(startUnix + 300UL) };

    // 0s remaining duration is invalid -> treated as expired
    TEST_ASSERT_TRUE(scheduler.isCycleExpired(startUnix, current, 0));

    // Corrupted huge remaining_duration_sec (0xFFFFFFFF) must not wrap uint32_t and pass
    TEST_ASSERT_TRUE(scheduler.isCycleExpired(startUnix, current, 0xFFFFFFFFUL));

    // Valid 600s remaining duration 5 minutes after start is NOT expired
    TEST_ASSERT_FALSE(scheduler.isCycleExpired(startUnix, current, 600));
}

static void test_scheduler_last_triggers_persist_across_reboot(void) {
    storage.begin();
    rtclock = RTClock();
    gState.mode = AppMode::MEDIO;
    uint32_t t1 = 1792915200UL; // 2026-10-25 08:00:00
    gState.now = { 2026, 10, 25, SCHED_MEDIO_SLOT0_H, SCHED_MEDIO_SLOT0_M, 0, 0, t1 };

    scheduler.begin();
    scheduler._lastMin = 0xFF;
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    wateringCtrl.stop();

    // Simulate reboot 1 hour later during DST fall-back repeated hour
    scheduler = Scheduler();
    scheduler.begin();
    gState.now = { 2026, 10, 25, SCHED_MEDIO_SLOT0_H, SCHED_MEDIO_SLOT0_M, 0, 0, (uint32_t)(t1 + 3600UL) };
    scheduler._lastMin = 0xFF;
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);
}

static void test_scheduler_clock_set_clears_last_triggers(void) {
    storage.begin();
    rtclock = RTClock();
    rtclock.begin();
    gState.mode = AppMode::MEDIO;

    rtclock.set(2026, 6, 15, SCHED_MEDIO_SLOT0_H, SCHED_MEDIO_SLOT0_M, 0);
    scheduler.begin();
    scheduler._lastMin = 0xFF;
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    wateringCtrl.stop();

    // Explicit manual clock rewind (via rtclock.set) increments manualRevision and re-arms slot
    rtclock.set(2026, 6, 15, SCHED_MEDIO_SLOT0_H - 1, 59, 55);
    scheduler.update(); // observes manualRevision change -> clearLastTriggers()
    rtclock.set(2026, 6, 15, SCHED_MEDIO_SLOT0_H, SCHED_MEDIO_SLOT0_M, 0);
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
}

static void test_scheduler_recovery_max_age_2h(void) {
    gState.mode = AppMode::DESATIVADO;
    uint32_t startUnix = 1781500000UL;

    SystemTime atLimit = { 2026, 6, 15, 11, 0, 0, 1, (uint32_t)(startUnix + RECOVERY_MAX_AGE_S) };
    TEST_ASSERT_FALSE(scheduler.isCycleExpired(startUnix, atLimit, 3600));

    // Exact +1 s boundary (7201 s) expires and logs ceiling 121 min
    SystemTime overLimitBy1s = { 2026, 6, 15, 11, 0, 1, 1, (uint32_t)(startUnix + RECOVERY_MAX_AGE_S + 1UL) };
    Serial.reset();
    TEST_ASSERT_TRUE(scheduler.isCycleExpired(startUnix, overLimitBy1s, 3600));
#if LOG_LEVEL >= LVL_WARN
    char expectedAgeMsg[160];
    snprintf(expectedAgeMsg, sizeof(expectedAgeMsg), TXT_LOG_SCHED_REC_AGE,
             121UL, (unsigned long)(RECOVERY_MAX_AGE_S / 60UL));
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedAgeMsg) != std::string::npos);
#endif

    // Remaining duration > max_cycle_sec (4801 s -> ceiling 81 min vs 80 min limit) logs in minutes
    Serial.reset();
    const uint32_t maxCycleSec = (uint32_t)NUM_ZONES * 20UL * 60UL;
    TEST_ASSERT_TRUE(scheduler.isCycleExpired(startUnix, atLimit, maxCycleSec + 1UL));
#if LOG_LEVEL >= LVL_WARN
    char expectedDurMsg[160];
    snprintf(expectedDurMsg, sizeof(expectedDurMsg), TXT_LOG_SCHED_REC_DUR,
             81UL, (unsigned long)(maxCycleSec / 60UL));
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedDurMsg) != std::string::npos);
#endif

    // onModeChanged(false) refreshes next slot quietly without emitting mode-change log
    gState.mode = AppMode::MEDIO;
    gState.rtc_valid = true;
    gState.now = { 2026, 6, 15, 7, 0, 0, 1, startUnix };
    Serial.reset();
    scheduler.onModeChanged(false);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT0_H, gState.next_hour);
    TEST_ASSERT_TRUE(Serial.outputBuffer.empty());

    gState.mode = AppMode::DESATIVADO;
    SystemTime withinLimit = { 2026, 6, 15, 8, 30, 0, 1, startUnix + 1800 };
    TEST_ASSERT_FALSE(scheduler.isCycleExpired(startUnix, withinLimit, 3600));
}

static void setSchedulerTestLocalNow(uint16_t y, uint8_t m, uint8_t d,
                                     uint8_t hh, uint8_t mm, uint8_t ss) {
    DateTime localDt(y, m, d, hh, mm, ss);
    DateTime utcDt = RTClock::localToUtc(localDt);
    gState.now = { y, m, d, hh, mm, ss, localDt.dayOfTheWeek(), utcDt.unixtime() };
}

static void setSchedulerTestUtcNow(uint32_t utcUnix) {
    DateTime localDt = RTClock::utcToLocal(DateTime(utcUnix));
    gState.now = { localDt.year(), localDt.month(), localDt.day(),
                   localDt.hour(), localDt.minute(), localDt.second(),
                   localDt.dayOfTheWeek(), utcUnix };
}

static void test_scheduler_catchup_grace_window_and_guards(void) {
    storage.begin();
    rtclock = RTClock();
    rtclock.begin();
    gState.mode = AppMode::MEDIO; // 08:00, 20:00
    gState.zones[0] = { "Z1", true, 5 };
    for (int i = 1; i < NUM_ZONES; i++) gState.zones[i].enabled = false;

    // 1. Power outage across 08:00 -> boot at 08:15 local catches up missed 08:00 slot
    setSchedulerTestLocalNow(2026, 6, 15, 8, 15, 0);
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    wateringCtrl.stop();

    // 2. Reboot again at 08:20 local -> _lastTriggerUnix[0] prevents duplicate catch-up
    setSchedulerTestLocalNow(2026, 6, 15, 8, 20, 0);
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);

    // 3. Past 30-min window (08:35 local) with cleared triggers -> does NOT catch up
    scheduler.clearLastTriggers();
    setSchedulerTestLocalNow(2026, 6, 15, 8, 35, 0);
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);

    // 4. Manual rtclock.set() at 08:10 local MUST NOT fire 08:00 catch-up
    scheduler = Scheduler();
    scheduler.begin();
    rtclock.set(2026, 6, 15, 8, 10, 0);
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);

    // 5. Midnight wrap catch-up (23:50 slot caught up at 00:10 local next day)
    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.slot_count = 1;
    cs.day_pattern = DayPattern::DAILY;
    cs.slots[0] = { 23, 50 };
    gState.mode = AppMode::PERSONALIZADO;
    setSchedulerTestLocalNow(2026, 6, 16, 0, 10, 0);
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    wateringCtrl.stop();

    // 6. DST spring-forward skipped hour (2026-03-29 01:00 UTC) catches up a skipped slot
    gState.auto_dst = true;
    const DateTime springGapLocal(DateTime(2026, 3, 29, 1, 30, 0).unixtime() + (int32_t)TIMEZONE_OFFSET * 3600L);
    cs.slots[0] = { springGapLocal.hour(), springGapLocal.minute() };
    scheduler.clearLastTriggers();
    setSchedulerTestUtcNow(DateTime(2026, 3, 29, 0, 59, 59).unixtime());
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);

    setSchedulerTestUtcNow(DateTime(2026, 3, 29, 1, 0, 0).unixtime());
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    wateringCtrl.stop();
}

static void test_scheduler_catchup_exact_boundaries_and_dst_fallback(void) {
    storage.begin();
    rtclock = RTClock();
    rtclock.begin();
    gState.mode = AppMode::MEDIO; // 08:00, 20:00
    gState.zones[0] = { "Z1", true, 5 };
    for (int i = 1; i < NUM_ZONES; i++) gState.zones[i].enabled = false;

    // 1. Exact 1800s vs 1801s catch-up boundary
    setSchedulerTestLocalNow(2026, 6, 15, 8, 30, 1);
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);

    setSchedulerTestLocalNow(2026, 6, 15, 8, 30, 0);
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    wateringCtrl.stop();

    // 2. Continuous 1-minute skip (wallJump == 120s: 07:59 -> 08:01) catches up 08:00
    scheduler.clearLastTriggers();
    setSchedulerTestLocalNow(2026, 6, 15, 7, 59, 0);
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);
    setSchedulerTestLocalNow(2026, 6, 15, 8, 1, 1);
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    wateringCtrl.stop();

    // 3. Continuous rtclock.set() forward jump across 08:00 (without onModeChanged) and onModeChanged() reset
    scheduler.clearLastTriggers();
    setSchedulerTestLocalNow(2026, 6, 15, 7, 59, 0);
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    rtclock.set(2026, 6, 15, 8, 10, 1);
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);

    setSchedulerTestLocalNow(2026, 6, 15, 8, 10, 0);
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.onModeChanged(false);
    setSchedulerTestLocalNow(2026, 6, 15, 8, 11, 1);
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);

    // 4. rtc_valid == false inhibits catch-up; active recovery at begin() suppresses _catchUpPending;
    //    MAX_RECOVERY_RETRIES aborting AUTO stays blocked by _lastTriggerUnix
    gState.rtc_valid = false;
    setSchedulerTestLocalNow(2026, 6, 15, 8, 15, 0);
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);

    gState.rtc_valid = true;
    wateringCtrl.startGeneral(WaterTrigger::MANUAL);
    scheduler = Scheduler();
    scheduler.begin();
    TEST_ASSERT_FALSE(scheduler._catchUpPending);
    wateringCtrl.stop();
    setSchedulerTestLocalNow(2026, 6, 15, 8, 16, 0);
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);

    scheduler.clearLastTriggers();
    setSchedulerTestLocalNow(2026, 6, 15, 8, 0, 0);
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    for (int r = 1; r <= MAX_RECOVERY_RETRIES; r++) {
        setSchedulerTestLocalNow(2026, 6, 15, 8, (uint8_t)(r * 5), 0);
        wateringCtrl = WateringController();
        wateringCtrl.begin();
        TEST_ASSERT_TRUE(wateringCtrl._active);
    }
    setSchedulerTestLocalNow(2026, 6, 15, 8, 20, 0);
    wateringCtrl = WateringController();
    wateringCtrl.begin();
    TEST_ASSERT_FALSE(wateringCtrl._active);
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);

    // 5. Continuous suspension expiration after missed slot (08:00) must NOT catch up
    scheduler.clearLastTriggers();
    gState.suspended = true;
    gState.suspended_until = RTClock::localToUtc(DateTime(2026, 6, 15, 8, 10, 0)).unixtime();
    setSchedulerTestLocalNow(2026, 6, 15, 7, 59, 0);
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    setSchedulerTestLocalNow(2026, 6, 15, 8, 5, 1);
    scheduler.update();
    TEST_ASSERT_TRUE(gState.suspended);
    setSchedulerTestLocalNow(2026, 6, 15, 8, 10, 2);
    scheduler.update();
    TEST_ASSERT_FALSE(gState.suspended);
    TEST_ASSERT_FALSE(wateringCtrl._active);
    setSchedulerTestLocalNow(2026, 6, 15, 8, 11, 3);
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);

    // 6. DST fall-back (2026-10-25): missed 1st repeated-hour slot (00:15 UTC) catches up inside 1st hour
    //    at exact 1800s boundary (00:45:00 UTC), skips 1801s (00:45:01 UTC), and blocks 2nd pass (01:15 UTC)
    gState.auto_dst = true;
    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.slot_count = 1;
    cs.day_pattern = DayPattern::DAILY;
    const DateTime fallSlot15Local = RTClock::utcToLocal(DateTime(2026, 10, 25, 0, 15, 0));
    cs.slots[0] = { fallSlot15Local.hour(), fallSlot15Local.minute() };
    gState.mode = AppMode::PERSONALIZADO;
    scheduler.clearLastTriggers();

    setSchedulerTestUtcNow(DateTime(2026, 10, 25, 0, 45, 1).unixtime());
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);

    setSchedulerTestUtcNow(DateTime(2026, 10, 25, 0, 45, 0).unixtime());
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    wateringCtrl.stop();

    setSchedulerTestUtcNow(DateTime(2026, 10, 25, 1, 15, 0).unixtime());
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);

    // 7. DST spring-forward outage bounds (+15m catches up skipped slot; +31m does not)
    //    and earlier pre-transition slot already run is NOT re-triggered at the DST jump
    scheduler.clearLastTriggers();
    const DateTime springGap30Local(DateTime(2026, 3, 29, 1, 30, 0).unixtime() + (int32_t)TIMEZONE_OFFSET * 3600L);
    cs.slots[0] = { springGap30Local.hour(), springGap30Local.minute() };
    setSchedulerTestUtcNow(DateTime(2026, 3, 29, 1, 15, 0).unixtime());
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    wateringCtrl.stop();

    scheduler.clearLastTriggers();
    setSchedulerTestUtcNow(DateTime(2026, 3, 29, 1, 31, 0).unixtime()); // >1800s
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);

    scheduler.clearLastTriggers();
    const DateTime preSpring45Local = RTClock::utcToLocal(DateTime(2026, 3, 29, 0, 45, 0));
    cs.slots[0] = { preSpring45Local.hour(), preSpring45Local.minute() };
    setSchedulerTestUtcNow(DateTime(2026, 3, 29, 0, 45, 0).unixtime());
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    wateringCtrl.stop();
    setSchedulerTestUtcNow(DateTime(2026, 3, 29, 0, 59, 59).unixtime());
    scheduler.update();
    setSchedulerTestUtcNow(DateTime(2026, 3, 29, 1, 0, 0).unixtime());
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);
}

static void test_scheduler_catchup_patterns_and_dense_slots(void) {
    storage.begin();
    rtclock = RTClock();
    rtclock.begin();
    gState.auto_dst = true;
    gState.zones[0] = { "Z1", true, 5 };
    for (int i = 1; i < NUM_ZONES; i++) gState.zones[i].enabled = false;

    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.slot_count = 1;
    cs.slots[0] = { 23, 50 };
    gState.mode = AppMode::PERSONALIZADO;

    // 1. DOW_MASK midnight wrap (2026-06-14 is Sun dow=0, 2026-06-15 is Mon dow=1, 2026-06-16 is Tue dow=2)
    cs.day_pattern = DayPattern::DOW_MASK;
    cs.dow_mask = (1 << 0); // Sunday enabled, Monday disabled
    setSchedulerTestLocalNow(2026, 6, 15, 0, 10, 0);
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    wateringCtrl.stop();

    scheduler.clearLastTriggers();
    cs.dow_mask = (1 << 2); // Tuesday enabled, Monday disabled -> must NOT catch up Monday 23:50
    setSchedulerTestLocalNow(2026, 6, 16, 0, 10, 0);
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);

    // 2. EVERY_X_DAYS midnight wrap preserves custom_ref_day and respects off-cycle days
    cs.day_pattern = DayPattern::EVERY_X_DAYS;
    cs.interval_days = 3;
    const uint32_t activeDay = DateTime(2026, 6, 15, 0, 0, 0).unixtime() / 86400UL;
    gState.custom_ref_day = activeDay;
    scheduler.clearLastTriggers();
    setSchedulerTestLocalNow(2026, 6, 16, 0, 10, 0);
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    TEST_ASSERT_EQUAL_UINT32(activeDay, gState.custom_ref_day);
    wateringCtrl.stop();

    scheduler.clearLastTriggers();
    setSchedulerTestLocalNow(2026, 6, 17, 0, 10, 0); // 2026-06-16 was off-cycle
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);
    TEST_ASSERT_EQUAL_UINT32(activeDay, gState.custom_ref_day);

    gState.custom_ref_day = 0xFFFFFFFFUL;
    TEST_ASSERT_FALSE(scheduler._tryCatchUp(gState.now));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFUL, gState.custom_ref_day);

    // 3. Dense custom slots (< 2h apart): picks newest missed slot and avoids exact-second collision
    cs.day_pattern = DayPattern::DAILY;
    cs.slot_count = 2;
    cs.slots[0] = { 8, 0 };
    cs.slots[1] = { 8, 20 };
    scheduler.clearLastTriggers();
    setSchedulerTestLocalNow(2026, 6, 15, 8, 25, 0);
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    TEST_ASSERT_EQUAL_UINT32(0UL, scheduler._lastTriggerUnix[0]);
    TEST_ASSERT_TRUE(scheduler._lastTriggerUnix[1] > 0UL);
    wateringCtrl.stop();

    // Exact-second / dead-time overlap against 08:30 next slot with 15-min cycle (901s total)
    gState.zones[0].duration_min = 15;
    cs.slots[0] = { 8, 0 };
    cs.slots[1] = { 8, 30 };
    scheduler.clearLastTriggers();
    setSchedulerTestLocalNow(2026, 6, 15, 8, 15, 0); // 08:15:00 + 901s >= 08:30:00 -> blocked
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);

    scheduler.clearLastTriggers();
    setSchedulerTestLocalNow(2026, 6, 15, 8, 14, 0); // 08:14:00 + 901s < 08:30:00 -> allowed
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    wateringCtrl.stop();
}

static void test_scheduler_non_manual_rtc_revisions_preserve_triggers(void) {
    storage.begin();
    gState.auto_dst = true;
    gState.mode = AppMode::MEDIO; // 08:00, 20:00
    gState.zones[0] = { "Z1", true, 5 };
    for (int i = 1; i < NUM_ZONES; i++) gState.zones[i].enabled = false;

    // 1. Forward RTC drift (>300s) after completed 08:00 slot must NOT wipe _lastTriggerUnix or catch-up double-run
    const DateTime slot08Utc = RTClock::localToUtc(DateTime(2026, 6, 15, 8, 0, 0));
    rtclock = RTClock();
    rtclock._rtc.mockPresent = true;
    rtclock._rtc.mockLostPower = false;
    rtclock._rtc.adjust(slot08Utc);
    rtclock.begin();
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    const uint32_t trig0 = scheduler._lastTriggerUnix[0];
    TEST_ASSERT_TRUE(trig0 > 0UL);
    wateringCtrl.stop();

    // Advance 5 min in software clock (08:05 local), then simulate RTC ahead by +6 min (08:11 local -> >300s drift)
    advanceMockMillis(300000UL);
    rtclock._rtc.adjust(DateTime(slot08Utc.unixtime() + 11UL * 60UL));
    rtclock.update();
    TEST_ASSERT_EQUAL_UINT8(8, gState.now.hour);
    TEST_ASSERT_EQUAL_UINT8(11, gState.now.min);
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);
    TEST_ASSERT_EQUAL_UINT32(trig0, scheduler._lastTriggerUnix[0]);

    // 2. Transient I2C glitch + hotplug OSF restore during October DST fall-back preserves sched_tr
    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.day_pattern = DayPattern::DAILY;
    cs.slot_count = 1;
    const DateTime fallSlot15Local = RTClock::utcToLocal(DateTime(2026, 10, 25, 0, 15, 0));
    cs.slots[0] = { fallSlot15Local.hour(), fallSlot15Local.minute() };
    gState.mode = AppMode::PERSONALIZADO;

    rtclock = RTClock();
    rtclock._rtc.mockPresent = true;
    rtclock._rtc.mockLostPower = false;
    rtclock._rtc.adjust(DateTime(2026, 10, 25, 0, 15, 0)); // 1st repeated-hour pass
    rtclock.begin();
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.clearLastTriggers();
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    const uint32_t dstTrig = scheduler._lastTriggerUnix[0];
    TEST_ASSERT_EQUAL_UINT32(DateTime(2026, 10, 25, 0, 15, 0).unixtime(), dstTrig);
    wateringCtrl.stop();

    // Simulate RTC drop + hotplug OSF restore at 00:30 UTC -> bumps revision(), not manualRevision()
    rtclock._found = false;
    rtclock._rtc.mockLostPower = true;
    advanceMockMillis(900000UL); // +15 min -> 00:30 UTC
    rtclock.update();
    scheduler.update();
    TEST_ASSERT_EQUAL_UINT32(dstTrig, scheduler._lastTriggerUnix[0]);

    // 45 min later at 01:15 UTC: repeated hour must STILL be blocked
    advanceMockMillis(2700000UL);
    rtclock._rtc.adjust(DateTime(2026, 10, 25, 1, 15, 0));
    rtclock.update();
    TEST_ASSERT_EQUAL_UINT8(fallSlot15Local.hour(), gState.now.hour);
    TEST_ASSERT_EQUAL_UINT8(fallSlot15Local.minute(), gState.now.min);
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);

    // 3. Cold-boot with absent RTC followed by hotplug must preserve persisted NVS sched_tr before catch-up
    gState.mode = AppMode::MEDIO;
    uint32_t savedTr[MAX_SLOTS_PER_MODE] = { slot08Utc.unixtime(), 0, 0, 0 }; // 08:00 local already ran
    storage.saveLastTriggers(savedTr, MAX_SLOTS_PER_MODE);

    gState.rtc_valid = false;
    rtclock = RTClock();
    rtclock._rtc.mockPresent = false;
    rtclock.begin();
    scheduler = Scheduler();
    scheduler.begin(); // loads savedTr from NVS, _catchUpPending = true

    // Hotplug RTC at 08:10 local (within 30-min catch-up window of 08:00)
    rtclock._rtc.mockPresent = true;
    rtclock._rtc.mockLostPower = false;
    rtclock._rtc.adjust(DateTime(slot08Utc.unixtime() + 10UL * 60UL));
    advanceMockMillis(35000UL);
    rtclock.update();
    TEST_ASSERT_TRUE(gState.rtc_valid);
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);
    TEST_ASSERT_EQUAL_UINT32(savedTr[0], scheduler._lastTriggerUnix[0]);
}

static void test_scheduler_spring_forward_next_unix_no_zones_and_same_min_jump(void) {
    storage.begin();
    rtclock = RTClock();
    rtclock.begin();
    gState.auto_dst = true;

    // 1. getNextCycleUnix() matches _tryCatchUp() spring-forward transition instant (01:00 UTC for skipped slot)
    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.day_pattern = DayPattern::DAILY;
    cs.slot_count = 1;
    const DateTime springGap45Local(DateTime(2026, 3, 29, 1, 45, 0).unixtime() + (int32_t)TIMEZONE_OFFSET * 3600L);
    cs.slots[0] = { springGap45Local.hour(), springGap45Local.minute() };
    gState.mode = AppMode::PERSONALIZADO;

    setSchedulerTestUtcNow(DateTime(2026, 3, 29, 0, 30, 0).unixtime());
    uint32_t nextUnix = scheduler.getNextCycleUnix(gState.now);
    TEST_ASSERT_EQUAL_UINT32(DateTime(2026, 3, 29, 1, 0, 0).unixtime(), nextUnix);

    // 2. Exact-minute slot with all zones disabled does NOT write sched_tr to NVS
    for (int i = 0; i < NUM_ZONES; i++) gState.zones[i].enabled = false;
    gState.mode = AppMode::MEDIO; // 08:00, 20:00
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.clearLastTriggers();
    setSchedulerTestLocalNow(2026, 6, 15, 8, 0, 0);
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);
    TEST_ASSERT_EQUAL_UINT32(0UL, scheduler._lastTriggerUnix[0]);
    uint32_t persisted[MAX_SLOTS_PER_MODE] = { 99, 99, 99, 99 };
    storage.loadLastTriggers(persisted, MAX_SLOTS_PER_MODE);
    TEST_ASSERT_EQUAL_UINT32(0UL, persisted[0]);

    // 3. Exact 60-minute mid-minute clock jump (07:00:15 -> 08:00:15, same min & sec) evaluates new hour slot immediately
    gState.zones[0] = { "Z1", true, 5 };
    rtclock = RTClock();
    rtclock._rtc.mockPresent = true;
    rtclock._rtc.mockLostPower = false;
    rtclock._rtc.adjust(RTClock::localToUtc(DateTime(2026, 6, 15, 7, 0, 15)));
    rtclock.begin();
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);

    // Jump exactly +60 min (same min=0, same sec=15) via rtclock.set()
    rtclock.set(2026, 6, 15, 8, 0, 15);
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    wateringCtrl.stop();

    // 4. Mid-run I2C recovery via rtclock.begin() jumping exactly +60 min (19:00:15 -> 20:00:15, same min)
    //    must bump revision() and evaluate 20:00 immediately without clearing sched_tr
    rtclock._rtc.adjust(RTClock::localToUtc(DateTime(2026, 6, 15, 19, 0, 15)));
    rtclock.begin();
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);
    const uint32_t savedTrig = scheduler._lastTriggerUnix[0];
    rtclock._rtc.adjust(RTClock::localToUtc(DateTime(2026, 6, 15, 20, 0, 15)));
    rtclock.begin();
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    TEST_ASSERT_EQUAL_UINT32(savedTrig, scheduler._lastTriggerUnix[0]);
    wateringCtrl.stop();
}

static void test_scheduler_setup_guard_dst_fallback_next_and_start_bounds(void) {
    storage.begin();
    gState.auto_dst = true;
    gState.zones[0] = { "Z1", true, 5 };
    for (int i = 1; i < NUM_ZONES; i++) gState.zones[i].enabled = false;

    // 1. Automatic scheduling and catch-up must stay inhibited while setup_done == false
    gState.setup_done = false;
    gState.mode = AppMode::MEDIO; // 08:00, 20:00
    setSchedulerTestLocalNow(2026, 6, 15, 8, 0, 0);
    scheduler = Scheduler();
    scheduler.begin();
    TEST_ASSERT_FALSE(scheduler._catchUpPending);
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);

    // Even inside the 30-min catch-up window, setup_done == false must not fire
    setSchedulerTestLocalNow(2026, 6, 15, 8, 10, 0);
    scheduler._catchUpPending = true;
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl._active);

    // Once setup completes and onModeChanged() runs, subsequent exact-minute slots fire normally
    gState.setup_done = true;
    scheduler.onModeChanged(false);
    setSchedulerTestLocalNow(2026, 6, 15, 20, 0, 0);
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    wateringCtrl.stop();

    // 2. computeNext() skips already-triggered October DST fall-back slot during the repeated hour,
    //    and isCycleExpired() honors persisted NVS sched_tr even when called before scheduler.begin()
    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.day_pattern = DayPattern::DAILY;
    cs.slot_count = 1;
    const DateTime fallSlot30Local = RTClock::utcToLocal(DateTime(2026, 10, 25, 0, 30, 0));
    cs.slots[0] = { fallSlot30Local.hour(), fallSlot30Local.minute() };
    gState.mode = AppMode::PERSONALIZADO;

    const uint32_t firstPassUtc = DateTime(2026, 10, 25, 0, 30, 0).unixtime();
    setSchedulerTestUtcNow(firstPassUtc);
    scheduler = Scheduler();
    scheduler.begin();
    scheduler.clearLastTriggers();
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl._active);
    wateringCtrl.stop();

    // After fall-back to 01:05 UTC, 2nd pass at 01:30 UTC will be blocked -> next must advance to next day
    const uint32_t secondPassUtc = DateTime(2026, 10, 25, 1, 5, 0).unixtime();
    setSchedulerTestUtcNow(secondPassUtc);
    uint8_t nextH = 0, nextM = 0;
    uint32_t nextDay = 0;
    TEST_ASSERT_TRUE(scheduler.computeNext(AppMode::PERSONALIZADO, gState.now, nextH, nextM, &nextDay, false));
    const DateTime expectedNextDay(DateTime(fallSlot30Local.year(), fallSlot30Local.month(), fallSlot30Local.day(), 0, 0, 0).unixtime() + 86400UL);
    DateTime nextDt(nextDay * 86400UL);
    TEST_ASSERT_EQUAL_UINT8(expectedNextDay.day(), nextDt.day());
    TEST_ASSERT_EQUAL_UINT8(fallSlot30Local.hour(), nextH);
    TEST_ASSERT_EQUAL_UINT8(fallSlot30Local.minute(), nextM);

    // Simulate cold reboot boot order: fresh Scheduler (before begin()) queried by wateringCtrl.begin()->isCycleExpired()
    scheduler = Scheduler();
    TEST_ASSERT_FALSE(scheduler.isCycleExpired(firstPassUtc, gState.now, 600));

    // 3. isCycleExpired() rejects pre-2020 and post-2099 start_unix safely (guards RTClib underflow)
    setSchedulerTestLocalNow(2026, 6, 15, 10, 0, 0);
    TEST_ASSERT_TRUE(scheduler.isCycleExpired(0UL, gState.now, 300));
    TEST_ASSERT_TRUE(scheduler.isCycleExpired(1000UL, gState.now, 300));
    TEST_ASSERT_TRUE(scheduler.isCycleExpired(4102444800UL, gState.now, 300));
}

static void test_scheduler_cycle_expired_respects_suspension_and_zero_zones(void) {
    storage.begin();
    gState.auto_dst = true;
    gState.setup_done = true;
    gState.mode = AppMode::INTENSO; // 07:00, 13:00, 19:00
    gState.zones[0] = { "Z1", true, 5 };
    for (int i = 1; i < NUM_ZONES; i++) gState.zones[i].enabled = false;
    scheduler = Scheduler();

    const uint32_t startUtc = RTClock::localToUtc(DateTime(2026, 6, 15, 12, 45, 0)).unixtime();
    setSchedulerTestLocalNow(2026, 6, 15, 12, 50, 0);

    // 1. Without suspension, 13:00 auto slot is 10 min away (< 2h SAFETY_GAP_SEC) -> expired
    gState.suspended = false;
    gState.suspended_until = 0;
    TEST_ASSERT_TRUE(scheduler.isCycleExpired(startUtc, gState.now, 600));

    // 2. With 5-day suspension active, 13:00 will NOT run -> manual recovery is NOT expired
    gState.suspended = true;
    gState.suspended_until = gState.now.unix + 5UL * 86400UL;
    TEST_ASSERT_FALSE(scheduler.isCycleExpired(startUtc, gState.now, 600));

    // 3. Even if blackout spanned 13:00 while suspended, 13:00 did not run -> still NOT expired
    setSchedulerTestLocalNow(2026, 6, 15, 13, 5, 0);
    TEST_ASSERT_FALSE(scheduler.isCycleExpired(startUtc, gState.now, 600));

    // 4. Suspension expiring at 13:00:30 (same minute as 13:00 slot!) -> 13:00 WILL fire -> expired
    setSchedulerTestLocalNow(2026, 6, 15, 12, 50, 0);
    const uint32_t slot13Utc = RTClock::localToUtc(DateTime(2026, 6, 15, 13, 0, 0)).unixtime();
    gState.suspended_until = slot13Utc + 30UL;
    TEST_ASSERT_TRUE(scheduler.isCycleExpired(startUtc, gState.now, 600));

    // 5. Suspension expiring at 13:05 (past 13:00, next unsuspended slot is 19:00 > 12:50+10m+2h) -> NOT expired
    gState.suspended_until = slot13Utc + 300UL;
    TEST_ASSERT_FALSE(scheduler.isCycleExpired(startUtc, gState.now, 600));

    // 6. When all zones are disabled, no auto cycle can execute -> NOT expired (e.g. CUSTOM manual recovery)
    gState.suspended = false;
    gState.suspended_until = 0;
    for (int i = 0; i < NUM_ZONES; i++) {
        gState.zones[i].enabled = false;
        gState.zones[i].duration_min = 0;
    }
    TEST_ASSERT_FALSE(Scheduler::hasEnabledZones());
    TEST_ASSERT_FALSE(scheduler.isCycleExpired(startUtc, gState.now, 600));
}

static void setLocalNow(uint16_t y, uint8_t m, uint8_t d, uint8_t hh, uint8_t mm, uint8_t ss) {
    DateTime localDt(y, m, d, hh, mm, ss);
    DateTime utcDt = RTClock::localToUtc(localDt);
    gState.now.year  = y;
    gState.now.month = m;
    gState.now.day   = d;
    gState.now.hour  = hh;
    gState.now.min   = mm;
    gState.now.sec   = ss;
    gState.now.dow   = localDt.dayOfTheWeek();
    gState.now.unix  = utcDt.unixtime();
}

static void test_scheduler_collision_defers_after_test_and_custom_runs(void) {
    // 1. 5 s TEST overlapping 08:00 defers and triggers AUTO immediately after TEST ends
    scheduler = Scheduler();
    gState.mode = AppMode::MEDIO;
    gState.zones[0] = { "Z1", true, 5 };
    gState.zones[1].enabled = false;
    gState.zones[2].enabled = false;
    gState.zones[3].enabled = false;
    setLocalNow(2026, 6, 15, 7, 59, 58);
    scheduler.begin();

    TEST_ASSERT_TRUE(wateringCtrl.startTest(0));
    setLocalNow(2026, 6, 15, 8, 0, 0);
    scheduler.update();
    TEST_ASSERT_TRUE(scheduler._collisionCatchUpPending);
    TEST_ASSERT_FALSE(scheduler.hasSkippedSlot());
    TEST_ASSERT_EQUAL(WaterTrigger::TEST, wateringCtrl.activeTrigger());

    // TEST finishes at 08:00:03 -> next second tick catches up 08:00 AUTO
    wateringCtrl.stop();
    setLocalNow(2026, 6, 15, 8, 0, 4);
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl.isActive());
    TEST_ASSERT_EQUAL(WaterTrigger::AUTO, wateringCtrl.activeTrigger());
    TEST_ASSERT_FALSE(scheduler.hasSkippedSlot());
    wateringCtrl.stop();

    // 2. CUSTOM spanning multiple minutes (07:58..08:04) preserves deferral across minute rollovers
    scheduler = Scheduler();
    storage.clear();
    setLocalNow(2026, 6, 15, 7, 58, 0);
    scheduler.begin();
    bool sel[NUM_ZONES] = { false, false, false, true };
    TEST_ASSERT_TRUE(wateringCtrl.startCustom(sel, 6));

    setLocalNow(2026, 6, 15, 8, 0, 0);
    scheduler.update();
    TEST_ASSERT_TRUE(scheduler._collisionCatchUpPending);

    // Minute rollovers while CUSTOM is still running must NOT drop deferral
    setLocalNow(2026, 6, 15, 8, 1, 1);
    scheduler.update();
    TEST_ASSERT_TRUE(scheduler._collisionCatchUpPending);
    setLocalNow(2026, 6, 15, 8, 3, 2);
    scheduler.update();
    TEST_ASSERT_TRUE(scheduler._collisionCatchUpPending);

    // CUSTOM finishes at 08:04:00 -> catches up 08:00 AUTO
    wateringCtrl.stop();
    setLocalNow(2026, 6, 15, 8, 4, 3);
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl.isActive());
    TEST_ASSERT_EQUAL(WaterTrigger::AUTO, wateringCtrl.activeTrigger());
    wateringCtrl.stop();

    // 3. CUSTOM running past SCHED_CATCHUP_WINDOW_S (>30 min) records skipped indicator instead
    scheduler = Scheduler();
    storage.clear();
    setLocalNow(2026, 6, 15, 7, 55, 0);
    scheduler.begin();
    TEST_ASSERT_TRUE(wateringCtrl.startCustom(sel, 20));
    setLocalNow(2026, 6, 15, 8, 0, 0);
    scheduler.update();
    TEST_ASSERT_TRUE(scheduler._collisionCatchUpPending);
    wateringCtrl.stop();
    setLocalNow(2026, 6, 15, 8, 35, 1);
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl.isActive());
    TEST_ASSERT_TRUE(scheduler.hasSkippedSlot());
    TEST_ASSERT_EQUAL_UINT8(8, scheduler.skippedHour());
    TEST_ASSERT_EQUAL_UINT8(0, scheduler.skippedMin());
}

static void test_scheduler_collision_manual_sets_skipped_indicator_no_double_water(void) {
    scheduler = Scheduler();
    gState.mode = AppMode::MEDIO;
    gState.zones[0] = { "Z1", true, 5 };
    gState.zones[1].enabled = false;
    gState.zones[2].enabled = false;
    gState.zones[3].enabled = false;
    setLocalNow(2026, 6, 15, 7, 58, 0);
    scheduler.begin();

    // Full MANUAL General overlaps 08:00 -> must NOT double-water after it finishes
    TEST_ASSERT_TRUE(wateringCtrl.startGeneral(WaterTrigger::MANUAL));
    setLocalNow(2026, 6, 15, 8, 0, 0);
    scheduler.update();
    TEST_ASSERT_FALSE(scheduler._collisionCatchUpPending);
    TEST_ASSERT_TRUE(scheduler.hasSkippedSlot());
    TEST_ASSERT_EQUAL_UINT8(8, scheduler.skippedHour());
    TEST_ASSERT_EQUAL_UINT8(0, scheduler.skippedMin());

    wateringCtrl.stop();
    setLocalNow(2026, 6, 15, 8, 5, 1);
    scheduler.update();
    TEST_ASSERT_FALSE(wateringCtrl.isActive());
    TEST_ASSERT_TRUE(scheduler.hasSkippedSlot());

    // Cleared once next scheduled AUTO run fires (20:00)
    setLocalNow(2026, 6, 15, 20, 0, 0);
    scheduler.update();
    TEST_ASSERT_TRUE(wateringCtrl.isActive());
    TEST_ASSERT_FALSE(scheduler.hasSkippedSlot());
    wateringCtrl.stop();
}

};

int main(void) {
    UNITY_BEGIN();
    RUN_UT(test_scheduler_medio_next_slot);
    RUN_UT(test_scheduler_intenso_next_slot);
    RUN_UT(test_scheduler_desativado_returns_false);
    RUN_UT(test_scheduler_custom_every_x_days);
    RUN_UT(test_scheduler_cycle_expired_check);
    RUN_UT(test_scheduler_suspension_auto_wake);
    RUN_UT(test_scheduler_month_end_and_leap_year_rollover);
    RUN_UT(test_scheduler_on_mode_changed_immediate_update);
    RUN_UT(test_scheduler_fraco_next_slot);
    RUN_UT(test_scheduler_dow_mask);
    RUN_UT(test_scheduler_dst_fallback_duplicate_block);
    RUN_UT(test_scheduler_on_watering_done_advances_slot);
    RUN_UT(test_scheduler_exact_minute_boundary);
    RUN_UT(test_scheduler_on_watering_done_does_not_skip_adjacent_minute_slot);
    RUN_UT(test_scheduler_cycle_expired_bounds_and_overflow_safety);
    RUN_UT(test_scheduler_last_triggers_persist_across_reboot);
    RUN_UT(test_scheduler_clock_set_clears_last_triggers);
    RUN_UT(test_scheduler_recovery_max_age_2h);
    RUN_UT(test_scheduler_catchup_grace_window_and_guards);
    RUN_UT(test_scheduler_catchup_exact_boundaries_and_dst_fallback);
    RUN_UT(test_scheduler_catchup_patterns_and_dense_slots);
    RUN_UT(test_scheduler_non_manual_rtc_revisions_preserve_triggers);
    RUN_UT(test_scheduler_spring_forward_next_unix_no_zones_and_same_min_jump);
    RUN_UT(test_scheduler_setup_guard_dst_fallback_next_and_start_bounds);
    RUN_UT(test_scheduler_cycle_expired_respects_suspension_and_zero_zones);
    RUN_UT(test_scheduler_collision_defers_after_test_and_custom_runs);
    RUN_UT(test_scheduler_collision_manual_sets_skipped_indicator_no_double_water);
    return UNITY_END();
}
