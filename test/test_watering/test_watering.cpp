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

    // During dead-time (first 20ms), relay must be OFF (HIGH)
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

void test_watering_multi_zone_sequence_and_history_logging(void) {
    history.clear();
    TEST_ASSERT_EQUAL_UINT16(0, history.entryCount());

    // Configure 2 active zones: Zone 0 (1 min) and Zone 2 (2 min)
    gState.zones[0].enabled = true;
    gState.zones[0].duration_min = 1;
    gState.zones[1].enabled = false;
    gState.zones[1].duration_min = 0;
    gState.zones[2].enabled = true;
    gState.zones[2].duration_min = 2;
    gState.zones[3].enabled = false;
    gState.zones[3].duration_min = 0;

    gState.now = { 2026, 6, 15, 8, 0, 0, 1, 1781500000UL };

    // Start auto cycle
    wateringCtrl.startGeneral(WaterTrigger::AUTO);
    TEST_ASSERT_TRUE(wateringCtrl._active);
    TEST_ASSERT_TRUE(wateringCtrl._isRelayDeadTimeWaiting);

    // 1. Pass dead-time for Zone 0
    advanceMockMillis(60);
    wateringCtrl.update();
    TEST_ASSERT_FALSE(wateringCtrl._isRelayDeadTimeWaiting);
    TEST_ASSERT_EQUAL_UINT8(LOW, digitalRead(PIN_RELAY_1)); // Zone 0 ON

    // 2. Complete Zone 0 duration (1 min = 60000 ms)
    advanceMockMillis(60000);
    wateringCtrl.update();
    TEST_ASSERT_EQUAL_UINT8(HIGH, digitalRead(PIN_RELAY_1)); // Zone 0 OFF
    TEST_ASSERT_TRUE(wateringCtrl._isWaiting); // Waiting ZONE_WAIT_DELAY_MS

    // 3. During inter-zone delay (ZONE_WAIT_DELAY_MS = 5000ms), all relays must remain OFF
    advanceMockMillis(2500);
    wateringCtrl.update();
    TEST_ASSERT_TRUE(wateringCtrl._isWaiting);
    TEST_ASSERT_EQUAL_UINT8(HIGH, digitalRead(PIN_RELAY_1));
    TEST_ASSERT_EQUAL_UINT8(HIGH, digitalRead(PIN_RELAY_3));

    // 4. Complete inter-zone delay (another 2600ms > 5000ms total)
    advanceMockMillis(2600);
    wateringCtrl.update();
    TEST_ASSERT_FALSE(wateringCtrl._isWaiting);
    TEST_ASSERT_TRUE(wateringCtrl._isRelayDeadTimeWaiting);

    // 5. Pass dead-time for Zone 2
    advanceMockMillis(60);
    wateringCtrl.update();
    TEST_ASSERT_FALSE(wateringCtrl._isRelayDeadTimeWaiting);
    TEST_ASSERT_EQUAL_UINT8(LOW, digitalRead(PIN_RELAY_3)); // Zone 2 ON

    // 6. Complete Zone 2 duration (2 min = 120000 ms)
    advanceMockMillis(120000);
    wateringCtrl.update();

    // 7. Verify cycle finished and relays OFF
    TEST_ASSERT_FALSE(wateringCtrl._active);
    TEST_ASSERT_FALSE(gState.watering.active);
    TEST_ASSERT_EQUAL_UINT8(HIGH, digitalRead(PIN_RELAY_3));

    // 8. Verify History recorded
    TEST_ASSERT_EQUAL_UINT16(1, history.entryCount());
    HistoryEntry entries[1] = {};
    TEST_ASSERT_EQUAL_UINT8(1, history.readLast(1, entries));
    TEST_ASSERT_EQUAL(WaterTrigger::AUTO, entries[0].trigger);
    TEST_ASSERT_EQUAL_UINT8(1, entries[0].zone_dur[0]);
    TEST_ASSERT_EQUAL_UINT8(0, entries[0].zone_dur[1]);
    TEST_ASSERT_EQUAL_UINT8(2, entries[0].zone_dur[2]);
    TEST_ASSERT_EQUAL_UINT8(0, entries[0].zone_dur[3]);

    // 9. Recovery state cleared
    RecoveryState rs = {};
    storage.loadRecoveryState(rs);
    TEST_ASSERT_FALSE(rs.active);
}

