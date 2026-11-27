#define HAS_TERMINAL_AWARE_LOG 1
#include <unity.h>
#include <new>
#include "TestCommon.h"
#include "../../test/mocks/TestCommon.cpp"
#include "../../src/AppState.cpp"
#include "../../src/Storage.cpp"

#include "Display.h"
#include "Encoder.h"
#include "RTClock.h"
#include "History.h"
#include "WateringController.h"
#include "Scheduler.h"
#include "UI.h"
#include "Terminal.h"

static Display sysDisp;
static Encoder sysEnc(PIN_CLK, PIN_DT, PIN_SW);
UI ui(sysDisp, sysEnc);

#include "../../src/Display.cpp"
#include "../../src/Encoder.cpp"
#include "../../src/RTClock.cpp"
#include "../../src/History.cpp"
#include "../../src/WateringController.cpp"
#include "../../src/Scheduler.cpp"
#include "../../src/ui/MenuBuilder.cpp"
#include "../../src/ui/ScreenCommon.cpp"
#include "../../src/ui/ScreenEditors.cpp"
#include "../../src/ui/ScreenIdle.cpp"
#include "../../src/ui/ScreenMenu.cpp"
#include "../../src/ui/ScreenSetup.cpp"
#include "../../src/UI.cpp"
#include "../../src/Terminal.cpp"
#include "../../src/SystemBoot.cpp"

static I2CRecoveryState _sysI2cState = {};
static void sysRecoverI2C() {
    recoverI2CWith(sysDisp, rtclock, _sysI2cState);
}

void setUp(void) {
    resetAllTestMocks();
    _sysI2cState = {};
    digitalWrite(PIN_CLK, HIGH);
    digitalWrite(PIN_DT, HIGH);
    digitalWrite(PIN_SW, HIGH);
    rtclock = RTClock();
    storage = Storage();
    history = History();
    wateringCtrl = WateringController();
    scheduler = Scheduler();
    terminal = Terminal();
    storage.begin();
    sysDisp = Display();
    sysDisp.begin();
    sysEnc.~Encoder();
    new (&sysEnc) Encoder(PIN_CLK, PIN_DT, PIN_SW);
    ui.~UI();
    new (&ui) UI(sysDisp, sysEnc);
}

void tearDown(void) {
    wateringCtrl.stop();
}

struct UnitTestAccess {
static void test_init_relay_pins_safe_core3_periman(void) {
    memset(_mock_gpio, LOW, sizeof(_mock_gpio));
    memset(_mock_gpio_configured, 0, sizeof(_mock_gpio_configured));
    memset(_mock_gpio_glitch_low, 0, sizeof(_mock_gpio_glitch_low));
    _mock_enforce_periman = true;

    initRelayPinsSafe();

    const uint8_t pins[NUM_ZONES] = { PIN_RELAY_1, PIN_RELAY_2, PIN_RELAY_3, PIN_RELAY_4 };
    for (uint8_t i = 0; i < NUM_ZONES; i++) {
        TEST_ASSERT_TRUE(_mock_gpio_configured[pins[i]]);
        TEST_ASSERT_EQUAL_UINT8(OUTPUT, _mock_gpio_mode[pins[i]]);
        TEST_ASSERT_EQUAL_UINT8(RELAY_OFF, digitalRead(pins[i]));
        TEST_ASSERT_FALSE(_mock_gpio_glitch_low[pins[i]]);
    }
    _mock_enforce_periman = false;
}

static void test_clear_i2c_bus_preloads_scl_and_generates_stop(void) {
    memset(_mock_gpio, LOW, sizeof(_mock_gpio));
    memset(_mock_gpio_configured, 0, sizeof(_mock_gpio_configured));
    memset(_mock_gpio_glitch_low, 0, sizeof(_mock_gpio_glitch_low));
    _mock_gpio[PIN_SDA] = HIGH;
    _mock_enforce_periman = true;

    uint32_t usBefore = micros();
    bool ok = clearI2CBus();

    TEST_ASSERT_TRUE(ok);
    // SCL must be pre-loaded HIGH via gpio_set_level() before pinMode(OUTPUT_OPEN_DRAIN | PULLUP)
    TEST_ASSERT_FALSE(_mock_gpio_glitch_low[PIN_SCL]);
    TEST_ASSERT_EQUAL_UINT8(OUTPUT_OPEN_DRAIN | PULLUP, _mock_gpio_mode[PIN_SCL]);
    TEST_ASSERT_EQUAL_UINT8(INPUT_PULLUP, _mock_gpio_mode[PIN_SDA]);
    TEST_ASSERT_EQUAL_UINT8(HIGH, digitalRead(PIN_SCL));
    TEST_ASSERT_EQUAL_UINT8(HIGH, digitalRead(PIN_SDA));
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2000UL, micros() - usBefore);

    _mock_enforce_periman = false;
}

