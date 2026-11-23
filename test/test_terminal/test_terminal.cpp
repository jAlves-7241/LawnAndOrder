#define HAS_TERMINAL_AWARE_LOG 1
#include <unity.h>
#include "TestCommon.h"
#include "../../test/mocks/TestCommon.cpp"
#include "../../src/AppState.cpp"
#include "../../src/Storage.cpp"
#include "RTClock.h"
#include "History.h"
#include "Terminal.h"
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
static bool _ui_in_setup = false;
static bool _ui_synced_setup = false;
static bool _ui_refreshed_clock = false;
class UI {
public:
    void goIdle() { _ui_went_idle = true; }
    bool inSetup() const { return _ui_in_setup; }
    void syncSetupState() {
        _ui_synced_setup = true;
        if (gState.setup_done) {
            _ui_in_setup = false;
            goIdle();
        } else {
            _ui_in_setup = true;
        }
    }
    void refreshClockEditors() { _ui_refreshed_clock = true; }
};
UI ui;

#include "../../src/Terminal.cpp"

void setUp(void) {
    resetAllTestMocks();
    _ui_went_idle = false;
    _ui_in_setup = false;
    _ui_synced_setup = false;
    _ui_refreshed_clock = false;
    terminal.begin();
    storage.begin();
    history.begin();
    gState.rtc_valid = true;
    Serial.reset();
}

void tearDown(void) {}

struct UnitTestAccess {
static void test_terminal_help_command(void) {
    Serial.feedInput("help\r\n");
    terminal.update();

    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_WELCOME_HDR) != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("TERMINAL SERVICE") == std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("SERVICO TERMINAL") == std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("TERMINAL COMMANDS") != std::string::npos ||
                     Serial.outputBuffer.find("COMANDOS TERMINAL") != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("help") != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("status") != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("set_time") != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("stop") != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.size() >= 4 &&
                     Serial.outputBuffer.substr(Serial.outputBuffer.size() - 4) == "\r\n\r\n");
}

static void test_terminal_status_command(void) {
    gState.mode = AppMode::MEDIO;
    gState.now = { 2026, 6, 15, 10, 0, 0, 1, 1781500000UL };

    Serial.feedInput("STATUS\r\n");
    terminal.update();

    TEST_ASSERT_TRUE(Serial.outputBuffer.find("STATUS") != std::string::npos ||
                     Serial.outputBuffer.find("ESTADO") != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.size() >= 4 &&
                     Serial.outputBuffer.substr(Serial.outputBuffer.size() - 4) == "\r\n\r\n");
}

static void test_terminal_set_time_command(void) {
    Serial.feedInput("set_time 2026-07-20 15:45:00\r\n");
    terminal.update();

    TEST_ASSERT_EQUAL_UINT16(2026, gState.now.year);
    TEST_ASSERT_EQUAL_UINT8(7, gState.now.month);
    TEST_ASSERT_EQUAL_UINT8(20, gState.now.day);
    TEST_ASSERT_EQUAL_UINT8(15, gState.now.hour);
    TEST_ASSERT_EQUAL_UINT8(45, gState.now.min);
    TEST_ASSERT_TRUE(gState.rtc_valid);
}

static void test_terminal_clear_history_confirmation_flow(void) {
    // Clearing when already empty reports empty immediately without prompting or failing
    history.clear();
    Serial.reset();
    Serial.feedInput("clear_history\r\n");
    terminal.update();
    TEST_ASSERT_FALSE(terminal._pendingClearHistory);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_HIST_EMPTY) != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_WIPE_FAIL) == std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_HIST_NOT_READY) == std::string::npos);

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

