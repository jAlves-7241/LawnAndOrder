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

    // Simulate disk operation in progress
    history._rotState = History::RotState::COPYING;
    TEST_ASSERT_EQUAL_UINT8(history._defHead, history._defTail);

    HistoryEntry deferred = { 2026, 8, 10, 8, 0, { 15, 0, 0, 0 }, WaterTrigger::AUTO, 0 };
    history.record(deferred);

    // Entry should be held in deferred queue without writing to disk yet
    TEST_ASSERT_NOT_EQUAL(history._defHead, history._defTail);
    TEST_ASSERT_EQUAL_UINT16(0, history.entryCount());

    // Restore state and drain queue (simulating completion)
    history._rotState = History::RotState::IDLE;
    history._defTail = (history._defTail + 1) % 4;
    history.record(deferred);
    TEST_ASSERT_EQUAL_UINT16(1, history.entryCount());
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

int main(int argc, char **argv) {
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
    return UNITY_END();
}
