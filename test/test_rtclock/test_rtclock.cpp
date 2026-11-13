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
    rtclock = RTClock();
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

void test_rtclock_throttle_after_internal_set(void) {
    rtclock.begin();
    gState.rtc_valid = true;
    setMockMillis(100000);
    rtclock._lastReadMs = 100000;

    uint32_t nowMs = millis();
    rtclock.set(gState.now.year, gState.now.month, gState.now.day,
                gState.now.hour, gState.now.min, gState.now.sec);
    rtclock._lastReadMs = nowMs;

    advanceMockMillis(10000);
    uint32_t savedMs = rtclock._lastReadMs;
    rtclock.update();
    TEST_ASSERT_EQUAL_UINT32(savedMs, rtclock._lastReadMs);

    advanceMockMillis(25000);
    rtclock.update();
    TEST_ASSERT_EQUAL_UINT32(millis(), rtclock._lastReadMs);
}

void test_rtclock_local_to_utc_roundtrip_and_ambiguous_hour(void) {
    gState.auto_dst = true;

    // Winter round-trip: 2026-01-15 08:00 local -> UTC -> local
    DateTime winterLocal(2026, 1, 15, 8, 0, 0);
    DateTime winterUtc = RTClock::localToUtc(winterLocal);
    DateTime winterBack = RTClock::utcToLocal(winterUtc);
    TEST_ASSERT_EQUAL_UINT8(8, winterBack.hour());
    TEST_ASSERT_EQUAL_UINT8(0, winterBack.minute());

    // Summer round-trip: 2026-07-15 08:00 local -> 07:00 UTC (for PT/UTC+0) -> 08:00 local
    DateTime summerLocal(2026, 7, 15, 8, 0, 0);
    DateTime summerUtc = RTClock::localToUtc(summerLocal);
    DateTime summerBack = RTClock::utcToLocal(summerUtc);
    TEST_ASSERT_EQUAL_UINT8(8, summerBack.hour());
    TEST_ASSERT_EQUAL_UINT8(0, summerBack.minute());

    // October fall-back ambiguous local hour: 2026-10-25 (last Sunday of Oct 2026)
    // Local repeated hour is (01:00 + TIMEZONE_OFFSET)..(01:59 + TIMEZONE_OFFSET).
    DateTime ambigLocal(2026, 10, 25, (uint8_t)(1 + TIMEZONE_OFFSET), 30, 0);

    // Before first occurrence (00:00 UTC): picks summer-time occurrence (00:30 UTC)
    DateTime beforeFirst(2026, 10, 25, 0, 0, 0);
    DateTime firstUtc = RTClock::localToUtc(ambigLocal, beforeFirst.unixtime());
    TEST_ASSERT_EQUAL_UINT8(0, firstUtc.hour());
    TEST_ASSERT_EQUAL_UINT8(30, firstUtc.minute());

    // After first occurrence (00:45 UTC): picks standard-time occurrence (01:30 UTC)
    DateTime afterFirst(2026, 10, 25, 0, 45, 0);
    DateTime secondUtc = RTClock::localToUtc(ambigLocal, afterFirst.unixtime());
    TEST_ASSERT_EQUAL_UINT8(1, secondUtc.hour());
    TEST_ASSERT_EQUAL_UINT8(30, secondUtc.minute());
}

void test_rtclock_midrun_begin_preserves_ram_and_restores_lost_power(void) {
    TEST_ASSERT_TRUE(rtclock.begin());
    rtclock.set(2026, 6, 15, 14, 30, 0);
    TEST_ASSERT_TRUE(gState.rtc_valid);

    // 1. Mid-run I2C bus recovery when RTC temporarily NACKs must preserve RAM validity
    rtclock._rtc.mockPresent = false;
    TEST_ASSERT_FALSE(rtclock.begin());
    TEST_ASSERT_TRUE(gState.rtc_valid);

    // 2. Mid-run RTC power glitch (lostPower=true) when RAM time is valid restores RTC hardware
    rtclock._rtc.mockPresent = true;
    rtclock._rtc.mockLostPower = true;
    TEST_ASSERT_TRUE(rtclock.begin());
    TEST_ASSERT_TRUE(gState.rtc_valid);
    TEST_ASSERT_FALSE(rtclock._rtc.mockLostPower);
}

