#include <unity.h>
#include "TestCommon.h"
#include "../../test/mocks/TestCommon.cpp"
#include "../../src/AppState.cpp"
#define private public
#include "Storage.h"
#undef private
#include "../../src/Storage.cpp"

static void recomputeHexCrc(char* hex) {
    size_t nBytes = sizeof(AppConfigBlob);
    uint8_t raw[sizeof(AppConfigBlob)] = {};
    for (size_t i = 0; i < nBytes; i++) {
        char tmp[3] = { hex[i * 2], hex[i * 2 + 1], '\0' };
        raw[i] = (uint8_t)strtoul(tmp, nullptr, 16);
    }
    uint16_t crc = Storage::_crc16(raw, offsetof(AppConfigBlob, crc16));
    raw[offsetof(AppConfigBlob, crc16)]     = (uint8_t)(crc & 0xFF);
    raw[offsetof(AppConfigBlob, crc16) + 1] = (uint8_t)((crc >> 8) & 0xFF);
    for (size_t i = 0; i < nBytes; i++) {
        snprintf(&hex[i * 2], 3, "%02X", raw[i]);
    }
}

void setUp(void) {
    resetAllTestMocks();
    storage.begin();
}

void tearDown(void) {}

void test_storage_export_hex_length_and_format(void) {
    char hex[128] = {};
    storage.exportConfigHex(hex, sizeof(hex));

    // Must be exactly sizeof(AppConfigBlob) * 2 characters (72 for v5)
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
    rs.retry_count = 2;

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
    TEST_ASSERT_EQUAL_UINT8(2, loaded.retry_count);

    // Corrupting a byte in NVS KEY_RECV without recomputing CRC must be rejected
    RecoveryState rawRs = {};
    TEST_ASSERT_EQUAL_UINT32(sizeof(rawRs), prefs.getBytes(KEY_RECV, &rawRs, sizeof(rawRs)));
    rawRs.queuePos = 0;
    prefs.putBytes(KEY_RECV, &rawRs, sizeof(rawRs));
    TEST_ASSERT_FALSE(storage.loadRecoveryState(loaded));
}

void test_storage_recovery_state_rejects_legacy_size_ver_bool_and_crc_bitflip(void) {
    RecoveryState rs = {};
    rs.version = NVS_VERSION;
    rs.active = true;
    rs.start_unix_time = 1768000000UL;
    rs.queuePos = 0;
    rs.queueLen = 1;
    rs.queue[0] = { 0, 600000UL };
    rs.trigger = WaterTrigger::AUTO;
    rs.retry_count = 1;
    storage.saveRecoveryState(rs);

    RecoveryState sentinel = {};
    sentinel.queuePos = 3;
    sentinel.retry_count = 7;
    sentinel.start_unix_time = 999UL;

    // 1. Legacy pre-CRC 34-byte blob must be rejected without mutating caller's struct
    uint8_t legacy[34] = {};
    legacy[0] = NVS_VERSION;
    legacy[1] = 1;
    prefs.putBytes(KEY_RECV, legacy, sizeof(legacy));
    RecoveryState loaded = sentinel;
    Serial.reset();
    TEST_ASSERT_FALSE(storage.loadRecoveryState(loaded));
    TEST_ASSERT_EQUAL_UINT8(3, loaded.queuePos);
    TEST_ASSERT_EQUAL_UINT8(7, loaded.retry_count);
    TEST_ASSERT_EQUAL_UINT32(999UL, loaded.start_unix_time);
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_NVS_RSN_SIZE) != std::string::npos);
#endif

    // 2. Wrong NVS_VERSION rejected
    storage.saveRecoveryState(rs);
    RecoveryState rawRs = {};
    TEST_ASSERT_EQUAL_UINT32(sizeof(rawRs), prefs.getBytes(KEY_RECV, &rawRs, sizeof(rawRs)));
    rawRs.version = NVS_VERSION - 1;
    prefs.putBytes(KEY_RECV, &rawRs, sizeof(rawRs));
    Serial.reset();
    TEST_ASSERT_FALSE(storage.loadRecoveryState(loaded));
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_NVS_RSN_VER) != std::string::npos);
#endif

    // 3. Invalid active bool byte (> 1) rejected
    storage.saveRecoveryState(rs);
    uint8_t rawBytes[sizeof(RecoveryState)] = {};
    TEST_ASSERT_EQUAL_UINT32(sizeof(rawBytes), prefs.getBytes(KEY_RECV, rawBytes, sizeof(rawBytes)));
    rawBytes[offsetof(RecoveryState, active)] = 2;
    prefs.putBytes(KEY_RECV, rawBytes, sizeof(rawBytes));
    Serial.reset();
    TEST_ASSERT_FALSE(storage.loadRecoveryState(loaded));
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_NVS_RSN_BOOL) != std::string::npos);
#endif

    // 4. Single-bit flip in crc16 itself rejected
    storage.saveRecoveryState(rs);
    TEST_ASSERT_EQUAL_UINT32(sizeof(rawRs), prefs.getBytes(KEY_RECV, &rawRs, sizeof(rawRs)));
    rawRs.crc16 ^= 0x0001U;
    prefs.putBytes(KEY_RECV, &rawRs, sizeof(rawRs));
    Serial.reset();
    TEST_ASSERT_FALSE(storage.loadRecoveryState(loaded));
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_NVS_PART_RECV) != std::string::npos);
#endif
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

