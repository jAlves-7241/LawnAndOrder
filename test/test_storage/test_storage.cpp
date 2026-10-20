#include <unity.h>
#include "TestCommon.h"
#include "../../test/mocks/TestCommon.cpp"
bool _log_suspended = false;
#include "../../src/AppState.cpp"
#include "../../src/Storage.cpp"

void setUp(void) {
    resetAllTestMocks();
    storage.begin();
}

void tearDown(void) {}

void test_storage_export_hex_length_and_format(void) {
    char hex[128] = {};
    storage.exportConfigHex(hex, sizeof(hex));

    // Must be exactly sizeof(AppConfigBlob) * 2 characters
    size_t expectedLen = sizeof(AppConfigBlob) * 2;
    TEST_ASSERT_EQUAL_UINT32(expectedLen, strlen(hex));

    // Verify all characters are valid uppercase hexadecimal
    for (size_t i = 0; i < expectedLen; i++) {
        char c = hex[i];
        bool isHex = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
        TEST_ASSERT_TRUE(isHex);
    }
}

void test_storage_export_and_import_roundtrip(void) {
    // 1. Set specific configuration in gState
    gState.mode = AppMode::INTENSO;
    gState.zones[0].enabled = true;
    gState.zones[0].duration_min = 12;
    gState.zones[1].enabled = false;
    gState.zones[1].duration_min = 0;
    gState.zones[2].enabled = true;
    gState.zones[2].duration_min = 18;
    gState.zones[3].enabled = true;
    gState.zones[3].duration_min = 5;
    gState.backlight_timeout_ms = 60000;
    gState.auto_dst = true;
    gState.setup_done = true;
    gState.suspended_until = 1770000000UL;

    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.interval_days = 3;
    cs.slot_count = 2;
    cs.slots[0].hour = 6;
    cs.slots[0].minute = 15;
    cs.slots[1].hour = 20;
    cs.slots[1].minute = 45;

    // 2. Export to hex
    char hex[128] = {};
    storage.exportConfigHex(hex, sizeof(hex));

    // 3. Reset gState to default values
    initAppState();
    TEST_ASSERT_EQUAL(AppMode::DESATIVADO, gState.mode);
    TEST_ASSERT_FALSE(gState.setup_done);

    // 4. Import hex and verify restored state
    bool ok = storage.importConfigHex(hex);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(AppMode::INTENSO, gState.mode);
    TEST_ASSERT_TRUE(gState.setup_done);
    TEST_ASSERT_TRUE(gState.auto_dst);
    TEST_ASSERT_EQUAL_UINT32(60000, gState.backlight_timeout_ms);
    TEST_ASSERT_EQUAL_UINT32(1770000000UL, gState.suspended_until);
    TEST_ASSERT_TRUE(gState.suspended);

    TEST_ASSERT_TRUE(gState.zones[0].enabled);
    TEST_ASSERT_EQUAL_UINT8(12, gState.zones[0].duration_min);
    TEST_ASSERT_FALSE(gState.zones[1].enabled);
    TEST_ASSERT_EQUAL_UINT8(0, gState.zones[1].duration_min);
    TEST_ASSERT_TRUE(gState.zones[2].enabled);
    TEST_ASSERT_EQUAL_UINT8(18, gState.zones[2].duration_min);

    // Verify custom schedule restored
    const ModeSchedule& restoredCs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    TEST_ASSERT_EQUAL_UINT8(3, restoredCs.interval_days);
    TEST_ASSERT_EQUAL_UINT8(2, restoredCs.slot_count);
    TEST_ASSERT_EQUAL_UINT8(6, restoredCs.slots[0].hour);
    TEST_ASSERT_EQUAL_UINT8(15, restoredCs.slots[0].minute);
    TEST_ASSERT_EQUAL_UINT8(20, restoredCs.slots[1].hour);
    TEST_ASSERT_EQUAL_UINT8(45, restoredCs.slots[1].minute);
}

void test_storage_import_rejects_invalid_inputs(void) {
    // 1. Null pointer
    TEST_ASSERT_FALSE(storage.importConfigHex(nullptr));

    // 2. Wrong length
    TEST_ASSERT_FALSE(storage.importConfigHex("0102030405"));

    // 3. Non-hex characters
    std::string invalidHex(sizeof(AppConfigBlob) * 2, 'G');
    TEST_ASSERT_FALSE(storage.importConfigHex(invalidHex.c_str()));

    // 4. Invalid mode byte (> _COUNT)
    char hex[128] = {};
    storage.exportConfigHex(hex, sizeof(hex));
    // Blob byte 1 is mode (chars 2 and 3 in hex string)
    hex[2] = 'F';
    hex[3] = 'F'; // Mode 255
    TEST_ASSERT_FALSE(storage.importConfigHex(hex));
}

void test_storage_recovery_state_roundtrip(void) {
    RecoveryState rs = {};
    rs.version = NVS_VERSION;
    rs.active = true;
    rs.start_unix_time = 1768000000UL;
    rs.queuePos = 1;
    rs.queueLen = 3;
    rs.queue[0] = { 0, 600000 };
    rs.queue[1] = { 2, 900000 };
    rs.queue[2] = { 3, 300000 };
    rs.zone_dur_min[0] = 10;
    rs.zone_dur_min[2] = 15;
    rs.zone_dur_min[3] = 5;
    rs.trigger = WaterTrigger::AUTO;

    storage.saveRecoveryState(rs);

    RecoveryState loaded = {};
    bool ok = storage.loadRecoveryState(loaded);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(loaded.active);
    TEST_ASSERT_EQUAL_UINT32(1768000000UL, loaded.start_unix_time);
    TEST_ASSERT_EQUAL_UINT8(1, loaded.queuePos);
    TEST_ASSERT_EQUAL_UINT8(3, loaded.queueLen);
    TEST_ASSERT_EQUAL_UINT8(2, loaded.queue[1].zone_idx);
    TEST_ASSERT_EQUAL_UINT32(900000, loaded.queue[1].duration_ms);
    TEST_ASSERT_EQUAL(WaterTrigger::AUTO, loaded.trigger);
}

void test_storage_save_and_load_persistence(void) {
    gState.mode = AppMode::FRACO;
    gState.zones[0].duration_min = 14;
    gState.setup_done = true;

    bool saved = storage.save();
    TEST_ASSERT_TRUE(saved);

    // Wipe RAM state
    initAppState();
    TEST_ASSERT_EQUAL(AppMode::DESATIVADO, gState.mode);

    // Load from mock NVS
    bool loaded = storage.load();
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_EQUAL(AppMode::FRACO, gState.mode);
    TEST_ASSERT_EQUAL_UINT8(14, gState.zones[0].duration_min);
    TEST_ASSERT_TRUE(gState.setup_done);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_storage_export_hex_length_and_format);
    RUN_TEST(test_storage_export_and_import_roundtrip);
    RUN_TEST(test_storage_import_rejects_invalid_inputs);
    RUN_TEST(test_storage_recovery_state_roundtrip);
    RUN_TEST(test_storage_save_and_load_persistence);
    return UNITY_END();
}
