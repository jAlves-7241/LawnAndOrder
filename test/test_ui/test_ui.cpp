#include <unity.h>
#include <new>
#include "TestCommon.h"
#include "../../test/mocks/TestCommon.cpp"
#include "../../src/AppState.cpp"
#include "../../src/Storage.cpp"

#include "RTClock.h"
#include "Display.h"
#include "Encoder.h"
#include "WateringController.h"
#include "Scheduler.h"
#include "UI.h"

#include "../../src/RTClock.cpp"
#include "../../src/History.cpp"
#include "../../src/WateringController.cpp"
#include "../../src/Scheduler.cpp"
#include "../../src/Display.cpp"
#include "../../src/Encoder.cpp"
#include "../../src/ui/MenuBuilder.cpp"
#include "../../src/ui/ScreenCommon.cpp"
#include "../../src/ui/ScreenEditors.cpp"
#include "../../src/ui/ScreenIdle.cpp"
#include "../../src/ui/ScreenMenu.cpp"
#include "../../src/ui/ScreenSetup.cpp"
#include "../../src/UI.cpp"

static Display testDisp;
static Encoder testEnc(PIN_CLK, PIN_DT, PIN_SW);
static UI      testUi(testDisp, testEnc);

struct UnitTestAccess {
static void simulateClick(UI& u) {
    digitalWrite(PIN_SW, LOW);
    u.update();
    advanceMockMillis(60);
    u.update();
    digitalWrite(PIN_SW, HIGH);
    u.update();
    advanceMockMillis(60);
    u.update();
}

static void simulateRotate(UI& u, int8_t steps) {
    testEnc._delta = steps;
    u.update();
}

static void test_display_delta_rows_and_nullptr_preservation_across_refresh(void) {
    testDisp.setRows("Row0", "Row1", "Row2", "Row3");
    TEST_ASSERT_EQUAL_STRING("Row0                ", testDisp._lcd.getRow(0));
    TEST_ASSERT_EQUAL_STRING("Row1                ", testDisp._lcd.getRow(1));
    TEST_ASSERT_EQUAL_STRING("Row2                ", testDisp._lcd.getRow(2));
    TEST_ASSERT_EQUAL_STRING("Row3                ", testDisp._lcd.getRow(3));

    // Identical content must perform zero LCD print calls
    testDisp._lcd.printCalls = 0;
    testDisp.setRows("Row0", "Row1", "Row2", "Row3");
    TEST_ASSERT_EQUAL_UINT32(0, testDisp._lcd.printCalls);

    // Partial update with nullptr rows updates only the changed row
    testDisp.setRows(nullptr, "Changed1", nullptr, nullptr);
    TEST_ASSERT_EQUAL_UINT32(1, testDisp._lcd.printCalls);
    TEST_ASSERT_EQUAL_STRING("Changed1            ", testDisp._lcd.getRow(1));
    TEST_ASSERT_EQUAL_STRING("Row2                ", testDisp._lcd.getRow(2));

    // Hardware glitch + recover() followed by nullptr rows must restore shadow (no 0xFF bytes!)
    testDisp._lcd.clear();
    testDisp.recover();
    testDisp.setRows("New0", nullptr, nullptr, nullptr);
    TEST_ASSERT_EQUAL_STRING("New0                ", testDisp._lcd.getRow(0));
    TEST_ASSERT_EQUAL_STRING("Changed1            ", testDisp._lcd.getRow(1));
    TEST_ASSERT_EQUAL_STRING("Row2                ", testDisp._lcd.getRow(2));
    TEST_ASSERT_EQUAL_STRING("Row3                ", testDisp._lcd.getRow(3));

    // 60s anti-corruption refresh with nullptr rows must also preserve all rows cleanly
    advanceMockMillis(61000);
    testDisp.setRows(nullptr, nullptr, nullptr, "Tick3");
    for (uint8_t r = 0; r < LCD_ROWS; r++) {
        const char* rowStr = testDisp._lcd.getRow(r);
        for (size_t c = 0; c < LCD_COLS; c++) {
            TEST_ASSERT_NOT_EQUAL((char)0xFF, rowStr[c]);
        }
    }
    TEST_ASSERT_EQUAL_STRING("Tick3               ", testDisp._lcd.getRow(3));
}

static void test_display_formatting_helpers_and_null_safety(void) {
    char buf[21] = {};

    TEST_ASSERT_EQUAL_UINT32(20, strlen(Display::fx(buf, "Hello")));
    TEST_ASSERT_EQUAL_STRING("Hello               ", buf);

    TEST_ASSERT_EQUAL_UINT32(20, strlen(Display::fx(buf, nullptr)));
    TEST_ASSERT_EQUAL_STRING("                    ", buf);

    TEST_ASSERT_EQUAL_UINT32(20, strlen(Display::cx(buf, "ABCD")));
    TEST_ASSERT_EQUAL_STRING("        ABCD        ", buf);

    TEST_ASSERT_EQUAL_UINT32(20, strlen(Display::hdr(buf, "MENU")));
    TEST_ASSERT_EQUAL_CHAR('~', buf[0]);
    TEST_ASSERT_EQUAL_CHAR('~', buf[19]);

    TEST_ASSERT_EQUAL_UINT32(20, strlen(Display::pbar(buf, 50)));
    TEST_ASSERT_TRUE(strstr(buf, " 50%") != nullptr);

    // Clamp > 100% safely
    TEST_ASSERT_EQUAL_UINT32(20, strlen(Display::pbar(buf, 250)));
    TEST_ASSERT_TRUE(strstr(buf, "100%") != nullptr);
}

static void test_menubuilder_dynamic_stop_watering_and_live_menu_refresh(void) {
    gState.setup_done = true;
    gState.rtc_valid = true;
    testUi.begin();

    // Start manual watering and open MAIN menu -> item 0 must offer stop_now
    gState.zones[0].enabled = true;
    gState.zones[0].duration_min = 10;
    wateringCtrl.startGeneral(WaterTrigger::MANUAL);
    testUi.goMenu(MenuID::MAIN);

    TEST_ASSERT_TRUE(strstr(testUi.getScreenMenu()._items[0].action, "stop_now") != nullptr);

    // Watering stops externally while user stays in MAIN menu -> live update removes stale Stop item
    wateringCtrl.stop();
    testUi.update();
    TEST_ASSERT_TRUE(strstr(testUi.getScreenMenu()._items[0].action, "stop_now") == nullptr);
}

static void test_menubuilder_history_localization_and_dynamic_test_duration(void) {
    gState.rtc_valid = true;
    history.clear();
    strncpy(gState.zones[0].name, "Front|Lawn", sizeof(gState.zones[0].name));
    gState.zones[0].name[sizeof(gState.zones[0].name) - 1] = '\0';

    HistoryEntry e = { 2026, 6, 15, 8, 30, { 10, 0, 0, 0 }, WaterTrigger::MANUAL, 0 };
    history.record(e);

    MenuItem items[MenuBuilder::MAX_ITEMS] = {};
    uint8_t count = 0;
    MenuBuilder::build(MenuID::HISTORICO, items, count);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(2, count);
    TEST_ASSERT_TRUE(strstr(items[0].label, TXT_HIST_TRIG_MANUAL) != nullptr);

    // TESTES menu must format ZONE_TEST_DURATION_S dynamically and sanitize '|' in zone names
    MenuBuilder::build(MenuID::TESTES, items, count);
    char expectedDur[16];
    snprintf(expectedDur, sizeof(expectedDur), "%ds", (int)ZONE_TEST_DURATION_S);
    TEST_ASSERT_TRUE(strstr(items[0].label, expectedDur) != nullptr);
    TEST_ASSERT_TRUE(strstr(items[0].action, expectedDur) != nullptr);
    TEST_ASSERT_TRUE(strstr(items[1].action, "Front/Lawn") != nullptr);
}

static void test_ui_setup_wizard_skip_mode_advances_to_zones_not_complete(void) {
    gState.setup_done = false;
    rtclock.begin();
    rtclock.set(2026, 6, 15, 10, 0, 0);

    testUi.begin();
    TEST_ASSERT_TRUE(testUi.inSetup());
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenSetupWelcome(), testUi._currentScreen);

    // 1. Welcome -> Date editor
    simulateClick(testUi);
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenDateEdit(), testUi._currentScreen);

    // 2. Date editor: Day -> Month -> Year -> Time editor
    simulateClick(testUi);
    simulateClick(testUi);
    simulateClick(testUi);
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenTimeEdit(), testUi._currentScreen);

    // 3. Time editor: Hour -> Minute -> SETUP_MODE
    simulateClick(testUi);
    simulateClick(testUi);
    TEST_ASSERT_EQUAL(SetupStep::MODE_SELECT, testUi.getSetupStep());
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenMenu(), testUi._currentScreen);
    TEST_ASSERT_EQUAL(MenuID::SETUP_MODE, testUi.getScreenMenu()._mid);

    // 4. Selecting "Skip >" (setup_advance) on SETUP_MODE MUST advance to ZONE_CONFIG, NOT finish setup!
    testUi.dispatchAction("setup_advance");
    TEST_ASSERT_FALSE(gState.setup_done);
    TEST_ASSERT_TRUE(testUi.inSetup());
    TEST_ASSERT_EQUAL(SetupStep::ZONE_CONFIG, testUi.getSetupStep());
    TEST_ASSERT_EQUAL(MenuID::SETUP_ZONES, testUi.getScreenMenu()._mid);

    // 5. Selecting "Save & Finish" (setup_advance) on SETUP_ZONES shows Setup Complete screen, click enters Idle
    testUi.dispatchAction("setup_advance");
    TEST_ASSERT_EQUAL(SetupStep::COMPLETE, testUi.getSetupStep());
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenSetupWelcome(), testUi._currentScreen);
    simulateClick(testUi);
    TEST_ASSERT_TRUE(gState.setup_done);
    TEST_ASSERT_FALSE(testUi.inSetup());
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenIdle(), testUi._currentScreen);
}

static void test_ui_setup_missing_rtc_skip_clock_back_and_ok_flow(void) {
    rtclock._rtc.mockPresent = false;
    gState.rtc_valid = false;
    gState.setup_done = false;

    testUi.begin();
    simulateClick(testUi); // Welcome -> DateEdit
    simulateClick(testUi); // Year
    simulateClick(testUi); // Month
    simulateClick(testUi); // Day -> TimeEdit
    simulateClick(testUi); // Hour
    simulateClick(testUi); // Minute -> RTC missing -> Confirm(skip_clock)

    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenConfirm(), testUi._currentScreen);
    TEST_ASSERT_EQUAL_STRING("skip_clock", testUi.getScreenConfirm()._tag);

    // Default selection is >Back (_selectionOk == false): clicking must return to DateEdit!
    TEST_ASSERT_FALSE(testUi.getScreenConfirm()._selectionOk);
    simulateClick(testUi);
    TEST_ASSERT_EQUAL(SetupStep::DATE_TIME, testUi.getSetupStep());
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenDateEdit(), testUi._currentScreen);

    // Advance again to Confirm(skip_clock), verify 350ms bounce guard, then rotate to >OK and click -> SETUP_MODE
    simulateClick(testUi);
    simulateClick(testUi);
    simulateClick(testUi);
    simulateClick(testUi);
    simulateClick(testUi);
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenConfirm(), testUi._currentScreen);
    advanceMockMillis(100);
    simulateRotate(testUi, 1);
    TEST_ASSERT_FALSE(testUi.getScreenConfirm()._selectionOk);
    advanceMockMillis(300);
    simulateRotate(testUi, 1);
    TEST_ASSERT_TRUE(testUi.getScreenConfirm()._selectionOk);
    simulateClick(testUi);

    TEST_ASSERT_EQUAL(SetupStep::MODE_SELECT, testUi.getSetupStep());
    TEST_ASSERT_EQUAL(MenuID::SETUP_MODE, testUi.getScreenMenu()._mid);
}

