#include <unity.h>
#include "TestCommon.h"
#include "../../test/mocks/TestCommon.cpp"
#include "../../src/AppState.cpp"
#include "../../src/Storage.cpp"

#define private public
#include "History.h"
#undef private

#include "../../src/History.cpp"

void setUp(void) {
    resetAllTestMocks();
    gState.rtc_valid = true;
    history.begin();
}

void tearDown(void) {}

void test_history_entry_to_line_format(void) {
    HistoryEntry e = {};
    e.year = 2026;
    e.month = 4;
    e.day = 26;
    e.hour = 18;
    e.min = 2;
    e.trigger = WaterTrigger::MANUAL;
    e.zone_dur[0] = 15;
    e.zone_dur[1] = 15;
    e.zone_dur[2] = 10;
    e.zone_dur[3] = 0;

    char line[128] = {};
    History::_entryToLine(e, line, sizeof(line));

    TEST_ASSERT_EQUAL_STRING("2026-04-26T18:02,M,15,15,10,0", line);

    // Test with AUTO trigger
    e.trigger = WaterTrigger::AUTO;
    History::_entryToLine(e, line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("2026-04-26T18:02,A,15,15,10,0", line);

    // Test with CUSTOM trigger
    e.trigger = WaterTrigger::CUSTOM;
    History::_entryToLine(e, line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("2026-04-26T18:02,C,15,15,10,0", line);
}

void test_history_line_to_entry_valid(void) {
    const char* csv = "2026-07-14T06:30,A,10,12,8,0";
    HistoryEntry e = {};

    bool ok = History::_lineToEntry(csv, e);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT16(2026, e.year);
    TEST_ASSERT_EQUAL_UINT8(7, e.month);
    TEST_ASSERT_EQUAL_UINT8(14, e.day);
    TEST_ASSERT_EQUAL_UINT8(6, e.hour);
    TEST_ASSERT_EQUAL_UINT8(30, e.min);
    TEST_ASSERT_EQUAL(WaterTrigger::AUTO, e.trigger);
    TEST_ASSERT_EQUAL_UINT8(10, e.zone_dur[0]);
    TEST_ASSERT_EQUAL_UINT8(12, e.zone_dur[1]);
    TEST_ASSERT_EQUAL_UINT8(8, e.zone_dur[2]);
    TEST_ASSERT_EQUAL_UINT8(0, e.zone_dur[3]);
}

void test_history_line_to_entry_rejects_corrupted(void) {
    HistoryEntry e = {};

    // 1. Invalid date separators
    TEST_ASSERT_FALSE(History::_lineToEntry("2026/07/14 06:30,A,10,10,10,10", e));

    // 2. Year before 2020
    TEST_ASSERT_FALSE(History::_lineToEntry("2015-05-10T10:00,A,10,10,10,10", e));

    // 3. Year after 2099
    TEST_ASSERT_FALSE(History::_lineToEntry("2105-05-10T10:00,A,10,10,10,10", e));

    // 4. Invalid month (> 12)
    TEST_ASSERT_FALSE(History::_lineToEntry("2026-13-10T10:00,A,10,10,10,10", e));

    // 5. Invalid day (> 31)
    TEST_ASSERT_FALSE(History::_lineToEntry("2026-05-32T10:00,A,10,10,10,10", e));

    // 6. Invalid hour (> 23)
    TEST_ASSERT_FALSE(History::_lineToEntry("2026-05-10T25:00,A,10,10,10,10", e));

    // 7. Invalid minute (> 59)
    TEST_ASSERT_FALSE(History::_lineToEntry("2026-05-10T10:60,A,10,10,10,10", e));
}

void test_history_record_and_read_last(void) {
    history.clear();
    TEST_ASSERT_EQUAL_UINT16(0, history.entryCount());

    HistoryEntry e1 = { 2026, 5, 1, 7, 0, { 10, 10, 0, 0 }, WaterTrigger::AUTO, 0 };
    history.record(e1);
    TEST_ASSERT_EQUAL_UINT16(1, history.entryCount());

    HistoryEntry e2 = { 2026, 5, 2, 7, 0, { 12, 12, 5, 0 }, WaterTrigger::MANUAL, 0 };
    history.record(e2);
    TEST_ASSERT_EQUAL_UINT16(2, history.entryCount());

    // TEST trigger must be ignored
    HistoryEntry eTest = { 2026, 5, 3, 7, 0, { 1, 0, 0, 0 }, WaterTrigger::TEST, 0 };
    history.record(eTest);
    TEST_ASSERT_EQUAL_UINT16(2, history.entryCount());

    // Read back last entries
    HistoryEntry out[4] = {};
    uint8_t count = history.readLast(4, out);
    TEST_ASSERT_EQUAL_UINT8(2, count);
    TEST_ASSERT_EQUAL_UINT8(1, out[0].day);
    TEST_ASSERT_EQUAL_UINT8(2, out[1].day);

    // Clear
    history.clear();
    TEST_ASSERT_EQUAL_UINT16(0, history.entryCount());
    TEST_ASSERT_EQUAL_UINT8(0, history.readLast(4, out));
}

void test_history_rotation_triggered_at_capacity(void) {
    history.clear();

    // Populate file with lines directly using bulk pre-allocation for speed
    std::string fullFile;
    fullFile.reserve(HISTORY_MAX_ENTRIES * 32);
    for (int i = 0; i < HISTORY_MAX_ENTRIES; i++) {
        fullFile.append("2026-06-01T08:00,A,10,10,0,0\r\n");
    }
    LittleFS.files[HISTORY_FILE] = std::move(fullFile);
    history._lineCount = HISTORY_MAX_ENTRIES;

    // Recording entry at capacity triggers async rotation
    HistoryEntry e = { 2026, 6, 2, 8, 0, { 15, 15, 0, 0 }, WaterTrigger::MANUAL, 0 };
    history.record(e);

    // Rotation state machine should be active
    TEST_ASSERT_TRUE(history._rotState != History::RotState::IDLE);

    // Run update iterations until rotation completes
    int safetyLimit = 500;
    while (history._rotState != History::RotState::IDLE && safetyLimit-- > 0) {
        history.update();
    }

    TEST_ASSERT_EQUAL(History::RotState::IDLE, history._rotState);
    uint16_t expectedRemaining = HISTORY_MAX_ENTRIES - (HISTORY_MAX_ENTRIES / 10) + 1;
    TEST_ASSERT_EQUAL_UINT16(expectedRemaining, history.entryCount());
}

void test_history_rejects_missing_columns(void) {
    HistoryEntry e = {};
    // 1. Line with only 1 zone duration instead of 4: should parse and pad remaining with 0
    const char* partialCsv = "2026-06-01T08:00,A,10";
    bool ok = History::_lineToEntry(partialCsv, e);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(10, e.zone_dur[0]);
    TEST_ASSERT_EQUAL_UINT8(0, e.zone_dur[1]);
    TEST_ASSERT_EQUAL_UINT8(0, e.zone_dur[2]);
    TEST_ASSERT_EQUAL_UINT8(0, e.zone_dur[3]);

    // 2. Truncated line without trigger: must be rejected
    TEST_ASSERT_FALSE(History::_lineToEntry("2026-06-01T08:00", e));

    // 3. Truncated date: must be rejected
    TEST_ASSERT_FALSE(History::_lineToEntry("2026-06", e));
}

void test_history_cache_fifo_eviction(void) {
    history.clear();
    TEST_ASSERT_EQUAL_UINT16(0, history.entryCount());

    // Record 7 entries (HISTORY_DISPLAY is 3)
    for (uint8_t day = 1; day <= 7; day++) {
        HistoryEntry e = { 2026, 7, day, 8, 0, { day, 0, 0, 0 }, WaterTrigger::AUTO, 0 };
        history.record(e);
    }

    TEST_ASSERT_EQUAL_UINT16(7, history.entryCount());

    // Cache should hold the latest HISTORY_DISPLAY (3) entries: days 5, 6, 7
    HistoryEntry out[5] = {};
    uint8_t count = history.readLast(5, out);
    TEST_ASSERT_EQUAL_UINT8(HISTORY_DISPLAY, count);
    TEST_ASSERT_EQUAL_UINT8(5, out[0].day);
    TEST_ASSERT_EQUAL_UINT8(6, out[1].day);
    TEST_ASSERT_EQUAL_UINT8(7, out[2].day);
}

void test_history_deferred_queue_when_busy(void) {
    history.clear();

    // Seed 1 record so export can run
    HistoryEntry base = { 2026, 8, 1, 8, 0, { 5, 0, 0, 0 }, WaterTrigger::AUTO, 0 };
    history.record(base);
    TEST_ASSERT_EQUAL_UINT16(1, history.entryCount());

    // Start async export so disk is busy, then queue 3 deferred entries (full 3-slot ring)
    TEST_ASSERT_TRUE(history.startExport());
    TEST_ASSERT_TRUE(history.isExporting());

    HistoryEntry d1 = { 2026, 8, 10, 8, 0, { 11, 0, 0, 0 }, WaterTrigger::AUTO, 0 };
    HistoryEntry d2 = { 2026, 8, 11, 8, 0, { 12, 0, 0, 0 }, WaterTrigger::MANUAL, 0 };
    HistoryEntry d3 = { 2026, 8, 12, 8, 0, { 13, 0, 0, 0 }, WaterTrigger::CUSTOM, 0 };
    history.record(d1);
    history.record(d2);
    history.record(d3);

    // None written yet while exporting
    TEST_ASSERT_NOT_EQUAL(history._defHead, history._defTail);
    TEST_ASSERT_EQUAL_UINT16(1, history.entryCount());

    // Finish export -> FOOTER drains d1 immediately; d2 and d3 stay queued
    int safety = 100;
    while (history.isExporting() && safety-- > 0) {
        history.update();
    }
    TEST_ASSERT_FALSE(history.isExporting());
    TEST_ASSERT_EQUAL_UINT16(2, history.entryCount());
    TEST_ASSERT_NOT_EQUAL(history._defHead, history._defTail);

    // Enqueue d4 while d2/d3 are still waiting -> must stay strictly behind d3 (no reordering)
    HistoryEntry d4 = { 2026, 8, 13, 8, 0, { 14, 0, 0, 0 }, WaterTrigger::AUTO, 0 };
    history.record(d4);

    // Subsequent IDLE update() ticks drain d2, d3, d4 one per tick without stranding
    history.update();
    TEST_ASSERT_EQUAL_UINT16(3, history.entryCount());
    history.update();
    TEST_ASSERT_EQUAL_UINT16(4, history.entryCount());
    history.update();
    TEST_ASSERT_EQUAL_UINT16(5, history.entryCount());
    TEST_ASSERT_EQUAL_UINT8(history._defHead, history._defTail);

    // Cache holds the latest 3 entries in exact chronological order: days 11, 12, 13
    HistoryEntry out[3] = {};
    TEST_ASSERT_EQUAL_UINT8(3, history.readLast(3, out));
    TEST_ASSERT_EQUAL_UINT8(11, out[0].day);
    TEST_ASSERT_EQUAL_UINT8(12, out[1].day);
    TEST_ASSERT_EQUAL_UINT8(13, out[2].day);
}

void test_history_export_stream(void) {
    history.clear();
    Serial.reset();

    HistoryEntry e1 = { 2026, 6, 1, 8, 0, { 10, 10, 0, 0 }, WaterTrigger::AUTO, 0 };
    HistoryEntry e2 = { 2026, 6, 2, 8, 0, { 12, 12, 0, 0 }, WaterTrigger::MANUAL, 0 };
    history.record(e1);
    history.record(e2);

    history.startExport();
    TEST_ASSERT_TRUE(history._exportState != History::ExportState::IDLE);

    int safety = 100;
    while (history._exportState != History::ExportState::IDLE && safety-- > 0) {
        history.update();
    }

    TEST_ASSERT_EQUAL(History::ExportState::IDLE, history._exportState);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("2026-06-01T08:00,A,10,10,0,0") != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("2026-06-02T08:00,M,12,12,0,0") != std::string::npos);
}

void test_history_guarded_format_on_mount_failure(void) {
    Preferences::resetStorage();
    storage.begin();
    LittleFS.reset();
    LittleFS.mountFail = true;

    bool ok = history.begin(true);
    TEST_ASSERT_TRUE(ok);

    HistoryEntry sample[HISTORY_DISPLAY] = {};
    sample[0].year = 2026;
    sample[0].month = 5;
    sample[0].day = 10;
    storage.saveHistoryCache(sample, sizeof(sample), 5);

    LittleFS.mountFail = true;
    bool guarded = history.begin(true);
    TEST_ASSERT_FALSE(guarded);
}

void test_history_line_to_entry_negative_and_overflow_bounds(void) {
    HistoryEntry e = {};

    // Negative hour or minute must be rejected
    TEST_ASSERT_FALSE(History::_lineToEntry("2026-05-10T-1:00,A,10,10,10,10", e));
    TEST_ASSERT_FALSE(History::_lineToEntry("2026-05-10T10:-5,A,10,10,10,10", e));

    // Negative and >255 zone durations must clamp to [0, 255] without uint8_t wraparound
    TEST_ASSERT_TRUE(History::_lineToEntry("2026-05-10T10:30,M,-5,999,20,0", e));
    TEST_ASSERT_EQUAL_UINT8(0, e.zone_dur[0]);
    TEST_ASSERT_EQUAL_UINT8(255, e.zone_dur[1]);
    TEST_ASSERT_EQUAL_UINT8(20, e.zone_dur[2]);
    TEST_ASSERT_EQUAL_UINT8(0, e.zone_dur[3]);

    // Null output buffer guard
    TEST_ASSERT_EQUAL_UINT8(0, history.readLast(3, nullptr));
}

void test_history_rotation_write_failure_preserves_original_file(void) {
    history.clear();

    std::string fullFile;
    fullFile.reserve(HISTORY_MAX_ENTRIES * 32);
    for (int i = 0; i < HISTORY_MAX_ENTRIES; i++) {
        fullFile.append("2026-06-01T08:00,A,10,10,0,0\r\n");
    }
    const size_t origSize = fullFile.size();
    LittleFS.files[HISTORY_FILE] = std::move(fullFile);
    history._lineCount = HISTORY_MAX_ENTRIES;

    HistoryEntry e = { 2026, 6, 2, 8, 0, { 15, 15, 0, 0 }, WaterTrigger::MANUAL, 0 };
    history.record(e);
    TEST_ASSERT_TRUE(history._rotState != History::RotState::IDLE);

    // Simulate flash write failure mid-rotation
    LittleFS.writeFail = true;
    int safetyLimit = 500;
    while (history._rotState != History::RotState::IDLE && safetyLimit-- > 0) {
        history.update();
    }
    LittleFS.writeFail = false;

    TEST_ASSERT_EQUAL(History::RotState::IDLE, history._rotState);
    // Original file must still exist intact; temp file must be cleaned up
    TEST_ASSERT_TRUE(LittleFS.exists(HISTORY_FILE));
    TEST_ASSERT_FALSE(LittleFS.exists("/hist_tmp.csv"));
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(origSize, LittleFS.files[HISTORY_FILE].size());
}

void test_history_corrupted_nvs_cache_rebuilds_from_fs(void) {
    Preferences::resetStorage();
    storage.begin();
    LittleFS.reset();

    LittleFS.files[HISTORY_FILE] =
        "2026-07-01T07:00,A,10,0,0,0\r\n"
        "2026-07-02T07:00,M,12,5,0,0\r\n";

    // Corrupt NVS cache entry (year = 9999)
    HistoryEntry badCache[HISTORY_DISPLAY] = {};
    badCache[0] = { 9999, 99, 99, 99, 99, { 1, 1, 1, 1 }, WaterTrigger::AUTO, 0 };
    storage.saveHistoryCache(badCache, sizeof(badCache), 50);

    TEST_ASSERT_TRUE(history.begin(false));
    TEST_ASSERT_EQUAL_UINT16(2, history.entryCount());

    HistoryEntry out[3] = {};
    uint8_t n = history.readLast(3, out);
    TEST_ASSERT_EQUAL_UINT8(2, n);
    TEST_ASSERT_EQUAL_UINT16(2026, out[0].year);
    TEST_ASSERT_EQUAL_UINT8(1, out[0].day);
    TEST_ASSERT_EQUAL_UINT8(2, out[1].day);
}

void test_history_begin_detects_truncated_file_via_filesize(void) {
    HistoryEntry e1 = { 2026, 7, 1, 8, 0, { 10, 0, 0, 0 }, WaterTrigger::AUTO, 0 };
    HistoryEntry e2 = { 2026, 7, 2, 8, 0, { 12, 0, 0, 0 }, WaterTrigger::MANUAL, 0 };
    history.record(e1);
    history.record(e2);
    TEST_ASSERT_EQUAL_UINT16(2, history.entryCount());

    // Mutate file in LittleFS (truncate to first line) leaving stale NVS cache
    LittleFS.files[HISTORY_FILE] = "2026-07-01T08:00,A,10,0,0,0\r\n";

    History fresh;
    TEST_ASSERT_TRUE(fresh.begin(false));
    TEST_ASSERT_EQUAL_UINT16(1, fresh.entryCount());

    HistoryEntry out[3] = {};
    TEST_ASSERT_EQUAL_UINT8(1, fresh.readLast(3, out));
    TEST_ASSERT_EQUAL_UINT8(1, out[0].day);
}

void test_history_pending_record_persisted_and_replayed_across_rotation(void) {
    history.clear();

    // Over-capacity file (HISTORY_MAX_ENTRIES + 5 lines) -> dynamic _rotDiscard trims to ~90%
    std::string bigFile;
    bigFile.reserve((HISTORY_MAX_ENTRIES + 5) * 32);
    for (int i = 0; i < HISTORY_MAX_ENTRIES + 5; i++) {
        bigFile.append("2026-06-01T08:00,A,10,0,0,0\r\n");
    }
    LittleFS.files[HISTORY_FILE] = std::move(bigFile);
    history._lineCount = HISTORY_MAX_ENTRIES + 5;

    HistoryEntry triggerRot = { 2026, 6, 2, 8, 0, { 11, 0, 0, 0 }, WaterTrigger::AUTO, 0 };
    history.record(triggerRot);
    TEST_ASSERT_TRUE(history._rotState != History::RotState::IDLE);

    // Record another entry while rotation is active -> persisted in /hist_pend.txt
    HistoryEntry deferred = { 2026, 6, 3, 9, 30, { 14, 0, 0, 0 }, WaterTrigger::MANUAL, 0 };
    history.record(deferred);
    TEST_ASSERT_TRUE(LittleFS.exists("/hist_pend.txt"));

    int safety = 1000;
    while (history._rotState != History::RotState::IDLE && safety-- > 0) {
        history.update();
    }
    TEST_ASSERT_EQUAL(History::RotState::IDLE, history._rotState);

    // Simulate power loss after pending file write by re-creating /hist_pend.txt and restarting
    LittleFS.files["/hist_pend.txt"] = "2026-06-04T07:45,C,6,0,0,0\r\n";
    History recovered;
    TEST_ASSERT_TRUE(recovered.begin(false));
    TEST_ASSERT_FALSE(LittleFS.exists("/hist_pend.txt"));

    HistoryEntry latest[1] = {};
    TEST_ASSERT_EQUAL_UINT8(1, recovered.readLast(1, latest));
    TEST_ASSERT_EQUAL_UINT8(4, latest[0].day);
    TEST_ASSERT_EQUAL(WaterTrigger::CUSTOM, latest[0].trigger);
}

void test_history_rejects_test_trigger_and_long_corrupt_line(void) {
    HistoryEntry e = {};
    TEST_ASSERT_FALSE(History::_lineToEntry("2026-06-01T08:00,T,5,0,0,0", e));

    // Overlong line (>= LINE_BUF) must not splice trailing bytes into a phantom entry
    std::string corrupt(127, 'X');
    corrupt += "2026-06-01T08:00,M,5,0,0,0\r\n";
    corrupt += "2026-06-05T10:00,A,8,0,0,0\r\n";
    LittleFS.files[HISTORY_FILE] = corrupt;

    History fresh;
    TEST_ASSERT_TRUE(fresh.begin(false));
    HistoryEntry out[2] = {};
    TEST_ASSERT_EQUAL_UINT8(1, fresh.readLast(2, out));
    TEST_ASSERT_EQUAL_UINT8(5, out[0].day);
}

void test_history_rotation_double_fail_export_suppression_and_guards(void) {
    // 1. record() without valid RTC logs TXT_LOG_HIST_SKIP_NO_RTC and drops entry
    history.clear();
    gState.rtc_valid = false;
    Serial.reset();
    HistoryEntry e1 = { 2026, 6, 1, 8, 0, { 10, 0, 0, 0 }, WaterTrigger::AUTO, 0 };
    history.record(e1);
    TEST_ASSERT_EQUAL_UINT16(0, history.entryCount());
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_HIST_SKIP_NO_RTC) != std::string::npos);
#endif
    gState.rtc_valid = true;

    // 2. clear() and startExport() when !_ready return false and log TXT_LOG_HIST_NOT_READY
    history._ready = false;
    Serial.reset();
    TEST_ASSERT_FALSE(history.clear());
    TEST_ASSERT_FALSE(history.startExport(true));
