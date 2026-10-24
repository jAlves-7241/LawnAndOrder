#define HAS_TERMINAL_AWARE_LOG 1
#include <unity.h>
#include "TestCommon.h"
#include "../../test/mocks/TestCommon.cpp"
#include "../../src/AppState.cpp"
#include "../../src/Storage.cpp"
#include "../../src/RTClock.cpp"
#include "../../src/History.cpp"
#include "../../src/WateringController.cpp"
#include "../../src/Scheduler.cpp"

// 1. Include src/UI.h renamed so its #pragma once triggers
#define UI RealUI
#include "UI.h"
#undef UI

// 2. Define lightweight mock UI matching the methods Terminal needs
static bool _ui_went_idle = false;
class UI {
public:
    void goIdle() { _ui_went_idle = true; }
};
UI ui;

#define private public
#include "Terminal.h"
#undef private

#include "../../src/Terminal.cpp"

void setUp(void) {
    resetAllTestMocks();
    _ui_went_idle = false;
    terminal.begin();
    storage.begin();
    history.begin();
    gState.rtc_valid = true;
    Serial.reset();
}

void tearDown(void) {}

void test_terminal_help_command(void) {
    Serial.feedInput("help\r\n");
    terminal.update();

    TEST_ASSERT_TRUE(Serial.outputBuffer.find("help") != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("status") != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("set_time") != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("stop") != std::string::npos);
}

void test_terminal_status_command(void) {
    gState.mode = AppMode::MEDIO;
    gState.now = { 2026, 6, 15, 10, 0, 0, 1, 1781500000UL };

    Serial.feedInput("STATUS\r\n");
    terminal.update();

    TEST_ASSERT_TRUE(Serial.outputBuffer.find("STATUS") != std::string::npos ||
                     Serial.outputBuffer.find("ESTADO") != std::string::npos);
}

void test_terminal_set_time_command(void) {
    Serial.feedInput("set_time 2026-07-20 15:45:00\r\n");
    terminal.update();

    TEST_ASSERT_EQUAL_UINT16(2026, gState.now.year);
    TEST_ASSERT_EQUAL_UINT8(7, gState.now.month);
    TEST_ASSERT_EQUAL_UINT8(20, gState.now.day);
    TEST_ASSERT_EQUAL_UINT8(15, gState.now.hour);
    TEST_ASSERT_EQUAL_UINT8(45, gState.now.min);
    TEST_ASSERT_TRUE(gState.rtc_valid);
}