static void test_ui_info_and_done_post_enter_rotation_bounce_guard(void) {
    gState.setup_done = true;
    testUi.begin();

    testUi.getScreenInfo().setup("H", "L1", "L2", "L3", MenuID::MAIN);
    testUi.changeScreen(&testUi.getScreenInfo());
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenInfo(), testUi._currentScreen);

    // Mechanical rotation bounce 100ms after enter (< 350ms) must NOT dismiss ScreenInfo
    advanceMockMillis(100);
    simulateRotate(testUi, 1);
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenInfo(), testUi._currentScreen);

    // Deliberate rotation after 400ms (>= 350ms) dismisses ScreenInfo back to MenuID::MAIN
    advanceMockMillis(300);
    simulateRotate(testUi, 1);
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenMenu(), testUi._currentScreen);
    TEST_ASSERT_EQUAL(MenuID::MAIN, testUi.getScreenMenu()._mid);
}

static void test_ui_actions_dst_refresh_custom_respace_zero_dur_and_reset_safety(void) {
    gState.setup_done = true;
    gState.auto_dst = true;
    rtclock.begin();
    rtclock.set(2026, 7, 15, 11, 0, 0); // Summer 11:00 local (10:00 UTC)
    testUi.begin();

    // 1. toggle_dst immediately refreshes gState.now.hour without waiting 30s
    testUi.dispatchAction("toggle_dst");
    TEST_ASSERT_FALSE(gState.auto_dst);
    TEST_ASSERT_EQUAL_UINT8(10, gState.now.hour);

    // 2. sel:4 (PERSONALIZADO) auto-respaces overlapping custom slots
    for (int i = 0; i < NUM_ZONES; i++) {
        gState.zones[i].enabled = true;
        gState.zones[i].duration_min = 20; // 81-min cycle
    }
    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.slot_count = 2;
    cs.slots[0] = { 8, 0 };
    cs.slots[1] = { 8, 15 };
    testUi.dispatchAction("sel:4");
    TEST_ASSERT_EQUAL(AppMode::PERSONALIZADO, gState.mode);
    uint16_t m0 = cs.slots[0].hour * 60 + cs.slots[0].minute;
    uint16_t m1 = cs.slots[1].hour * 60 + cs.slots[1].minute;
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(81, m1 - m0);

    // 3. "general" when enabled=true but duration_min=0 must show Info warning, NOT start watering
    for (int i = 0; i < NUM_ZONES; i++) {
        gState.zones[i].enabled = true;
        gState.zones[i].duration_min = 0;
    }
    TEST_ASSERT_TRUE(testUi.executeConfirmed("general"));
    TEST_ASSERT_FALSE(wateringCtrl.isActive());
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenInfo(), testUi._currentScreen);

    // 4. "reset" while watering is active must stop relays before rebooting
    gState.zones[0].enabled = true;
    gState.zones[0].duration_min = 10;
    wateringCtrl.startGeneral(WaterTrigger::MANUAL);
    advanceMockMillis(60);
    wateringCtrl.update();
    TEST_ASSERT_EQUAL_UINT8(LOW, digitalRead(PIN_RELAY_1));

    testUi.executeConfirmed("reset");
    TEST_ASSERT_FALSE(wateringCtrl.isActive());
    TEST_ASSERT_EQUAL_UINT8(HIGH, digitalRead(PIN_RELAY_1));
    TEST_ASSERT_TRUE(ESP.rebooted);
}

static void test_ui_idle_screen_waiting_between_zones_and_backlight_never(void) {
    gState.setup_done = true;
    gState.rtc_valid = true;
    gState.zones[0] = { "Zone 1", true, 1 };
    gState.zones[1] = { "Zone 2", true, 2 };
    gState.zones[2].enabled = false;
    gState.zones[3].enabled = false;
    testUi.begin();
    testUi.goIdle();

    // Start watering, finish Zone 0 -> inter-zone pause renders TXT_IDLE_WAITING on LCD row 2
    wateringCtrl.startGeneral(WaterTrigger::MANUAL);
    advanceMockMillis(60);
    wateringCtrl.update();
    advanceMockMillis(60000);
    wateringCtrl.update();
    TEST_ASSERT_TRUE(gState.watering.is_waiting);

    testUi.update();
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(1), TXT_IDLE_WAITING) != nullptr);
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(2), "Z2 Zone 2") != nullptr);

    // Backlight NEVER mode must still wake display if display was turned off
    gState.backlight_timeout_ms = BACKLIGHT_TIMEOUT_NEVER;
    testDisp.backlightOff();
    testDisp.displayOff();
    TEST_ASSERT_FALSE(testDisp.isBacklightOn());
    simulateRotate(testUi, 1);
    TEST_ASSERT_TRUE(testDisp.isBacklightOn());
    TEST_ASSERT_TRUE(testDisp.isDisplayOn());
}

inline static uint8_t _i2cRecoverCallbackCalls = 0;
static void _mockI2cRecoverCb() {
    _i2cRecoverCallbackCalls++;
}

static void test_display_i2c_fault_abort_cooldown_and_recovery_callback(void) {
    _i2cRecoverCallbackCalls = 0;
    testDisp.setErrorCallback(_mockI2cRecoverCb);

    testDisp._lcd.mockI2cError = 1;
    // Trigger 3 consecutive failed frames -> error callback fires once at 3rd error
    Serial.reset();
    for (int i = 0; i < 3; i++) {
        testDisp.setRows("A", "B", "C", "D");
    }
    TEST_ASSERT_TRUE(testDisp.hasBusFault());
    TEST_ASSERT_EQUAL_UINT8(1, _i2cRecoverCallbackCalls);
    TEST_ASSERT_TRUE(testDisp._commErrWarned);
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_LCD_COMM_ERR) != std::string::npos);
#endif

    // Additional failures inside 10s cooldown (even >= 3 frames) must NOT re-trigger callback or increment _consecErrors
    advanceMockMillis(2000);
    for (int i = 0; i < 3; i++) {
        testDisp.setRows("E", "F", "G", "H");
    }
    TEST_ASSERT_EQUAL_UINT8(1, _i2cRecoverCallbackCalls);
    TEST_ASSERT_EQUAL_UINT8(0, testDisp._consecErrors);

    // After 10s cooldown, 3 fresh failed frames are required before callback triggers again; warning stays deduplicated
    advanceMockMillis(10000);
    Serial.reset();
    testDisp.setRows("I", "J", "K", "L");
    TEST_ASSERT_EQUAL_UINT8(1, _i2cRecoverCallbackCalls);
    testDisp.setRows("I", "J", "K", "L");
    TEST_ASSERT_EQUAL_UINT8(1, _i2cRecoverCallbackCalls);
    testDisp.setRows("I", "J", "K", "L");
    TEST_ASSERT_EQUAL_UINT8(2, _i2cRecoverCallbackCalls);
    TEST_ASSERT_TRUE(testDisp.hasBusFault());
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_LCD_COMM_ERR) == std::string::npos);

    // Wake/sleep helpers must clear stale _lcd._i2cErr once bus recovers
    testDisp._lcd.mockI2cError = 0;
    testDisp.displayOff();
    TEST_ASSERT_FALSE(testDisp.isDisplayOn());
    TEST_ASSERT_FALSE(testDisp.hasBusFault());
    testDisp.displayOn();
    TEST_ASSERT_TRUE(testDisp.isDisplayOn());
    testDisp.backlightOff();
    TEST_ASSERT_FALSE(testDisp.isBacklightOn());
    testDisp.backlightOn();
    TEST_ASSERT_TRUE(testDisp.isBacklightOn());

    testDisp.setRows("Row0", "Row1", "Row2", "Row3");
    TEST_ASSERT_FALSE(testDisp.hasBusFault());
    TEST_ASSERT_FALSE(testDisp._commErrWarned);
    TEST_ASSERT_EQUAL_STRING("Row0                ", testDisp._lcd.getRow(0));
}

static void test_ui_four_cycle_horarios_and_zero_dur_overlap_guard(void) {
    gState.setup_done = true;
    gState.mode = AppMode::PERSONALIZADO;
    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.slot_count = 4;
    cs.interval_days = 3;
    cs.slots[0] = { 6, 0 };
    cs.slots[1] = { 12, 0 };
    cs.slots[2] = { 18, 0 };
    cs.slots[3] = { 22, 0 };

    testUi.begin();
    testUi.dispatchAction("horarios");
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenInfo(), testUi._currentScreen);
    TEST_ASSERT_EQUAL_STRING("H:06:00 12:00 18:00 ", testUi.getScreenInfo()._rows[2]);
    TEST_ASSERT_EQUAL_STRING("  22:00 (3d)        ", testUi.getScreenInfo()._rows[3]);
    TEST_ASSERT_EQUAL_STRING("H:06:00 12:00 18:00 ", testDisp._lcd.getRow(2));
    TEST_ASSERT_EQUAL_STRING("  22:00 (3d)        ", testDisp._lcd.getRow(3));

    // Zero total zone duration (all zones disabled) must still prevent identical timestamps
    for (int i = 0; i < NUM_ZONES; i++) {
        gState.zones[i].enabled = false;
        gState.zones[i].duration_min = 0;
    }
    cs.slot_count = 2;
    cs.slots[0] = { 8, 0 };
    cs.slots[1] = { 8, 0 };
    testUi.dispatchAction("sel:4");
    TEST_ASSERT_TRUE(cs.slots[0].hour != cs.slots[1].hour || cs.slots[0].minute != cs.slots[1].minute);
}

static void test_ui_clock_editor_refresh_and_freq_days_without_rtc(void) {
    gState.setup_done = true;
    rtclock.begin();
    rtclock.set(2026, 6, 15, 10, 0, 0);
    testUi.begin();

    // Open ScreenDateEdit, update RTC externally, call refreshClockEditors()
    testUi.getScreenDateEdit().setup(MenuID::DEF);
    testUi.changeScreen(&testUi.getScreenDateEdit());
    TEST_ASSERT_EQUAL_UINT8(15, testUi.getScreenDateEdit()._deDay);

    rtclock.set(2027, 8, 22, 14, 30, 0);
    testUi.refreshClockEditors();
    TEST_ASSERT_EQUAL_UINT16(2027, testUi.getScreenDateEdit()._deYear);
    TEST_ASSERT_EQUAL_UINT8(8, testUi.getScreenDateEdit()._deMonth);
    TEST_ASSERT_EQUAL_UINT8(22, testUi.getScreenDateEdit()._deDay);

    // Open ScreenTimeEdit(RTC), update RTC externally, call refreshClockEditors()
    testUi.getScreenTimeEdit().setup(TimeEditContext::RTC, 0, MenuID::DEF);
    testUi.changeScreen(&testUi.getScreenTimeEdit());
    rtclock.set(2028, 3, 5, 19, 45, 0);
    testUi.refreshClockEditors();
    TEST_ASSERT_EQUAL_UINT8(19, testUi.getScreenTimeEdit()._teHour);
    TEST_ASSERT_EQUAL_UINT8(45, testUi.getScreenTimeEdit()._teMin);
    TEST_ASSERT_EQUAL_UINT16(2028, testUi.getDateEditYear());

    // Saving FREQ_DAYS without valid RTC keeps custom_ref_day at 0xFFFFFFFFUL
    gState.rtc_valid = false;
    gState.custom_ref_day = 12345UL;
    testUi.getScreenDurPick().setup(3, DurContext::FREQ_DAYS, 0, MenuID::CFG_CUSTOM);
    testUi.changeScreen(&testUi.getScreenDurPick());
    simulateClick(testUi);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFUL, gState.custom_ref_day);
}

static void test_ui_settings_logs_unknown_confirm_and_missing_rtc_warning(void) {
    gState.setup_done = true;
    rtclock.begin();
    rtclock.set(2026, 6, 15, 10, 0, 0);
    testUi.begin();

    // 1. Backlight timeout actions: seconds, always-on, and invalid format guard
    Serial.reset();
    testUi.dispatchAction("bl:30000");
    TEST_ASSERT_EQUAL_UINT32(30000UL, gState.backlight_timeout_ms);
#if LOG_LEVEL >= LVL_INFO
    char expectedBlSec[96];
    snprintf(expectedBlSec, sizeof(expectedBlSec), TXT_LOG_UI_BL_SEC, 30UL);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedBlSec) != std::string::npos);