static void test_recover_i2c_with_throttle_and_wraparound(void) {
    I2CRecoveryState recState = {};
    Wire.beginCalls = 0;
    setMockMillis(5000);
    Serial.reset();

    // 1. First recovery runs immediately and re-initializes Wire, RTC, and LCD
    recoverI2CWith(sysDisp, rtclock, recState);
    TEST_ASSERT_TRUE(recState.hasRecovered);
    TEST_ASSERT_EQUAL_UINT32(1, Wire.beginCalls);
    TEST_ASSERT_TRUE(gState.rtc_valid);
#if LOG_LEVEL >= LVL_INFO
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_I2C_REC_DONE) != std::string::npos);
#endif

    // 2. Call within 10 s is throttled and notifies Display::onRecoveryThrottled()
    advanceMockMillis(4000);
    sysDisp._inErrorCallback = true;
    sysDisp._errorDeferred = false;
    recoverI2CWith(sysDisp, rtclock, recState);
    sysDisp._inErrorCallback = false;
    TEST_ASSERT_EQUAL_UINT32(1, Wire.beginCalls);
    TEST_ASSERT_TRUE(sysDisp._errorDeferred);

    // 3. After 10 s cooldown expires, recovery runs again
    advanceMockMillis(6000);
    recoverI2CWith(sysDisp, rtclock, recState);
    TEST_ASSERT_EQUAL_UINT32(2, Wire.beginCalls);

    // 4. 32-bit millis() wraparound keeps throttle accurate
    recState = {};
    setMockMillis(0xFFFFFFF0UL);
    recoverI2CWith(sysDisp, rtclock, recState);
    TEST_ASSERT_EQUAL_UINT32(3, Wire.beginCalls);

    advanceMockMillis(5000); // wraps to ~0x1378
    recoverI2CWith(sysDisp, rtclock, recState);
    TEST_ASSERT_EQUAL_UINT32(3, Wire.beginCalls); // still throttled

    advanceMockMillis(5000); // elapsed == 10000 ms across wrap
    recoverI2CWith(sysDisp, rtclock, recState);
    TEST_ASSERT_EQUAL_UINT32(4, Wire.beginCalls);
}

static void test_evaluate_boot_suspension_all_paths(void) {
    // 1. No suspension stored
    gState.suspended = true;
    gState.suspended_until = 0;
    evaluateBootSuspension();
    TEST_ASSERT_FALSE(gState.suspended);

    // 2. Stored suspension with invalid RTC: keep suspended_until intact without wiping NVS
    const uint32_t targetUnix = DateTime(2026, 6, 20, 12, 0, 0).unixtime();
    gState.rtc_valid = false;
    gState.suspended = false;
    gState.suspended_until = targetUnix;
    TEST_ASSERT_TRUE(storage.save());
    Serial.reset();
    evaluateBootSuspension();
    TEST_ASSERT_TRUE(gState.suspended);
    TEST_ASSERT_EQUAL_UINT32(targetUnix, gState.suspended_until);
#if LOG_LEVEL >= LVL_WARN
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_SUSP_KEPT_NO_CLK) != std::string::npos);
#endif

    // 3. Active future suspension with valid RTC stays active and logs end date
    gState.rtc_valid = true;
    gState.now.unix = targetUnix - 3600UL;
    gState.suspended = false;
    Serial.reset();
    evaluateBootSuspension();
    TEST_ASSERT_TRUE(gState.suspended);
    TEST_ASSERT_EQUAL_UINT32(targetUnix, gState.suspended_until);
#if LOG_LEVEL >= LVL_INFO
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_SUSP_EXP_CLR) == std::string::npos);
#endif

    // 4. Expired suspension with valid RTC clears RAM and persists cleared state to NVS
    gState.now.unix = targetUnix;
    Serial.reset();
    evaluateBootSuspension();
    TEST_ASSERT_FALSE(gState.suspended);
    TEST_ASSERT_EQUAL_UINT32(0UL, gState.suspended_until);
#if LOG_LEVEL >= LVL_INFO
    TEST_ASSERT_TRUE(Serial.outputBuffer.find(TXT_LOG_SUSP_EXP_CLR) != std::string::npos);
#endif
    initAppState();
    TEST_ASSERT_TRUE(storage.load());
    TEST_ASSERT_FALSE(gState.suspended);
    TEST_ASSERT_EQUAL_UINT32(0UL, gState.suspended_until);
}

inline static bool _afterI2cHookCalled = false;
static void testAfterI2cHook() {
    _afterI2cHookCalled = true;
}