#if LOG_LEVEL >= LVL_ERROR
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_HIST_NOT_READY) != std::string::npos);
#endif
    history._ready = true;

    // 3. Rotation fallback double-failure (_rotSrc missing + writeFail = true) logs FILE_ERR without false ROT_OPEN_FAIL
    history._lineCount = HISTORY_MAX_ENTRIES;
    LittleFS.remove(HISTORY_FILE);
    LittleFS.writeFail = true;
    Serial.reset();
    history.record(e1);
    TEST_ASSERT_EQUAL_UINT16(HISTORY_MAX_ENTRIES, history.entryCount());
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_HIST_ROT_OPEN_FAIL) == std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_HIST_LOGGED) == std::string::npos);
#if LOG_LEVEL >= LVL_ERROR
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_HIST_FILE_ERR) != std::string::npos);
#endif
    LittleFS.writeFail = false;

    // When fallback append succeeds (_rotSrc missing, writeFail = false), ROT_OPEN_FAIL is logged
    LittleFS.remove(HISTORY_FILE);
    Serial.reset();
    history.record(e1);
    TEST_ASSERT_EQUAL_UINT16(HISTORY_MAX_ENTRIES + 1, history.entryCount());
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_HIST_ROT_OPEN_FAIL) != std::string::npos);
#endif

    // 4. record() during CSV export defers cleanly without triggering warn/err suppression alert
    history.clear();
    history.record(e1);
    Serial.reset();
    TEST_ASSERT_TRUE(history.startExport(true));
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_START_EXPORT) != std::string::npos);

    HistoryEntry e2 = { 2026, 6, 2, 8, 0, { 12, 0, 0, 0 }, WaterTrigger::MANUAL, 0 };
    history.record(e2);
    TEST_ASSERT_EQUAL_UINT16(0, _log_suppressed_warn_err);

    // Emit an info-only log during export -> footer logs TXT_LOG_EXPORT_SUPPRESSED_INFO, not warning
    _logWrite(false, TXT_LOG_LVL_INFO, TXT_LOG_TAG_SYS, "%s", TXT_LOG_SYS_READY);
    int safety = 100;
    while (history.isExporting() && safety-- > 0) {
        history.update();
    }
    TEST_ASSERT_EQUAL_UINT16(2, history.entryCount());
