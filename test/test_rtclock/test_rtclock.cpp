#include <unity.h>
#include "TestCommon.h"
#include "../../test/mocks/TestCommon.cpp"
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
    // Dec 31 23:59:59 local (adjust UTC by -TIMEZONE_OFFSET so rollover works in any timezone)
    DateTime endOfYearLocalTarget(2026, 12, 31, 23, 59, 59);
    DateTime endOfYearUtc(endOfYearLocalTarget.unixtime() - (int32_t)TIMEZONE_OFFSET * 3600L);
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
    // Transition occurs at 01:00 UTC -> repeated local instant matches 01:30 UTC + TIMEZONE_OFFSET.
    DateTime ambigStdUtc(2026, 10, 25, 1, 30, 0);
    DateTime ambigLocal(ambigStdUtc.unixtime() + (int32_t)TIMEZONE_OFFSET * 3600L);

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

void test_rtclock_battery_warning_dedup_export_deferral_and_hotplug_logs(void) {
    TEST_ASSERT_TRUE(rtclock.begin());
    Serial.reset();
    TEST_ASSERT_TRUE(rtclock.set(2026, 6, 15, 12, 0, 0));
#if LOG_LEVEL >= LVL_INFO
    char expectedSet[128];
    snprintf(expectedSet, sizeof(expectedSet), TXT_LOG_RTC_TIME_SET, 2026, 6, 15, 12, 0, 0);
    size_t firstPos = Serial.outputBuffer.find(expectedSet);
    TEST_ASSERT_TRUE(firstPos != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedSet, firstPos + 1) == std::string::npos);
#endif
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_RTC_NOT_FOUND) == std::string::npos);

    // 1. Persistent lostPower across repeated 30 s polls logs warning only once until a clean poll
    rtclock._rtc.mockLostPower = true;
    advanceMockMillis(31000);
    Serial.reset();
    rtclock.update();
    TEST_ASSERT_TRUE(rtclock._batRecoveryWarned);
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_RTC_BAT_FAIL_REC) != std::string::npos);
#endif

    rtclock._rtc.mockLostPower = true;
    advanceMockMillis(31000);
    Serial.reset();
    rtclock.update();
    TEST_ASSERT_TRUE(rtclock._batRecoveryWarned);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_RTC_BAT_FAIL_REC) == std::string::npos);

    // Clean poll (mockLostPower == false) resets _batRecoveryWarned so a future battery glitch logs again
    advanceMockMillis(31000);
    rtclock.update();
    TEST_ASSERT_FALSE(rtclock._batRecoveryWarned);

    // 2. While CSV export is active (_log_suspended = true), 30 s hardware poll is deferred so alerts aren't swallowed
    _log_suspended = true;
    rtclock._rtc.mockLostPower = true;
    advanceMockMillis(31000);
    rtclock.update();
    TEST_ASSERT_TRUE(rtclock._rtc.mockLostPower);
    TEST_ASSERT_FALSE(rtclock._batRecoveryWarned);

    _log_suspended = false;
    Serial.reset();
    rtclock.update();
    TEST_ASSERT_TRUE(rtclock._batRecoveryWarned);
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_RTC_BAT_FAIL_REC) != std::string::npos);
#endif

    // 3. Hotplug with software-valid clock and lostPower restores time to RTC, bumps revision, and logs HOT_RESTORED
    rtclock._found = false;
    rtclock._rtc.mockPresent = true;
    rtclock._rtc.mockLostPower = true;
    uint32_t revBefore = rtclock.revision();
    advanceMockMillis(31000);
    Serial.reset();
    rtclock.update();
    TEST_ASSERT_EQUAL_UINT32(revBefore + 1, rtclock.revision());
#if LOG_LEVEL >= LVL_INFO
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_RTC_HOT_RESTORED) != std::string::npos);
#endif

    // 4. Hotplug when RAM clock is invalid and lostPower is set logs TXT_LOG_RTC_BAT_LOW
    rtclock._found = false;
    gState.rtc_valid = false;
    rtclock._rtc.mockLostPower = true;
    advanceMockMillis(31000);
    Serial.reset();
    rtclock.update();
    TEST_ASSERT_FALSE(gState.rtc_valid);
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_RTC_BAT_LOW) != std::string::npos);
#endif

    // 5. Unexpected RTC time jump (> 300s drift) increments revision and logs TXT_LOG_RTC_TIME_JUMP
    rtclock._rtc.mockLostPower = false;
    TEST_ASSERT_TRUE(rtclock.set(2026, 6, 15, 10, 0, 0));
    uint32_t revBeforeJump = rtclock.revision();
    rtclock._rtc.adjust(DateTime(gState.now.unix + 3600UL)); // +1h UTC jump
    advanceMockMillis(31000);
    Serial.reset();
    rtclock.update();
    TEST_ASSERT_EQUAL_UINT32(revBeforeJump + 1, rtclock.revision());
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_RTC_TIME_JUMP) != std::string::npos);
#endif
}