#endif

    Serial.reset();
    testUi.dispatchAction("bl:4294967295");
    TEST_ASSERT_EQUAL_UINT32(BACKLIGHT_TIMEOUT_NEVER, gState.backlight_timeout_ms);
#if LOG_LEVEL >= LVL_INFO
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_UI_BL_NEVER) != std::string::npos);
#endif

    Serial.reset();
    testUi.dispatchAction("bl:invalid");
#if LOG_LEVEL >= LVL_WARN
    char expectedBadBl[96];
    snprintf(expectedBadBl, sizeof(expectedBadBl), TXT_LOG_UI_UNKNOWN_ACTION, "bl:invalid");
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedBadBl) != std::string::npos);
#endif

    // 2. Editor save logs: CFG_ZONE, FREQ_DAYS, NUM_CYCLES, CUSTOM_CYCLE (quiet scheduler refresh)
    Serial.reset();
    testUi.getScreenDurPick().setup(12, DurContext::CFG_ZONE, 0, MenuID::CFG_ZONAS);
    testUi.changeScreen(&testUi.getScreenDurPick());
    simulateClick(testUi);
#if LOG_LEVEL >= LVL_INFO
    char expectedZoneSet[96];
    snprintf(expectedZoneSet, sizeof(expectedZoneSet), TXT_LOG_UI_ZONE_SET,
             1, gState.zones[0].name, TXT_SER_ENABLED, 12U);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedZoneSet) != std::string::npos);
#endif

    Serial.reset();
    testUi.getScreenDurPick().setup(4, DurContext::FREQ_DAYS, 0, MenuID::CFG_CUSTOM);
    testUi.changeScreen(&testUi.getScreenDurPick());
    simulateClick(testUi);
#if LOG_LEVEL >= LVL_INFO
    char expectedFreq[96];
    snprintf(expectedFreq, sizeof(expectedFreq), TXT_LOG_UI_INTERVAL_SET, 4U);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedFreq) != std::string::npos);
#endif

    Serial.reset();
    testUi.getScreenDurPick().setup(2, DurContext::NUM_CYCLES, 0, MenuID::CFG_CUSTOM);
    testUi.changeScreen(&testUi.getScreenDurPick());
    simulateClick(testUi);
#if LOG_LEVEL >= LVL_INFO
    char expectedCycles[96];
    snprintf(expectedCycles, sizeof(expectedCycles), TXT_LOG_UI_CYCLES_SET, 2U);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedCycles) != std::string::npos);
#endif

    Serial.reset();
    testUi.getScreenTimeEdit().setup(TimeEditContext::CUSTOM_CYCLE, 0, MenuID::CFG_CUSTOM);
    testUi.changeScreen(&testUi.getScreenTimeEdit());
    simulateClick(testUi); // hours -> minutes
    simulateClick(testUi); // save slot
#if LOG_LEVEL >= LVL_INFO
    char expectedSlot[96];
    snprintf(expectedSlot, sizeof(expectedSlot), TXT_LOG_UI_SLOT_SET,
             1, (unsigned)MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO].slots[0].hour,
             (unsigned)MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO].slots[0].minute);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedSlot) != std::string::npos);
#endif

    // 3. Confirming "general" with all zones disabled logs TXT_LOG_START_NO_ZONES
    for (int i = 0; i < NUM_ZONES; i++) {
        gState.zones[i].enabled = false;
        gState.zones[i].duration_min = 0;
    }
    Serial.reset();
    TEST_ASSERT_TRUE(testUi.executeConfirmed("general"));
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenInfo(), testUi._currentScreen);
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_START_NO_ZONES) != std::string::npos);
#endif

    // 4. Unknown confirm tag logs warning and shows error screen instead of "-- EXECUTED --"
    testUi.getScreenConfirm().setup("Test?", "", MenuID::MAIN, "bogus_tag");
    testUi.changeScreen(&testUi.getScreenConfirm());
    testUi.getScreenConfirm()._selectionOk = true;
    Serial.reset();
    simulateClick(testUi);
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenInfo(), testUi._currentScreen);
    TEST_ASSERT_NOT_NULL(strstr(testUi.getScreenInfo()._rows[0], TXT_ERR_TITLE));
#if LOG_LEVEL >= LVL_WARN
    char expectedUnknown[96];
    snprintf(expectedUnknown, sizeof(expectedUnknown), TXT_LOG_UI_UNKNOWN_ACTION, "bogus_tag");
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedUnknown) != std::string::npos);
#endif

    // 5. ScreenTimeEdit(RTC) when hardware RTC is disconnected logs TXT_LOG_RTC_NOT_FOUND
    rtclock._found = false;
    rtclock._rtc.mockPresent = false;
    testUi.getScreenTimeEdit().setup(TimeEditContext::RTC, 0, MenuID::DEF);
    testUi.changeScreen(&testUi.getScreenTimeEdit());
    simulateClick(testUi); // hours -> minutes
    Serial.reset();
    simulateClick(testUi); // attempt save -> missing RTC
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_RTC_NOT_FOUND) != std::string::npos);
#endif
}

static void test_display_recover_flushes_shadow_and_sync_catches_always_on_lcd_fault(void) {
    // 1. Display::recover() while _displayOn == true immediately restores _shadow onto LCD
    testDisp.setRows("Line0", "Line1", "Line2", "Line3");
    testDisp._lcd.clear();
    TEST_ASSERT_EQUAL_STRING("                    ", testDisp._lcd.getRow(0));
    testDisp.recover();
    TEST_ASSERT_EQUAL_STRING("Line0               ", testDisp._lcd.getRow(0));
    TEST_ASSERT_EQUAL_STRING("Line1               ", testDisp._lcd.getRow(1));
    TEST_ASSERT_EQUAL_STRING("Line2               ", testDisp._lcd.getRow(2));
    TEST_ASSERT_EQUAL_STRING("Line3               ", testDisp._lcd.getRow(3));

    // 2. In BACKLIGHT_TIMEOUT_NEVER mode on ScreenIdle, an LCD-only I2C failure detected at a
    //    minute tick (or 60s refresh) reaches _consecErrors >= 3 within ~2s via UI::update()
    _i2cRecoverCallbackCalls = 0;
    testDisp.setErrorCallback(_mockI2cRecoverCb);
    gState.setup_done = true;
    gState.rtc_valid = true;
    gState.backlight_timeout_ms = BACKLIGHT_TIMEOUT_NEVER;
    gState.now = { 2026, 6, 15, 10, 0, 0, 1, 1781517600UL };
    testUi.begin();
    testUi.goIdle();

    // Inject LCD-only I2C failure and trigger 1 minute rollover (error #1)
    testDisp._lcd.mockI2cError = 1;
    gState.now.min = 1;
    advanceMockMillis(1000);
    testUi.update();
    TEST_ASSERT_TRUE(testDisp.hasBusFault());
    TEST_ASSERT_EQUAL_UINT8(1, testDisp._consecErrors);
    TEST_ASSERT_EQUAL_UINT8(0, _i2cRecoverCallbackCalls);

    // Without another minute change, 1s UI sync retries dirty rows -> hits 3 errors in 2s!
    advanceMockMillis(1000);
    testUi.update();
    TEST_ASSERT_EQUAL_UINT8(2, testDisp._consecErrors);
    TEST_ASSERT_EQUAL_UINT8(0, _i2cRecoverCallbackCalls);

    advanceMockMillis(1000);
    testUi.update();
    TEST_ASSERT_EQUAL_UINT8(1, _i2cRecoverCallbackCalls);

    testDisp._lcd.mockI2cError = 0;
    testDisp.setErrorCallback(nullptr);
}

static void test_ui_idle_displays_battery_low_warning_immediately_and_clears_on_set(void) {
    gState.setup_done = true;
    rtclock.begin();
    rtclock.set(2026, 6, 15, 10, 0, 0);
    testUi.begin();
    testUi.goIdle();

    // Row 1 is blank while battery is healthy
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(1), TXT_IDLE_RTC_BAT) == nullptr);

    // Mid-run battery loss (mockLostPower=true) surfaces warning immediately on LCD Row 1 while keeping clock
    rtclock._rtc.mockLostPower = true;
    advanceMockMillis(31000);
    rtclock.update();
    testUi.update();
    TEST_ASSERT_TRUE(gState.rtc_valid);
    TEST_ASSERT_TRUE(rtclock.isBatteryLow());
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(0), "10:00") != nullptr);
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(1), TXT_IDLE_RTC_BAT) != nullptr);

    // Explicitly setting the clock clears the battery warning from ScreenIdle
    TEST_ASSERT_TRUE(rtclock.set(2026, 6, 15, 10, 5, 0));
    testUi.update();
    TEST_ASSERT_FALSE(rtclock.isBatteryLow());
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(1), TXT_IDLE_RTC_BAT) == nullptr);

    // Cold boot with dead battery (!rtc_valid) shows battery warning alongside time-set prompt
    rtclock = RTClock();
    gState.rtc_valid = false;
    rtclock._rtc.mockLostPower = true;
    TEST_ASSERT_TRUE(rtclock.begin());
    testUi.goIdle();
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(0), "--:--") != nullptr);
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(1), TXT_IDLE_RTC_BAT) != nullptr);
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(3), TXT_IDLE_SET_TIME) != nullptr);
}

static void initUiIdleFixture(void) {
    gState.setup_done = true;
    rtclock.begin();
    rtclock.set(2026, 6, 15, 10, 0, 0);
    history.clear();
    testUi.begin();
    testUi.goIdle();
}

static void test_ui_immediate_save_on_edit(void) {
    initUiIdleFixture();
    // Immediate save on UI edit without waiting for goIdle()
    gState.backlight_timeout_ms = 120000UL;
    storage.save();
    testUi.dispatchAction("bl:60000");
    TEST_ASSERT_EQUAL_UINT32(60000UL, gState.backlight_timeout_ms);
    gState.backlight_timeout_ms = 120000UL;
    TEST_ASSERT_TRUE(storage.load());
    TEST_ASSERT_EQUAL_UINT32(60000UL, gState.backlight_timeout_ms);
}

static void test_ui_quiet_wake_recovery_without_log_spam(void) {
    initUiIdleFixture();
    // Wake from displayOff() runs quiet Display::recover(true) without recovery log spam
    testDisp.displayOff();
    testDisp.backlightOff();
    uint32_t prevClears = testDisp._lcd.clearCalls;
    Serial.reset();
    simulateRotate(testUi, 1);
    TEST_ASSERT_TRUE(testDisp.isDisplayOn());
    TEST_ASSERT_EQUAL_UINT32(prevClears + 1, testDisp._lcd.clearCalls);
#if LOG_LEVEL >= LVL_INFO
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_LCD_RECOVERED) == std::string::npos);
#endif
}

static void test_ui_periodic_6h_lcd_recovery(void) {
    initUiIdleFixture();
    // Periodic 6h LCD re-init while idle, display ON, and not watering
    gState.backlight_timeout_ms = BACKLIGHT_TIMEOUT_NEVER;
    testUi.goIdle();
    uint32_t prevClears = testDisp._lcd.clearCalls;
    advanceMockMillis(6UL * 3600UL * 1000UL);
    Serial.reset();
    testUi.update();
    TEST_ASSERT_EQUAL_UINT32(prevClears + 1, testDisp._lcd.clearCalls);
#if LOG_LEVEL >= LVL_INFO
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_LCD_RECOVERED) == std::string::npos);
#endif
}