#if LOG_LEVEL >= LVL_INFO
    char expectedInfoSup[128];
    snprintf(expectedInfoSup, sizeof(expectedInfoSup), TXT_LOG_EXPORT_SUPPRESSED_INFO, 1U);
    // Note: if LOG_LEVEL == LVL_DEBUG, record() also emitted 1 debug log -> count is 2
#if LOG_LEVEL >= LVL_DEBUG
    snprintf(expectedInfoSup, sizeof(expectedInfoSup), TXT_LOG_EXPORT_SUPPRESSED_INFO, 2U);
#endif
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedInfoSup) != std::string::npos);
#endif
}

void test_history_rotation_read_failure_preserves_original_file(void) {
    history.clear();

    std::string fullFile;
    fullFile.reserve(HISTORY_MAX_ENTRIES * 32);
    for (int i = 0; i < HISTORY_MAX_ENTRIES; i++) {
        fullFile.append("2026-06-01T08:00,A,10,10,0,0\r\n");
    }
    const size_t origSize = fullFile.size();
    LittleFS.files[HISTORY_FILE] = std::move(fullFile);
    history._lineCount = HISTORY_MAX_ENTRIES;

    HistoryEntry e = { 2026, 6, 2, 8, 0, { 15, 15, 0, 0 }, WaterTrigger::MANUAL, 0 };
    history.record(e);
    TEST_ASSERT_EQUAL(History::RotState::COPYING, history._rotState);

    // Read a few chunks, then trigger mid-file LittleFS read error (bytesRead == 0 while available > 0)
    history.update();
    LittleFS.readFail = true;
    int safetyLimit = 500;
    while (history._rotState != History::RotState::IDLE && safetyLimit-- > 0) {
        history.update();
        LittleFS.readFail = false;
    }

    TEST_ASSERT_EQUAL(History::RotState::IDLE, history._rotState);
    TEST_ASSERT_TRUE(LittleFS.exists(HISTORY_FILE));
    TEST_ASSERT_FALSE(LittleFS.exists("/hist_tmp.csv"));
    TEST_ASSERT_EQUAL_UINT16(HISTORY_MAX_ENTRIES + 1, history.entryCount());
    TEST_ASSERT_GREATER_THAN_UINT32(origSize, LittleFS.files[HISTORY_FILE].size());

    HistoryEntry latest[1] = {};
    TEST_ASSERT_EQUAL_UINT8(1, history.readLast(1, latest));
    TEST_ASSERT_EQUAL_UINT8(2, latest[0].day);
    TEST_ASSERT_EQUAL(WaterTrigger::MANUAL, latest[0].trigger);
}