void test_terminal_clear_history_confirmation_flow(void) {
    HistoryEntry e = { 2026, 6, 1, 8, 0, { 10, 10, 0, 0 }, WaterTrigger::AUTO, 0 };
    history.record(e);
    TEST_ASSERT_EQUAL_UINT16(1, history.entryCount());

    Serial.feedInput("clear_history\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(terminal._pendingClearHistory);
    TEST_ASSERT_EQUAL_UINT16(1, history.entryCount());

    Serial.feedInput("y\r\n");
    terminal.update();
    TEST_ASSERT_FALSE(terminal._pendingClearHistory);
    TEST_ASSERT_EQUAL_UINT16(0, history.entryCount());
    TEST_ASSERT_TRUE(_ui_went_idle);
}

void test_terminal_clear_history_cancellation(void) {
    HistoryEntry e = { 2026, 6, 1, 8, 0, { 10, 10, 0, 0 }, WaterTrigger::AUTO, 0 };
    history.record(e);

    Serial.feedInput("clear_history\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(terminal._pendingClearHistory);

    Serial.feedInput("n\r\n");
    terminal.update();
    TEST_ASSERT_FALSE(terminal._pendingClearHistory);
    TEST_ASSERT_EQUAL_UINT16(1, history.entryCount());
}

void test_terminal_ansi_arrow_keys_filtered(void) {
    Serial.feedInput("s\033[Atatus\r\n");
    terminal.update();

    TEST_ASSERT_TRUE(Serial.outputBuffer.find("STATUS") != std::string::npos ||
                     Serial.outputBuffer.find("ESTADO") != std::string::npos);
}

void test_terminal_buffer_overflow_protection(void) {
    std::string longInput(270, 'x');
    longInput += "\r\n";
    Serial.feedInput(longInput);

    // Terminal reads in 64-byte chunks per update to prevent CPU hogging
    while (Serial.available() > 0) {
        terminal.update();
    }

    TEST_ASSERT_EQUAL_UINT16(0, terminal._bufLen);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("Error") != std::string::npos ||
                     Serial.outputBuffer.find("Erro") != std::string::npos ||
                     Serial.outputBuffer.find("overflow") != std::string::npos ||
                     Serial.outputBuffer.find("too long") != std::string::npos ||
                     Serial.outputBuffer.find("demasiado longo") != std::string::npos);
}

void test_terminal_reboot_command(void) {
    Serial.feedInput("reboot\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(ESP.rebooted);
}

void test_terminal_i2c_scan_command(void) {
    Wire.detectedAddresses.push_back(0x27);
    Wire.detectedAddresses.push_back(0x68);

    Serial.feedInput("i2c_scan\r\n");
    terminal.update();

    TEST_ASSERT_TRUE(Serial.outputBuffer.find("0x27") != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("0x68") != std::string::npos);
}

void test_terminal_export_and_import_config_commands(void) {
    Serial.reset();
    _ui_went_idle = false;

    // Export config
    Serial.feedInput("export_config\r\n");
    terminal.update();

    // Verify hex output present
    char validHex[sizeof(AppConfigBlob) * 2 + 1];
    storage.exportConfigHex(validHex, sizeof(validHex));
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(validHex) != std::string::npos);

    // Import valid config
    Serial.reset();
    std::string importCmd = std::string("import_config ") + validHex + "\r\n";
    Serial.feedInput(importCmd);
    while (Serial.available() > 0) {
        terminal.update();
    }
    TEST_ASSERT_TRUE(_ui_went_idle);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_IMP_OK) != std::string::npos);

    // Import missing argument
    Serial.reset();
    Serial.feedInput("import_config\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_ERR_IMP_FMT) != std::string::npos);

    // Import invalid blob
    Serial.reset();
    Serial.feedInput("import_config 123456\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_ERR_IMP_BLOB) != std::string::npos);
}

void test_terminal_export_history_command(void) {
    history.clear();
    Serial.reset();

    // Export when empty
    Serial.feedInput("export_history\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_HIST_EMPTY) != std::string::npos);

    // Export with record
    HistoryEntry e = { 2026, 6, 1, 8, 0, { 10, 10, 0, 0 }, WaterTrigger::AUTO, 0 };
    history.record(e);

    Serial.reset();
    Serial.feedInput("export_history\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_START_EXPORT) != std::string::npos);

    int safety = 100;
    while (history.isExporting() && safety-- > 0) {
        history.update();
    }
    TEST_ASSERT_FALSE(history.isExporting());
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("2026-06-01T08:00,A,10,10,0,0") != std::string::npos);
}

void test_terminal_unknown_and_case_insensitive_commands(void) {
    Serial.reset();

    // Unknown command
    Serial.feedInput("UNKNOWN_FOOBAR\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("UNKNOWN_FOOBAR") != std::string::npos);

    // Case insensitivity with whitespace: "  HeLp  \r\n"
    Serial.reset();
    Serial.feedInput("  HeLp  \r\n");
    terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("status") != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("set_time") != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("stop") != std::string::npos);

    // Blank line (only CRLF)
    Serial.reset();
    Serial.feedInput("\r\n");
    terminal.update();
    // Must not crash
}

void test_terminal_stop_command(void) {
    Serial.reset();
    Serial.feedInput("stop\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("No") != std::string::npos ||
                     Serial.outputBuffer.find("Nenhuma") != std::string::npos);

    wateringCtrl.begin();
    gState.zones[0].enabled = true;
    gState.zones[0].duration_min = 10;
    wateringCtrl.startGeneral(WaterTrigger::MANUAL);
    TEST_ASSERT_TRUE(wateringCtrl.isActive());
    TEST_ASSERT_TRUE(gState.watering.active);

    Serial.reset();
    Serial.feedInput("stop\r\n");
    terminal.update();
    TEST_ASSERT_FALSE(wateringCtrl.isActive());
    TEST_ASSERT_FALSE(gState.watering.active);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("stopped") != std::string::npos ||
                     Serial.outputBuffer.find("parada") != std::string::npos);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_terminal_help_command);
    RUN_TEST(test_terminal_status_command);
    RUN_TEST(test_terminal_stop_command);
    RUN_TEST(test_terminal_set_time_command);
    RUN_TEST(test_terminal_clear_history_confirmation_flow);
    RUN_TEST(test_terminal_clear_history_cancellation);
    RUN_TEST(test_terminal_ansi_arrow_keys_filtered);
    RUN_TEST(test_terminal_buffer_overflow_protection);
    RUN_TEST(test_terminal_reboot_command);
    RUN_TEST(test_terminal_i2c_scan_command);
    RUN_TEST(test_terminal_export_and_import_config_commands);
    RUN_TEST(test_terminal_export_history_command);
    RUN_TEST(test_terminal_unknown_and_case_insensitive_commands);
    return UNITY_END();
}