static void test_terminal_clear_history_cancellation(void) {
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

static void test_terminal_ansi_arrow_keys_filtered(void) {
    Serial.feedInput("s\033[Atatus\r\n");
    terminal.update();

    TEST_ASSERT_TRUE(Serial.outputBuffer.find("STATUS") != std::string::npos ||
                     Serial.outputBuffer.find("ESTADO") != std::string::npos);
}

static void test_terminal_buffer_overflow_protection(void) {
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

static void test_terminal_reboot_command(void) {
    Serial.feedInput("reboot\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(ESP.rebooted);
}

static void test_terminal_i2c_scan_command(void) {
    Wire.detectedAddresses.push_back(0x27);
    Wire.detectedAddresses.push_back(0x68);

    Serial.feedInput("i2c_scan\r\n");
    terminal.update();

    TEST_ASSERT_TRUE(Serial.outputBuffer.find("0x27") != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("0x68") != std::string::npos);
}

static void test_terminal_export_and_import_config_commands(void) {
    Serial.reset();
    _ui_went_idle = false;
    _ui_in_setup = false;
    _ui_synced_setup = false;
    _ui_refreshed_clock = false;

    gState.setup_done = true;
    // Export config
    Serial.feedInput("export_config\r\n");
    terminal.update();

    // Verify hex output present
    char validHex[sizeof(AppConfigBlob) * 2 + 1];
    storage.exportConfigHex(validHex, sizeof(validHex));
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(validHex) != std::string::npos);

    // Import valid config
    gState.setup_done = false;
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

static void test_terminal_export_history_command(void) {
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

static void test_terminal_unknown_and_case_insensitive_commands(void) {
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

static void test_terminal_stop_command(void) {
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

static void test_terminal_set_time_rejects_trailing_garbage_and_plus_sign(void) {
    rtclock = RTClock();
    gState.rtc_valid = false;

    // 1. Trailing garbage after valid timestamp
    Serial.reset();
    Serial.feedInput("set_time 2026-07-20 15:45:00PM\r\n");
    terminal.update();
    TEST_ASSERT_FALSE(gState.rtc_valid);

    // 2. Leading '+' sign in numeric token
    Serial.reset();
    Serial.feedInput("set_time +2026-07-20 15:45:00\r\n");
    terminal.update();
    TEST_ASSERT_FALSE(gState.rtc_valid);

    // 3. 5-digit year overflow
    Serial.reset();
    Serial.feedInput("set_time 20260-07-20 15:45:00\r\n");
    terminal.update();
    TEST_ASSERT_FALSE(gState.rtc_valid);
}

static void test_terminal_status_truthful_when_rtc_invalid(void) {
    gState.rtc_valid = false;
    gState.mode = AppMode::MEDIO;
    gState.suspended = true;
    gState.suspended_until = 1781510000UL;
    gState.next_hour = 8;
    gState.next_min = 0;

    // 1. Suspended without valid RTC -> prints SUSP_NO_RTC instead of bogus million-hour countdown
    Serial.reset();
    Serial.feedInput("status\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_STAT_SUSP_NO_RTC) != std::string::npos);

    // 2. Active mode without valid RTC (not suspended) -> prints AWAIT_RTC instead of active schedule
    gState.suspended = false;
    gState.suspended_until = 0;
    Serial.reset();
    Serial.feedInput("status\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_STAT_AWAIT_RTC) != std::string::npos);
}

static void test_terminal_reboot_deenergizes_active_relays_preserving_recovery(void) {
    wateringCtrl.begin();
    gState.zones[0].enabled = true;
    gState.zones[0].duration_min = 10;
    wateringCtrl.startGeneral(WaterTrigger::MANUAL);
    advanceMockMillis(60);
    wateringCtrl.update();
    TEST_ASSERT_EQUAL_UINT8(LOW, digitalRead(PIN_RELAY_1));

    Serial.reset();
    Serial.feedInput("reboot\r\n");
    terminal.update();

    TEST_ASSERT_TRUE(ESP.rebooted);
    TEST_ASSERT_EQUAL_UINT8(HIGH, digitalRead(PIN_RELAY_1));

    RecoveryState rs = {};
    TEST_ASSERT_TRUE(storage.loadRecoveryState(rs));
    TEST_ASSERT_TRUE(rs.active);
}

static void test_terminal_pending_clear_expires_and_ctrl_c_clears_overflow(void) {
    HistoryEntry e = { 2026, 6, 1, 8, 0, { 10, 0, 0, 0 }, WaterTrigger::AUTO, 0 };
    history.record(e);
    TEST_ASSERT_EQUAL_UINT16(1, history.entryCount());

    Serial.reset();
    Serial.feedInput("clear_history\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(terminal._pendingClearHistory);

    // Advance >= IDLE_TIMEOUT_MS (30s) then send 'y' -> must expire before processing 'y'
    advanceMockMillis(IDLE_TIMEOUT_MS + 10);
    Serial.reset();
    Serial.feedInput("y\r\n");
    terminal.update();
    TEST_ASSERT_FALSE(terminal._pendingClearHistory);
    TEST_ASSERT_EQUAL_UINT16(1, history.entryCount());
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_WIPE_EXPIRED) != std::string::npos);

    // Trigger buffer overflow, then recover immediately with Ctrl+C (0x03) without Enter
    Serial.reset();
    std::string tooLong(270, 'x');
    Serial.feedInput(tooLong);
    while (Serial.available() > 0) {
        terminal.update();
    }
    TEST_ASSERT_TRUE(terminal._ignore_rest);

    Serial.reset();
    Serial.feedInput("\x03help\r\n");
    terminal.update();
    TEST_ASSERT_FALSE(terminal._ignore_rest);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("status") != std::string::npos);
}

static void test_terminal_import_active_guard_setup_sync_and_set_time_strict(void) {
    // 1. Export a setup_done=false config, then import while inSetup is true
    gState.setup_done = false;
    char hexSetupFalse[sizeof(AppConfigBlob) * 2 + 1] = {};
    storage.exportConfigHex(hexSetupFalse, sizeof(hexSetupFalse));

    gState.setup_done = true;
    char hexSetupTrue[sizeof(AppConfigBlob) * 2 + 1] = {};
    storage.exportConfigHex(hexSetupTrue, sizeof(hexSetupTrue));

    // Active watering blocks CLI import_config
    wateringCtrl.begin();
    gState.zones[0].enabled = true;
    gState.zones[0].duration_min = 5;
    wateringCtrl.startGeneral(WaterTrigger::MANUAL);
    TEST_ASSERT_TRUE(gState.watering.active);

    Serial.reset();
    std::string cmd = std::string("import_config ") + hexSetupTrue + "\r\n";
    Serial.feedInput(cmd);
    while (Serial.available() > 0) terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_ERR_IMP_ACTIVE) != std::string::npos);
    wateringCtrl.stop();

    // Importing valid config calls syncSetupState() and leaves setup if setup_done==true
    _ui_in_setup = true;
    _ui_synced_setup = false;
    _ui_went_idle = false;
    Serial.reset();
    Serial.feedInput(cmd);
    while (Serial.available() > 0) terminal.update();
    TEST_ASSERT_TRUE(_ui_synced_setup);
    TEST_ASSERT_FALSE(_ui_in_setup);
    TEST_ASSERT_TRUE(_ui_went_idle);

    // clear_history while _ui_in_setup == true must NOT kick UI to goIdle()
    HistoryEntry setupEntry = { 2026, 6, 15, 8, 0, { 10, 0, 0, 0 }, WaterTrigger::AUTO, 0 };
    history.record(setupEntry);
    TEST_ASSERT_EQUAL_UINT16(1, history.entryCount());
    _ui_in_setup = true;
    _ui_went_idle = false;
    Serial.feedInput("clear_history\r\ny\r\n");
    while (Serial.available() > 0) terminal.update();
    TEST_ASSERT_EQUAL_UINT16(0, history.entryCount());
    TEST_ASSERT_FALSE(_ui_went_idle);

    // Strict set_time rejects internal spaces and calls refreshClockEditors() on success
    _ui_refreshed_clock = false;
    gState.rtc_valid = false;
    Serial.reset();
    Serial.feedInput("set_time 2026- 7-20 15:45:00\r\n");
    terminal.update();
    TEST_ASSERT_FALSE(gState.rtc_valid);
    TEST_ASSERT_FALSE(_ui_refreshed_clock);

    Serial.reset();
    Serial.feedInput("set_time 2026-07-20 15:45:00\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(gState.rtc_valid);
    TEST_ASSERT_TRUE(_ui_refreshed_clock);
}

static void test_terminal_midline_batch_coalesces_tail_redraw(void) {
    Serial.reset();
    // Feed "ab", move cursor Left twice to index 0, then insert "XYZ" in one batch
    Serial.feedInput("ab\033[D\033[DXYZ");
    terminal.update();

    TEST_ASSERT_EQUAL_UINT16(5, terminal._bufLen);
    terminal._buffer[terminal._bufLen] = '\0';
    TEST_ASSERT_EQUAL_STRING("XYZab", terminal._buffer);

    // Count erase-to-EOL "\033[K" occurrences -> must be coalesced to 1!
    size_t countK = 0;
    size_t pos = 0;
    while ((pos = Serial.outputBuffer.find("\033[K", pos)) != std::string::npos) {
        countK++;
        pos += 3;
    }
    TEST_ASSERT_EQUAL_UINT32(1, countK);
}

static void test_logging_standardized_format_and_seconds_precision(void) {
    // 1. Pre-RTC uptime format: second precision only (no milliseconds)
    gState.rtc_valid = false;
    advanceMockMillis(5123);
    unsigned long upSec = millis() / 1000UL;
    unsigned long h = upSec / 3600UL;
    unsigned long m = (upSec % 3600UL) / 60UL;
    unsigned long s = upSec % 60UL;

    char expectedUptime[192];
    snprintf(expectedUptime, sizeof(expectedUptime), "[%s %02lu:%02lu:%02lu] [%s] [%s] %s",
             TXT_LOG_UPTIME_PREFIX, h, m, s,
             TXT_LOG_LVL_INFO, TXT_LOG_TAG_SYS, TXT_LOG_SYS_READY);

    Serial.reset();
    _logWrite(false, TXT_LOG_LVL_INFO, TXT_LOG_TAG_SYS, "%s", TXT_LOG_SYS_READY);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedUptime) != std::string::npos);
    TEST_ASSERT_EQUAL_PTR(nullptr, strstr(Serial.outputBuffer.c_str(), ".123"));
    TEST_ASSERT_TRUE(Serial.outputBuffer.size() >= 4 &&
                     Serial.outputBuffer.substr(Serial.outputBuffer.size() - 4) == "\r\n\r\n");

    Serial.reset();
    LOG_I(TXT_LOG_TAG_SYS, TXT_LOG_SYS_READY);
#if LOG_LEVEL >= LVL_INFO
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedUptime) != std::string::npos);
#else
    TEST_ASSERT_TRUE(Serial.outputBuffer.empty());
#endif

    // 2. Post-RTC calendar timestamp format: [YYYY-MM-DD HH:MM:SS] [LEVEL] [Component]
    gState.rtc_valid = true;
    gState.now = { 2026, 6, 15, 9, 5, 7, 1, 1781514307UL };
    char expectedRtc[128];
    snprintf(expectedRtc, sizeof(expectedRtc), "[2026-06-15 09:05:07] [%s] [%s]",
             TXT_LOG_LVL_WARN, TXT_LOG_TAG_CLOCK);

    Serial.reset();
    _logWrite(true, TXT_LOG_LVL_WARN, TXT_LOG_TAG_CLOCK, "%s", TXT_LOG_RTC_BAT_LOW);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedRtc) != std::string::npos);

    Serial.reset();
    LOG_W(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_BAT_LOW);
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedRtc) != std::string::npos);
#else
    TEST_ASSERT_TRUE(Serial.outputBuffer.empty());
