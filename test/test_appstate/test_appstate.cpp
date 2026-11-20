#include <unity.h>
#include <stddef.h>
#include "TestCommon.h"
#include "../../test/mocks/TestCommon.cpp"
#include "../../src/AppState.cpp"
#include "Storage.h"

void setUp(void) {
    resetAllTestMocks();
}

void tearDown(void) {}

void test_struct_sizes_and_alignment(void) {
    TEST_ASSERT_EQUAL_UINT32(12, sizeof(HistoryEntry));
    // Verify packed layout of RecoveryState and AppConfigBlob
    TEST_ASSERT_EQUAL_UINT32(36, sizeof(RecoveryState));
    TEST_ASSERT_EQUAL_UINT32(34, offsetof(RecoveryState, crc16));
    TEST_ASSERT_EQUAL_UINT32(36, sizeof(AppConfigBlob));
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

    // Human-readable serial name helpers (i2cDeviceName, serialTriggerName, serialModeName)
    TEST_ASSERT_EQUAL_STRING(TXT_I2C_DEV_LCD, i2cDeviceName(LCD_ADDR));
    TEST_ASSERT_EQUAL_STRING(TXT_I2C_DEV_LCD, i2cDeviceName(0x3F));
    TEST_ASSERT_EQUAL_STRING(TXT_I2C_DEV_RTC, i2cDeviceName(0x68));
    TEST_ASSERT_EQUAL_STRING(TXT_I2C_DEV_EEPROM, i2cDeviceName(0x57));
    TEST_ASSERT_EQUAL_STRING(TXT_I2C_DEV_UNKNOWN, i2cDeviceName(0x12));
    TEST_ASSERT_EQUAL_STRING(TXT_SER_TRIG_AUTO, serialTriggerName(WaterTrigger::AUTO));
    TEST_ASSERT_EQUAL_STRING(TXT_SER_TRIG_MANUAL, serialTriggerName(WaterTrigger::MANUAL));
    TEST_ASSERT_EQUAL_STRING(TXT_SER_TRIG_CUSTOM, serialTriggerName(WaterTrigger::CUSTOM));
    TEST_ASSERT_EQUAL_STRING(TXT_SER_TRIG_TEST, serialTriggerName(WaterTrigger::TEST));
}

void test_calendar_and_custom_schedule_helpers(void) {
    initAppState();

    // daysInMonth: normal, leap, century non-leap, 400-yr leap, out-of-range
    TEST_ASSERT_EQUAL_UINT8(0, daysInMonth(2026, 0));
    TEST_ASSERT_EQUAL_UINT8(0, daysInMonth(2026, 13));
    TEST_ASSERT_EQUAL_UINT8(28, daysInMonth(2026, 2));
    TEST_ASSERT_EQUAL_UINT8(29, daysInMonth(2024, 2));
    TEST_ASSERT_EQUAL_UINT8(28, daysInMonth(2100, 2));
    TEST_ASSERT_EQUAL_UINT8(29, daysInMonth(2000, 2));
    TEST_ASSERT_EQUAL_UINT8(30, daysInMonth(2026, 4));
    TEST_ASSERT_EQUAL_UINT8(31, daysInMonth(2026, 12));

    // computeMinCustomSpacing: enabled durations + inter-zone pauses, min 1 when all off
    for (int i = 0; i < NUM_ZONES; i++) { gState.zones[i].enabled = false; gState.zones[i].duration_min = 0; }
    TEST_ASSERT_EQUAL_UINT16(1, computeMinCustomSpacing());
    gState.zones[0] = { "Z1", true, 5 };
    TEST_ASSERT_EQUAL_UINT16(5, computeMinCustomSpacing());
    gState.zones[2] = { "Z3", true, 10 };
    TEST_ASSERT_EQUAL_UINT16(16, computeMinCustomSpacing()); // 5 + 10 + 1 transition

    // sortCustomSlots, hasCustomSlotOverlap (including midnight wrap), and respaceCustomSlots
    ModeSchedule sched = {};
    sched.slot_count = 3;
    sched.slots[0] = { 23, 55 };
    sched.slots[1] = { 0, 5 };
    sched.slots[2] = { 12, 0 };
    sortCustomSlots(sched);
    TEST_ASSERT_EQUAL_UINT8(0, sched.slots[0].hour);
    TEST_ASSERT_EQUAL_UINT8(12, sched.slots[1].hour);
    TEST_ASSERT_EQUAL_UINT8(23, sched.slots[2].hour);
    TEST_ASSERT_TRUE(hasCustomSlotOverlap(sched, 15));  // 23:55 -> 00:05 is 10 min < 15
    TEST_ASSERT_FALSE(hasCustomSlotOverlap(sched, 10)); // exactly 10 min >= 10

    sched.slots[3] = { 22, 22 };
    respaceCustomSlots(sched);
    TEST_ASSERT_FALSE(hasCustomSlotOverlap(sched, 16));
    TEST_ASSERT_EQUAL_UINT8(0, sched.slots[3].hour);
    TEST_ASSERT_EQUAL_UINT8(0, sched.slots[3].minute);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_struct_sizes_and_alignment);
    RUN_TEST(test_init_app_state_defaults);
    RUN_TEST(test_mode_schedules_table);
    RUN_TEST(test_calendar_and_custom_schedule_helpers);
    return UNITY_END();
}
