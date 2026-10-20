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

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_history_entry_to_line_format);
    RUN_TEST(test_history_line_to_entry_valid);
    RUN_TEST(test_history_line_to_entry_rejects_corrupted);
    RUN_TEST(test_history_record_and_read_last);
    return UNITY_END();
}