#endif

    // 3. Long message indicates truncation cleanly with '...' at exact 255-char cap (LOG_FMT_BUF_SIZE - 1)
    std::string longText(300, 'A');
    Serial.reset();
    _logWrite(false, TXT_LOG_LVL_INFO, TXT_LOG_TAG_SYS, "%s", longText.c_str());
    size_t eol = Serial.outputBuffer.find("\r\n");
    TEST_ASSERT_TRUE(eol != std::string::npos);
    TEST_ASSERT_EQUAL_UINT32(LOG_FMT_BUF_SIZE - 1, (uint32_t)eol);
    TEST_ASSERT_EQUAL_STRING("...", Serial.outputBuffer.substr(eol - 3, 3).c_str());
}

static void test_logging_export_suppression_summary_and_human_modes(void) {
    // Human-readable mode names (never raw enum integers)
    TEST_ASSERT_EQUAL_STRING(TXT_TERM_MODE_DISABLED, serialModeName(AppMode::DESATIVADO));
    TEST_ASSERT_EQUAL_STRING(TXT_TERM_MODE_CUSTOM, serialModeName(AppMode::PERSONALIZADO));

    gState.rtc_valid = true;
    gState.now = { 2026, 6, 1, 8, 5, 0, 1, 1780301100UL };
    history.clear();
    Serial.reset();
    HistoryEntry e = { 2026, 6, 1, 8, 0, { 10, 10, 0, 0 }, WaterTrigger::AUTO, 0 };
    history.record(e);
#if LOG_LEVEL >= LVL_INFO
    char expectedZ1[32];
    snprintf(expectedZ1, sizeof(expectedZ1), TXT_SER_ZONE_DUR_FMT, "", 1, 10U);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedZ1) != std::string::npos);
