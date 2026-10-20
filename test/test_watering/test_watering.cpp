#include <unity.h>
#include "TestCommon.h"
#include "../../test/mocks/TestCommon.cpp"
#include "../../src/AppState.cpp"
#include "../../src/Storage.cpp"
#include "../../src/RTClock.cpp"
#include "../../src/History.cpp"

#define private public
#include "WateringController.h"
#include "Scheduler.h"
#undef private

#include "../../src/WateringController.cpp"
#include "../../src/Scheduler.cpp"

void setUp(void) {
    resetAllTestMocks();
    gState.rtc_valid = true;
    storage.begin();
    history.begin();
    wateringCtrl.begin();
}

void tearDown(void) {
    wateringCtrl.stop();
}

void test_watering_general_builds_queue_and_skips_disabled(void) {
    // Configure zones: 0 and 2 enabled, 1 and 3 disabled / 0 min
    gState.zones[0].enabled = true;
    gState.zones[0].duration_min = 10;

    gState.zones[1].enabled = false;
    gState.zones[1].duration_min = 0;

    gState.zones[2].enabled = true;
    gState.zones[2].duration_min = 5;

    gState.zones[3].enabled = true;
    gState.zones[3].duration_min = 0; // 0 duration must be skipped!

    wateringCtrl.startGeneral(WaterTrigger::MANUAL);

    TEST_ASSERT_TRUE(wateringCtrl._active);
    TEST_ASSERT_TRUE(gState.watering.active);
    TEST_ASSERT_EQUAL_UINT8(2, wateringCtrl._queueLen);

    // Queue entry 0 should be Zone 0 (10 min = 600,000 ms)
    TEST_ASSERT_EQUAL_UINT8(0, wateringCtrl._queue[0].zone_idx);
    TEST_ASSERT_EQUAL_UINT32(600000UL, wateringCtrl._queue[0].duration_ms);

    // Queue entry 1 should be Zone 2 (5 min = 300,000 ms)
    TEST_ASSERT_EQUAL_UINT8(2, wateringCtrl._queue[1].zone_idx);
    TEST_ASSERT_EQUAL_UINT32(300000UL, wateringCtrl._queue[1].duration_ms);
}

void test_watering_custom_selection(void) {
    bool selected[NUM_ZONES] = { false, true, false, true };
    uint8_t durMin = 8; // 8 minutes

    wateringCtrl.startCustom(selected, durMin);

    TEST_ASSERT_TRUE(wateringCtrl._active);
    TEST_ASSERT_EQUAL_UINT8(2, wateringCtrl._queueLen);

    TEST_ASSERT_EQUAL_UINT8(1, wateringCtrl._queue[0].zone_idx);
    TEST_ASSERT_EQUAL_UINT32(480000UL, wateringCtrl._queue[0].duration_ms);

    TEST_ASSERT_EQUAL_UINT8(3, wateringCtrl._queue[1].zone_idx);
    TEST_ASSERT_EQUAL_UINT32(480000UL, wateringCtrl._queue[1].duration_ms);
}

void test_watering_dead_time_and_relay_activation(void) {
    gState.zones[0].enabled = true;
    gState.zones[0].duration_min = 10;
    for (int i = 1; i < NUM_ZONES; i++) gState.zones[i].enabled = false;

    wateringCtrl.startGeneral(WaterTrigger::MANUAL);
    TEST_ASSERT_TRUE(wateringCtrl._active);

    // During dead-time (first 50ms), relay must be OFF (HIGH)
    TEST_ASSERT_TRUE(wateringCtrl._isRelayDeadTimeWaiting);
    TEST_ASSERT_EQUAL_UINT8(HIGH, digitalRead(PIN_RELAY_1));

    // Advance time past dead-time (e.g. +60ms) and call update()
    advanceMockMillis(60);
    wateringCtrl.update();

    // Dead-time elapsed -> Relay 1 must be ON (LOW)!
    TEST_ASSERT_FALSE(wateringCtrl._isRelayDeadTimeWaiting);
    TEST_ASSERT_EQUAL_UINT8(LOW, digitalRead(PIN_RELAY_1));
    TEST_ASSERT_EQUAL_UINT8(HIGH, digitalRead(PIN_RELAY_2));
    TEST_ASSERT_EQUAL_UINT8(HIGH, digitalRead(PIN_RELAY_3));
    TEST_ASSERT_EQUAL_UINT8(HIGH, digitalRead(PIN_RELAY_4));
}