void test_watering_emergency_stop_partial_duration_recorded(void) {
    history.clear();
    gState.zones[0].enabled = true;
    gState.zones[0].duration_min = 10;
    for (int i = 1; i < NUM_ZONES; i++) gState.zones[i].enabled = false;
    gState.now = { 2026, 6, 15, 8, 0, 0, 1, 1781500000UL };

    // Run 1: Stop after 45s (>= 30s) -> should record 1 min
    wateringCtrl.startGeneral(WaterTrigger::MANUAL);
    advanceMockMillis(60);
    wateringCtrl.update();
    advanceMockMillis(45000);
    wateringCtrl.stop();

    TEST_ASSERT_EQUAL_UINT16(1, history.entryCount());
    HistoryEntry e[1] = {};
    history.readLast(1, e);
    TEST_ASSERT_EQUAL_UINT8(1, e[0].zone_dur[0]);

    // Run 2: Stop after 10s (< 30s) -> should record 0 min (no entry logged)
    wateringCtrl.startGeneral(WaterTrigger::MANUAL);
    advanceMockMillis(60);
    wateringCtrl.update();
    advanceMockMillis(10000);
    wateringCtrl.stop();

    // Entry count should still be 1 (second run was ignored due to 0 duration)
    TEST_ASSERT_EQUAL_UINT16(1, history.entryCount());
}

void test_watering_empty_selection_does_not_activate(void) {
    for (int i = 0; i < NUM_ZONES; i++) {
        gState.zones[i].enabled = false;
        gState.zones[i].duration_min = 0;
    }

    wateringCtrl.startGeneral(WaterTrigger::MANUAL);
    TEST_ASSERT_FALSE(wateringCtrl._active);
    TEST_ASSERT_FALSE(gState.watering.active);

    bool noZones[NUM_ZONES] = { false, false, false, false };
    wateringCtrl.startCustom(noZones, 10);
    TEST_ASSERT_FALSE(wateringCtrl._active);
}

void test_watering_recovery_rejects_corrupted_and_suspended_states(void) {
    DateTime slotDT(2026, 6, 15, SCHED_MEDIO_SLOT0_H, SCHED_MEDIO_SLOT0_M, 0);
    uint32_t startUnix = slotDT.unixtime() - 3600;
    gState.mode = AppMode::MEDIO;
    gState.auto_dst = true;
    gState.now = { 2026, 6, 15, SCHED_MEDIO_SLOT0_H, (uint8_t)(SCHED_MEDIO_SLOT0_M + 2), 0, 1, startUnix + 120 };

    // 1. Zero duration_ms must be rejected
    RecoveryState rs = {};
    rs.version = NVS_VERSION;
    rs.active = true;
    rs.start_unix_time = startUnix;
    rs.queuePos = 0;
    rs.queueLen = 1;
    rs.queue[0] = { 0, 0 }; // Corrupted 0ms!
    rs.trigger = WaterTrigger::MANUAL;
    storage.saveRecoveryState(rs);
    wateringCtrl.begin();
    TEST_ASSERT_FALSE(wateringCtrl.isActive());

    // 2. Duration > 20 min (1,200,000 ms) must be rejected
    rs.queue[0] = { 0, 2000000UL };
    storage.saveRecoveryState(rs);
    wateringCtrl.begin();
    TEST_ASSERT_FALSE(wateringCtrl.isActive());

    // 3. Duplicate zone_idx in queue must be rejected
    rs.queueLen = 2;
    rs.queue[0] = { 1, 600000UL };
    rs.queue[1] = { 1, 600000UL }; // Duplicate Zone 1!
    storage.saveRecoveryState(rs);
    wateringCtrl.begin();
    TEST_ASSERT_FALSE(wateringCtrl.isActive());

    // 4. AUTO recovery while system is suspended must be skipped
    gState.suspended = true;
    rs.queueLen = 1;
    rs.queue[0] = { 0, 600000UL };
    rs.trigger = WaterTrigger::AUTO;
    storage.saveRecoveryState(rs);
    wateringCtrl.begin();
    TEST_ASSERT_FALSE(wateringCtrl.isActive());
    RecoveryState cleared = {};
    storage.loadRecoveryState(cleared);
    TEST_ASSERT_FALSE(cleared.active);
}