void test_storage_rejects_corrupted_boolean_bytes(void) {
    char hex[128] = {};
    storage.exportConfigHex(hex, sizeof(hex));

    // Find offset of auto_dst in hex string (offsetof * 2)
    size_t autoDstHexOffset = offsetof(AppConfigBlob, auto_dst) * 2;
    hex[autoDstHexOffset] = '0';
    hex[autoDstHexOffset + 1] = '2'; // Invalid bool byte (value 2)
    recomputeHexCrc(hex);

    bool ok = storage.importConfigHex(hex);
    TEST_ASSERT_FALSE(ok);
}

void test_storage_duration_clamping_on_import(void) {
    char hex[128] = {};
    storage.exportConfigHex(hex, sizeof(hex));

    // Zone 0 duration is at offsetof(AppConfigBlob, zones[0].duration_min)
    size_t durOffset = (offsetof(AppConfigBlob, zones) + 1) * 2;
    hex[durOffset] = '1';
    hex[durOffset + 1] = 'E'; // 30 minutes in hex (0x1E > 20)

    uint8_t origDur = gState.zones[0].duration_min;
    TEST_ASSERT_FALSE(storage.importConfigHex(hex)); // Bad CRC rejected
    recomputeHexCrc(hex);
    TEST_ASSERT_FALSE(storage.importConfigHex(hex)); // Valid CRC but >20m duration strictly rejected
    TEST_ASSERT_EQUAL_UINT8(origDur, gState.zones[0].duration_min);

    // Valid export must also be rejected while watering is active
    storage.exportConfigHex(hex, sizeof(hex));
    gState.watering.active = true;
    TEST_ASSERT_FALSE(storage.importConfigHex(hex));
    gState.watering.active = false;
    TEST_ASSERT_TRUE(storage.importConfigHex(hex));
}

void test_storage_import_rejects_version_mismatch(void) {
    char hex[128] = {};
    storage.exportConfigHex(hex, sizeof(hex));

    // Change version (first byte / 2 hex chars) to 0xFE
    hex[0] = 'F';
    hex[1] = 'E';
    TEST_ASSERT_FALSE(storage.importConfigHex(hex));
    recomputeHexCrc(hex);
    TEST_ASSERT_FALSE(storage.importConfigHex(hex));
}

void test_storage_clear_wipes_all(void) {
    gState.mode = AppMode::INTENSO;
    storage.save();

    // Verify it was saved
    initAppState();
    TEST_ASSERT_TRUE(storage.load());
    TEST_ASSERT_EQUAL(AppMode::INTENSO, gState.mode);

    // Clear storage
    storage.clear();

    // After clear, load should fail (clean state)
    initAppState();
    TEST_ASSERT_FALSE(storage.load());
    TEST_ASSERT_EQUAL(AppMode::DESATIVADO, gState.mode);
}