void test_history_periodic_remount_and_force_repair(void) {
    Preferences::resetStorage();
    storage.begin();
    LittleFS.reset();

    // Seed NVS history cache so guarded begin() refuses auto-format
    HistoryEntry sample[HISTORY_DISPLAY] = {};
    sample[0] = { 2026, 5, 10, 8, 0, { 10, 0, 0, 0 }, WaterTrigger::AUTO, 0 };
    storage.saveHistoryCache(sample, sizeof(sample), 1, 28);

    LittleFS.mountFail = true;
    TEST_ASSERT_FALSE(history.begin(true));
    TEST_ASSERT_FALSE(history.isReady());

    // Plain clear() remains blocked while unmounted
    TEST_ASSERT_FALSE(history.clear(false));
    TEST_ASSERT_FALSE(history.isReady());

    // Clear transient mount error -> periodic 24h update() remounts non-destructively
    LittleFS.mountFail = false;
    LittleFS.files[HISTORY_FILE] = "2026-05-10T08:00,A,10,0,0,0\r\n";
    advanceMockMillis(86400000UL - 1000UL);
    history.update();
    TEST_ASSERT_FALSE(history.isReady());

    advanceMockMillis(1000UL);
    history.update();
    TEST_ASSERT_TRUE(history.isReady());
    TEST_ASSERT_EQUAL_UINT16(1, history.entryCount());

    // Persistent mount failure can be explicitly repaired via clear(true)
    LittleFS.mountFail = true;
    TEST_ASSERT_FALSE(history.begin(true));
    TEST_ASSERT_FALSE(history.isReady());
    TEST_ASSERT_TRUE(history.clear(true));
    TEST_ASSERT_TRUE(history.isReady());
    TEST_ASSERT_EQUAL_UINT16(0, history.entryCount());
    TEST_ASSERT_FALSE(storage.hasHistoryData());
}