void test_watering_inter_zone_wait_updates_upcoming_zone_index(void) {
    gState.zones[0].enabled = true;
    gState.zones[0].duration_min = 1;
    gState.zones[1].enabled = false;
    gState.zones[2].enabled = true;
    gState.zones[2].duration_min = 2;
    gState.zones[3].enabled = false;

    wateringCtrl.startGeneral(WaterTrigger::MANUAL);
    advanceMockMillis(60);
    wateringCtrl.update();
    TEST_ASSERT_EQUAL_UINT8(0, gState.watering.zone_idx);

    // Finish Zone 0 -> enters inter-zone wait before Zone 2
    advanceMockMillis(60000);
    wateringCtrl.update();
    TEST_ASSERT_TRUE(gState.watering.is_waiting);
    TEST_ASSERT_EQUAL_UINT8(2, gState.watering.zone_idx);
    TEST_ASSERT_EQUAL_UINT8(0, gState.watering.progress_pct);
}

void test_watering_deenergize_relays_preserves_recovery_state(void) {
    gState.zones[0].enabled = true;
    gState.zones[0].duration_min = 10;
    wateringCtrl.startGeneral(WaterTrigger::MANUAL);
    advanceMockMillis(60);
    wateringCtrl.update();
    TEST_ASSERT_EQUAL_UINT8(LOW, digitalRead(PIN_RELAY_1));

    wateringCtrl.deenergizeRelays();
    TEST_ASSERT_EQUAL_UINT8(HIGH, digitalRead(PIN_RELAY_1));
    TEST_ASSERT_EQUAL_UINT8(HIGH, digitalRead(PIN_RELAY_2));
    TEST_ASSERT_EQUAL_UINT8(HIGH, digitalRead(PIN_RELAY_3));
    TEST_ASSERT_EQUAL_UINT8(HIGH, digitalRead(PIN_RELAY_4));

    RecoveryState rs = {};
    TEST_ASSERT_TRUE(storage.loadRecoveryState(rs));
    TEST_ASSERT_TRUE(rs.active);
}