void test_watering_emergency_stop(void) {
    gState.zones[0].enabled = true;
    gState.zones[0].duration_min = 10;

    wateringCtrl.startGeneral(WaterTrigger::MANUAL);
    advanceMockMillis(60);
    wateringCtrl.update();
    TEST_ASSERT_EQUAL_UINT8(LOW, digitalRead(PIN_RELAY_1)); // Actively watering

    // Emergency stop
    wateringCtrl.stop();

    TEST_ASSERT_FALSE(wateringCtrl._active);
    TEST_ASSERT_FALSE(gState.watering.active);
    // All relays must immediately be OFF (HIGH)
    TEST_ASSERT_EQUAL_UINT8(HIGH, digitalRead(PIN_RELAY_1));
    TEST_ASSERT_EQUAL_UINT8(HIGH, digitalRead(PIN_RELAY_2));
    TEST_ASSERT_EQUAL_UINT8(HIGH, digitalRead(PIN_RELAY_3));
    TEST_ASSERT_EQUAL_UINT8(HIGH, digitalRead(PIN_RELAY_4));
}

void test_watering_test_run_does_not_log_to_history(void) {
    history.clear();
    TEST_ASSERT_EQUAL_UINT16(0, history.entryCount());

    wateringCtrl.startTest(1); // Test Zone 1
    TEST_ASSERT_TRUE(wateringCtrl._active);
    TEST_ASSERT_EQUAL(WaterTrigger::TEST, wateringCtrl._runTrigger);
    TEST_ASSERT_EQUAL_UINT32(ZONE_TEST_DURATION_S * 1000UL, wateringCtrl._queue[0].duration_ms);

    // Advance past test duration (30s) + dead-time
    advanceMockMillis(60);
    wateringCtrl.update(); // starts relay
    advanceMockMillis(ZONE_TEST_DURATION_S * 1000UL + 100);
    wateringCtrl.update(); // finishes cycle

    TEST_ASSERT_FALSE(wateringCtrl._active);
    // Must NOT have written an entry to history
    TEST_ASSERT_EQUAL_UINT16(0, history.entryCount());
}

void test_watering_blackout_recovery_resumes_interrupted_cycle(void) {
    // 1. Prepare interrupted state in storage
    // Slot 0 for MEDIO is 08:00 local time (SCHED_MEDIO_SLOT0_H). In June 2026, DST is active (UTC = 07:00)
    DateTime slotDT(2026, 6, 15, SCHED_MEDIO_SLOT0_H, SCHED_MEDIO_SLOT0_M, 0);
    uint32_t startUnix = slotDT.unixtime() - 3600;

    RecoveryState rs = {};
    rs.version = NVS_VERSION;
    rs.active = true;
    rs.start_unix_time = startUnix;
    rs.queuePos = 1; // Interrupted at 2nd entry (Zone 2)
    rs.queueLen = 2;
    rs.queue[0] = { 0, 600000 };
    rs.queue[1] = { 2, 600000 };
    rs.zone_dur_min[0] = 10;
    rs.zone_dur_min[2] = 0;
    rs.trigger = WaterTrigger::AUTO;
    storage.saveRecoveryState(rs);

    // 2. Set current clock 5 minutes after start (08:05 local, still well before 20:00 next slot)
    gState.mode = AppMode::MEDIO;
    gState.auto_dst = true;
    gState.now = { 2026, 6, 15, SCHED_MEDIO_SLOT0_H, (uint8_t)(SCHED_MEDIO_SLOT0_M + 5), 0, 1, startUnix + 300 };

    // 3. Re-initialize controller (simulating reboot)
    wateringCtrl.begin();

    // 4. Verify cycle resumed from Zone 2
    TEST_ASSERT_TRUE(wateringCtrl._active);
    TEST_ASSERT_EQUAL_UINT8(1, wateringCtrl._queuePos);
    TEST_ASSERT_EQUAL_UINT8(2, wateringCtrl._queue[wateringCtrl._queuePos].zone_idx);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_watering_general_builds_queue_and_skips_disabled);
    RUN_TEST(test_watering_custom_selection);
    RUN_TEST(test_watering_dead_time_and_relay_activation);
    RUN_TEST(test_watering_emergency_stop);
    RUN_TEST(test_watering_test_run_does_not_log_to_history);
    RUN_TEST(test_watering_blackout_recovery_resumes_interrupted_cycle);
    return UNITY_END();
}