void test_history_remount_watering_guard_and_clear_edge_cases(void) {
    Preferences::resetStorage();
    storage.begin();
    LittleFS.reset();

    // 1. Seed NVS history cache and fail initial mount -> 24h remount waits while watering is active
    HistoryEntry sample[HISTORY_DISPLAY] = {};
    sample[0] = { 2026, 5, 10, 8, 0, { 10, 0, 0, 0 }, WaterTrigger::AUTO, 0 };
    storage.saveHistoryCache(sample, sizeof(sample), 1, 28);
    LittleFS.mountFail = true;
    TEST_ASSERT_FALSE(history.begin(true));

    LittleFS.mountFail = false;
    LittleFS.files[HISTORY_FILE] = "2026-05-10T08:00,A,10,0,0,0\r\n";
    gState.watering.active = true;
    advanceMockMillis(86400000UL);
    history.update();
    TEST_ASSERT_FALSE(history.isReady());

    // Once watering stops, overdue 24h remount executes non-destructively
    gState.watering.active = false;
    history.update();
    TEST_ASSERT_TRUE(history.isReady());
    TEST_ASSERT_EQUAL_UINT16(1, history.entryCount());

    // 2. Orphaned /hist_tmp.csv is cleaned by clear(false) even when rotation is IDLE
    LittleFS.files["/hist_tmp.csv"] = "stale temp line\r\n";
    TEST_ASSERT_TRUE(history.clear(false));
    TEST_ASSERT_FALSE(LittleFS.exists("/hist_tmp.csv"));

    // 3. Mounted FS unlink failure blocks clear(false), while clear(true) falls back to format()
    gState.rtc_valid = true;
    HistoryEntry e = { 2026, 5, 11, 9, 0, { 5, 0, 0, 0 }, WaterTrigger::MANUAL, 0 };
    history.record(e);
    TEST_ASSERT_EQUAL_UINT16(1, history.entryCount());

    LittleFS.removeFail = true;
    TEST_ASSERT_FALSE(history.clear(false));
    TEST_ASSERT_TRUE(history.isReady());
    TEST_ASSERT_TRUE(history.clear(true));
    TEST_ASSERT_TRUE(history.isReady());
    TEST_ASSERT_EQUAL_UINT16(0, history.entryCount());
    TEST_ASSERT_FALSE(LittleFS.exists(HISTORY_FILE));

    // 4. Permanent format failure makes clear(true) fail cleanly and log TXT_LOG_HIST_FORMAT_FAIL
    LittleFS.mountFail = true;
    LittleFS.formatFail = true;
    TEST_ASSERT_FALSE(history.begin(true));
    Serial.reset();
    TEST_ASSERT_FALSE(history.clear(true));
    TEST_ASSERT_FALSE(history.isReady());
#if LOG_LEVEL >= LVL_ERROR
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_HIST_FORMAT_FAIL) != std::string::npos);
#endif
}