void test_watering_recovery_retry_cap_and_stable_reset(void) {
    DateTime nowDt(2026, 6, 15, 10, 2, 0);
    uint32_t startUnix = nowDt.unixtime() - 120;
    gState.rtc_valid = true;
    gState.mode = AppMode::DESATIVADO;
    gState.now = { 2026, 6, 15, 10, 2, 0, 1, nowDt.unixtime() };

    RecoveryState rs = {};
    rs.version = NVS_VERSION;
    rs.active = true;
    rs.start_unix_time = startUnix;
    rs.queuePos = 0;
    rs.queueLen = 1;
    rs.queue[0] = { 0, 600000UL }; // 10 min
    rs.zone_dur_min[0] = 0;
    rs.trigger = WaterTrigger::MANUAL;
    rs.retry_count = 0;
    storage.saveRecoveryState(rs);

    // 3 consecutive boots suffering periodic brownouts (even >30s into zone run)
    // still increment retry_count 1..3 instead of looping for 2 hours
    for (uint8_t attempt = 1; attempt <= MAX_RECOVERY_RETRIES; attempt++) {
        wateringCtrl = WateringController();
        wateringCtrl.begin();
        TEST_ASSERT_TRUE(wateringCtrl.isActive());
        advanceMockMillis(RELAY_DEADTIME_MS + 10);
        wateringCtrl.update();
        advanceMockMillis(45000UL);
        wateringCtrl.update();
        RecoveryState cur = {};
        TEST_ASSERT_TRUE(storage.loadRecoveryState(cur));
        TEST_ASSERT_EQUAL_UINT8(attempt, cur.retry_count);
    }

    // Next boot after MAX_RECOVERY_RETRIES aborts recovery and clears state
    wateringCtrl = WateringController();
    wateringCtrl.begin();
    TEST_ASSERT_FALSE(wateringCtrl.isActive());
    RecoveryState cleared = {};
    TEST_ASSERT_TRUE(storage.loadRecoveryState(cleared));
    TEST_ASSERT_FALSE(cleared.active);

    // Mid-zone progress checkpoints persist remaining duration & accumulated history,
    // and completing the zone resets retry_count before starting the next zone
    history.clear();
    RecoveryState mrs = {};
    mrs.version = NVS_VERSION;
    mrs.active = true;
    mrs.start_unix_time = startUnix;
    mrs.queuePos = 0;
    mrs.queueLen = 2;
    mrs.queue[0] = { 0, 10UL * 60000UL }; // 10 min
    mrs.queue[1] = { 1, 4UL * 60000UL };  // 4 min
    mrs.trigger = WaterTrigger::MANUAL;
    mrs.retry_count = 1;
    storage.saveRecoveryState(mrs);

    wateringCtrl = WateringController();
    wateringCtrl.begin();
    TEST_ASSERT_TRUE(wateringCtrl.isActive());
    advanceMockMillis(RELAY_DEADTIME_MS + 10);
    wateringCtrl.update();

    // Run 4 minutes (crosses 2 checkpoints of RECOVERY_CHECKPOINT_MIN = 2 min)
    advanceMockMillis(2UL * 60000UL);
    wateringCtrl.update();
    advanceMockMillis(2UL * 60000UL);
    wateringCtrl.update();
    RecoveryState mid = {};
    TEST_ASSERT_TRUE(storage.loadRecoveryState(mid));
    TEST_ASSERT_EQUAL_UINT32(6UL * 60000UL, mid.queue[0].duration_ms);
    TEST_ASSERT_EQUAL_UINT8(4, mid.zone_dur_min[0]);
    TEST_ASSERT_EQUAL_UINT8(2, mid.retry_count);

    // Simulate brownout & reboot: Zone 0 resumes only the remaining 6 min
    wateringCtrl = WateringController();
    wateringCtrl.begin();
    TEST_ASSERT_TRUE(wateringCtrl.isActive());
    TEST_ASSERT_EQUAL_UINT32(6UL * 60000UL, wateringCtrl._zoneDurationMs);
    advanceMockMillis(RELAY_DEADTIME_MS + 10);
    wateringCtrl.update();

    // Finish Zone 0 (6 min) -> advances to Zone 1 and resets retry_count to 0
    advanceMockMillis(6UL * 60000UL);
    wateringCtrl.update();
    TEST_ASSERT_TRUE(wateringCtrl._isWaiting);
    RecoveryState z1 = {};
    TEST_ASSERT_TRUE(storage.loadRecoveryState(z1));
    TEST_ASSERT_EQUAL_UINT8(1, z1.queuePos);
    TEST_ASSERT_EQUAL_UINT8(0, z1.retry_count);
    TEST_ASSERT_EQUAL_UINT8(10, z1.zone_dur_min[0]);

    // Finish Zone 1 (4 min) -> history records cumulative 10 min + 4 min
    advanceMockMillis(ZONE_WAIT_DELAY_MS + 10);
    wateringCtrl.update();
    advanceMockMillis(RELAY_DEADTIME_MS + 10);
    wateringCtrl.update();
    advanceMockMillis(4UL * 60000UL);
    wateringCtrl.update();
    TEST_ASSERT_FALSE(wateringCtrl.isActive());

    HistoryEntry entries[1] = {};
    TEST_ASSERT_EQUAL_UINT8(1, history.readLast(1, entries));
    TEST_ASSERT_EQUAL_UINT8(10, entries[0].zone_dur[0]);
    TEST_ASSERT_EQUAL_UINT8(4, entries[0].zone_dur[1]);
}

void test_watering_invalid_custom_duration_preserves_active_cycle(void) {
    gState.zones[0].enabled = true;
    gState.zones[0].duration_min = 10;
    wateringCtrl.startGeneral(WaterTrigger::MANUAL);
    advanceMockMillis(60);
    wateringCtrl.update();
    TEST_ASSERT_TRUE(wateringCtrl.isActive());
    TEST_ASSERT_EQUAL_UINT8(LOW, digitalRead(PIN_RELAY_1));

    bool sel[NUM_ZONES] = { false, true, false, false };
    wateringCtrl.startCustom(sel, 0);
    wateringCtrl.startCustom(sel, 21);
    wateringCtrl.startCustom(nullptr, 5);

    TEST_ASSERT_TRUE(wateringCtrl.isActive());
    TEST_ASSERT_EQUAL_UINT8(LOW, digitalRead(PIN_RELAY_1));
    TEST_ASSERT_EQUAL_UINT8(HIGH, digitalRead(PIN_RELAY_2));
}