#endif

    Serial.reset();
    history.startExport();
    TEST_ASSERT_TRUE(_log_suspended);

    // Emit logs while CSV export is active: must NOT appear in CSV, but must be counted
    unsigned expectedTotal = 0;
    unsigned expectedWarnErr = 0;
    _logWrite(false, TXT_LOG_LVL_INFO, TXT_LOG_TAG_SYS, "%s", TXT_LOG_SYS_READY);
    expectedTotal++;
    LOG_I(TXT_LOG_TAG_SYS, TXT_LOG_SYS_READY);
#if LOG_LEVEL >= LVL_INFO
    expectedTotal++;
#endif
    LOG_W(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_BAT_LOW);
#if LOG_LEVEL >= LVL_WARN
    expectedTotal++;
    expectedWarnErr++;
#endif
    LOG_E(TXT_LOG_TAG_LCD, TXT_LOG_LCD_REC_FAIL);
#if LOG_LEVEL >= LVL_ERROR
    expectedTotal++;
    expectedWarnErr++;
#endif
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_SYS_READY) == std::string::npos);
    TEST_ASSERT_EQUAL_UINT16(expectedTotal, _log_suppressed_total);
    TEST_ASSERT_EQUAL_UINT16(expectedWarnErr, _log_suppressed_warn_err);

    int safety = 100;
    while (history.isExporting() && safety-- > 0) {
        history.update();
    }
    TEST_ASSERT_FALSE(history.isExporting());
    TEST_ASSERT_FALSE(_log_suspended);
    TEST_ASSERT_EQUAL_UINT16(0, _log_suppressed_total);
    TEST_ASSERT_EQUAL_UINT16(0, _log_suppressed_warn_err);

#if LOG_LEVEL >= LVL_WARN
    char expectedSummary[256];
    snprintf(expectedSummary, sizeof(expectedSummary), TXT_LOG_EXPORT_SUPPRESSED,
             expectedTotal, expectedWarnErr);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedSummary) != std::string::npos);
#endif
}

static void test_terminal_sw_clock_status_pipelined_export_and_wipe_failure(void) {
    // 1. Software clock (rtc_valid == true, mockPresent == false) reports TXT_TERM_STAT_RTC_SW and omits Unix Time
    rtclock._found = false;
    rtclock._rtc.mockPresent = false;
    TEST_ASSERT_FALSE(rtclock.set(2026, 6, 15, 11, 20, 0));
    TEST_ASSERT_TRUE(gState.rtc_valid);

    Serial.reset();
    Serial.feedInput("status\n");
    terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_STAT_RTC_SW) != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_STAT_RTC_OK) == std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("Unix") == std::string::npos);

    // 2. Blocked export_history (while rotation is running or file missing) prints CLI feedback even if LOG_LEVEL==LVL_NONE
    history.clear();
    HistoryEntry e = { 2026, 6, 15, 8, 0, { 10, 0, 0, 0 }, WaterTrigger::AUTO, 0 };
    history.record(e);
    history._rotState = History::RotState::COPYING;
    Serial.reset();
    Serial.feedInput("export_history\n");
    terminal.update();
    TEST_ASSERT_FALSE(history.isExporting());
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_START_EXPORT) == std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_NO_EXPORT_ROT) != std::string::npos);
    history._rotState = History::RotState::IDLE;

    // Missing history file with non-zero cached count prints export-open failure unconditionally
    std::string savedHist = LittleFS.files[HISTORY_FILE];
    LittleFS.remove(HISTORY_FILE);
    Serial.reset();
    Serial.feedInput("export_history\n");
    terminal.update();
    TEST_ASSERT_FALSE(history.isExporting());
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_HIST_EXPORT_FAIL) != std::string::npos);
    LittleFS.files[HISTORY_FILE] = savedHist;

    // 3. Pipelined "export_history\nstatus\n" in one batch drains trailing command so CSV stream stays uncorrupted
    Serial.reset();
    Serial.feedInput("export_history\nstatus\n");
    terminal.update();
    TEST_ASSERT_TRUE(history.isExporting());
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_START_EXPORT) != std::string::npos);

    // Emit an info-only log during export -> footer logs TXT_LOG_EXPORT_SUPPRESSED_INFO
    _logWrite(false, TXT_LOG_LVL_INFO, TXT_LOG_TAG_SYS, "%s", TXT_LOG_SYS_READY);
    int safety = 100;
    while (history.isExporting() && safety-- > 0) {
        history.update();
    }
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("2026-06-15T08:00,A,10,0,0,0") != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_SYS_STATUS_HDR) == std::string::npos);
#if LOG_LEVEL >= LVL_INFO
    char expectedInfoSup[128];
    snprintf(expectedInfoSup, sizeof(expectedInfoSup), TXT_LOG_EXPORT_SUPPRESSED_INFO, 1U);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedInfoSup) != std::string::npos);