void test_history_buffered_filesize_partial_writes_rotation_tail_and_strict_csv(void) {
    // 1. Buffered VFSFileImpl::size() emulation: record() must flush/close before saving fsize to NVS
    Preferences::resetStorage();
    storage.begin();
    LittleFS.reset();
    LittleFS.bufferedSize = true;
    gState.rtc_valid = true;
    TEST_ASSERT_TRUE(history.begin(true));

    HistoryEntry e1 = { 2026, 9, 1, 7, 30, { 8, 6, 0, 0 }, WaterTrigger::AUTO, 0 };
    history.record(e1);
    TEST_ASSERT_EQUAL_UINT16(1, history.entryCount());

    // Stored NVS fileSize must equal actual flushed file size (NOT 0!)
    HistoryEntry nvsCache[HISTORY_DISPLAY] = {};
    uint16_t nvsCount = 0;
    uint32_t nvsFsize = 0;
    TEST_ASSERT_TRUE(storage.loadHistoryCache(nvsCache, sizeof(nvsCache), nvsCount, &nvsFsize));
    TEST_ASSERT_EQUAL_UINT16(1, nvsCount);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)LittleFS.files[HISTORY_FILE].size(), nvsFsize);
    TEST_ASSERT_GREATER_THAN_UINT32(0, nvsFsize);

    // Fresh boot must load directly from NVS quick cache without FS rebuild
    Serial.reset();
    History fresh;
    TEST_ASSERT_TRUE(fresh.begin(false));
    TEST_ASSERT_EQUAL_UINT16(1, fresh.entryCount());
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_HIST_FS_REASON_REBUILD) == std::string::npos);

    // 2. Partial write failure must be rejected cleanly in both sync record() and async rotation
    LittleFS.partialWriteFail = true;
    HistoryEntry ePartial = { 2026, 9, 2, 7, 30, { 10, 0, 0, 0 }, WaterTrigger::MANUAL, 0 };
    history.record(ePartial);
    TEST_ASSERT_EQUAL_UINT16(1, history.entryCount()); // NOT incremented on partial write
    LittleFS.partialWriteFail = false;

    // 3. Rotation when file has more valid lines than _lineCount preserves newest tail entries
    history.clear();
    std::string oversized;
    oversized.reserve((HISTORY_MAX_ENTRIES + 12) * 32);
    for (int i = 0; i < HISTORY_MAX_ENTRIES + 10; i++) {
        oversized.append("2026-06-01T08:00,A,10,0,0,0\r\n");
    }
    oversized.append("2026-06-29T09:15,M,7,0,0,0\r\n");
    oversized.append("2026-06-30T10:20,C,9,0,0,0\r\n");
    LittleFS.files[HISTORY_FILE] = std::move(oversized);
    history._lineCount = HISTORY_MAX_ENTRIES; // Deliberately underestimated by 12 lines

    HistoryEntry newest = { 2026, 7, 1, 11, 0, { 5, 0, 0, 0 }, WaterTrigger::AUTO, 0 };
    history.record(newest);
    int safety = 1000;
    while (history._rotState != History::RotState::IDLE && safety-- > 0) {
        history.update();
    }
    TEST_ASSERT_EQUAL(History::RotState::IDLE, history._rotState);
    // Tail entries 2026-06-29 and 2026-06-30 must NOT have been cut off!
    TEST_ASSERT_TRUE(LittleFS.files[HISTORY_FILE].find("2026-06-29T09:15,M,7,0,0,0") != std::string::npos);
    TEST_ASSERT_TRUE(LittleFS.files[HISTORY_FILE].find("2026-06-30T10:20,C,9,0,0,0") != std::string::npos);
    TEST_ASSERT_TRUE(LittleFS.files[HISTORY_FILE].find("2026-07-01T11:00,A,5,0,0,0") != std::string::npos);

    // 4. Stricter _lineToEntry() checks: missing zone comma, trigger garbage, non-numeric dur, invalid calendar day
    HistoryEntry parsed = {};
    TEST_ASSERT_FALSE(History::_lineToEntry("2026-04-26T18:02,A", parsed));
    TEST_ASSERT_FALSE(History::_lineToEntry("2026-04-26T18:02,Mabc,10,0,0,0", parsed));
    TEST_ASSERT_FALSE(History::_lineToEntry("2026-04-26T18:02,M,abc,0,0,0", parsed));
    TEST_ASSERT_FALSE(History::_lineToEntry("2026-04-26T18:02,M,10x,0,0,0", parsed));
    TEST_ASSERT_FALSE(History::_lineToEntry("2026-02-29T10:00,A,10,0,0,0", parsed)); // 2026 not leap
    TEST_ASSERT_TRUE(History::_lineToEntry("2028-02-29T10:00,A,10,0,0,0", parsed));  // 2028 leap
}