void test_watering_logs_seconds_for_test_and_clean_stop_during_deadtime(void) {
    // 1. 5 s hardware test completion must log seconds, never "(1 min)"
    wateringCtrl.startTest(0);
    advanceMockMillis(RELAY_DEADTIME_MS + 10);
    wateringCtrl.update();
    Serial.reset();

    advanceMockMillis(ZONE_TEST_DURATION_S * 1000UL);
    wateringCtrl.update();
    TEST_ASSERT_FALSE(wateringCtrl.isActive());
#if LOG_LEVEL >= LVL_INFO
    char expectedTestDone[128];
    snprintf(expectedTestDone, sizeof(expectedTestDone),
             TXT_LOG_ZONE_DEACTIVATED_SEC,
             1, gState.zones[0].name, (unsigned long)ZONE_TEST_DURATION_S);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedTestDone) != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("(1 min)") == std::string::npos);
#endif

    // 2. Stopping during initial pre-Zone-1 dead-time must NOT log inter-zone transition
    gState.zones[0].enabled = true;
    gState.zones[0].duration_min = 5;
    gState.zones[1].enabled = true;
    gState.zones[1].duration_min = 5;
    wateringCtrl.startGeneral(WaterTrigger::MANUAL);
    TEST_ASSERT_TRUE(wateringCtrl._isRelayDeadTimeWaiting);
    Serial.reset();

    wateringCtrl.stop();
    TEST_ASSERT_FALSE(wateringCtrl.isActive());
    char unexpectedTrans[128];
    snprintf(unexpectedTrans, sizeof(unexpectedTrans),
             TXT_LOG_STOP_DURING_WAIT, 1, gState.zones[0].name);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(unexpectedTrans) == std::string::npos);
#if LOG_LEVEL >= LVL_INFO
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_WATER_INT) != std::string::npos);
#endif

    // 3. Early stop under 60 s after zone starts must log seconds
    wateringCtrl.startGeneral(WaterTrigger::MANUAL);
    advanceMockMillis(RELAY_DEADTIME_MS + 10);
    wateringCtrl.update();
    advanceMockMillis(12000);
    Serial.reset();

    wateringCtrl.stop();
#if LOG_LEVEL >= LVL_INFO
    char expectedIntSec[128];
    snprintf(expectedIntSec, sizeof(expectedIntSec),
             TXT_LOG_ZONE_DEACTIVATED_INT_SEC,
             1, gState.zones[0].name, 12UL);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedIntSec) != std::string::npos);
#endif

    // 4. Stopping during inter-zone pause must log TXT_LOG_STOP_DURING_WAIT for next zone
    gState.zones[0].duration_min = 1;
    wateringCtrl.startGeneral(WaterTrigger::MANUAL);
    advanceMockMillis(RELAY_DEADTIME_MS + 10);
    wateringCtrl.update();
    advanceMockMillis(60000);
    wateringCtrl.update();
    TEST_ASSERT_TRUE(wateringCtrl._isWaiting);
    Serial.reset();

    wateringCtrl.stop();
#if LOG_LEVEL >= LVL_INFO
    char expectedWaitStop[128];
    snprintf(expectedWaitStop, sizeof(expectedWaitStop),
             TXT_LOG_STOP_DURING_WAIT, 2, gState.zones[1].name);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedWaitStop) != std::string::npos);
#endif
}

void test_watering_oob_deadtime_guard_cycle_done_order_and_arg_warnings(void) {
    // 1. startCustom / startTest invalid arguments emit clear warnings
    bool sel[NUM_ZONES] = { true, false, false, false };
    Serial.reset();
    wateringCtrl.startCustom(sel, 0);
#if LOG_LEVEL >= LVL_WARN
    char expectedInvDur[96];
    snprintf(expectedInvDur, sizeof(expectedInvDur), TXT_LOG_START_INV_DUR, 0U);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedInvDur) != std::string::npos);