#endif

    // 4. clear_history without force when History is not ready prints TXT_TERM_HIST_UNAVAIL immediately without prompting
    history._ready = false;
    Serial.reset();
    Serial.feedInput("clear_history\n");
    terminal.update();
    TEST_ASSERT_FALSE(terminal._pendingClearHistory);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_HIST_UNAVAIL) != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_WIPE_OK) == std::string::npos);

    // 5. clear_history force repairs failed LittleFS mount after confirmation
    LittleFS.mountFail = true;
    Serial.reset();
    Serial.feedInput("clear_history force\n");
    terminal.update();
    Serial.feedInput("y\n");
    terminal.update();
    TEST_ASSERT_TRUE(history.isReady());
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_WIPE_OK) != std::string::npos);
}

static void test_terminal_status_reports_and_clears_rtc_battery_warning(void) {
    rtclock = RTClock();
    rtclock.begin();
    rtclock.set(2026, 6, 15, 12, 0, 0);

    // 1. Healthy battery omits RTC battery warning line
    Serial.reset();
    Serial.feedInput("status\n");
    terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_STAT_RTC_OK) != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_STAT_RTC_BAT) == std::string::npos);

    // 2. Mid-run battery failure restored from RAM still shows battery warning in status
    rtclock._rtc.mockLostPower = true;
    advanceMockMillis(31000);
    rtclock.update();
    TEST_ASSERT_TRUE(rtclock.isBatteryLow());
    Serial.reset();
    Serial.feedInput("status\n");
    terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_STAT_RTC_OK) != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_STAT_RTC_BAT) != std::string::npos);

    // 3. set_time clears the warning in status
    Serial.reset();
    Serial.feedInput("set_time 2026-06-15 12:05:00\n");
    terminal.update();
    TEST_ASSERT_FALSE(rtclock.isBatteryLow());
    Serial.reset();
    Serial.feedInput("status\n");
    terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_STAT_RTC_BAT) == std::string::npos);
}

static void test_terminal_clear_history_force_args_timeout_and_empty_repair(void) {
    // 1. Unrecognized arguments are rejected without entering confirmation prompt
    Serial.reset();
    Serial.feedInput("clear_history -f\n");
    terminal.update();
    TEST_ASSERT_FALSE(terminal._pendingClearHistory);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_ERR_CLEAR_HIST_ARGS) != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_WIPE_WARN) == std::string::npos);

    Serial.reset();
    Serial.feedInput("clear_history force extra\n");
    terminal.update();
    TEST_ASSERT_FALSE(terminal._pendingClearHistory);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_ERR_CLEAR_HIST_ARGS) != std::string::npos);

    // 2. "--force" alias sets _pendingClearForce; "n" cancels and clears _pendingClearForce
    LittleFS.mountFail = true;
    history.begin(false);
    TEST_ASSERT_FALSE(history.isReady());

    Serial.reset();
    Serial.feedInput("clear_history --force\n");
    terminal.update();
    TEST_ASSERT_TRUE(terminal._pendingClearHistory);
    TEST_ASSERT_TRUE(terminal._pendingClearForce);
    Serial.feedInput("n\n");
    terminal.update();
    TEST_ASSERT_FALSE(terminal._pendingClearHistory);
    TEST_ASSERT_FALSE(terminal._pendingClearForce);
    TEST_ASSERT_FALSE(history.isReady());
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_CANCELLED) != std::string::npos);

    // 3. 30 s timeout resets both _pendingClearHistory and _pendingClearForce
    Serial.reset();
    Serial.feedInput("clear_history force\n");
    terminal.update();
    TEST_ASSERT_TRUE(terminal._pendingClearHistory);
    TEST_ASSERT_TRUE(terminal._pendingClearForce);
    advanceMockMillis(IDLE_TIMEOUT_MS);
    terminal.update();
    TEST_ASSERT_FALSE(terminal._pendingClearHistory);
    TEST_ASSERT_FALSE(terminal._pendingClearForce);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_WIPE_EXPIRED) != std::string::npos);

    // Subsequent plain clear_history without force rejects unmounted FS upfront without prompting
    Serial.reset();
    Serial.feedInput("clear_history\n");
    terminal.update();
    TEST_ASSERT_FALSE(terminal._pendingClearHistory);
    TEST_ASSERT_FALSE(terminal._pendingClearForce);
    TEST_ASSERT_FALSE(history.isReady());
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_HIST_UNAVAIL) != std::string::npos);

    // 4. "--force" repairs mount and also succeeds when mounted FS is already empty
    LittleFS.mountFail = false;
    LittleFS.reset();
    history.begin();
    TEST_ASSERT_TRUE(history.isReady());
    TEST_ASSERT_EQUAL_UINT16(0, history.entryCount());
    Serial.reset();
    Serial.feedInput("clear_history --force\n");
    terminal.update();
    TEST_ASSERT_TRUE(terminal._pendingClearHistory);
    Serial.feedInput("y\n");
    terminal.update();
    TEST_ASSERT_TRUE(history.isReady());
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_WIPE_OK) != std::string::npos);

    // 5. Plain clear_history removes a corrupt-only /history.csv even when entryCount() settled to 0
    LittleFS.files[HISTORY_FILE] = "CORRUPT_ONLY_LINE\r\n";
    history = History();
    TEST_ASSERT_TRUE(history.begin());
    TEST_ASSERT_EQUAL_UINT16(0, history.entryCount());
    TEST_ASSERT_TRUE(history.hasFileData());
    Serial.reset();
    Serial.feedInput("clear_history\n");
    terminal.update();
    TEST_ASSERT_TRUE(terminal._pendingClearHistory);
    Serial.feedInput("y\n");
    terminal.update();
    TEST_ASSERT_FALSE(history.hasFileData());
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_WIPE_OK) != std::string::npos);
}