static void test_display_exponential_backoff_on_persistent_i2c_fault(void) {
    // Exponential backoff on persistent LCD I2C failure (10s -> 60s -> 300s)
    _i2cRecoverCallbackCalls = 0;
    testDisp.setErrorCallback(_mockI2cRecoverCb);
    testDisp._lcd.mockI2cError = 1;
    for (int i = 0; i < 3; i++) testDisp.setRows("A", "B", "C", "D");
    TEST_ASSERT_EQUAL_UINT8(1, _i2cRecoverCallbackCalls);

    advanceMockMillis(10000);
    for (int i = 0; i < 3; i++) testDisp.setRows("A", "B", "C", "D");
    TEST_ASSERT_EQUAL_UINT8(2, _i2cRecoverCallbackCalls);

    // After 2nd attempt, cooldown becomes 60s (10s is not enough)
    advanceMockMillis(10000);
    for (int i = 0; i < 3; i++) testDisp.setRows("A", "B", "C", "D");
    TEST_ASSERT_EQUAL_UINT8(2, _i2cRecoverCallbackCalls);

    advanceMockMillis(50000);
    for (int i = 0; i < 3; i++) testDisp.setRows("A", "B", "C", "D");
    TEST_ASSERT_EQUAL_UINT8(3, _i2cRecoverCallbackCalls);

    // After 3rd attempt, cooldown becomes 300s (60s is not enough)
    advanceMockMillis(60000);
    for (int i = 0; i < 3; i++) testDisp.setRows("A", "B", "C", "D");
    TEST_ASSERT_EQUAL_UINT8(3, _i2cRecoverCallbackCalls);

    testDisp._lcd.mockI2cError = 0;
    testDisp.setRows("OK0", "OK1", "OK2", "OK3");
    TEST_ASSERT_FALSE(testDisp.hasBusFault());
    TEST_ASSERT_EQUAL_UINT32(10000UL, testDisp._recoverCooldownMs);
    testDisp.setErrorCallback(nullptr);
}

static void test_ui_idle_last_watering_history_err_and_rtc_missing(void) {
    initUiIdleFixture();
    // ScreenIdle shows Last watering, History unavail, and RTC disconnected
    HistoryEntry he = { 2026, 6, 14, 8, 30, { 10, 0, 0, 0 }, WaterTrigger::AUTO, 0 };
    history.record(he);
    testUi.goIdle();
    testUi.update();
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(2), "14") != nullptr);
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(2), "08:30") != nullptr);

    history._ready = false;
    testUi.update();
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(2), TXT_IDLE_HIST_ERR) != nullptr);
    history._ready = true;

    rtclock._found = false;
    testUi.update();
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(1), TXT_IDLE_RTC_MISSING) != nullptr);
}

inline static Display* _throttledDispPtr = nullptr;
inline static uint8_t  _throttledCbCalls = 0;
inline static bool     _simulateThrottle = false;
static void _mockThrottledI2cRecoverCb(void) {
    _throttledCbCalls++;
    if (_simulateThrottle && _throttledDispPtr) {
        _throttledDispPtr->onRecoveryThrottled();
    }
}

static void test_ui_setup_wizard_defers_nvs_writes_until_completion(void) {
    // Setup wizard defers NVS writes (including sel:4 custom mode) until completion
    gState.setup_done = false;
    gState.mode = AppMode::DESATIVADO;
    storage.save();
    rtclock.begin();
    rtclock.set(2026, 6, 15, 10, 0, 0);
    testUi.begin();
    TEST_ASSERT_TRUE(testUi.inSetup());
    testUi.dispatchAction("sel:4");
    TEST_ASSERT_EQUAL(AppMode::PERSONALIZADO, gState.mode);
    // NVS must still retain pre-wizard state until setup finishes
    AppState snapshot = gState;
    TEST_ASSERT_TRUE(storage.load());
    TEST_ASSERT_FALSE(gState.setup_done);
    TEST_ASSERT_EQUAL(AppMode::DESATIVADO, gState.mode);
    gState = snapshot;

    testUi.dispatchAction("setup_advance"); // CUSTOM_CONFIG -> ZONE_CONFIG
    testUi.dispatchAction("setup_advance"); // ZONE_CONFIG -> COMPLETE
    simulateClick(testUi);                  // COMPLETE -> save & goIdle
    TEST_ASSERT_TRUE(gState.setup_done);
    TEST_ASSERT_FALSE(testUi.inSetup());
    gState.mode = AppMode::DESATIVADO;
    TEST_ASSERT_TRUE(storage.load());
    TEST_ASSERT_EQUAL(AppMode::PERSONALIZADO, gState.mode);
}

static void test_ui_wake_from_display_off_skips_stale_shadow_flush(void) {
    initUiIdleFixture();
    // Wake from displayOff() does NOT flush stale shadow text prior to ScreenIdle::render()
    gState.backlight_timeout_ms = 60000UL;
    testUi.goIdle();
    testDisp.displayOff();
    testDisp.backlightOff();
    rtclock.set(2026, 6, 15, 11, 5, 0);
    uint32_t prevClears = testDisp._lcd.clearCalls;
    testDisp._lcd.printCalls = 0;
    simulateRotate(testUi, 1);
    TEST_ASSERT_TRUE(testDisp.isDisplayOn());
    TEST_ASSERT_EQUAL_UINT32(prevClears + 1, testDisp._lcd.clearCalls);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(4, testDisp._lcd.printCalls);
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(0), "11:05") != nullptr);
}

static void test_ui_periodic_6h_recovery_guards(void) {
    initUiIdleFixture();
    // 6h periodic LCD recovery is suppressed when watering, in setup, or under bus fault
    gState.backlight_timeout_ms = BACKLIGHT_TIMEOUT_NEVER;
    testUi.goIdle();
    advanceMockMillis(6UL * 3600UL * 1000UL);
    uint32_t prevClears = testDisp._lcd.clearCalls;
    gState.watering.active = true;
    testUi.update();
    TEST_ASSERT_EQUAL_UINT32(prevClears, testDisp._lcd.clearCalls);
    gState.watering.active = false;

    testUi.setInSetup(true);
    testUi.update();
    TEST_ASSERT_EQUAL_UINT32(prevClears, testDisp._lcd.clearCalls);
    testUi.setInSetup(false);

    testDisp._busFault = true;
    testUi.update();
    TEST_ASSERT_EQUAL_UINT32(prevClears, testDisp._lcd.clearCalls);
    testDisp._busFault = false;
    testUi.update(); // now all guards are clear -> 6h recover executes
    TEST_ASSERT_EQUAL_UINT32(prevClears + 1, testDisp._lcd.clearCalls);
}

static void test_display_throttled_recovery_defers_without_escalating_backoff(void) {
    // Shared recoverI2C() throttle defers without escalating backoff; full 300s backoff fires 4th attempt
    _throttledDispPtr = &testDisp;
    _throttledCbCalls = 0;
    _simulateThrottle = true;
    testDisp.setErrorCallback(_mockThrottledI2cRecoverCb);
    testDisp._lcd.mockI2cError = 1;
    for (int i = 0; i < 3; i++) testDisp.setRows("A", "B", "C", "D");
    TEST_ASSERT_EQUAL_UINT8(1, _throttledCbCalls);
    TEST_ASSERT_EQUAL_UINT8(0, testDisp._recoverAttempts);
    TEST_ASSERT_EQUAL_UINT32(10000UL, testDisp._recoverCooldownMs);

    _simulateThrottle = false;
    advanceMockMillis(10000UL);
    for (int i = 0; i < 3; i++) testDisp.setRows("A", "B", "C", "D"); // attempt 1 -> 10s
    TEST_ASSERT_EQUAL_UINT8(2, _throttledCbCalls);
    TEST_ASSERT_EQUAL_UINT8(1, testDisp._recoverAttempts);

    advanceMockMillis(10000UL);
    for (int i = 0; i < 3; i++) testDisp.setRows("A", "B", "C", "D"); // attempt 2 -> 60s
    TEST_ASSERT_EQUAL_UINT8(3, _throttledCbCalls);
    advanceMockMillis(60000UL);
    for (int i = 0; i < 3; i++) testDisp.setRows("A", "B", "C", "D"); // attempt 3 -> 300s
    TEST_ASSERT_EQUAL_UINT8(4, _throttledCbCalls);
    TEST_ASSERT_EQUAL_UINT32(300000UL, testDisp._recoverCooldownMs);

    advanceMockMillis(299999UL);
    for (int i = 0; i < 3; i++) testDisp.setRows("A", "B", "C", "D"); // still inside 300s
    TEST_ASSERT_EQUAL_UINT8(4, _throttledCbCalls);
    advanceMockMillis(1UL);
    for (int i = 0; i < 3; i++) testDisp.setRows("A", "B", "C", "D"); // fires 4th attempt at 300s
    TEST_ASSERT_EQUAL_UINT8(5, _throttledCbCalls);

    testDisp._lcd.mockI2cError = 0;
    testDisp.setRows("OK0", "OK1", "OK2", "OK3");
    testDisp.setErrorCallback(nullptr);
}

static void test_ui_idle_cold_rtc_missing_and_battery_priority(void) {
    // Cold boot missing RTC (!gState.rtc_valid && !isPresent) shows RTC missing on Row 1 + Set Time on Row 3,
    // and battery-low warning takes priority over !isPresent; verify formatted Last watering date
    gState.setup_done = true;
    gState.rtc_valid = true;
    HistoryEntry he = { 2026, 6, 14, 8, 30, { 10, 0, 0, 0 }, WaterTrigger::AUTO, 0 };
    history.record(he);
    rtclock = RTClock();
    rtclock._rtc.mockPresent = false;
    gState.rtc_valid = false;
    TEST_ASSERT_FALSE(rtclock.begin());
    testUi.begin();
    testUi.goIdle();
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(0), "--:--") != nullptr);
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(1), TXT_IDLE_RTC_MISSING) != nullptr);
    char expectedLast[LCD_COLS + 1];
    snprintf(expectedLast, sizeof(expectedLast), TXT_IDLE_LAST_FMT, 14, TXT_MONTH_JUN, 8, 30);
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(2), expectedLast) != nullptr);
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(3), TXT_IDLE_SET_TIME) != nullptr);

    rtclock._batteryLow = true;
    testUi.update();
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(1), TXT_IDLE_RTC_BAT) != nullptr);
}

static void test_ui_time_edit_hw_write_fail_leaves_rtc_invalid(void) {
    // Failed hardware write in ScreenTimeEdit leaves rtc_valid == false (no silent SW fallback)
    rtclock = RTClock();
    rtclock._rtc.mockPresent = true;
    rtclock._rtc.mockAdjustFail = true;
    rtclock._found = true;
    gState.rtc_valid = false;
    gState.now = { 2026, 1, 1, 0, 0, 0, 4, 0 };
    testUi.setInSetup(false);
    testUi.setDateCache(2026, 6, 15);
    testUi.getScreenTimeEdit().setup(TimeEditContext::RTC, 0, MenuID::DEF);
    testUi.changeScreen(&testUi.getScreenTimeEdit());
    simulateClick(testUi); // hour -> min
    simulateClick(testUi); // attempt save -> hwOk == false
    TEST_ASSERT_FALSE(gState.rtc_valid);
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenInfo(), testUi._currentScreen);
}

static void test_ui_num_cycles_preserves_slots_and_zeroes_trailing(void) {
    // NUM_CYCLES with unchanged slot_count preserves custom times; decreasing zeroes unused trailing slots
    gState.setup_done = true;
    gState.mode = AppMode::PERSONALIZADO;
    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.slot_count = 2;
    cs.slots[0] = { 6, 15 };
    cs.slots[1] = { 19, 45 };
    cs.slots[2] = { 22, 0 };
    testUi.begin();

    testUi.getScreenDurPick().setup(2, DurContext::NUM_CYCLES, 0, MenuID::CFG_CUSTOM);
    testUi.changeScreen(&testUi.getScreenDurPick());
    simulateClick(testUi); // unchanged count -> preserve slots
    TEST_ASSERT_EQUAL_UINT8(2, cs.slot_count);
    TEST_ASSERT_EQUAL_UINT8(6, cs.slots[0].hour);
    TEST_ASSERT_EQUAL_UINT8(15, cs.slots[0].minute);
    TEST_ASSERT_EQUAL_UINT8(19, cs.slots[1].hour);
    TEST_ASSERT_EQUAL_UINT8(45, cs.slots[1].minute);

    testUi.getScreenDurPick().setup(1, DurContext::NUM_CYCLES, 0, MenuID::CFG_CUSTOM);
    testUi.changeScreen(&testUi.getScreenDurPick());
    simulateClick(testUi); // decrease 2 -> 1 -> zeroes trailing slots
    TEST_ASSERT_EQUAL_UINT8(1, cs.slot_count);
    TEST_ASSERT_EQUAL_UINT8(0, cs.slots[1].hour);
    TEST_ASSERT_EQUAL_UINT8(0, cs.slots[1].minute);
    TEST_ASSERT_EQUAL_UINT8(0, cs.slots[2].hour);
}