#endif

    Serial.reset();
    wateringCtrl.startCustom(nullptr, 5);
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_START_NO_ZONES) != std::string::npos);
#endif

    Serial.reset();
    wateringCtrl.startTest(NUM_ZONES);
#if LOG_LEVEL >= LVL_ERROR
    char expectedOob[96];
    snprintf(expectedOob, sizeof(expectedOob), TXT_LOG_ZONE_OOB,
             (unsigned)NUM_ZONES + 1U, (unsigned)NUM_ZONES);
#endif
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedOob) != std::string::npos);
#endif

    // 2. Corrupted out-of-bounds _zoneIdx during relay dead-time aborts cleanly without bogus duration/wait logs
    gState.zones[0].enabled = true;
    gState.zones[0].duration_min = 2;
    for (int i = 1; i < NUM_ZONES; i++) gState.zones[i].enabled = false;
    wateringCtrl.startGeneral(WaterTrigger::MANUAL);
    TEST_ASSERT_TRUE(wateringCtrl._isRelayDeadTimeWaiting);
    wateringCtrl._queuePos = 1;
    wateringCtrl._zoneIdx = NUM_ZONES;
    advanceMockMillis(RELAY_DEADTIME_MS + 10);
    Serial.reset();

    wateringCtrl.update();
    TEST_ASSERT_FALSE(wateringCtrl.isActive());
#if LOG_LEVEL >= LVL_ERROR
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedOob) != std::string::npos);
#endif
#if LOG_LEVEL >= LVL_INFO
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_WATER_INT) != std::string::npos);
#endif
    auto extractMidPhrase = [](const char* fmt) -> std::string {
        const char* afterParen = strstr(fmt, ") ");
        const char* start = afterParen ? (afterParen + 2) : fmt;
        const char* pct = strchr(start, '%');
        return pct ? std::string(start, (size_t)(pct - start)) : std::string(start);
    };
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(extractMidPhrase(TXT_LOG_STOP_DURING_WAIT)) == std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(extractMidPhrase(TXT_LOG_ZONE_DEACTIVATED_INT)) == std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(extractMidPhrase(TXT_LOG_ZONE_DEACTIVATED_INT_SEC)) == std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(extractMidPhrase(TXT_LOG_ZONE_ACTIVE_MIN)) == std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(extractMidPhrase(TXT_LOG_ZONE_ACTIVE_SEC)) == std::string::npos);

    // 3. Normal cycle completion logs TXT_LOG_CYCLE_DONE before _finishCycle() (Schedule / History logs)
    gState.rtc_valid = true;
    gState.now = { 2026, 6, 15, 8, 0, 0, 1, 1781510400UL };
    gState.mode = AppMode::MEDIO;
    gState.zones[0].enabled = true;
    gState.zones[0].duration_min = 1;
    wateringCtrl.startGeneral(WaterTrigger::AUTO);
    advanceMockMillis(RELAY_DEADTIME_MS + 10);
    wateringCtrl.update();
    Serial.reset();

    advanceMockMillis(60000UL);
    wateringCtrl.update();
    TEST_ASSERT_FALSE(wateringCtrl.isActive());
#if LOG_LEVEL >= LVL_INFO
    size_t posCycleDone = Serial.outputBuffer.find(TXT_LOG_CYCLE_DONE);
    size_t posHist = Serial.outputBuffer.find(TXT_LOG_TAG_HIST);
    TEST_ASSERT_TRUE(posCycleDone != std::string::npos);
    TEST_ASSERT_TRUE(posHist != std::string::npos);
    TEST_ASSERT_LESS_THAN_UINT32((uint32_t)posHist, (uint32_t)posCycleDone);
#endif
}