static void test_terminal_phase4_export_unmounted_i2c_guard_reboot_setup_and_status_date(void) {
    // 1. export_history on unmounted FS reports unavailable (not "History is empty.")
    HistoryEntry seed = { 2026, 6, 15, 8, 0, { 5, 0, 0, 0 }, WaterTrigger::MANUAL, 0 };
    history.record(seed);
    LittleFS.mountFail = true;
    history.begin();
    TEST_ASSERT_FALSE(history.isReady());
    Serial.reset();
    Serial.feedInput("export_history\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_HIST_UNAVAIL) != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_HIST_EMPTY) == std::string::npos);
#if LOG_LEVEL >= LVL_ERROR
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_HIST_NOT_READY) != std::string::npos);
#endif
    LittleFS.mountFail = false;
    history.begin();

    // 2. i2c_scan while watering is active is rejected cleanly
    gState.watering.active = true;
    Wire.detectedAddresses.push_back(0x27);
    Serial.reset();
    Serial.feedInput("i2c_scan\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_ERR_I2C_ACTIVE) != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("0x27") == std::string::npos);
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_I2C_SCAN_ACTIVE) != std::string::npos);
#endif
    gState.watering.active = false;

    // 3. reboot during Setup Wizard does NOT call ui.goIdle()
    _ui_in_setup = true;
    _ui_went_idle = false;
    ESP.reset();
    Serial.reset();
    Serial.feedInput("reboot\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(ESP.rebooted);
    TEST_ASSERT_FALSE(_ui_went_idle);
    _ui_in_setup = false;

    // 4. status prints full YYYY-MM-DD HH:MM for next scheduled watering (multi-day custom mode)
    gState.setup_done = true;
    gState.mode = AppMode::PERSONALIZADO;
    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.day_pattern = DayPattern::EVERY_X_DAYS;
    cs.interval_days = 3;
    cs.slot_count = 1;
    cs.slots[0] = { 7, 30 };
    const uint32_t todayDay = DateTime(2026, 6, 15, 0, 0, 0).unixtime() / 86400UL;
    gState.custom_ref_day = todayDay;
    gState.rtc_valid = true;
    gState.now = { 2026, 6, 15, 10, 0, 0, 1, DateTime(2026, 6, 15, 9, 0, 0).unixtime() };
    Serial.reset();
    Serial.feedInput("status\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("2026-06-18 07:30") != std::string::npos);

    // 4b. status reports setup standby instead of ACTIVE while initial setup wizard is incomplete
    gState.setup_done = false;
    Serial.reset();
    Serial.feedInput("status\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_STAT_SETUP) != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find("2026-06-18 07:30") == std::string::npos);
    gState.setup_done = true;

    // 5. Expired clear_history confirmation appends blank line
    HistoryEntry he = { 2026, 6, 15, 8, 0, { 5, 0, 0, 0 }, WaterTrigger::MANUAL, 0 };
    history.record(he);
    Serial.reset();
    Serial.feedInput("clear_history\r\n");
    terminal.update();
    advanceMockMillis(IDLE_TIMEOUT_MS);
    Serial.reset();
    Serial.feedInput("y\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_WIPE_EXPIRED) != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.size() >= 4 &&
                     Serial.outputBuffer.substr(Serial.outputBuffer.size() - 4) == "\r\n\r\n");
}

static void test_terminal_set_time_reports_effective_dst_gap_and_utc_range_rejection(void) {
    rtclock = RTClock();
    rtclock.begin();
    gState.auto_dst = true;

    // Spring-forward gap at 2026-03-29 01:30 UTC + TIMEZONE_OFFSET shifts forward by +1h
    const DateTime gapLocal(DateTime(2026, 3, 29, 1, 30, 0).unixtime() + (int32_t)TIMEZONE_OFFSET * 3600L);
    const DateTime effLocal(gapLocal.unixtime() + 3600UL);
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "set_time %04u-%02u-%02u %02u:%02u:00\r\n",
             (unsigned)gapLocal.year(), (unsigned)gapLocal.month(), (unsigned)gapLocal.day(),
             (unsigned)gapLocal.hour(), (unsigned)gapLocal.minute());
    char expectedEff[32];
    snprintf(expectedEff, sizeof(expectedEff), "%04u-%02u-%02u %02u:%02u:00",
             (unsigned)effLocal.year(), (unsigned)effLocal.month(), (unsigned)effLocal.day(),
             (unsigned)effLocal.hour(), (unsigned)effLocal.minute());

    Serial.reset();
    Serial.feedInput(cmd);
    terminal.update();
    TEST_ASSERT_EQUAL_UINT8(effLocal.hour(), gState.now.hour);
    TEST_ASSERT_EQUAL_UINT8(effLocal.minute(), gState.now.min);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedEff) != std::string::npos);

