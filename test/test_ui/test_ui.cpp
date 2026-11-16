#include <unity.h>
#include <new>
#include "TestCommon.h"
#include "../../test/mocks/TestCommon.cpp"
#include "../../src/AppState.cpp"
#include "../../src/Storage.cpp"

#define private public
#include "RTClock.h"
#include "Display.h"
#include "Encoder.h"
#include "WateringController.h"
#include "Scheduler.h"
#include "UI.h"
#undef private

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

void setUp(void) {
    resetAllTestMocks();
    rtclock = RTClock();
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

void test_display_delta_rows_and_nullptr_preservation_across_refresh(void) {
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

void test_display_formatting_helpers_and_null_safety(void) {
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

void test_menubuilder_dynamic_stop_watering_and_live_menu_refresh(void) {
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

void test_menubuilder_history_localization_and_dynamic_test_duration(void) {
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

void test_ui_setup_wizard_skip_mode_advances_to_zones_not_complete(void) {
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

void test_ui_setup_missing_rtc_skip_clock_back_and_ok_flow(void) {
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

    // Advance again to Confirm(skip_clock), rotate to >OK, and click -> advances to SETUP_MODE
    simulateClick(testUi);
    simulateClick(testUi);
    simulateClick(testUi);
    simulateClick(testUi);
    simulateClick(testUi);
    TEST_ASSERT_EQUAL_PTR(&testUi.getScreenConfirm(), testUi._currentScreen);
    simulateRotate(testUi, 1);
    TEST_ASSERT_TRUE(testUi.getScreenConfirm()._selectionOk);
    simulateClick(testUi);

    TEST_ASSERT_EQUAL(SetupStep::MODE_SELECT, testUi.getSetupStep());
    TEST_ASSERT_EQUAL(MenuID::SETUP_MODE, testUi.getScreenMenu()._mid);
}

void test_ui_info_and_done_post_enter_rotation_bounce_guard(void) {
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

void test_ui_actions_dst_refresh_custom_respace_zero_dur_and_reset_safety(void) {
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

void test_ui_idle_screen_waiting_between_zones_and_backlight_never(void) {
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

static uint8_t _i2cRecoverCallbackCalls = 0;
static void _mockI2cRecoverCb() {
    _i2cRecoverCallbackCalls++;
}

void test_display_i2c_fault_abort_cooldown_and_recovery_callback(void) {
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

void test_ui_four_cycle_horarios_and_zero_dur_overlap_guard(void) {
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

void test_ui_clock_editor_refresh_and_freq_days_without_rtc(void) {
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

void test_ui_settings_logs_unknown_confirm_and_missing_rtc_warning(void) {
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

void test_display_recover_flushes_shadow_and_sync_catches_always_on_lcd_fault(void) {
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

void test_ui_idle_displays_battery_low_warning_immediately_and_clears_on_set(void) {
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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_display_delta_rows_and_nullptr_preservation_across_refresh);
    RUN_TEST(test_display_formatting_helpers_and_null_safety);
    RUN_TEST(test_menubuilder_dynamic_stop_watering_and_live_menu_refresh);
    RUN_TEST(test_menubuilder_history_localization_and_dynamic_test_duration);
    RUN_TEST(test_ui_setup_wizard_skip_mode_advances_to_zones_not_complete);
    RUN_TEST(test_ui_setup_missing_rtc_skip_clock_back_and_ok_flow);
    RUN_TEST(test_ui_info_and_done_post_enter_rotation_bounce_guard);
    RUN_TEST(test_ui_actions_dst_refresh_custom_respace_zero_dur_and_reset_safety);
    RUN_TEST(test_ui_idle_screen_waiting_between_zones_and_backlight_never);
    RUN_TEST(test_display_i2c_fault_abort_cooldown_and_recovery_callback);
    RUN_TEST(test_ui_four_cycle_horarios_and_zero_dur_overlap_guard);
    RUN_TEST(test_ui_clock_editor_refresh_and_freq_days_without_rtc);
    RUN_TEST(test_ui_settings_logs_unknown_confirm_and_missing_rtc_warning);
    RUN_TEST(test_display_recover_flushes_shadow_and_sync_catches_always_on_lcd_fault);
    RUN_TEST(test_ui_idle_displays_battery_low_warning_immediately_and_clears_on_set);
    return UNITY_END();
}
