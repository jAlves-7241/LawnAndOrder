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

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_eu_dst_winter_and_summer_months);
    RUN_TEST(test_eu_dst_march_transition_boundary);
    RUN_TEST(test_eu_dst_october_transition_boundary);
    RUN_TEST(test_utc_to_local_conversion);
    return UNITY_END();
}