void test_storage_history_cache_persistence(void) {
    HistoryEntry sample[3] = {};
    sample[0] = { 2026, 6, 1, 8, 0, { 10, 0, 0, 0 }, WaterTrigger::AUTO, 0 };
    sample[1] = { 2026, 6, 2, 8, 0, { 12, 0, 0, 0 }, WaterTrigger::MANUAL, 0 };
    sample[2] = { 2026, 6, 3, 8, 0, { 15, 0, 0, 0 }, WaterTrigger::CUSTOM, 0 };

    storage.saveHistoryCache(sample, sizeof(sample), 99);

    HistoryEntry loaded[3] = {};
    uint16_t loadedCount = 0;
    bool ok = storage.loadHistoryCache(loaded, sizeof(loaded), loadedCount);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT16(99, loadedCount);
    TEST_ASSERT_EQUAL_UINT8(1, loaded[0].day);
    TEST_ASSERT_EQUAL_UINT8(2, loaded[1].day);
    TEST_ASSERT_EQUAL_UINT8(3, loaded[2].day);
    TEST_ASSERT_EQUAL(WaterTrigger::CUSTOM, loaded[2].trigger);
}

void test_storage_dormant_custom_overlap_auto_respaced_vs_active_rejected(void) {
    // 1. Fixed mode (MEDIO) with dormant overlapping PERSONALIZADO slots + edited 20m zones
    gState.mode = AppMode::MEDIO;
    for (int i = 0; i < NUM_ZONES; i++) {
        gState.zones[i].enabled = true;
        gState.zones[i].duration_min = 20; // Total cycle ~81 min
    }
    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.slot_count = 2;
    cs.interval_days = 2;
    cs.slots[0] = { 8, 0 };
    cs.slots[1] = { 8, 30 }; // Overlaps 81-min cycle!

    TEST_ASSERT_TRUE(storage.save());
    initAppState();

    // Must NOT wipe user config on load when PERSONALIZADO is dormant; must auto-respace
    TEST_ASSERT_TRUE(storage.load());
    TEST_ASSERT_EQUAL(AppMode::MEDIO, gState.mode);
    const ModeSchedule& loadedCs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    uint16_t m0 = loadedCs.slots[0].hour * 60 + loadedCs.slots[0].minute;
    uint16_t m1 = loadedCs.slots[1].hour * 60 + loadedCs.slots[1].minute;
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(81, m1 - m0);

    // 2. When PERSONALIZADO IS the active mode, overlapping slots in imported hex MUST be rejected
    gState.mode = AppMode::PERSONALIZADO;
    MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO].slot_count = 2;
    MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO].slots[0] = { 8, 0 };
    MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO].slots[1] = { 8, 10 };
    char badHex[128] = {};
    storage.exportConfigHex(badHex, sizeof(badHex));
    TEST_ASSERT_FALSE(storage.importConfigHex(badHex));
}

void test_storage_zero_duration_normalizes_enabled_false(void) {
    gState.zones[0].enabled = true;
    gState.zones[0].duration_min = 0;
    TEST_ASSERT_TRUE(storage.save());
    // Persistence must not mutate caller RAM state; normalization occurs in _stateToBlob() / _blobToState()
    TEST_ASSERT_TRUE(gState.zones[0].enabled);

    initAppState();
    TEST_ASSERT_TRUE(storage.load());
    TEST_ASSERT_FALSE(gState.zones[0].enabled);
    TEST_ASSERT_EQUAL_UINT8(0, gState.zones[0].duration_min);
}

void test_storage_nvs_load_crc_rejection_and_clamping(void) {
    gState.mode = AppMode::FRACO;
    gState.zones[0].enabled = true;
    gState.zones[0].duration_min = 12;
    TEST_ASSERT_TRUE(storage.save());

    AppConfigBlob blob = {};
    TEST_ASSERT_EQUAL_UINT32(sizeof(blob), prefs.getBytes(KEY_CFG, &blob, sizeof(blob)));

    // 1. Corrupt byte in NVS without updating CRC -> load() rejects
    blob.zones[0].duration_min = 9;
    prefs.putBytes(KEY_CFG, &blob, sizeof(blob));
    initAppState();
    TEST_ASSERT_FALSE(storage.load());
    TEST_ASSERT_EQUAL(AppMode::DESATIVADO, gState.mode);

    // 2. Corrupted duration > 20 in NVS WITH valid CRC -> load() defensively clamps to 8
    blob.zones[0].duration_min = 30;
    blob.crc16 = Storage::_crc16((const uint8_t*)&blob, offsetof(AppConfigBlob, crc16));
    prefs.putBytes(KEY_CFG, &blob, sizeof(blob));
    initAppState();
    TEST_ASSERT_TRUE(storage.load());
    TEST_ASSERT_EQUAL_UINT8(8, gState.zones[0].duration_min);
}