static uint8_t _mockRtcErrCbCalls = 0;
static void _mockRtcErrCb() {
    _mockRtcErrCbCalls++;
}

void test_rtclock_i2c_read_fail_blocks_spurious_adjust_and_hotplug_corrupt_guard(void) {
    TEST_ASSERT_TRUE(rtclock.begin());
    TEST_ASSERT_TRUE(rtclock.set(2026, 6, 15, 10, 0, 0));
    uint32_t revBefore = rtclock.revision();

    // 1. Even if mockLostPower is spuriously set, an I2C read error must NEVER call _rtc.adjust(),
    //    and 3 consecutive read failures must trigger _errorCb and reset _found = false.
    _mockRtcErrCbCalls = 0;
    rtclock.setErrorCallback(_mockRtcErrCb);
    rtclock._rtc.mockLostPower = true;
    rtclock._rtc.mockI2cReadFail = true;
    DateTime hwBefore = rtclock._rtc.now();

    advanceMockMillis(31000);
    rtclock.update();
    TEST_ASSERT_TRUE(rtclock._rtc.mockLostPower); // adjust() was NOT called!
    TEST_ASSERT_EQUAL_UINT32(hwBefore.unixtime(), rtclock._rtc.now().unixtime());
    TEST_ASSERT_EQUAL_UINT8(1, rtclock._consecErrors);
    TEST_ASSERT_EQUAL_UINT8(0, _mockRtcErrCbCalls);

    advanceMockMillis(6000);
    rtclock.update();
    TEST_ASSERT_EQUAL_UINT8(2, rtclock._consecErrors);

    advanceMockMillis(6000);
    rtclock.update();
    TEST_ASSERT_EQUAL_UINT8(0, rtclock._consecErrors);
    TEST_ASSERT_FALSE(rtclock.isPresent());
    TEST_ASSERT_EQUAL_UINT8(1, _mockRtcErrCbCalls);
    TEST_ASSERT_TRUE(gState.rtc_valid); // RAM software clock preserved mid-run

    // 2. Hotplug when wasValid == true, !_lostPower, and corrupt DateTime (year 2000):
    //    must NOT write RAM time to RTC, must NOT bump revision, and must leave _found = false.
    rtclock._rtc.mockI2cReadFail = false;
    rtclock._rtc.mockLostPower = false;
    rtclock._rtc.adjust(DateTime(2000, 1, 1, 0, 0, 0));
    advanceMockMillis(31000);
    rtclock.update();
    TEST_ASSERT_FALSE(rtclock.isPresent());
    TEST_ASSERT_EQUAL_UINT32(revBefore, rtclock.revision());
    TEST_ASSERT_EQUAL_UINT16(2000, rtclock._rtc.now().year()); // untouched

    // 3. Hotplug with I2C read fail right after ACK also keeps _found = false
    rtclock._rtc.mockI2cReadFail = true;
    advanceMockMillis(31000);
    rtclock.update();
    TEST_ASSERT_FALSE(rtclock.isPresent());
    TEST_ASSERT_EQUAL_UINT32(revBefore, rtclock.revision());

    rtclock.setErrorCallback(nullptr);
}