static void test_ui_custom_cycle_logs_post_sort_index(void) {
    // CUSTOM_CYCLE logs post-sort index when editing slot 2 earlier than slot 1
    gState.setup_done = true;
    gState.mode = AppMode::PERSONALIZADO;
    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.slot_count = 2;
    cs.slots[0] = { 8, 0 };
    cs.slots[1] = { 20, 0 };
    testUi.begin();

    testUi.getScreenTimeEdit().setup(TimeEditContext::CUSTOM_CYCLE, 1, MenuID::CFG_CUSTOM);
    testUi.changeScreen(&testUi.getScreenTimeEdit());
    testUi.getScreenTimeEdit()._teHour = 6;
    testUi.getScreenTimeEdit()._teMin = 30;
    testUi.getScreenTimeEdit()._teField = 1;
    Serial.reset();
    simulateClick(testUi);
    TEST_ASSERT_EQUAL_UINT8(6, cs.slots[0].hour);
    TEST_ASSERT_EQUAL_UINT8(30, cs.slots[0].minute);
#if LOG_LEVEL >= LVL_INFO
    char expectedPostSort[96];
    snprintf(expectedPostSort, sizeof(expectedPostSort), TXT_LOG_UI_SLOT_SET, 1, 6U, 30U);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(expectedPostSort) != std::string::npos);
#endif
}

static void test_ui_suspend_clamps_to_max_valid_unix(void) {
    // SUSPEND clamps suspended_until to UNIX_MAX_VALID (4102444799UL) near 2099-12-31
    gState.setup_done = true;
    gState.rtc_valid = true;
    gState.now = { 2099, 12, 30, 12, 0, 0, 3, DateTime(2099, 12, 30, 12, 0, 0).unixtime() };
    testUi.begin();
    testUi.getScreenDurPick().setup(10, DurContext::SUSPEND, 0, MenuID::PROG);
    testUi.changeScreen(&testUi.getScreenDurPick());
    simulateClick(testUi);
    TEST_ASSERT_TRUE(gState.suspended);
    TEST_ASSERT_EQUAL_UINT32(UNIX_MAX_VALID, gState.suspended_until);
}

static void test_ui_cancel_suspend_recomputes_next_slot(void) {
    // cancel_susp recomputes next_* immediately via scheduler.onModeChanged(false)
    initUiIdleFixture();
    gState.mode = AppMode::MEDIO; // 08:00, 20:00
    gState.suspended = true;
    gState.suspended_until = gState.now.unix + 86400UL;
    gState.next_hour = 0;
    gState.next_min = 0;
    testUi.dispatchAction("cancel_susp");
    TEST_ASSERT_FALSE(gState.suspended);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT1_H, gState.next_hour);
    TEST_ASSERT_EQUAL_UINT8(SCHED_MEDIO_SLOT1_M, gState.next_min);
}

static void test_ui_idle_rerenders_on_same_minute_hour_change(void) {
    initUiIdleFixture();
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(0), "10:00") != nullptr);
    gState.now.hour = 11; // same minute (00)
    testUi.update();
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(0), "11:00") != nullptr);
}

static void test_ui_menu_live_refresh_clamps_offset_and_updates_prog(void) {
    initUiIdleFixture();
    // ScreenMenu::update() clamps _off on MAIN shrink (no blank row) and live-refreshes PROG on suspension expiry
    gState.watering.active = true;
    testUi.goMenu(MenuID::MAIN); // 5 items (0..4)
    testUi.getScreenMenu()._cur = 4;
    testUi.getScreenMenu()._off = 2;
    testUi.getScreenMenu().render(testUi);
    gState.watering.active = false;
    testUi.update(); // shrinks 5 -> 4 items; _off must clamp to 1 so row 3 is not blank!
    TEST_ASSERT_EQUAL_UINT8(3, testUi.getScreenMenu()._cur);
    TEST_ASSERT_EQUAL_UINT8(1, testUi.getScreenMenu()._off);
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(3), TXT_BACK) != nullptr);

    gState.suspended = true;
    testUi.goMenu(MenuID::PROG);
    gState.suspended = false;
    testUi.update();
    bool hasSuspendOption = false;
    for (uint8_t i = 0; i < testUi.getScreenMenu()._itemCount; i++) {
        if (strcmp(testUi.getScreenMenu()._items[i].action, "dur_pick:suspend") == 0) {
            hasSuspendOption = true;
        }
    }
    TEST_ASSERT_TRUE(hasSuspendOption);
}

static void test_ui_go_idle_during_setup_does_not_flush_config(void) {
    storage.clear();
    gState.setup_done = false;
    gState.mode = AppMode::DESATIVADO;
    testUi.setInSetup(true);
    testUi.flagConfigChanged();
    testUi.goIdle();
    TEST_ASSERT_FALSE(storage.load());
}

static void test_ui_strict_action_parsing_rejects_invalid_inputs(void) {
    initUiIdleFixture();
    // Strict index parsing rejects non-numeric actions and out-of-range bl: timeout
    uint32_t prevBl = gState.backlight_timeout_ms;
    testUi.dispatchAction("bl:4000000"); // > 3600000ms and != NEVER
    TEST_ASSERT_EQUAL_UINT32(prevBl, gState.backlight_timeout_ms);
    testUi.dispatchAction("cfgz:abc");
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenIdle(), testUi._currentScreen);
    gState.custom_sel[0] = false;
    testUi.dispatchAction("cz:abc");
    TEST_ASSERT_FALSE(gState.custom_sel[0]);
    testUi.dispatchAction("time_edit:cabc");
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenIdle(), testUi._currentScreen);
    TEST_ASSERT_TRUE(testUi.executeConfirmed("test_abc"));
    TEST_ASSERT_FALSE(wateringCtrl.isActive());
}

static void test_ui_factory_reset_repairs_unmounted_history_fs(void) {
    // Factory reset calls history.clear(true) and repairs unmounted FS (even while mountFail is active)
    gState.rtc_valid = true;
    HistoryEntry preReset = { 2026, 6, 15, 8, 0, { 5, 0, 0, 0 }, WaterTrigger::MANUAL, 0 };
    history.record(preReset);
    LittleFS.mountFail = true;
    history.begin();
    TEST_ASSERT_FALSE(history.isReady());
    ESP.reset();
    TEST_ASSERT_TRUE(testUi.executeConfirmed("reset"));
    TEST_ASSERT_TRUE(history.isReady());
    TEST_ASSERT_TRUE(ESP.rebooted);
}

static void test_display_mid_frame_i2c_error_keeps_coherent_shadow(void) {
    // Mid-frame I2C error during setRows() must still stage rows 1..3 so recovery renders a coherent frame
    testDisp.setRows("Old0", "Old1", "Old2", "Old3");
    TEST_ASSERT_EQUAL_STRING("Old0                ", testDisp._lcd.getRow(0));
    TEST_ASSERT_EQUAL_STRING("Old3                ", testDisp._lcd.getRow(3));

    testDisp._lcd.mockI2cError = 1;
    testDisp.setRows("New0", "New1", "New2", "New3");
    TEST_ASSERT_TRUE(testDisp.hasBusFault());

    testDisp._lcd.mockI2cError = 0;
    advanceMockMillis(1000UL);
    testDisp.sync();
    TEST_ASSERT_FALSE(testDisp.hasBusFault());
    TEST_ASSERT_EQUAL_STRING("New0                ", testDisp._lcd.getRow(0));
    TEST_ASSERT_EQUAL_STRING("New1                ", testDisp._lcd.getRow(1));
    TEST_ASSERT_EQUAL_STRING("New2                ", testDisp._lcd.getRow(2));
    TEST_ASSERT_EQUAL_STRING("New3                ", testDisp._lcd.getRow(3));
}

static void test_ui_setup_welcome_wakes_with_backlight_never(void) {
    // Switching to Setup Welcome while asleep + BACKLIGHT_TIMEOUT_NEVER wakes and renders new screen cleanly
    gState.setup_done = true;
    gState.backlight_timeout_ms = 60000UL;
    testUi.begin();
    testUi.goIdle();
    testDisp.backlightOff();
    testDisp.displayOff();
    TEST_ASSERT_FALSE(testDisp.isDisplayOn());

    gState.setup_done = false;
    gState.backlight_timeout_ms = BACKLIGHT_TIMEOUT_NEVER;
    testUi.syncSetupState(); // enters ScreenSetupWelcome while display is still OFF
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenSetupWelcome(), testUi._currentScreen);

    testUi.update();
    TEST_ASSERT_TRUE(testDisp.isDisplayOn());
    TEST_ASSERT_TRUE(testDisp.isBacklightOn());
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(0), TXT_WELCOME) != nullptr);
}

static void test_display_sleep_i2c_error_keeps_logical_on_and_recovers(void) {
    // I2C error on backlightOff()/displayOff() keeps logical ON state, avoids retry storm, and recovers after bus clears
    gState.setup_done = true;
    gState.backlight_timeout_ms = 60000UL;
    testUi.begin();
    testDisp._lcd.mockI2cError = 1;
    testDisp.backlightOff();
    TEST_ASSERT_TRUE(testDisp.isBacklightOn());
    TEST_ASSERT_TRUE(testDisp.hasBusFault());
    testDisp.displayOff();
    TEST_ASSERT_TRUE(testDisp.isDisplayOn());

    // Bus clears -> sync() clears _busFault -> backlightOff() & displayOff() succeed cleanly
    testDisp._lcd.mockI2cError = 0;
    advanceMockMillis(1000UL);
    testDisp.sync();
    TEST_ASSERT_FALSE(testDisp.hasBusFault());
    testDisp.backlightOff();
    testDisp.displayOff();
    TEST_ASSERT_FALSE(testDisp.isBacklightOn());
    TEST_ASSERT_FALSE(testDisp.isDisplayOn());
}

static void test_ui_freq_days_unchanged_preserves_ref_day_and_triggers(void) {
    // FREQ_DAYS with unchanged interval preserves existing custom_ref_day and scheduler trigger locks
    gState.setup_done = true;
    gState.rtc_valid = true;
    gState.now = { 2026, 6, 15, 10, 0, 0, 1, DateTime(2026, 6, 15, 9, 0, 0).unixtime() };
    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.interval_days = 3;
    const uint32_t origRefDay = 20000UL;
    gState.custom_ref_day = origRefDay;
    scheduler._lastTriggerUnix[0] = 1781500000UL;
    testUi.begin();

    testUi.getScreenDurPick().setup(3, DurContext::FREQ_DAYS, 0, MenuID::CFG_CUSTOM);
    testUi.changeScreen(&testUi.getScreenDurPick());
    simulateClick(testUi);
    TEST_ASSERT_EQUAL_UINT32(origRefDay, gState.custom_ref_day);
    TEST_ASSERT_EQUAL_UINT32(1781500000UL, scheduler._lastTriggerUnix[0]);
}

static void test_ui_time_edit_displays_normalized_spring_forward_dst_gap(void) {
    // ScreenTimeEdit displays effective normalized local time on spring-forward DST gap
    gState.auto_dst = true;
    rtclock = RTClock();
    rtclock.begin();
    testUi.setInSetup(false);
    const DateTime gapLocal(DateTime(2026, 3, 29, 1, 30, 0).unixtime() + (int32_t)TIMEZONE_OFFSET * 3600L);
    const DateTime effLocal(gapLocal.unixtime() + 3600UL);
    testUi.setDateCache(gapLocal.year(), gapLocal.month(), gapLocal.day());
    testUi.getScreenTimeEdit().setup(TimeEditContext::RTC, 0, MenuID::DEF);
    testUi.getScreenTimeEdit()._teHour = gapLocal.hour();
    testUi.getScreenTimeEdit()._teMin = gapLocal.minute();
    testUi.getScreenTimeEdit()._teField = 1;
    testUi.changeScreen(&testUi.getScreenTimeEdit());
    simulateClick(testUi);
    char expectedDone[LCD_COLS + 1];
    snprintf(expectedDone, sizeof(expectedDone), TXT_TIME_FMT, (int)effLocal.hour(), (int)effLocal.minute());
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenDone(), testUi._currentScreen);
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(1), expectedDone) != nullptr);
}