static void test_boot_system_full_order_and_recovery_invariants(void) {
    // 1. Seed a saved config + active recovery state + future suspension in NVS,
    //    then run real bootSystem() and verify order-sensitive invariants:
    //    Wire.begin() called, afterI2cInit invoked, suspension evaluated BEFORE recovery,
    //    AUTO recovery blocked by active suspension, and UI starts on Idle.
    gState.setup_done = true;
    gState.mode = AppMode::MEDIO;
    gState.zones[0] = { "Z1", true, 5 };
    gState.zones[1].enabled = false;
    gState.zones[2].enabled = false;
    gState.zones[3].enabled = false;
    const uint32_t bootUtc = RTClock::localToUtc(DateTime(2026, 6, 15, 8, 5, 0)).unixtime();
    gState.suspended = true;
    gState.suspended_until = bootUtc + 3600UL;
    TEST_ASSERT_TRUE(storage.save());

    RecoveryState rs = {};
    rs.active = true;
    rs.start_unix_time = bootUtc - 300UL;
    rs.queueLen = 1;
    rs.queuePos = 0;
    rs.queue[0] = { 0, 5UL * 60000UL };
    rs.trigger = WaterTrigger::AUTO;
    TEST_ASSERT_TRUE(storage.saveRecoveryState(rs));

    // Reconstruct RAM singletons to simulate power-on while preserving NVS
    Wire.reset();
    _afterI2cHookCalled = false;
    rtclock = RTClock();
    rtclock._rtc.adjust(DateTime(bootUtc));
    storage = Storage();
    history = History();
    wateringCtrl = WateringController();
    scheduler = Scheduler();
    terminal = Terminal();
    sysDisp = Display();
    ui.~UI();
    new (&ui) UI(sysDisp, sysEnc);

    bootSystem({ sysDisp, sysEnc, ui, sysRecoverI2C, testAfterI2cHook });

    TEST_ASSERT_TRUE(_afterI2cHookCalled);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1, Wire.beginCalls);
    TEST_ASSERT_TRUE(gState.suspended);
    TEST_ASSERT_FALSE(wateringCtrl.isActive()); // suspension evaluated BEFORE recovery
    TEST_ASSERT_EQUAL_PTR(&ui.getScreenIdle(), ui._currentScreen);

    // 2. Active recovery suppresses scheduler._catchUpPending (scheduler.begin() runs AFTER wateringCtrl.begin())
    gState.suspended = false;
    gState.suspended_until = 0;
    TEST_ASSERT_TRUE(storage.save());
    TEST_ASSERT_TRUE(storage.saveRecoveryState(rs));

    rtclock = RTClock();
    rtclock._rtc.adjust(DateTime(bootUtc));
    storage = Storage();
    wateringCtrl = WateringController();
    scheduler = Scheduler();
    ui.~UI();
    new (&ui) UI(sysDisp, sysEnc);

    bootSystem({ sysDisp, sysEnc, ui, sysRecoverI2C, nullptr });
    TEST_ASSERT_TRUE(wateringCtrl.isActive());
    TEST_ASSERT_FALSE(scheduler._catchUpPending);
    wateringCtrl.stop();

    // 3. Corrupt both NVS config slots while an active RecoveryState exists ->
    //    bootSystem() must refuse to resume watering, preserve partial history, and surface config-lost alert
    history.clear();
    rs.zone_dur_min[0] = 3;
    TEST_ASSERT_TRUE(storage.saveRecoveryState(rs));
    uint8_t bad[44];
    memset(bad, 0xCC, sizeof(bad));
    prefs.putBytes("cfg", bad, sizeof(bad));
    prefs.putBytes("cfg_b", bad, sizeof(bad));
    rtclock = RTClock();
    rtclock._rtc.adjust(DateTime(bootUtc));
    storage = Storage();
    wateringCtrl = WateringController();
    scheduler = Scheduler();
    ui.~UI();
    new (&ui) UI(sysDisp, sysEnc);

    bootSystem({ sysDisp, sysEnc, ui, sysRecoverI2C, nullptr });
    TEST_ASSERT_TRUE(storage.configLoadFailed());
    TEST_ASSERT_FALSE(wateringCtrl.isActive());
    TEST_ASSERT_EQUAL_UINT8(RELAY_OFF, digitalRead(PIN_RELAY_1));
    TEST_ASSERT_EQUAL_UINT16(1, history.entryCount());
    TEST_ASSERT_TRUE(ui.inSetup());
    TEST_ASSERT_EQUAL_PTR(&ui.getScreenInfo(), ui._currentScreen);
    TEST_ASSERT_TRUE(strstr(sysDisp._lcd.getRow(0), TXT_ERR_CFG_LOST_TITLE) != nullptr);

    // 4. tickSystem() advances subsystems without error
    advanceMockMillis(1000);
    tickSystem(ui);
}

};

int main(void) {
    UNITY_BEGIN();
    RUN_UT(test_init_relay_pins_safe_core3_periman);
    RUN_UT(test_clear_i2c_bus_preloads_scl_and_generates_stop);
    RUN_UT(test_recover_i2c_with_throttle_and_wraparound);
    RUN_UT(test_evaluate_boot_suspension_all_paths);
    RUN_UT(test_boot_system_full_order_and_recovery_invariants);
    return UNITY_END();
}
