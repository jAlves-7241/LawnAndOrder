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
    Wire.detectedAddresses.clear();
    ESP.rebooted = false;
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

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_terminal_help_command);
    RUN_TEST(test_terminal_status_command);
    RUN_TEST(test_terminal_set_time_command);
    RUN_TEST(test_terminal_clear_history_confirmation_flow);
    RUN_TEST(test_terminal_clear_history_cancellation);
    RUN_TEST(test_terminal_ansi_arrow_keys_filtered);
    RUN_TEST(test_terminal_buffer_overflow_protection);
    RUN_TEST(test_terminal_reboot_command);
    RUN_TEST(test_terminal_i2c_scan_command);
    return UNITY_END();
}