void test_watering_max_zone_cap_and_periodic_pin_refresh(void) {
    // 1. Periodic idle pin refresh restores corrupted inactive relay GPIO
    wateringCtrl.begin();
    digitalWrite(PIN_RELAY_2, RELAY_ON);
    advanceMockMillis(1000);
    wateringCtrl.update();
    TEST_ASSERT_EQUAL_UINT8(RELAY_OFF, digitalRead(PIN_RELAY_2));

    // 2. Active zone periodic pin refresh restores corrupted active/inactive GPIOs
    gState.rtc_valid = true;
    gState.now = { 2026, 6, 15, 8, 0, 0, 1, 1781510400UL };
    gState.zones[0] = { "Z1", true, 5 };
    for (int i = 1; i < NUM_ZONES; i++) gState.zones[i].enabled = false;
    wateringCtrl.startGeneral(WaterTrigger::MANUAL);
    advanceMockMillis(RELAY_DEADTIME_MS + 10);
    wateringCtrl.update();
    TEST_ASSERT_EQUAL_UINT8(RELAY_ON, digitalRead(PIN_RELAY_1));

    digitalWrite(PIN_RELAY_1, RELAY_OFF);
    digitalWrite(PIN_RELAY_3, RELAY_ON);
    advanceMockMillis(1000);
    wateringCtrl.update();
    TEST_ASSERT_EQUAL_UINT8(RELAY_ON, digitalRead(PIN_RELAY_1));
    TEST_ASSERT_EQUAL_UINT8(RELAY_OFF, digitalRead(PIN_RELAY_3));

    // 3. Corrupted _zoneDurationMs (> 20 min) is forcefully capped at 20 min
    wateringCtrl._zoneDurationMs = 30UL * 60000UL;
    Serial.reset();
    advanceMockMillis(20UL * 60000UL);
    wateringCtrl.update();
    TEST_ASSERT_FALSE(wateringCtrl.isActive());
    TEST_ASSERT_EQUAL_UINT8(RELAY_OFF, digitalRead(PIN_RELAY_1));
#if LOG_LEVEL >= LVL_ERROR
    char expectedCap[96];
    snprintf(expectedCap, sizeof(expectedCap), TXT_LOG_ZONE_MAX_CAP, 1U, 1200UL);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedCap) != std::string::npos);
#endif
    HistoryEntry latest[1] = {};
    TEST_ASSERT_EQUAL_UINT8(1, history.readLast(1, latest));
    TEST_ASSERT_EQUAL_UINT8(20, latest[0].zone_dur[0]);

    // 4. Corrupted _zoneDurationMs during TEST run is capped at ZONE_TEST_DURATION_S
    wateringCtrl.startTest(0);
    advanceMockMillis(RELAY_DEADTIME_MS + 10);
    wateringCtrl.update();
    wateringCtrl._zoneDurationMs = 60000UL;
    advanceMockMillis((uint32_t)ZONE_TEST_DURATION_S * 1000UL);
    wateringCtrl.update();
    TEST_ASSERT_FALSE(wateringCtrl.isActive());
    TEST_ASSERT_EQUAL_UINT8(RELAY_OFF, digitalRead(PIN_RELAY_1));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_watering_general_builds_queue_and_skips_disabled);
    RUN_TEST(test_watering_custom_selection);
    RUN_TEST(test_watering_dead_time_and_relay_activation);
    RUN_TEST(test_watering_emergency_stop);
    RUN_TEST(test_watering_test_run_does_not_log_to_history);
    RUN_TEST(test_watering_blackout_recovery_resumes_interrupted_cycle);
    RUN_TEST(test_watering_multi_zone_sequence_and_history_logging);
    RUN_TEST(test_watering_emergency_stop_partial_duration_recorded);
    RUN_TEST(test_watering_empty_selection_does_not_activate);
    RUN_TEST(test_watering_recovery_rejects_corrupted_and_suspended_states);
    RUN_TEST(test_watering_inter_zone_wait_updates_upcoming_zone_index);
    RUN_TEST(test_watering_deenergize_relays_preserves_recovery_state);
    RUN_TEST(test_watering_recovery_retry_cap_and_stable_reset);
    RUN_TEST(test_watering_invalid_custom_duration_preserves_active_cycle);
    RUN_TEST(test_watering_logs_seconds_for_test_and_clean_stop_during_deadtime);
    RUN_TEST(test_watering_oob_deadtime_guard_cycle_done_order_and_arg_warnings);
    RUN_TEST(test_watering_max_zone_cap_and_periodic_pin_refresh);
    return UNITY_END();
}