#if TIMEZONE_OFFSET != 0
    // Boundary local time whose UTC conversion falls outside [2020..2099] must reject, not claim SW fallback
    _ui_refreshed_clock = false;
    uint32_t revBefore = rtclock.manualRevision();
    Serial.reset();
#if TIMEZONE_OFFSET > 0
    Serial.feedInput("set_time 2020-01-01 00:00:00\r\n");
#else
    Serial.feedInput("set_time 2099-12-31 23:00:00\r\n");
#endif
    terminal.update();
    TEST_ASSERT_EQUAL_UINT32(revBefore, rtclock.manualRevision());
    TEST_ASSERT_FALSE(_ui_refreshed_clock);
    char expectedErr[96];
    snprintf(expectedErr, sizeof(expectedErr), TXT_TERM_ERR_YEAR, DATE_YEAR_MIN, DATE_YEAR_MAX);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedErr) != std::string::npos);
#endif
}

static void test_terminal_import_config_clears_expired_suspension_immediately(void) {
    rtclock = RTClock();
    rtclock.begin();
    TEST_ASSERT_TRUE(rtclock.set(2026, 6, 15, 10, 0, 0));
    const uint32_t nowUnix = gState.now.unix;

    // 1. Export config with an already-expired suspended_until timestamp
    gState.setup_done = true;
    gState.mode = AppMode::MEDIO;
    gState.suspended = true;
    gState.suspended_until = nowUnix - 3600UL;
    char expiredHex[sizeof(AppConfigBlob) * 2 + 1] = {};
    storage.exportConfigHex(expiredHex, sizeof(expiredHex));

    // Import via terminal -> expired suspension must clear immediately in RAM and NVS
    gState.suspended = false;
    gState.suspended_until = 0;
    Serial.reset();
    std::string cmd = std::string("import_config ") + expiredHex + "\r\n";
    Serial.feedInput(cmd);
    while (Serial.available() > 0) terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_IMP_OK) != std::string::npos);
    TEST_ASSERT_FALSE(gState.suspended);
    TEST_ASSERT_EQUAL_UINT32(0UL, gState.suspended_until);

    initAppState();
    TEST_ASSERT_TRUE(storage.load());
    TEST_ASSERT_FALSE(gState.suspended);
    TEST_ASSERT_EQUAL_UINT32(0UL, gState.suspended_until);

    // 2. Future suspended_until must remain active after import
    TEST_ASSERT_TRUE(rtclock.set(2026, 6, 15, 10, 0, 0));
    gState.setup_done = true;
    gState.mode = AppMode::MEDIO;
    gState.suspended = true;
    gState.suspended_until = gState.now.unix + 7200UL;
    char futureHex[sizeof(AppConfigBlob) * 2 + 1] = {};
    storage.exportConfigHex(futureHex, sizeof(futureHex));

    gState.suspended = false;
    gState.suspended_until = 0;
    Serial.reset();
    cmd = std::string("import_config ") + futureHex + "\r\n";
    Serial.feedInput(cmd);
    while (Serial.available() > 0) terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_IMP_OK) != std::string::npos);
    TEST_ASSERT_TRUE(gState.suspended);
    TEST_ASSERT_EQUAL_UINT32(gState.now.unix + 7200UL, gState.suspended_until);
}

static void test_terminal_ansi_control_abort_and_zero_zones_status(void) {
    // 1. Incomplete CSI (ESC [) followed by Enter or Ctrl+C must abort ANSI state and execute control key
    Serial.reset();
    Serial.feedInput("help\033[\r");
    terminal.update();
    TEST_ASSERT_FALSE(terminal._in_ansi);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_WELCOME_HDR) != std::string::npos);

    Serial.reset();
    Serial.feedInput("abc\033[\x03help\r");
    terminal.update();
    TEST_ASSERT_FALSE(terminal._in_ansi);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_WELCOME_HDR) != std::string::npos);

    // 1b. ECMA-48 non-alpha CSI final bytes (0x40..0x7E, e.g. '@' = 0x40) must terminate cleanly without swallowing the next char
    Serial.reset();
    Serial.feedInput("\033[@help\r");
    terminal.update();
    TEST_ASSERT_FALSE(terminal._in_ansi);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_WELCOME_HDR) != std::string::npos);

    // 2. status must report TXT_TERM_STAT_NO_ZONES instead of ACTIVE when all zones are disabled
    gState.rtc_valid = true;
    gState.setup_done = true;
    gState.suspended = false;
    gState.mode = AppMode::MEDIO;
    gState.now = { 2026, 6, 15, 10, 0, 0, 1, DateTime(2026, 6, 15, 9, 0, 0).unixtime() };
    for (int i = 0; i < NUM_ZONES; i++) {
        gState.zones[i].enabled = false;
        gState.zones[i].duration_min = 0;
    }
    Serial.reset();
    Serial.feedInput("status\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_STAT_NO_ZONES) != std::string::npos);
}