static void test_ui_confirm_post_enter_rotation_bounce_guard(void) {
    initUiIdleFixture();
    // ScreenConfirm 350ms post-enter rotation bounce guard protects destructive confirmation
    testUi.getScreenConfirm().setup("Erase ALL", "settings?", MenuID::DEF_AVANCADO, "reset");
    testUi.changeScreen(&testUi.getScreenConfirm());
    TEST_ASSERT_FALSE(testUi.getScreenConfirm()._selectionOk);
    advanceMockMillis(200);
    simulateRotate(testUi, 1);
    TEST_ASSERT_FALSE(testUi.getScreenConfirm()._selectionOk);
    advanceMockMillis(200);
    simulateRotate(testUi, 1);
    TEST_ASSERT_TRUE(testUi.getScreenConfirm()._selectionOk);
}

static void test_ui_inplace_menu_toggles_preserve_cursor_and_scroll(void) {
    initUiIdleFixture();
    // In-place menu toggles (CUSTOM_ZONAS & toggle_dst) preserve cursor/scroll and copy action safely
    gState.custom_sel[3] = false;
    testUi.goMenu(MenuID::CUSTOM_ZONAS);
    simulateRotate(testUi, 3); // highlight Z4 (scrolls _off to 1)
    TEST_ASSERT_EQUAL_UINT8(3, testUi.getScreenMenu()._cur);
    TEST_ASSERT_EQUAL_UINT8(1, testUi.getScreenMenu()._off);
    simulateClick(testUi);     // toggles Z4 via ScreenMenu::handleClick()
    TEST_ASSERT_TRUE(gState.custom_sel[3]);
    TEST_ASSERT_EQUAL(MenuID::CUSTOM_ZONAS, testUi.getScreenMenu()._mid);
    TEST_ASSERT_EQUAL_UINT8(3, testUi.getScreenMenu()._cur);
    TEST_ASSERT_EQUAL_UINT8(1, testUi.getScreenMenu()._off);

    testUi.goMenu(MenuID::DEF_AVANCADO);
    bool foundDst = false;
    uint8_t dstIdx = 0;
    for (uint8_t i = 0; i < testUi.getScreenMenu()._itemCount; i++) {
        if (strcmp(testUi.getScreenMenu()._items[i].action, "toggle_dst") == 0) {
            dstIdx = i;
            foundDst = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(foundDst);
    bool prevDst = gState.auto_dst;
    testUi.getScreenMenu()._cur = dstIdx;
    simulateClick(testUi);
    TEST_ASSERT_EQUAL(!prevDst, gState.auto_dst);
    TEST_ASSERT_EQUAL(MenuID::DEF_AVANCADO, testUi.getScreenMenu()._mid);
    TEST_ASSERT_EQUAL_UINT8(dstIdx, testUi.getScreenMenu()._cur);
}

static void test_ui_time_edit_distinguishes_utc_range_error_from_missing_rtc(void) {
    initUiIdleFixture();
    // ScreenTimeEdit distinguishes UTC range rejection from missing RTC (normal & setup flows)
    gState.auto_dst = false;
    testUi.setInSetup(false);
    // Out-of-range cached year (or TZ-shifted UTC boundary) while hardware RTC is present
#if TIMEZONE_OFFSET > 0
    testUi.setDateCache(2020, 1, 1);
    uint8_t badH = 0;
#elif TIMEZONE_OFFSET < 0
    testUi.setDateCache(2099, 12, 31);
    uint8_t badH = 23;
#else
    testUi.setDateCache(2019, 12, 31);
    uint8_t badH = 23;
#endif
    testUi.getScreenTimeEdit().setup(TimeEditContext::RTC, 0, MenuID::DEF);
    testUi.getScreenTimeEdit()._teHour = badH;
    testUi.getScreenTimeEdit()._teMin = 0;
    testUi.getScreenTimeEdit()._teField = 1;
    testUi.changeScreen(&testUi.getScreenTimeEdit());
    simulateClick(testUi);
    TEST_ASSERT_TRUE(rtclock.isPresent());
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenInfo(), testUi._currentScreen);
    TEST_ASSERT_NOT_NULL(strstr(testUi.getScreenInfo()._rows[1], TXT_ERR_INVALID_DATE));
    TEST_ASSERT_NOT_NULL(strstr(testUi.getScreenInfo()._rows[2], TXT_ERR_DATE_RANGE));
    TEST_ASSERT_NULL(strstr(testUi.getScreenInfo()._rows[0], TXT_ERR_NO_RTC));
    simulateClick(testUi); // dismiss -> back to MenuID::DEF
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenMenu(), testUi._currentScreen);
    TEST_ASSERT_EQUAL(MenuID::DEF, testUi.getScreenMenu()._mid);

    // Same range error during Setup Wizard shows ScreenInfo (NOT skip_clock) and returns to ScreenDateEdit
    testUi.setInSetup(true);
    testUi._setupStep = SetupStep::DATE_TIME;
#if TIMEZONE_OFFSET > 0
    testUi.setDateCache(2020, 1, 1);
#elif TIMEZONE_OFFSET < 0
    testUi.setDateCache(2099, 12, 31);
#else
    testUi.setDateCache(2019, 12, 31);
#endif
    testUi.getScreenTimeEdit().setup(TimeEditContext::RTC, 0, MenuID::MAIN);
    testUi.getScreenTimeEdit()._teHour = badH;
    testUi.getScreenTimeEdit()._teMin = 0;
    testUi.getScreenTimeEdit()._teField = 1;
    testUi.changeScreen(&testUi.getScreenTimeEdit());
    simulateClick(testUi);
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenInfo(), testUi._currentScreen);
    TEST_ASSERT_NOT_NULL(strstr(testUi.getScreenInfo()._rows[1], TXT_ERR_INVALID_DATE));
    simulateClick(testUi); // dismiss -> returns to ScreenDateEdit
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenDateEdit(), testUi._currentScreen);
}

static void test_ui_horarios_screen_uses_distinct_frequency_prefix(void) {
    initUiIdleFixture();
    // Schedule info screen uses "F:" frequency prefix (never duplicates "H:" on row 3)
    gState.mode = AppMode::MEDIO;
    testUi.dispatchAction("horarios");
    TEST_ASSERT_EQUAL_CHAR('H', testUi.getScreenInfo()._rows[2][0]);
    TEST_ASSERT_EQUAL_CHAR('F', testUi.getScreenInfo()._rows[3][0]);
    TEST_ASSERT_NOT_NULL(strstr(testUi.getScreenInfo()._rows[3], TXT_PROG_DAILY));

    gState.mode = AppMode::PERSONALIZADO;
    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.slot_count = 2;
    cs.interval_days = 3;
    testUi.dispatchAction("horarios");
    TEST_ASSERT_EQUAL_CHAR('F', testUi.getScreenInfo()._rows[3][0]);
}

static void test_ui_idle_long_interval_shows_date_short_shows_weekday(void) {
    rtclock.begin();
    rtclock.set(2026, 6, 1, 9, 0, 0); // Monday June 1, 09:00
    gState.setup_done = true;
    gState.suspended = false;
    gState.zones[0] = { "Z1", true, 5 };
    for (int i = 1; i < NUM_ZONES; i++) gState.zones[i].enabled = false;
    testUi.begin();

    // Custom interval >= 7 days shows explicit date (11Jun) instead of ambiguous weekday
    gState.mode = AppMode::PERSONALIZADO;
    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.day_pattern = DayPattern::EVERY_X_DAYS;
    cs.interval_days = 10;
    cs.slot_count = 1;
    cs.slots[0] = { 8, 0 };
    DateTime monLocal(2026, 6, 1, 0, 0, 0);
    gState.custom_ref_day = monLocal.unixtime() / 86400UL;
    testUi.goIdle();
    char expectedDate[21];
    snprintf(expectedDate, sizeof(expectedDate), "11%s", TXT_MONTH_JUN);
    TEST_ASSERT_NOT_NULL(strstr(testDisp._lcd.getRow(3), expectedDate));

    // Interval 3 days (< 7) keeps friendly weekday name (Thursday)
    cs.interval_days = 3;
    testUi.goIdle();
    TEST_ASSERT_NOT_NULL(strstr(testDisp._lcd.getRow(3), TXT_DOW_THU));
}

static void test_ui_idle_zero_enabled_zones_shows_warning(void) {
    initUiIdleFixture();
    gState.mode = AppMode::MEDIO;
    gState.zones[0] = { "Z1", true, 5 };
    for (int i = 1; i < NUM_ZONES; i++) gState.zones[i].enabled = false;
    testUi.goIdle();

    // All zones disabled shows TXT_NO_ACTIVE_ZONES on ScreenIdle row 3 (live update too)
    gState.zones[0].enabled = false;
    gState.zones[0].duration_min = 0;
    testUi.update();
    TEST_ASSERT_NOT_NULL(strstr(testDisp._lcd.getRow(3), TXT_NO_ACTIVE_ZONES));
}

static void test_ui_suspend_zero_days_cancels_cleanly(void) {
    initUiIdleFixture();
    // DurContext::SUSPEND value 0 displays TXT_CANCEL and cancels cleanly without suspending
    testUi.getScreenDurPick().setup(SUSPEND_DEFAULT_DAYS, DurContext::SUSPEND, 0, MenuID::PROG);
    testUi.changeScreen(&testUi.getScreenDurPick());
    simulateRotate(testUi, -3); // 3 -> 0
    TEST_ASSERT_NOT_NULL(strstr(testDisp._lcd.getRow(1), TXT_CANCEL));
    simulateClick(testUi);
    TEST_ASSERT_FALSE(gState.suspended);
    TEST_ASSERT_EQUAL_UINT32(0UL, gState.suspended_until);
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenMenu(), testUi._currentScreen);
    TEST_ASSERT_EQUAL(MenuID::PROG, testUi.getScreenMenu()._mid);
}

static void test_ui_settings_date_edit_fails_fast_when_rtc_missing(void) {
    initUiIdleFixture();
    // Settings -> Date/Time fails fast when RTC is missing outside Setup Wizard
    rtclock._found = false;
    rtclock._rtc.mockPresent = false;
    testUi.setInSetup(false);
    testUi.dispatchAction("date_edit:rtc");
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenInfo(), testUi._currentScreen);
    TEST_ASSERT_NOT_NULL(strstr(testUi.getScreenInfo()._rows[0], TXT_ERR_NO_RTC));
}

static void test_ui_main_menu_live_rebuild_preserves_selected_action(void) {
    initUiIdleFixture();
    // MAIN menu live rebuild preserves cursor on selected action when stop_now inserts/removes
    testUi.goMenu(MenuID::MAIN);
    simulateRotate(testUi, 1); // highlight "Schedule" (go:prog) at index 1
    TEST_ASSERT_EQUAL_STRING("go:prog", testUi.getScreenMenu()._items[testUi.getScreenMenu()._cur].action);
    gState.watering.active = true;
    testUi.update();
    TEST_ASSERT_EQUAL_STRING("stop_now", testUi.getScreenMenu()._items[0].action);
    TEST_ASSERT_EQUAL_STRING("go:prog", testUi.getScreenMenu()._items[testUi.getScreenMenu()._cur].action);
    gState.watering.active = false;
    testUi.update();
    TEST_ASSERT_EQUAL_STRING("go:prog", testUi.getScreenMenu()._items[testUi.getScreenMenu()._cur].action);
}