void test_rtclock_refresh_local_time_on_dst_toggle(void) {
    gState.auto_dst = true;
    rtclock.begin();
    // Summer local 11:00 (with DST+1 in UTC+0 -> 10:00 UTC)
    rtclock.set(2026, 7, 15, 11, 0, 0);
    TEST_ASSERT_EQUAL_UINT8(11, gState.now.hour);

    // Disable auto_dst and refresh immediately without waiting 30s hardware poll
    gState.auto_dst = false;
    rtclock.refreshLocalTime();
    TEST_ASSERT_EQUAL_UINT8(10, gState.now.hour);
}

void test_rtclock_initial_lost_power_preserves_osf_and_rejects_invalid_set(void) {
    rtclock._rtc.mockPresent = true;
    rtclock._rtc.mockLostPower = true;
    gState.rtc_valid = false;

    TEST_ASSERT_TRUE(rtclock.begin());
    TEST_ASSERT_FALSE(gState.rtc_valid);
    // Hardware OSF must NOT be cleared on initial boot when RAM time was invalid
    TEST_ASSERT_TRUE(rtclock._rtc.mockLostPower);

    // Invalid calendar date (Feb 30) must return false without changing clock or revision
    uint32_t revBefore = rtclock.revision();
    TEST_ASSERT_FALSE(rtclock.set(2026, 2, 30, 12, 0, 0));
    TEST_ASSERT_FALSE(gState.rtc_valid);
    TEST_ASSERT_EQUAL_UINT32(revBefore, rtclock.revision());

    // Valid set while RTC hardware missing (mockPresent=false) returns false but updates RAM fallback
    rtclock._rtc.mockPresent = false;
    TEST_ASSERT_FALSE(rtclock.set(2026, 6, 15, 10, 0, 0));
    TEST_ASSERT_TRUE(gState.rtc_valid);
    TEST_ASSERT_EQUAL_UINT32(revBefore + 1, rtclock.revision());
}

void test_rtclock_hotplug_validates_reading_and_tracks_revision(void) {
    rtclock._rtc.mockPresent = false;
    TEST_ASSERT_FALSE(rtclock.begin());
    TEST_ASSERT_FALSE(gState.rtc_valid);

    // Re-attach RTC carrying invalid year (2000) -> hotplug must keep rtc_valid false
    rtclock._rtc.mockPresent = true;
    rtclock._rtc.adjust(DateTime(2000, 1, 1, 0, 0, 0));
    advanceMockMillis(35000);
    rtclock.update();
    TEST_ASSERT_FALSE(gState.rtc_valid);

    // Valid hardware time succeeds and valid set() increments revision
    rtclock._rtc.adjust(DateTime(2026, 6, 15, 9, 0, 0));
    advanceMockMillis(35000);
    rtclock.update();
    TEST_ASSERT_TRUE(gState.rtc_valid);

    uint32_t rev = rtclock.revision();
    TEST_ASSERT_TRUE(rtclock.set(2026, 6, 15, 10, 30, 0));
    TEST_ASSERT_EQUAL_UINT32(rev + 1, rtclock.revision());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_eu_dst_winter_and_summer_months);
    RUN_TEST(test_eu_dst_march_transition_boundary);
    RUN_TEST(test_eu_dst_october_transition_boundary);
    RUN_TEST(test_utc_to_local_conversion);
    RUN_TEST(test_rtclock_leap_year_dst_calculation);
    RUN_TEST(test_rtclock_year_end_rollover);
    RUN_TEST(test_rtclock_throttle_after_internal_set);
    RUN_TEST(test_rtclock_local_to_utc_roundtrip_and_ambiguous_hour);
    RUN_TEST(test_rtclock_midrun_begin_preserves_ram_and_restores_lost_power);
    RUN_TEST(test_rtclock_refresh_local_time_on_dst_toggle);
    RUN_TEST(test_rtclock_initial_lost_power_preserves_osf_and_rejects_invalid_set);
    RUN_TEST(test_rtclock_hotplug_validates_reading_and_tracks_revision);
    return UNITY_END();
}