static void test_terminal_lone_esc_preserves_next_char(void) {
    // 1. Lone ESC followed immediately by a printable non-CSI/SS3 char must not swallow it
    Serial.reset();
    Serial.feedInput("\033help\r");
    terminal.update();
    TEST_ASSERT_FALSE(terminal._in_ansi);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_WELCOME_HDR) != std::string::npos);

    // 2. Lone ESC followed by >= 50 ms pause before 'O' or '[' expires ANSI state cleanly
    Serial.reset();
    Serial.feedInput("\033");
    terminal.update();
    TEST_ASSERT_TRUE(terminal._in_ansi);
    advanceMockMillis(60);
    Serial.feedInput("Open");
    terminal.update();
    TEST_ASSERT_FALSE(terminal._in_ansi);
    TEST_ASSERT_EQUAL_UINT16(4, terminal._bufLen);
    TEST_ASSERT_EQUAL_MEMORY("Open", terminal._buffer, 4);
    Serial.feedInput("\x03");
    terminal.update();

    // 3. Real SS3 Home (ESC O H) arriving promptly still executes cursor move
    Serial.reset();
    Serial.feedInput("bc\033OHa");
    terminal.update();
    TEST_ASSERT_EQUAL_UINT16(3, terminal._bufLen);
    TEST_ASSERT_EQUAL_MEMORY("abc", terminal._buffer, 3);
}

static void test_terminal_status_reports_config_lost_and_skipped_slot(void) {
    // 1. Trigger a real MANUAL collision at 08:00 via public APIs
    gState.rtc_valid = true;
    gState.setup_done = true;
    gState.mode = AppMode::MEDIO;
    gState.zones[0] = { "Z1", true, 5 };
    gState.zones[1].enabled = false;
    gState.zones[2].enabled = false;
    gState.zones[3].enabled = false;
    rtclock.begin();
    rtclock.set(2026, 6, 15, 7, 59, 0);
    scheduler.begin();
    TEST_ASSERT_TRUE(wateringCtrl.startGeneral(WaterTrigger::MANUAL));
    rtclock.set(2026, 6, 15, 8, 0, 0);
    scheduler.update();
    TEST_ASSERT_TRUE(scheduler.hasSkippedSlot());
    wateringCtrl.stop();

    // 2. Corrupt NVS so storage.configLoadFailed() latches true
    uint8_t garbage[44];
    memset(garbage, 0xEE, sizeof(garbage));
    prefs.putBytes("cfg", garbage, sizeof(garbage));
    prefs.putBytes("cfg_b", garbage, sizeof(garbage));
    TEST_ASSERT_FALSE(storage.load());
    TEST_ASSERT_TRUE(storage.configLoadFailed());

    Serial.reset();
    Serial.feedInput("status\r\n");
    terminal.update();
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_TERM_STAT_CFG_LOST) != std::string::npos);
    char expectedSkip[96];
    snprintf(expectedSkip, sizeof(expectedSkip), TXT_TERM_STAT_SKIPPED, 8, 0);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedSkip) != std::string::npos);
}

};

int main(void) {
    UNITY_BEGIN();
    RUN_UT(test_terminal_help_command);
    RUN_UT(test_terminal_status_command);
    RUN_UT(test_terminal_stop_command);
    RUN_UT(test_terminal_set_time_command);
    RUN_UT(test_terminal_clear_history_confirmation_flow);
    RUN_UT(test_terminal_clear_history_cancellation);
    RUN_UT(test_terminal_ansi_arrow_keys_filtered);
    RUN_UT(test_terminal_buffer_overflow_protection);
    RUN_UT(test_terminal_reboot_command);
    RUN_UT(test_terminal_i2c_scan_command);
    RUN_UT(test_terminal_export_and_import_config_commands);
    RUN_UT(test_terminal_export_history_command);
    RUN_UT(test_terminal_unknown_and_case_insensitive_commands);
    RUN_UT(test_terminal_set_time_rejects_trailing_garbage_and_plus_sign);
    RUN_UT(test_terminal_status_truthful_when_rtc_invalid);
    RUN_UT(test_terminal_reboot_deenergizes_active_relays_preserving_recovery);
    RUN_UT(test_terminal_pending_clear_expires_and_ctrl_c_clears_overflow);
    RUN_UT(test_terminal_import_active_guard_setup_sync_and_set_time_strict);
    RUN_UT(test_terminal_midline_batch_coalesces_tail_redraw);
    RUN_UT(test_logging_standardized_format_and_seconds_precision);
    RUN_UT(test_logging_export_suppression_summary_and_human_modes);
    RUN_UT(test_terminal_sw_clock_status_pipelined_export_and_wipe_failure);
    RUN_UT(test_terminal_status_reports_and_clears_rtc_battery_warning);
    RUN_UT(test_terminal_clear_history_force_args_timeout_and_empty_repair);
    RUN_UT(test_terminal_phase4_export_unmounted_i2c_guard_reboot_setup_and_status_date);
    RUN_UT(test_terminal_set_time_reports_effective_dst_gap_and_utc_range_rejection);
    RUN_UT(test_terminal_import_config_clears_expired_suspension_immediately);
    RUN_UT(test_terminal_ansi_control_abort_and_zero_zones_status);
    RUN_UT(test_terminal_lone_esc_preserves_next_char);
    RUN_UT(test_terminal_status_reports_config_lost_and_skipped_slot);
    return UNITY_END();
}