void test_history_partial_write_isolation_pending_dedup_corrupt_settle_export_overflow_and_tail_fallback(void) {
    // 1. [M-5] Partial write without newline is isolated before next valid record()
    Preferences::resetStorage();
    storage.begin();
    LittleFS.reset();
    gState.rtc_valid = true;
    TEST_ASSERT_TRUE(history.begin(true));

    HistoryEntry e1 = { 2026, 9, 1, 7, 30, { 8, 6, 0, 0 }, WaterTrigger::AUTO, 0 };
    history.record(e1);
    LittleFS.partialWriteFail = true;
    HistoryEntry eFail = { 2026, 9, 2, 7, 30, { 9, 0, 0, 0 }, WaterTrigger::MANUAL, 0 };
    history.record(eFail); // leaves unterminated partial fragment in /history.csv
    LittleFS.partialWriteFail = false;

    HistoryEntry e2 = { 2026, 9, 3, 8, 15, { 12, 4, 0, 0 }, WaterTrigger::CUSTOM, 0 };
    history.record(e2);
    TEST_ASSERT_EQUAL_UINT16(2, history.entryCount());

    // Force full FS rebuild on fresh boot -> both e1 and e2 must survive cleanly!
    Preferences::resetStorage();
    storage.begin();
    History rebuilt;
    TEST_ASSERT_TRUE(rebuilt.begin(false));
    HistoryEntry out[3] = {};
    TEST_ASSERT_EQUAL_UINT8(2, rebuilt.readLast(3, out));
    TEST_ASSERT_EQUAL_UINT8(1, out[0].day);
    TEST_ASSERT_EQUAL_UINT8(3, out[1].day);
    TEST_ASSERT_EQUAL(WaterTrigger::CUSTOM, out[1].trigger);

    // 2. [M-6] Pending replay skips duplicate when last line of HISTORY_FILE already matches
    Preferences::resetStorage();
    storage.begin();
    LittleFS.reset();
    LittleFS.files[HISTORY_FILE] =
        "2026-09-01T07:30,A,8,6,0,0\r\n"
        "2026-09-03T08:15,C,12,4,0,0\r\n";
    LittleFS.files["/hist_pend.txt"] = "2026-09-03T08:15,C,12,4,0,0\r\n";
    History dedup;
    TEST_ASSERT_TRUE(dedup.begin(false));
    TEST_ASSERT_FALSE(LittleFS.exists("/hist_pend.txt"));
    TEST_ASSERT_EQUAL_UINT16(2, dedup.entryCount());
    // Ensure the matching line appears exactly once
    const std::string& content = LittleFS.files[HISTORY_FILE];
    size_t firstPos = content.find("2026-09-03T08:15,C,12,4,0,0");
    TEST_ASSERT_TRUE(firstPos != std::string::npos);
    TEST_ASSERT_TRUE(content.find("2026-09-03T08:15,C,12,4,0,0", firstPos + 1) == std::string::npos);

    // 3. [M-7a] Corrupt-only file settles to _lineCount == 0 and avoids boot rebuild loop
    Preferences::resetStorage();
    storage.begin();
    LittleFS.reset();
    LittleFS.files[HISTORY_FILE] = "CORRUPT_LINE_ONE\r\nCORRUPT_LINE_TWO\r\n";
    History corruptBoot1;
    TEST_ASSERT_TRUE(corruptBoot1.begin(false));
    TEST_ASSERT_EQUAL_UINT16(0, corruptBoot1.entryCount());
    TEST_ASSERT_EQUAL_UINT8(0, corruptBoot1.readLast(3, out));

    Serial.reset();
    History corruptBoot2;
    TEST_ASSERT_TRUE(corruptBoot2.begin(false));
    TEST_ASSERT_EQUAL_UINT16(0, corruptBoot2.entryCount());
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_HIST_FS_REASON_REBUILD) == std::string::npos);

    // 4. [M-7b] Serial export discards overlong >= LINE_BUF lines without emitting truncated data
    Preferences::resetStorage();
    storage.begin();
    LittleFS.reset();
    std::string overlong(160, 'Z');
    LittleFS.files[HISTORY_FILE] =
        "2026-09-10T06:00,A,10,0,0,0\r\n" +
        overlong + "\r\n" +
        "2026-09-11T06:00,M,15,0,0,0\r\n";
    History expHist;
    TEST_ASSERT_TRUE(expHist.begin(false));
    Serial.reset();
    TEST_ASSERT_TRUE(expHist.startExport());
    int safety = 100;
    while (expHist.isExporting() && safety-- > 0) {
        expHist.update();
    }
    TEST_ASSERT_EQUAL_UINT16(2, expHist.exportSent());
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("2026-09-10T06:00,A,10,0,0,0") != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("2026-09-11T06:00,M,15,0,0,0") != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("ZZZZZZZZ") == std::string::npos);

    // 5. [L-8] _populateCache() falls back to full scan when >512 bytes of tail corruption follow valid entries
    Preferences::resetStorage();
    storage.begin();
    LittleFS.reset();
    std::string tailCorrupt =
        "2026-09-01T06:00,A,10,0,0,0\r\n"
        "2026-09-02T06:00,M,12,0,0,0\r\n"
        "2026-09-03T06:00,C,14,0,0,0\r\n";
    for (int i = 0; i < 40; i++) {
        tailCorrupt += "INVALID_TAIL_GARBAGE_ROW\r\n"; // >1000 bytes (> tailSize 512)
    }
    LittleFS.files[HISTORY_FILE] = tailCorrupt;
    History fallbackHist;
    TEST_ASSERT_TRUE(fallbackHist.begin(false));
    TEST_ASSERT_EQUAL_UINT8(3, fallbackHist.readLast(3, out));
    TEST_ASSERT_EQUAL_UINT8(1, out[0].day);
    TEST_ASSERT_EQUAL_UINT8(2, out[1].day);
    TEST_ASSERT_EQUAL_UINT8(3, out[2].day);

    // 6. Rename-only failure during rotation preserves original file and appends pending entry cleanly
    Preferences::resetStorage();
    storage.begin();
    LittleFS.reset();
    gState.rtc_valid = true;
    TEST_ASSERT_TRUE(history.begin(true));
    std::string fullFile;
    fullFile.reserve(HISTORY_MAX_ENTRIES * 32);
    for (int i = 0; i < HISTORY_MAX_ENTRIES; i++) {
        fullFile.append("2026-06-01T08:00,A,10,0,0,0\r\n");
    }
    LittleFS.files[HISTORY_FILE] = std::move(fullFile);
    history._lineCount = HISTORY_MAX_ENTRIES;
    LittleFS.renameFail = true;

    HistoryEntry eRen = { 2026, 9, 20, 9, 0, { 11, 0, 0, 0 }, WaterTrigger::MANUAL, 0 };
    history.record(eRen);
    safety = 500;
    while (history._rotState != History::RotState::IDLE && safety-- > 0) {
        history.update();
    }
    LittleFS.renameFail = false;
    TEST_ASSERT_EQUAL(History::RotState::IDLE, history._rotState);
    TEST_ASSERT_FALSE(LittleFS.exists("/hist_tmp.csv"));
    TEST_ASSERT_FALSE(LittleFS.exists("/hist_pend.txt"));
    TEST_ASSERT_EQUAL_UINT16(HISTORY_MAX_ENTRIES + 1, history.entryCount());
    TEST_ASSERT_EQUAL_UINT8(1, history.readLast(1, out));
    TEST_ASSERT_EQUAL_UINT8(20, out[0].day);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_history_entry_to_line_format);
    RUN_TEST(test_history_line_to_entry_valid);
    RUN_TEST(test_history_line_to_entry_rejects_corrupted);
    RUN_TEST(test_history_record_and_read_last);
    RUN_TEST(test_history_rotation_triggered_at_capacity);
    RUN_TEST(test_history_rejects_missing_columns);
    RUN_TEST(test_history_cache_fifo_eviction);
    RUN_TEST(test_history_deferred_queue_when_busy);
    RUN_TEST(test_history_export_stream);
    RUN_TEST(test_history_guarded_format_on_mount_failure);
    RUN_TEST(test_history_line_to_entry_negative_and_overflow_bounds);
    RUN_TEST(test_history_rotation_write_failure_preserves_original_file);
    RUN_TEST(test_history_corrupted_nvs_cache_rebuilds_from_fs);
    RUN_TEST(test_history_begin_detects_truncated_file_via_filesize);
    RUN_TEST(test_history_pending_record_persisted_and_replayed_across_rotation);
    RUN_TEST(test_history_rejects_test_trigger_and_long_corrupt_line);
    RUN_TEST(test_history_rotation_double_fail_export_suppression_and_guards);
    RUN_TEST(test_history_rotation_read_failure_preserves_original_file);
    RUN_TEST(test_history_periodic_remount_and_force_repair);
    RUN_TEST(test_history_remount_watering_guard_and_clear_edge_cases);
    RUN_TEST(test_history_buffered_filesize_partial_writes_rotation_tail_and_strict_csv);
    RUN_TEST(test_history_partial_write_isolation_pending_dedup_corrupt_settle_export_overflow_and_tail_fallback);
    return UNITY_END();
}