void test_storage_history_blob_atomic_and_filesize(void) {
    TEST_ASSERT_FALSE(storage.hasHistoryData());

    HistoryEntry sample[2] = {};
    sample[0] = { 2026, 6, 1, 8, 0, { 10, 0, 0, 0 }, WaterTrigger::AUTO, 0 };
    sample[1] = { 2026, 6, 2, 9, 15, { 5, 5, 0, 0 }, WaterTrigger::MANUAL, 0 };
    storage.saveHistoryCache(sample, sizeof(sample), 42, 1234UL);
    TEST_ASSERT_TRUE(storage.hasHistoryData());

    HistoryEntry loaded[2] = {};
    uint16_t lineCount = 0;
    uint32_t fileSize = 0;
    TEST_ASSERT_TRUE(storage.loadHistoryCache(loaded, sizeof(loaded), lineCount, &fileSize));
    TEST_ASSERT_EQUAL_UINT16(42, lineCount);
    TEST_ASSERT_EQUAL_UINT32(1234UL, fileSize);
    TEST_ASSERT_EQUAL_UINT8(2, loaded[1].day);

    // Flip last CRC byte of KEY_HBLOB in NVS -> loadHistoryCache must reject cleanly
    uint8_t raw[128] = {};
    size_t sz = prefs.getBytes(KEY_HBLOB, raw, sizeof(raw));
    TEST_ASSERT_GREATER_THAN_UINT32(sizeof(HistoryNvsHeader) + 2, sz);
    raw[sz - 1] ^= 0xFF;
    prefs.putBytes(KEY_HBLOB, raw, sz);
    TEST_ASSERT_FALSE(storage.loadHistoryCache(loaded, sizeof(loaded), lineCount, &fileSize));
}

void test_storage_last_triggers_roundtrip(void) {
    uint32_t ts[MAX_SLOTS_PER_MODE] = { 1781500000UL, 1781510000UL, 0UL, 1781520000UL };
    storage.saveLastTriggers(ts, MAX_SLOTS_PER_MODE);

    uint32_t loaded[MAX_SLOTS_PER_MODE] = {};
    TEST_ASSERT_TRUE(storage.loadLastTriggers(loaded, MAX_SLOTS_PER_MODE));
    TEST_ASSERT_EQUAL_UINT32(1781500000UL, loaded[0]);
    TEST_ASSERT_EQUAL_UINT32(1781510000UL, loaded[1]);
    TEST_ASSERT_EQUAL_UINT32(0UL, loaded[2]);
    TEST_ASSERT_EQUAL_UINT32(1781520000UL, loaded[3]);

    storage.clear();
    TEST_ASSERT_FALSE(storage.loadLastTriggers(loaded, MAX_SLOTS_PER_MODE));
}

void test_storage_clear_guard_invalid_hist_cache_and_respace_logs(void) {
    // 1. storage.clear() when !_ready returns false and logs NS_FAIL without MEM_CLEARED
    storage._ready = false;
    Serial.reset();
    TEST_ASSERT_FALSE(storage.clear());
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_MEM_CLEARED) == std::string::npos);
#if LOG_LEVEL >= LVL_ERROR
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_NVS_NS_FAIL) != std::string::npos);
#endif
    storage._ready = true;

    // 2. loadHistoryCache() with truncated/invalid header logs TXT_LOG_NVS_HIST_INV
    uint8_t badByte = 0xAA;
    prefs.putBytes(KEY_HBLOB, &badByte, 1);
    HistoryEntry loaded[2] = {};
    uint16_t lineCount = 0;
    uint32_t fileSize = 0;
    Serial.reset();
    TEST_ASSERT_FALSE(storage.loadHistoryCache(loaded, sizeof(loaded), lineCount, &fileSize));
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_NVS_HIST_INV) != std::string::npos);
#endif

    // 3. Dormant custom slots overlapping on load() auto-respace and log TXT_LOG_NVS_CUSTOM_RESPACED
    gState.mode = AppMode::MEDIO;
    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.slot_count = 2;
    cs.slots[0] = { 8, 0 };
    cs.slots[1] = { 8, 10 };
    TEST_ASSERT_TRUE(storage.save());

    Serial.reset();
    TEST_ASSERT_TRUE(storage.load());
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_NVS_CUSTOM_RESPACED) != std::string::npos);
#endif
}

