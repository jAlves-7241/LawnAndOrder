#include <unity.h>
#include "TestCommon.h"
#include "../../test/mocks/TestCommon.cpp"
bool _log_suspended = false;
#include "../../src/AppState.cpp"
#include "../../src/Storage.cpp"

// Expose private static methods for white-box unit testing without altering production files
#define private public
#include "RTClock.h"
#undef private

#include "../../src/RTClock.cpp"

void setUp(void) {
    resetAllTestMocks();
}

void tearDown(void) {}

void test_eu_dst_winter_and_summer_months(void) {
    // January - definitely no DST
    DateTime winter(2026, 1, 15, 12, 0, 0);
    TEST_ASSERT_FALSE(RTClock::_isEU_DST(winter));

    // February - no DST
    DateTime feb(2026, 2, 20, 12, 0, 0);
    TEST_ASSERT_FALSE(RTClock::_isEU_DST(feb));

    // June - definitely DST
    DateTime summer(2026, 6, 21, 12, 0, 0);
    TEST_ASSERT_TRUE(RTClock::_isEU_DST(summer));

    // July - definitely DST
    DateTime july(2026, 7, 15, 12, 0, 0);
    TEST_ASSERT_TRUE(RTClock::_isEU_DST(july));

    // December - definitely no DST
    DateTime dec(2026, 12, 25, 12, 0, 0);
    TEST_ASSERT_FALSE(RTClock::_isEU_DST(dec));
}

void test_eu_dst_march_transition_boundary(void) {
    // In 2026, March 31 is Tuesday. Last Sunday is March 29.
    // Transition happens at 01:00 UTC.
    
    // Day before transition
    DateTime dayBefore(2026, 3, 28, 23, 59, 59);
    TEST_ASSERT_FALSE(RTClock::_isEU_DST(dayBefore));

    // Transition day before 01:00 UTC
    DateTime march29_0059(2026, 3, 29, 0, 59, 59);
    TEST_ASSERT_FALSE(RTClock::_isEU_DST(march29_0059));

    // Transition exact minute 01:00 UTC -> DST activates
    DateTime march29_0100(2026, 3, 29, 1, 0, 0);
    TEST_ASSERT_TRUE(RTClock::_isEU_DST(march29_0100));

    // Later on transition day
    DateTime march29_1400(2026, 3, 29, 14, 0, 0);
    TEST_ASSERT_TRUE(RTClock::_isEU_DST(march29_1400));

    // Day after transition
    DateTime dayAfter(2026, 3, 30, 0, 0, 0);
    TEST_ASSERT_TRUE(RTClock::_isEU_DST(dayAfter));
}

void test_eu_dst_october_transition_boundary(void) {
    // In 2026, October 31 is Saturday. Last Sunday is October 25.
    // Transition ends at 01:00 UTC.
    
    // Day before transition
    DateTime dayBefore(2026, 10, 24, 23, 59, 59);
    TEST_ASSERT_TRUE(RTClock::_isEU_DST(dayBefore));

    // Transition day before 01:00 UTC -> still in DST
    DateTime oct25_0059(2026, 10, 25, 0, 59, 59);
    TEST_ASSERT_TRUE(RTClock::_isEU_DST(oct25_0059));

    // Transition exact minute 01:00 UTC -> DST ends
    DateTime oct25_0100(2026, 10, 25, 1, 0, 0);
    TEST_ASSERT_FALSE(RTClock::_isEU_DST(oct25_0100));

    // Day after transition
    DateTime dayAfter(2026, 10, 26, 0, 0, 0);
    TEST_ASSERT_FALSE(RTClock::_isEU_DST(dayAfter));
}