static void test_ui_setup_mode_includes_disabled_option(void) {
    initUiIdleFixture();
    // SETUP_MODE includes Disabled (sel:3) option
    testUi.goMenu(MenuID::SETUP_MODE);
    bool foundDisabled = false;
    for (uint8_t i = 0; i < testUi.getScreenMenu()._itemCount; i++) {
        if (strcmp(testUi.getScreenMenu()._items[i].action, "sel:3") == 0) {
            foundDisabled = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(foundDisabled);
}

static void test_ui_setup_completion_retries_failed_save_on_throttle(void) {
    initUiIdleFixture();
    // Setup completion delegates failed-save retry solely to Storage (single save attempt, no duplicate goIdle() save)
    Preferences::writeFail = true;
    gState.setup_done = false;
    testUi.setInSetup(true);
    testUi.getScreenSetupWelcome().setup(true);
    testUi.changeScreen(&testUi.getScreenSetupWelcome());
    Serial.reset();
    simulateClick(testUi); // sets setup_done=true, single save() fails -> Storage arms _savePending
    TEST_ASSERT_TRUE(gState.setup_done);
    TEST_ASSERT_FALSE(testUi._configChanged);
    TEST_ASSERT_TRUE(storage.hasPendingSave());
#if LOG_LEVEL >= LVL_ERROR
    // Verify only one write-failure log was emitted (goIdle() did not re-call storage.save())
    char errMsg[96];
    snprintf(errMsg, sizeof(errMsg), TXT_LOG_NVS_WRITE_FAIL, TXT_LOG_NVS_PART_CFG);
    size_t firstPos = Serial.outputBuffer.find(errMsg);
    TEST_ASSERT_TRUE(firstPos != std::string::npos);
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(errMsg, firstPos + 1) == std::string::npos);
#endif

    Preferences::writeFail = false;
    advanceMockMillis(2000); // still within 5 s retry throttle
    storage.update();
    TEST_ASSERT_TRUE(storage.hasPendingSave());
    advanceMockMillis(3500); // >= 5 s -> Storage retries and succeeds
    storage.update();
    TEST_ASSERT_FALSE(storage.hasPendingSave());
    initAppState();
    TEST_ASSERT_TRUE(storage.load());
    TEST_ASSERT_TRUE(gState.setup_done);
}

static void test_display_recover_while_sleeping_keeps_display_off(void) {
    // Display::recover() while sleeping (_displayOn == false) never flips LCD displayState ON
    testDisp.displayOff();
    TEST_ASSERT_FALSE(testDisp._lcd.displayState);
    testDisp.recover(true, false);
    TEST_ASSERT_FALSE(testDisp._lcd.displayState);
}

static void test_display_failed_wake_arms_sync_retry(void) {
    // Failed backlightOn()/displayOn() arms sync retry so Display::sync() turns hardware back ON once I2C clears
    testDisp.displayOn();
    testDisp.backlightOff();
    testDisp.displayOff();
    TEST_ASSERT_FALSE(testDisp._lcd.backlightState);
    TEST_ASSERT_FALSE(testDisp._lcd.displayState);
    testDisp._lcd.mockI2cError = 1;
    testDisp.displayOn();
    testDisp.backlightOn();
    TEST_ASSERT_TRUE(testDisp.hasBusFault());
    TEST_ASSERT_FALSE(testDisp._lcd.displayState);
    TEST_ASSERT_FALSE(testDisp._lcd.backlightState);

    testDisp._lcd.mockI2cError = 0;
    advanceMockMillis(1000UL);
    testDisp.sync();
    TEST_ASSERT_FALSE(testDisp.hasBusFault());
    TEST_ASSERT_TRUE(testDisp._lcd.displayState);
    TEST_ASSERT_TRUE(testDisp._lcd.backlightState);
}

static void test_ui_cancel_suspend_preserves_persisted_triggers(void) {
    initUiIdleFixture();
    // cancel_susp refreshes next_* WITHOUT clearing persisted DST fall-back trigger guards
    gState.mode = AppMode::MEDIO;
    gState.suspended = true;
    gState.suspended_until = gState.now.unix + 86400UL;
    const uint32_t savedTrigger = gState.now.unix - 1800UL;
    scheduler._lastTriggerUnix[0] = savedTrigger;
    storage.saveLastTriggers(scheduler._lastTriggerUnix, MAX_SLOTS_PER_MODE);
    testUi.dispatchAction("cancel_susp");
    TEST_ASSERT_FALSE(gState.suspended);
    TEST_ASSERT_EQUAL_UINT32(savedTrigger, scheduler._lastTriggerUnix[0]);
    uint32_t persistedTr[MAX_SLOTS_PER_MODE] = {};
    TEST_ASSERT_TRUE(storage.loadLastTriggers(persistedTr, MAX_SLOTS_PER_MODE));
    TEST_ASSERT_EQUAL_UINT32(savedTrigger, persistedTr[0]);
}

static void test_ui_confirm_setup_handles_nullptr_tag(void) {
    // ScreenConfirm::setup() handles nullptr tag safely
    testUi.getScreenConfirm().setup("Line1", "Line2", MenuID::MAIN, nullptr);
    TEST_ASSERT_EQUAL_STRING("", testUi.getScreenConfirm()._tag);
}

static void test_ui_blocks_manual_and_test_while_watering_active(void) {
    gState.setup_done = true;
    gState.rtc_valid = true;
    gState.zones[0] = { "Z1", true, 5 };
    gState.zones[1] = { "Z2", false, 0 };
    gState.zones[2] = { "Z3", false, 0 };
    gState.zones[3] = { "Z4", false, 0 };
    gState.custom_sel[0] = true;
    gState.custom_dur_min = 3;
    testUi.begin();

    TEST_ASSERT_TRUE(wateringCtrl.startGeneral(WaterTrigger::AUTO));
    TEST_ASSERT_TRUE(gState.watering.active);

    // Early menu gates block entering MANUAL and TESTES while watering is active
    testUi.dispatchAction("go:manual");
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenInfo(), testUi._currentScreen);
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(1), TXT_WATERING_ACTIVE) != nullptr);
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(2), TXT_STOP_FIRST) != nullptr);

    testUi.goIdle();
    testUi.dispatchAction("go:testes");
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenInfo(), testUi._currentScreen);
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(1), TXT_WATERING_ACTIVE) != nullptr);

    // Late confirm gates (e.g. AUTO started while sitting on ScreenConfirm) also block and return true
    const char* blockedTags[] = { "general", "custom", "test_all", "test_0" };
    for (size_t i = 0; i < sizeof(blockedTags) / sizeof(blockedTags[0]); i++) {
        testUi.goIdle();
        TEST_ASSERT_TRUE(testUi.executeConfirmed(blockedTags[i]));
        TEST_ASSERT_EQUAL_PTR(&testUi.getScreenInfo(), testUi._currentScreen);
        TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(1), TXT_WATERING_ACTIVE) != nullptr);
    }

    // Active AUTO run remains intact
    TEST_ASSERT_TRUE(wateringCtrl.isActive());
    TEST_ASSERT_EQUAL(WaterTrigger::AUTO, wateringCtrl.activeTrigger());
}

static void test_ui_idle_displays_skipped_slot_notice(void) {
    scheduler = Scheduler();
    gState.setup_done = true;
    gState.rtc_valid = true;
    gState.mode = AppMode::MEDIO;
    gState.zones[0] = { "Z1", true, 5 };
    gState.zones[1].enabled = false;
    gState.zones[2].enabled = false;
    gState.zones[3].enabled = false;
    rtclock.begin();
    rtclock.set(2026, 6, 15, 7, 59, 0);
    scheduler.begin();
    testUi.begin();

    // MANUAL General overlaps 08:00 -> skipped indicator set
    TEST_ASSERT_TRUE(wateringCtrl.startGeneral(WaterTrigger::MANUAL));
    rtclock.set(2026, 6, 15, 8, 0, 0);
    scheduler.update();
    TEST_ASSERT_TRUE(scheduler.hasSkippedSlot());
    wateringCtrl.stop();

    testUi.update();
    char expected[LCD_COLS + 1];
    snprintf(expected, sizeof(expected), TXT_IDLE_SKIPPED_FMT, 8, 0);
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(1), expected) != nullptr);
}

static void test_ui_shows_config_lost_alert_only_on_load_failure(void) {
    // 1. Fresh empty NVS -> Setup Welcome directly (no false error screen)
    storage.clear();
    initAppState();
    TEST_ASSERT_FALSE(storage.load());
    TEST_ASSERT_FALSE(storage.configLoadFailed());
    testUi.begin();
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenSetupWelcome(), testUi._currentScreen);

    // 2. Corrupt NVS load failure -> persistent ScreenInfo alert before Setup Welcome
    uint8_t garbage[44];
    memset(garbage, 0xEE, sizeof(garbage));
    prefs.putBytes("cfg", garbage, sizeof(garbage));
    initAppState();
    TEST_ASSERT_FALSE(storage.load());
    TEST_ASSERT_TRUE(storage.configLoadFailed());

    testUi.begin();
    TEST_ASSERT_TRUE(testUi.inSetup());
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenInfo(), testUi._currentScreen);
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(0), TXT_ERR_CFG_LOST_TITLE) != nullptr);

    // Must remain visible past IDLE_TIMEOUT_MS until acknowledged
    advanceMockMillis(IDLE_TIMEOUT_MS + 5000UL);
    testUi.update();
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenInfo(), testUi._currentScreen);

    // Clicking advances into Setup Welcome
    simulateClick(testUi);
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenSetupWelcome(), testUi._currentScreen);
}

static void test_display_late_hotplug_runs_lcd_init_before_writing_rows(void) {
    // Boot with LCD missing (NACK on begin()): HD44780 4-bit init never ran
    Display hotDisp;
    hotDisp._lcd.mockI2cError = 1;
    hotDisp.begin();
    TEST_ASSERT_TRUE(hotDisp.hasBusFault());
    TEST_ASSERT_TRUE(hotDisp._initNeeded);
    TEST_ASSERT_FALSE(hotDisp._lcd.initialized);

    // One frame fails before hitting 3 consecutive errors (stages shadow rows)
    hotDisp.setRows("Row0", "Row1", "Row2", "Row3");
    TEST_ASSERT_TRUE(hotDisp.hasBusFault());
    TEST_ASSERT_TRUE(hotDisp._initNeeded);
    TEST_ASSERT_EQUAL_UINT8(2, hotDisp._consecErrors);

    // LCD comes online before 3rd error -> next sync() MUST run 4-bit init() before printing rows
    hotDisp._lcd.mockI2cError = 0;
    const uint32_t initsBefore = hotDisp._lcd.initCalls;
    advanceMockMillis(1000UL);
    hotDisp.sync();
    TEST_ASSERT_FALSE(hotDisp.hasBusFault());
    TEST_ASSERT_FALSE(hotDisp._initNeeded);
    TEST_ASSERT_TRUE(hotDisp._lcd.initialized);
    TEST_ASSERT_EQUAL_UINT32(initsBefore + 1, hotDisp._lcd.initCalls);
    TEST_ASSERT_TRUE(hotDisp._lcd.displayState);
    TEST_ASSERT_TRUE(hotDisp._lcd.backlightState);
    TEST_ASSERT_EQUAL_STRING("Row0                ", hotDisp._lcd.getRow(0));
    TEST_ASSERT_EQUAL_STRING("Row3                ", hotDisp._lcd.getRow(3));
}