void test_storage_backlight_upper_bound_import_and_load(void) {
    char hexBuf[sizeof(AppConfigBlob) * 2 + 1];

    // 1. importConfigHex rejects backlight_timeout_ms > 3600000ms (unless BACKLIGHT_TIMEOUT_NEVER)
    gState.backlight_timeout_ms = 0xFFFFFFFEUL;
    storage.exportConfigHex(hexBuf, sizeof(hexBuf));
    initAppState();
    TEST_ASSERT_FALSE(storage.importConfigHex(hexBuf));

    gState.backlight_timeout_ms = 3600001UL;
    storage.exportConfigHex(hexBuf, sizeof(hexBuf));
    initAppState();
    TEST_ASSERT_FALSE(storage.importConfigHex(hexBuf));

    // Accepts valid upper bound 3600000UL and BACKLIGHT_TIMEOUT_NEVER
    gState.backlight_timeout_ms = 3600000UL;
    storage.exportConfigHex(hexBuf, sizeof(hexBuf));
    initAppState();
    TEST_ASSERT_TRUE(storage.importConfigHex(hexBuf));
    TEST_ASSERT_EQUAL_UINT32(3600000UL, gState.backlight_timeout_ms);

    gState.backlight_timeout_ms = BACKLIGHT_TIMEOUT_NEVER;
    storage.exportConfigHex(hexBuf, sizeof(hexBuf));
    initAppState();
    TEST_ASSERT_TRUE(storage.importConfigHex(hexBuf));
    TEST_ASSERT_EQUAL_UINT32(BACKLIGHT_TIMEOUT_NEVER, gState.backlight_timeout_ms);

    // 2. NVS load() defensively falls back to 120000UL if stored backlight_timeout_ms exceeds 3600000UL
    gState.backlight_timeout_ms = 0xFFFFFFFEUL;
    TEST_ASSERT_TRUE(storage.save());
    initAppState();
    TEST_ASSERT_TRUE(storage.load());
    TEST_ASSERT_EQUAL_UINT32(120000UL, gState.backlight_timeout_ms);
}

void test_storage_begin_idempotent_oversized_blobs_write_rollback_and_range_clamp(void) {
    // 1. Storage::begin() is idempotent (must not fail on repeat call when already _ready)
    TEST_ASSERT_TRUE(storage._ready);
    storage.begin();
    TEST_ASSERT_TRUE(storage._ready);

    // 2. Preferences::beginFail sets _ready = false cleanly
    Storage failedStorage;
    Preferences::beginFail = true;
    Serial.reset();
    failedStorage.begin();
    TEST_ASSERT_FALSE(failedStorage._ready);
    Preferences::beginFail = false;

    // Re-open shared prefs handle after failedStorage.begin() called prefs.end()
    storage._ready = false;
    storage.begin();
    TEST_ASSERT_TRUE(storage._ready);

    // 3. Oversized NVS blobs (> sizeof(struct)) report size mismatch, NOT empty
    uint8_t oversized[64];
    memset(oversized, 0x5A, sizeof(oversized));

    prefs.putBytes(KEY_CFG, oversized, sizeof(oversized));
    Serial.reset();
    TEST_ASSERT_FALSE(storage.load());
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_NVS_EMPTY) == std::string::npos);
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_NVS_SIZE_MISMATCH) != std::string::npos);
#endif

    prefs.putBytes(KEY_RECV, oversized, sizeof(oversized));
    RecoveryState rs = {};
    Serial.reset();
    TEST_ASSERT_FALSE(storage.loadRecoveryState(rs));
#if LOG_LEVEL >= LVL_WARN
    char expectedRecv[128];
    snprintf(expectedRecv, sizeof(expectedRecv), TXT_LOG_NVS_RECV_CORRUPT, TXT_NVS_RSN_SIZE);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedRecv) != std::string::npos);
