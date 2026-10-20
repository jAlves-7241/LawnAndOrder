#include <unity.h>
#include "TestCommon.h"
#include "../../test/mocks/TestCommon.cpp"
#include "../../src/AppState.cpp"
#include "../../src/Storage.cpp"
#include "../../src/RTClock.cpp"
#include "../../src/History.cpp"
#include "../../src/WateringController.cpp"

#define private public
#include "Scheduler.h"
#undef private

#include "../../src/Scheduler.cpp"

void setUp(void) {
    resetAllTestMocks();
    gState.rtc_valid = true;
}

void tearDown(void) {}

void test_scheduler_medio_next_slot(void) {
    gState.mode = AppMode::MEDIO;

    // Morning: 05:00 -> next is 07:00
    SystemTime now = { 2026, 6, 15, 5, 0, 0, 1, 1781500000UL };
    uint8_t nextH = 0, nextM = 0;
    bool ok = scheduler.computeNext(AppMode::MEDIO, now, nextH, nextM);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT0_H, nextH);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT0_M, nextM);

    // Midday: 12:00 -> next is 21:30
    now.hour = 12;
    ok = scheduler.computeNext(AppMode::MEDIO, now, nextH, nextM);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT1_H, nextH);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT1_M, nextM);

    // Late night: 22:00 -> wraps to tomorrow 07:00
    now.hour = 22;
    ok = scheduler.computeNext(AppMode::MEDIO, now, nextH, nextM);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT0_H, nextH);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT0_M, nextM);
}

void test_scheduler_intenso_next_slot(void) {
    gState.mode = AppMode::INTENSO;

    SystemTime now = { 2026, 6, 15, 4, 0, 0, 1, 1781500000UL };
    uint8_t nextH = 0, nextM = 0;

    // 04:00 -> next is slot 0 (06:00)
    bool ok = scheduler.computeNext(AppMode::INTENSO, now, nextH, nextM);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(SCHED_INTENSO_SLOT0_H, nextH);

    // 10:00 -> next is slot 1 (14:00)
    now.hour = 10;
    ok = scheduler.computeNext(AppMode::INTENSO, now, nextH, nextM);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(SCHED_INTENSO_SLOT1_H, nextH);

    // 16:00 -> next is slot 2 (22:00)
    now.hour = 16;
    ok = scheduler.computeNext(AppMode::INTENSO, now, nextH, nextM);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(SCHED_INTENSO_SLOT2_H, nextH);

    // 23:00 -> wraps to tomorrow slot 0 (06:00)
    now.hour = 23;
    ok = scheduler.computeNext(AppMode::INTENSO, now, nextH, nextM);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(SCHED_INTENSO_SLOT0_H, nextH);
}

void test_scheduler_desativado_returns_false(void) {
    SystemTime now = { 2026, 6, 15, 12, 0, 0, 1, 1781500000UL };
    uint8_t nextH = 99, nextM = 99;
    bool ok = scheduler.computeNext(AppMode::DESATIVADO, now, nextH, nextM);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_UINT8(0, nextH);
    TEST_ASSERT_EQUAL_UINT8(0, nextM);
}

void test_scheduler_custom_every_x_days(void) {
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

void test_scheduler_cycle_expired_check(void) {
    gState.mode = AppMode::MEDIO;
    uint32_t startUnix = 1781500000UL;

    // Current time is 15 hours after start (> 8h max allowed duration) -> expired!
    SystemTime current = { 2026, 6, 15, 15, 0, 0, 1, startUnix + 15 * 3600UL };
    bool expired = scheduler.isCycleExpired(startUnix, current, 600);
    TEST_ASSERT_TRUE(expired);

    // Current time is before start time (clock went backwards) -> expired!
    current.unix = startUnix - 100;
    expired = scheduler.isCycleExpired(startUnix, current, 600);
    TEST_ASSERT_TRUE(expired);
}

void test_scheduler_suspension_auto_wake(void) {
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

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_scheduler_medio_next_slot);
    RUN_TEST(test_scheduler_intenso_next_slot);
    RUN_TEST(test_scheduler_desativado_returns_false);
    RUN_TEST(test_scheduler_custom_every_x_days);
    RUN_TEST(test_scheduler_cycle_expired_check);
    RUN_TEST(test_scheduler_suspension_auto_wake);
    return UNITY_END();
}