void test_rtclock_battery_low_latch_periodic_recheck_and_clear(void) {
    // 1. Cold boot with OSF active sets isBatteryLow() immediately; 30 s check keeps it while OSF stays set
    rtclock._rtc.mockPresent = true;
    rtclock._rtc.mockLostPower = true;
    gState.rtc_valid = false;
    TEST_ASSERT_TRUE(rtclock.begin());
    TEST_ASSERT_FALSE(gState.rtc_valid);
    TEST_ASSERT_TRUE(rtclock.isBatteryLow());

    advanceMockMillis(31000);
    rtclock.update();
    TEST_ASSERT_FALSE(gState.rtc_valid);
    TEST_ASSERT_TRUE(rtclock.isBatteryLow());

    // 2. If OSF clears on a periodic 30 s recheck while !rtc_valid (contact/fluke resolved), auto-recovers
    rtclock._rtc.mockLostPower = false;
    rtclock._rtc.adjust(DateTime(2026, 6, 15, 9, 0, 0));
    advanceMockMillis(31000);
    rtclock.update();
    TEST_ASSERT_TRUE(gState.rtc_valid);
    TEST_ASSERT_FALSE(rtclock.isBatteryLow());

    // 3. Mid-run battery loss when RAM time is valid restores RTC but keeps isBatteryLow() latched across clean polls
    rtclock._rtc.mockLostPower = true;
    advanceMockMillis(31000);
    Serial.reset();
    rtclock.update();
    TEST_ASSERT_TRUE(gState.rtc_valid);
    TEST_ASSERT_FALSE(rtclock._rtc.mockLostPower);
    TEST_ASSERT_TRUE(rtclock.isBatteryLow());
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_RTC_BAT_FAIL_REC) != std::string::npos);
#endif

    // Clean 30 s poll and mid-run recoverI2C (begin() with wasValid=true) must NOT wipe active battery warning
    advanceMockMillis(31000);
    rtclock.update();
    TEST_ASSERT_TRUE(rtclock.isBatteryLow());
    TEST_ASSERT_TRUE(rtclock.begin());
    TEST_ASSERT_TRUE(rtclock.isBatteryLow());

    // 4. Failed set() keeps isBatteryLow() latched; successful set() clears it
    rtclock._rtc.mockPresent = false;
    TEST_ASSERT_FALSE(rtclock.set(2026, 6, 15, 10, 0, 0));
    TEST_ASSERT_TRUE(rtclock.isBatteryLow());

    rtclock._rtc.mockPresent = true;
    TEST_ASSERT_TRUE(rtclock.set(2026, 6, 15, 10, 5, 0));
    TEST_ASSERT_FALSE(rtclock.isBatteryLow());

    // 5. Hotplug recovery with OSF active latches isBatteryLow() and logs battery warning
    rtclock._found = false;
    rtclock._rtc.mockLostPower = true;
    advanceMockMillis(31000);
    Serial.reset();
    rtclock.update();
    TEST_ASSERT_TRUE(rtclock.isBatteryLow());
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_RTC_BAT_FAIL_REC) != std::string::npos);
#endif
}

