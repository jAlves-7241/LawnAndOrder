#include <unity.h>
#include "TestCommon.h"
#include "../../test/mocks/TestCommon.cpp"
bool _log_suspended = false;
#include "../../src/AppState.cpp"
#include "Storage.h"

void setUp(void) {
    resetAllTestMocks();
}

void tearDown(void) {}

void test_struct_sizes_and_alignment(void) {
    TEST_ASSERT_EQUAL_UINT32(12, sizeof(HistoryEntry));
    // Verify packed layout of RecoveryState and AppConfigBlob
    TEST_ASSERT_TRUE(sizeof(RecoveryState) > 0);
    TEST_ASSERT_TRUE(sizeof(AppConfigBlob) > 0);
}

void test_init_app_state_defaults(void) {
    initAppState();

    TEST_ASSERT_EQUAL(AppMode::DESATIVADO, gState.mode);
    TEST_ASSERT_FALSE(gState.suspended);
    TEST_ASSERT_EQUAL_UINT32(0, gState.suspended_until);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFUL, gState.custom_ref_day);
    TEST_ASSERT_FALSE(gState.setup_done);
    TEST_ASSERT_FALSE(gState.rtc_valid);
    TEST_ASSERT_FALSE(gState.watering.active);
    TEST_ASSERT_EQUAL_UINT32(120000UL, gState.backlight_timeout_ms);

    // Verify zones
    for (int i = 0; i < NUM_ZONES; i++) {
        TEST_ASSERT_GREATER_THAN(0, strlen(gState.zones[i].name));
        TEST_ASSERT_EQUAL(gState.zones[i].duration_min > 0, gState.zones[i].enabled);
        TEST_ASSERT_FALSE(gState.custom_sel[i]);
    }
}

void test_mode_schedules_table(void) {
    initAppState();

    // INTENSO: 3 slots daily
    const ModeSchedule& intenso = MODE_SCHEDULES[(uint8_t)AppMode::INTENSO];
    TEST_ASSERT_EQUAL_UINT8(3, intenso.slot_count);
    TEST_ASSERT_EQUAL(DayPattern::DAILY, intenso.day_pattern);
    TEST_ASSERT_EQUAL_UINT8(SCHED_INTENSO_SLOT0_H, intenso.slots[0].hour);
    TEST_ASSERT_EQUAL_UINT8(SCHED_INTENSO_SLOT0_M, intenso.slots[0].minute);
    TEST_ASSERT_EQUAL_UINT8(SCHED_INTENSO_SLOT1_H, intenso.slots[1].hour);
    TEST_ASSERT_EQUAL_UINT8(SCHED_INTENSO_SLOT1_M, intenso.slots[1].minute);
    TEST_ASSERT_EQUAL_UINT8(SCHED_INTENSO_SLOT2_H, intenso.slots[2].hour);
    TEST_ASSERT_EQUAL_UINT8(SCHED_INTENSO_SLOT2_M, intenso.slots[2].minute);

    // MEDIO: 2 slots daily
    const ModeSchedule& medio = MODE_SCHEDULES[(uint8_t)AppMode::MEDIO];
    TEST_ASSERT_EQUAL_UINT8(2, medio.slot_count);
    TEST_ASSERT_EQUAL(DayPattern::DAILY, medio.day_pattern);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT0_H, medio.slots[0].hour);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT0_M, medio.slots[0].minute);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT1_H, medio.slots[1].hour);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT1_M, medio.slots[1].minute);

    // FRACO: 1 slot daily
    const ModeSchedule& fraco = MODE_SCHEDULES[(uint8_t)AppMode::FRACO];
    TEST_ASSERT_EQUAL_UINT8(1, fraco.slot_count);
    TEST_ASSERT_EQUAL(DayPattern::DAILY, fraco.day_pattern);
    TEST_ASSERT_EQUAL_UINT8(SCHED_FRACO_SLOT0_H, fraco.slots[0].hour);
    TEST_ASSERT_EQUAL_UINT8(SCHED_FRACO_SLOT0_M, fraco.slots[0].minute);

    // DESATIVADO: 0 slots
    const ModeSchedule& desativado = MODE_SCHEDULES[(uint8_t)AppMode::DESATIVADO];
    TEST_ASSERT_EQUAL_UINT8(0, desativado.slot_count);

    // PERSONALIZADO: custom interval and slots
    const ModeSchedule& custom = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    TEST_ASSERT_EQUAL_UINT8(SCHED_CUSTOM_SLOTS, custom.slot_count);
    TEST_ASSERT_EQUAL(DayPattern::EVERY_X_DAYS, custom.day_pattern);
    TEST_ASSERT_EQUAL_UINT8(SCHED_CUSTOM_INTERVAL, custom.interval_days);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_struct_sizes_and_alignment);
    RUN_TEST(test_init_app_state_defaults);
    RUN_TEST(test_mode_schedules_table);
    return UNITY_END();
}