void test_utc_to_local_conversion(void) {
    gState.auto_dst = true;

    // Winter: UTC 10:00 -> Local 10:00 + TIMEZONE_OFFSET (0 for PT) = 10:00
    DateTime winterUtc(2026, 1, 15, 10, 0, 0);
    DateTime winterLocal = RTClock::utcToLocal(winterUtc);
    TEST_ASSERT_EQUAL_UINT8(10 + TIMEZONE_OFFSET, winterLocal.hour());
    TEST_ASSERT_EQUAL_UINT8(0, winterLocal.minute());

    // Summer: UTC 10:00 -> Local 10:00 + TIMEZONE_OFFSET + 1h (DST) = 11:00
    DateTime summerUtc(2026, 7, 15, 10, 0, 0);
    DateTime summerLocal = RTClock::utcToLocal(summerUtc);
    TEST_ASSERT_EQUAL_UINT8(10 + TIMEZONE_OFFSET + 1, summerLocal.hour());
    TEST_ASSERT_EQUAL_UINT8(0, summerLocal.minute());

    // Disable auto_dst -> Summer should NOT add DST hour
    gState.auto_dst = false;
    DateTime summerLocalNoDst = RTClock::utcToLocal(summerUtc);
    TEST_ASSERT_EQUAL_UINT8(10 + TIMEZONE_OFFSET, summerLocalNoDst.hour());
}

void test_rtclock_leap_year_dst_calculation(void) {
    // 2028 is a leap year.
    // In March 2028, March 31 is Friday -> Last Sunday is March 26.
    DateTime march26_before(2028, 3, 26, 0, 59, 59);
    TEST_ASSERT_FALSE(RTClock::_isEU_DST(march26_before));

    DateTime march26_after(2028, 3, 26, 1, 0, 0);
    TEST_ASSERT_TRUE(RTClock::_isEU_DST(march26_after));

    // In October 2028, October 31 is Tuesday -> Last Sunday is October 29.
    DateTime oct29_before(2028, 10, 29, 0, 59, 59);
    TEST_ASSERT_TRUE(RTClock::_isEU_DST(oct29_before));

    DateTime oct29_after(2028, 10, 29, 1, 0, 0);
    TEST_ASSERT_FALSE(RTClock::_isEU_DST(oct29_after));
}

void test_rtclock_year_end_rollover(void) {
    gState.auto_dst = false;
    // Dec 31 23:59:59 UTC
    DateTime endOfYearUtc(2026, 12, 31, 23, 59, 59);
    DateTime local = RTClock::utcToLocal(endOfYearUtc);

    TEST_ASSERT_EQUAL_UINT16(2026, local.year());
    TEST_ASSERT_EQUAL_UINT8(12, local.month());
    TEST_ASSERT_EQUAL_UINT8(31, local.day());
    TEST_ASSERT_EQUAL_UINT8(23, local.hour());
    TEST_ASSERT_EQUAL_UINT8(59, local.minute());
    TEST_ASSERT_EQUAL_UINT8(59, local.second());

    // Midnight rollover +1s into new year
    DateTime newYearUtc = endOfYearUtc + TimeSpan(1);
    DateTime newYearLocal = RTClock::utcToLocal(newYearUtc);
    TEST_ASSERT_EQUAL_UINT16(2027, newYearLocal.year());
    TEST_ASSERT_EQUAL_UINT8(1, newYearLocal.month());
    TEST_ASSERT_EQUAL_UINT8(1, newYearLocal.day());
    TEST_ASSERT_EQUAL_UINT8(0, newYearLocal.hour());
    TEST_ASSERT_EQUAL_UINT8(0, newYearLocal.minute());
    TEST_ASSERT_EQUAL_UINT8(0, newYearLocal.second());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_eu_dst_winter_and_summer_months);
    RUN_TEST(test_eu_dst_march_transition_boundary);
    RUN_TEST(test_eu_dst_october_transition_boundary);
    RUN_TEST(test_utc_to_local_conversion);
    RUN_TEST(test_rtclock_leap_year_dst_calculation);
    RUN_TEST(test_rtclock_year_end_rollover);
    return UNITY_END();
}