void test_rtclock_manual_revision_only_tracks_valid_sets(void) {
    TEST_ASSERT_EQUAL_UINT32(0, rtclock.manualRevision());
    TEST_ASSERT_TRUE(rtclock.begin());
    TEST_ASSERT_EQUAL_UINT32(0, rtclock.manualRevision());

    // Invalid calendar dates/years must NOT increment manualRevision()
    TEST_ASSERT_FALSE(rtclock.set(2026, 2, 30, 12, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(0, rtclock.manualRevision());
    TEST_ASSERT_FALSE(rtclock.set(DATE_YEAR_MIN - 1, 6, 15, 12, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(0, rtclock.manualRevision());

    // Valid set() with hardware present increments manualRevision()
    TEST_ASSERT_TRUE(rtclock.set(2026, 6, 15, 10, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(1, rtclock.manualRevision());

    // Valid set() falling back to software RAM clock still increments manualRevision()
    rtclock._rtc.mockPresent = false;
    TEST_ASSERT_FALSE(rtclock.set(2026, 6, 15, 10, 5, 0));
    TEST_ASSERT_TRUE(gState.rtc_valid);
    TEST_ASSERT_EQUAL_UINT32(2, rtclock.manualRevision());

    // Hotplug sync bumps revision() but NOT manualRevision()
    rtclock._found = false;
    rtclock._rtc.mockPresent = true;
    rtclock._rtc.adjust(DateTime(2026, 6, 15, 9, 10, 0));
    uint32_t revBefore = rtclock.revision();
    advanceMockMillis(31000);
    rtclock.update();
    TEST_ASSERT_EQUAL_UINT32(revBefore + 1, rtclock.revision());
    TEST_ASSERT_EQUAL_UINT32(2, rtclock.manualRevision());

    // Hotplug OSF restore bumps revision() but NOT manualRevision()
    rtclock._found = false;
    rtclock._rtc.mockLostPower = true;
    revBefore = rtclock.revision();
    advanceMockMillis(31000);
    rtclock.update();
    TEST_ASSERT_EQUAL_UINT32(revBefore + 1, rtclock.revision());
    TEST_ASSERT_EQUAL_UINT32(2, rtclock.manualRevision());

    // >300s hardware clock drift bumps revision() but NOT manualRevision()
    rtclock._rtc.adjust(DateTime(2026, 6, 15, 11, 0, 0));
    revBefore = rtclock.revision();
    advanceMockMillis(31000);
    rtclock.update();
    TEST_ASSERT_EQUAL_UINT32(revBefore + 1, rtclock.revision());
    TEST_ASSERT_EQUAL_UINT32(2, rtclock.manualRevision());
}

void test_rtclock_adjust_verify_bcd_12h_0xff_and_no_sw_fallback(void) {
    // 1. Successful set() updates _lastReadMs to millis() (no redundant poll on next loop)
    setMockMillis(50000);
    TEST_ASSERT_TRUE(rtclock.begin());
    TEST_ASSERT_TRUE(rtclock.set(2026, 6, 15, 10, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(50000UL, rtclock._lastReadMs);

    // 2. Silent adjust() failure on an RTC holding an older valid date MUST fail verification
    rtclock._rtc.mockAdjustFail = true;
    uint32_t revBefore = rtclock.revision();
    uint32_t manRevBefore = rtclock.manualRevision();
    TEST_ASSERT_FALSE(rtclock.set(2026, 6, 15, 14, 0, 0, /*allowSwFallback=*/false));
    TEST_ASSERT_EQUAL_UINT8(10, gState.now.hour); // RAM untouched when allowSwFallback=false
    TEST_ASSERT_EQUAL_UINT32(revBefore, rtclock.revision());
    TEST_ASSERT_EQUAL_UINT32(manRevBefore, rtclock.manualRevision());
    rtclock._rtc.mockAdjustFail = false;

    // 3. allowSwFallback=false when RTC is absent keeps rtc_valid=false
    rtclock = RTClock();
    rtclock._rtc.mockPresent = false;
    gState.rtc_valid = false;
    TEST_ASSERT_FALSE(rtclock.set(2026, 6, 15, 11, 0, 0, /*allowSwFallback=*/false));
    TEST_ASSERT_FALSE(gState.rtc_valid);

    // 4. Raw DS3231 Wire/BCD decoding: 12-hour AM/PM and 0xFF floating-bus rejection
    rtclock = RTClock();
    rtclock._rtc.mockPresent = true;
    rtclock._rtc.mockRawOverride = true;
    rtclock._rtc.mockRawStatus = 0x00;

    // Helper: sec=05, min=15, dow=2, day=15, month=06, year=26 (2026)
    auto setRaw12h = [&](uint8_t hrReg) {
        rtclock._rtc.mockRawTime[0] = 0x05;
        rtclock._rtc.mockRawTime[1] = 0x15;
        rtclock._rtc.mockRawTime[2] = hrReg;
        rtclock._rtc.mockRawTime[3] = 0x02;
        rtclock._rtc.mockRawTime[4] = 0x15;
        rtclock._rtc.mockRawTime[5] = 0x06;
        rtclock._rtc.mockRawTime[6] = 0x26;
    };

    bool ok = false, lost = false;
    // 12:15 AM (bit6=1, PM=0, BCD 12 = 0x12 -> 0x52) -> 00:15
    setRaw12h(0x52);
    DateTime dt = rtclock._readRtc(ok, lost);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(lost);
    TEST_ASSERT_EQUAL_UINT8(0, dt.hour());
    TEST_ASSERT_EQUAL_UINT8(15, dt.minute());

    // 05:15 AM (bit6=1, PM=0, BCD 05 = 0x05 -> 0x45) -> 05:15
    setRaw12h(0x45);
    dt = rtclock._readRtc(ok, lost);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(5, dt.hour());

    // 12:15 PM (bit6=1, PM=1, BCD 12 = 0x12 -> 0x72) -> 12:15
    setRaw12h(0x72);
    dt = rtclock._readRtc(ok, lost);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(12, dt.hour());

    // 05:15 PM (bit6=1, PM=1, BCD 05 = 0x05 -> 0x65) -> 17:15
    setRaw12h(0x65);
    dt = rtclock._readRtc(ok, lost);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(17, dt.hour());

    // All-0xFF floating bus must be rejected (ok=false), NOT mistaken for valid OSF!
    rtclock._rtc.mockRawStatus = 0xFF;
    memset(rtclock._rtc.mockRawTime, 0xFF, 7);
    dt = rtclock._readRtc(ok, lost);
    TEST_ASSERT_FALSE(ok);

    // Invalid reserved status bit (0x10) also rejected
    rtclock._rtc.mockRawStatus = 0x10;
    dt = rtclock._readRtc(ok, lost);
    TEST_ASSERT_FALSE(ok);

    // Genuine OSF (0x80, reserved bits 6..4 == 0) with corrupt BCD returns ok=true, lostPower=true
    rtclock._rtc.mockRawStatus = 0x80;
    dt = rtclock._readRtc(ok, lost);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(lost);

    // 5. suspended_until shift clamps to 2099-12-31 23:59:59 UTC (4102444799UL), never UINT32_MAX
    rtclock = RTClock();
    storage.begin();
    TEST_ASSERT_TRUE(rtclock.begin());
    TEST_ASSERT_TRUE(rtclock.set(2026, 6, 15, 10, 0, 0));
    gState.suspended = true;
    gState.suspended_until = gState.now.unix + 10UL * 86400UL;
    TEST_ASSERT_TRUE(rtclock.set(2099, 12, 30, 12, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(4102444799UL, gState.suspended_until);
}

void test_rtclock_osf_recovery_advances_sw_clock_before_restore(void) {
    // 1. Periodic update() OSF restore preserves elapsed seconds since last SW tick
    setMockMillis(1000);
    TEST_ASSERT_TRUE(rtclock.begin());
    TEST_ASSERT_TRUE(rtclock.set(2026, 6, 15, 10, 0, 0));
    const uint32_t baseUnix = gState.now.unix;

    rtclock._rtc.mockLostPower = true;
    advanceMockMillis(45000UL); // 45s elapsed without intermediate update()
    rtclock.update();
    TEST_ASSERT_EQUAL_UINT32(baseUnix + 45UL, gState.now.unix);
    TEST_ASSERT_EQUAL_UINT32(baseUnix + 45UL, rtclock._rtc.now().unixtime());

    // 2. Hotplug OSF restore also advances SW clock before writing back to DS3231
    rtclock._found = false;
    rtclock._rtc.mockLostPower = true;
    advanceMockMillis(35000UL);
    rtclock.update();
    TEST_ASSERT_TRUE(rtclock.isPresent());
    TEST_ASSERT_EQUAL_UINT32(baseUnix + 80UL, gState.now.unix);
    TEST_ASSERT_EQUAL_UINT32(baseUnix + 80UL, rtclock._rtc.now().unixtime());

    // 3. Mid-run begin() (e.g. recoverI2C()) with OSF advances SW clock before restoring RTC
    rtclock._rtc.mockLostPower = true;
    advanceMockMillis(12000UL);
    TEST_ASSERT_TRUE(rtclock.begin());
    TEST_ASSERT_EQUAL_UINT32(baseUnix + 92UL, gState.now.unix);
    TEST_ASSERT_EQUAL_UINT32(baseUnix + 92UL, rtclock._rtc.now().unixtime());

    // 4. Mid-run begin() during recoverI2C() bumps revision() on >300s jump or !wasValid->valid,
    //    without touching manualRevision() or bumping on small (<300s) syncs
    const uint32_t revSmall = rtclock.revision();
    const uint32_t manSmall = rtclock.manualRevision();
    rtclock._rtc.adjust(DateTime(gState.now.unix + 10UL));
    TEST_ASSERT_TRUE(rtclock.begin());
    TEST_ASSERT_EQUAL_UINT32(revSmall, rtclock.revision());
    TEST_ASSERT_EQUAL_UINT32(manSmall, rtclock.manualRevision());

    rtclock._rtc.adjust(DateTime(gState.now.unix + 3600UL)); // +1h jump during I2C recovery
    TEST_ASSERT_TRUE(rtclock.begin());
    TEST_ASSERT_EQUAL_UINT32(revSmall + 1, rtclock.revision());
    TEST_ASSERT_EQUAL_UINT32(manSmall, rtclock.manualRevision());

    gState.rtc_valid = false;
    const uint32_t revInvalid = rtclock.revision();
    TEST_ASSERT_TRUE(rtclock.begin());
    TEST_ASSERT_TRUE(gState.rtc_valid);
    TEST_ASSERT_EQUAL_UINT32(revInvalid + 1, rtclock.revision());
    TEST_ASSERT_EQUAL_UINT32(manSmall, rtclock.manualRevision());
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
    RUN_TEST(test_rtclock_battery_warning_dedup_export_deferral_and_hotplug_logs);
    RUN_TEST(test_rtclock_i2c_read_fail_blocks_spurious_adjust_and_hotplug_corrupt_guard);
    RUN_TEST(test_rtclock_battery_low_latch_periodic_recheck_and_clear);
    RUN_TEST(test_rtclock_manual_revision_only_tracks_valid_sets);
    RUN_TEST(test_rtclock_adjust_verify_bcd_12h_0xff_and_no_sw_fallback);
    RUN_TEST(test_rtclock_osf_recovery_advances_sw_clock_before_restore);
    return UNITY_END();
}