static void test_ui_confirm_done_back_menu_config_lost_no_flash_and_custom_setup_single_save(void) {
    initUiIdleFixture();
    gState.zones[0] = { "Z1", true, 5 };
    gState.zones[1].enabled = false;
    gState.zones[2].enabled = false;
    gState.zones[3].enabled = false;
    gState.custom_sel[0] = true;
    gState.custom_dur_min = 3;

    // 1. Confirming custom run routes ScreenDone back to MAIN (not MANUAL while watering is active)
    testUi.getScreenConfirm().setup("Zonas: Z1", "Dur: 3 min", MenuID::MANUAL, "custom");
    testUi.changeScreen(&testUi.getScreenConfirm());
    advanceMockMillis(400UL);
    simulateRotate(testUi, 1); // select OK
    simulateClick(testUi);
    TEST_ASSERT_TRUE(wateringCtrl.isActive());
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenDone(), testUi._currentScreen);
    TEST_ASSERT_EQUAL(MenuID::MAIN, testUi.getScreenDone()._backMenu);
    wateringCtrl.stop();

    // Confirming test run also routes ScreenDone back to MAIN (while Cancel still returns to TESTES)
    testUi.getScreenConfirm().setup("Test Z1", "5s?", MenuID::TESTES, "test_0");
    testUi.changeScreen(&testUi.getScreenConfirm());
    simulateClick(testUi); // Cancel (default selection is Back)
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenMenu(), testUi._currentScreen);
    TEST_ASSERT_EQUAL(MenuID::TESTES, testUi.getScreenMenu()._mid);

    testUi.getScreenConfirm().setup("Test Z1", "5s?", MenuID::TESTES, "test_0");
    testUi.changeScreen(&testUi.getScreenConfirm());
    advanceMockMillis(400UL);
    simulateRotate(testUi, 1); // select OK
    simulateClick(testUi);
    TEST_ASSERT_TRUE(wateringCtrl.isActive());
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenDone(), testUi._currentScreen);
    TEST_ASSERT_EQUAL(MenuID::MAIN, testUi.getScreenDone()._backMenu);
    wateringCtrl.stop();

    // 2. Corrupt NVS boot does NOT flash Welcome screen before ScreenInfo config-lost alert
    uint8_t garbage[44];
    memset(garbage, 0xEE, sizeof(garbage));
    prefs.putBytes("cfg", garbage, sizeof(garbage));
    initAppState();
    TEST_ASSERT_FALSE(storage.load());
    TEST_ASSERT_TRUE(storage.configLoadFailed());
    testDisp.begin();
    testDisp._lcd.printCalls = 0;
    testUi.begin();
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenInfo(), testUi._currentScreen);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(4, testDisp._lcd.printCalls);

    // 3. Completing Setup Wizard in PERSONALIZADO performs a single NVS config save (no double seq bump)
    storage.clear();
    initAppState();
    rtclock.begin();
    rtclock.set(2026, 6, 15, 10, 0, 0);
    gState.mode = AppMode::PERSONALIZADO;
    gState.custom_ref_day = 0xFFFFFFFFUL;
    testUi.setInSetup(true);
    testUi.flagConfigChanged();
    testUi.getScreenSetupWelcome().setup(true);
    testUi.changeScreen(&testUi.getScreenSetupWelcome());
    simulateClick(testUi);
    TEST_ASSERT_TRUE(gState.setup_done);
    TEST_ASSERT_NOT_EQUAL(0xFFFFFFFFUL, gState.custom_ref_day);
    TEST_ASSERT_EQUAL_UINT32(1UL, storage._configSeq);
}

static void test_ui_sync_setup_resets_activity_and_history_menu_live_refreshes(void) {
    // 1. Exiting Setup Wizard via syncSetupState() (e.g. import_config) resets _lastActivity so LCD stays lit
    gState.setup_done = false;
    gState.backlight_timeout_ms = 120000UL;
    testUi.begin();
    TEST_ASSERT_TRUE(testUi.inSetup());

    advanceMockMillis(300000UL); // 5 min sitting in Setup Wizard (> backlight + display off)
    gState.setup_done = true;
    testUi.syncSetupState();
    testUi.update();
    TEST_ASSERT_FALSE(testUi.inSetup());
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenIdle(), testUi._currentScreen);
    TEST_ASSERT_TRUE(testDisp.isBacklightOn());
    TEST_ASSERT_TRUE(testDisp.isDisplayOn());

    // 2. MenuID::HISTORICO live-rebuilds when a watering record is added while viewing History
    gState.rtc_valid = true;
    history.clear();
    testUi.goMenu(MenuID::HISTORICO);
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(1), TXT_NO_RECORDS) != nullptr);

    HistoryEntry he = { 2026, 6, 18, 9, 45, { 5, 0, 0, 0 }, WaterTrigger::AUTO, 0 };
    history.record(he);
    testUi.update();
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(1), "09:45") != nullptr);

    // 3. ScreenIdle updates row 2 on history.revision() change even if entryCount stays unchanged at rotation start
    testUi.goIdle();
    testUi.update();
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(2), "09:45") != nullptr);
    history._lineCount = HISTORY_MAX_ENTRIES;
    HistoryEntry he2 = { 2026, 6, 19, 11, 20, { 6, 0, 0, 0 }, WaterTrigger::MANUAL, 0 };
    history.record(he2);
    TEST_ASSERT_TRUE(history.isRotating());
    testUi.update();
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(2), "11:20") != nullptr);
}

static void test_ui_freq_days_rejects_midnight_wrap_overlap(void) {
    initUiIdleFixture();
    gState.mode = AppMode::PERSONALIZADO;
    gState.zones[0] = { "Z1", true, 15 };
    gState.zones[1] = { "Z2", true, 15 };
    gState.zones[2].enabled = false;
    gState.zones[2].duration_min = 0;
    gState.zones[3].enabled = false;
    gState.zones[3].duration_min = 0;

    // With interval_days == 2, midnight wrap does not apply, so 00:05 and 23:55 are valid
    ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    cs.interval_days = 2;
    cs.slot_count = 2;
    cs.slots[0] = { 0, 5 };
    cs.slots[1] = { 23, 55 };
    TEST_ASSERT_FALSE(hasCustomSlotOverlap(cs, computeMinCustomSpacing()));
    TEST_ASSERT_TRUE(storage.save());

    // Changing frequency to 1 day enables midnight wrap (10 min gap < 31 min spacing) -> must reject
    testUi.getScreenDurPick().setup(1, DurContext::FREQ_DAYS, 0, MenuID::CFG_CUSTOM);
    testUi.changeScreen(&testUi.getScreenDurPick());
    simulateClick(testUi);

    TEST_ASSERT_EQUAL_UINT8(2, cs.interval_days);
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenInfo(), testUi._currentScreen);
    TEST_ASSERT_TRUE(strstr(testDisp._lcd.getRow(0), TXT_ERR_OVERLAP) != nullptr);

    // Persisted NVS config must remain valid and loadable across reboot
    initAppState();
    TEST_ASSERT_TRUE(storage.load());
    TEST_ASSERT_EQUAL(AppMode::PERSONALIZADO, gState.mode);
    TEST_ASSERT_EQUAL_UINT8(2, MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO].interval_days);
}

};

void setUp(void) {
    resetAllTestMocks();
    rtclock = RTClock();
    storage = Storage();
    history = History();
    storage.begin();
    history.begin();
    wateringCtrl.begin();
    digitalWrite(PIN_CLK, HIGH);
    digitalWrite(PIN_DT, HIGH);
    digitalWrite(PIN_SW, HIGH);
    testDisp = Display();
    testDisp.begin();
    testEnc.begin();
    testUi.~UI();
    new (&testUi) UI(testDisp, testEnc);
}

void tearDown(void) {
    wateringCtrl.stop();
}

int main(void) {
    UNITY_BEGIN();
    RUN_UT(test_display_delta_rows_and_nullptr_preservation_across_refresh);
    RUN_UT(test_display_formatting_helpers_and_null_safety);
    RUN_UT(test_menubuilder_dynamic_stop_watering_and_live_menu_refresh);
    RUN_UT(test_menubuilder_history_localization_and_dynamic_test_duration);
    RUN_UT(test_ui_setup_wizard_skip_mode_advances_to_zones_not_complete);
    RUN_UT(test_ui_setup_missing_rtc_skip_clock_back_and_ok_flow);
    RUN_UT(test_ui_info_and_done_post_enter_rotation_bounce_guard);
    RUN_UT(test_ui_actions_dst_refresh_custom_respace_zero_dur_and_reset_safety);
    RUN_UT(test_ui_idle_screen_waiting_between_zones_and_backlight_never);
    RUN_UT(test_display_i2c_fault_abort_cooldown_and_recovery_callback);
    RUN_UT(test_ui_four_cycle_horarios_and_zero_dur_overlap_guard);
    RUN_UT(test_ui_clock_editor_refresh_and_freq_days_without_rtc);
    RUN_UT(test_ui_settings_logs_unknown_confirm_and_missing_rtc_warning);
    RUN_UT(test_display_recover_flushes_shadow_and_sync_catches_always_on_lcd_fault);
    RUN_UT(test_ui_idle_displays_battery_low_warning_immediately_and_clears_on_set);
    RUN_UT(test_ui_immediate_save_on_edit);
    RUN_UT(test_ui_quiet_wake_recovery_without_log_spam);
    RUN_UT(test_ui_periodic_6h_lcd_recovery);
    RUN_UT(test_display_exponential_backoff_on_persistent_i2c_fault);
    RUN_UT(test_ui_idle_last_watering_history_err_and_rtc_missing);
    RUN_UT(test_ui_setup_wizard_defers_nvs_writes_until_completion);
    RUN_UT(test_ui_wake_from_display_off_skips_stale_shadow_flush);
    RUN_UT(test_ui_periodic_6h_recovery_guards);
    RUN_UT(test_display_throttled_recovery_defers_without_escalating_backoff);
    RUN_UT(test_ui_idle_cold_rtc_missing_and_battery_priority);
    RUN_UT(test_ui_time_edit_hw_write_fail_leaves_rtc_invalid);
    RUN_UT(test_ui_num_cycles_preserves_slots_and_zeroes_trailing);
    RUN_UT(test_ui_custom_cycle_logs_post_sort_index);
    RUN_UT(test_ui_suspend_clamps_to_max_valid_unix);
    RUN_UT(test_ui_cancel_suspend_recomputes_next_slot);
    RUN_UT(test_ui_idle_rerenders_on_same_minute_hour_change);
    RUN_UT(test_ui_menu_live_refresh_clamps_offset_and_updates_prog);
    RUN_UT(test_ui_go_idle_during_setup_does_not_flush_config);
    RUN_UT(test_ui_strict_action_parsing_rejects_invalid_inputs);
    RUN_UT(test_ui_factory_reset_repairs_unmounted_history_fs);
    RUN_UT(test_display_mid_frame_i2c_error_keeps_coherent_shadow);
    RUN_UT(test_ui_setup_welcome_wakes_with_backlight_never);
    RUN_UT(test_display_sleep_i2c_error_keeps_logical_on_and_recovers);
    RUN_UT(test_ui_freq_days_unchanged_preserves_ref_day_and_triggers);
    RUN_UT(test_ui_time_edit_displays_normalized_spring_forward_dst_gap);
    RUN_UT(test_ui_confirm_post_enter_rotation_bounce_guard);
    RUN_UT(test_ui_inplace_menu_toggles_preserve_cursor_and_scroll);
    RUN_UT(test_ui_time_edit_distinguishes_utc_range_error_from_missing_rtc);
    RUN_UT(test_ui_horarios_screen_uses_distinct_frequency_prefix);
    RUN_UT(test_ui_idle_long_interval_shows_date_short_shows_weekday);
    RUN_UT(test_ui_idle_zero_enabled_zones_shows_warning);
    RUN_UT(test_ui_suspend_zero_days_cancels_cleanly);
    RUN_UT(test_ui_settings_date_edit_fails_fast_when_rtc_missing);
    RUN_UT(test_ui_main_menu_live_rebuild_preserves_selected_action);
    RUN_UT(test_ui_setup_mode_includes_disabled_option);
    RUN_UT(test_ui_setup_completion_retries_failed_save_on_throttle);
    RUN_UT(test_display_recover_while_sleeping_keeps_display_off);
    RUN_UT(test_display_failed_wake_arms_sync_retry);
    RUN_UT(test_ui_cancel_suspend_preserves_persisted_triggers);
    RUN_UT(test_ui_confirm_setup_handles_nullptr_tag);
    RUN_UT(test_ui_blocks_manual_and_test_while_watering_active);
    RUN_UT(test_ui_idle_displays_skipped_slot_notice);
    RUN_UT(test_ui_shows_config_lost_alert_only_on_load_failure);
    RUN_UT(test_display_late_hotplug_runs_lcd_init_before_writing_rows);
    RUN_UT(test_ui_confirm_done_back_menu_config_lost_no_flash_and_custom_setup_single_save);
    RUN_UT(test_ui_sync_setup_resets_activity_and_history_menu_live_refreshes);
    RUN_UT(test_ui_freq_days_rejects_midnight_wrap_overlap);
    return UNITY_END();
}