#endif

    prefs.putBytes(KEY_SCHED_TR, oversized, sizeof(oversized));
    uint32_t tr[MAX_SLOTS_PER_MODE] = {};
    Serial.reset();
    TEST_ASSERT_FALSE(storage.loadLastTriggers(tr, MAX_SLOTS_PER_MODE));
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_NVS_TRIG_CORRUPT) != std::string::npos);
#endif

    // 4. NVS write failure during importConfigHex rolls back RAM state atomically
    storage.clear();
    gState.mode = AppMode::INTENSO;
    char hexIntenso[sizeof(AppConfigBlob) * 2 + 1] = {};
    storage.exportConfigHex(hexIntenso, sizeof(hexIntenso));

    initAppState();
    gState.mode = AppMode::FRACO;
    Preferences::writeFail = true;
    TEST_ASSERT_FALSE(storage.importConfigHex(hexIntenso));
    TEST_ASSERT_EQUAL(AppMode::FRACO, gState.mode);
    Preferences::writeFail = false;

    // 5. NVS load() defensively clamps out-of-range suspended_until and custom_ref_day
    gState.mode = AppMode::MEDIO;
    gState.suspended = true;
    gState.suspended_until = 0xFFFFFFFEUL;
    gState.custom_ref_day = 99999UL;
    TEST_ASSERT_TRUE(storage.save());

    initAppState();
    TEST_ASSERT_TRUE(storage.load());
    TEST_ASSERT_TRUE(gState.suspended);
    TEST_ASSERT_EQUAL_UINT32(4102444799UL, gState.suspended_until);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFUL, gState.custom_ref_day);

    // 6. Storage::update() retries failed config saves on a 5 s throttle,
    //    saveRecoveryState() reports write failure, and failed importConfigHex() preserves _savePending
    TEST_ASSERT_FALSE(storage.hasPendingSave());
    gState.mode = AppMode::INTENSO;
    Preferences::writeFail = true;
    TEST_ASSERT_FALSE(storage.save());
    TEST_ASSERT_TRUE(storage.hasPendingSave());

    RecoveryState failRs = {};
    failRs.active = true;
    TEST_ASSERT_FALSE(storage.saveRecoveryState(failRs));

    Preferences::writeFail = false;
    advanceMockMillis(2000UL);
    storage.update();
    TEST_ASSERT_TRUE(storage.hasPendingSave()); // still inside 5 s throttle
    advanceMockMillis(3500UL);
    storage.update();
    TEST_ASSERT_FALSE(storage.hasPendingSave()); // retried & persisted
    initAppState();
    TEST_ASSERT_TRUE(storage.load());
    TEST_ASSERT_EQUAL(AppMode::INTENSO, gState.mode);

    // Failed importConfigHex() must not arm _savePending when previous state was clean
    Preferences::writeFail = true;
    TEST_ASSERT_FALSE(storage.importConfigHex(hexIntenso));
    TEST_ASSERT_FALSE(storage.hasPendingSave());
    Preferences::writeFail = false;
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_storage_export_hex_length_and_format);
    RUN_TEST(test_storage_export_and_import_roundtrip);
    RUN_TEST(test_storage_import_rejects_invalid_inputs);
    RUN_TEST(test_storage_recovery_state_roundtrip);
    RUN_TEST(test_storage_recovery_state_rejects_legacy_size_ver_bool_and_crc_bitflip);
    RUN_TEST(test_storage_save_and_load_persistence);
    RUN_TEST(test_storage_rejects_corrupted_boolean_bytes);
    RUN_TEST(test_storage_duration_clamping_on_import);
    RUN_TEST(test_storage_import_rejects_version_mismatch);
    RUN_TEST(test_storage_clear_wipes_all);
    RUN_TEST(test_storage_history_cache_persistence);
    RUN_TEST(test_storage_dormant_custom_overlap_auto_respaced_vs_active_rejected);
    RUN_TEST(test_storage_zero_duration_normalizes_enabled_false);
    RUN_TEST(test_storage_nvs_load_crc_rejection_and_clamping);
    RUN_TEST(test_storage_history_blob_atomic_and_filesize);
    RUN_TEST(test_storage_last_triggers_roundtrip);
    RUN_TEST(test_storage_clear_guard_invalid_hist_cache_and_respace_logs);
    RUN_TEST(test_storage_backlight_upper_bound_import_and_load);
    RUN_TEST(test_storage_begin_idempotent_oversized_blobs_write_rollback_and_range_clamp);
    return UNITY_END();
}
