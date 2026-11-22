#include <unity.h>
#include "TestCommon.h"
#include "../../test/mocks/TestCommon.cpp"
#include "../../src/AppState.cpp"
#include "../../src/Storage.cpp"

#define private public
#include "Display.h"
#include "RTClock.h"
#undef private

#include "../../src/Display.cpp"
#include "../../src/RTClock.cpp"
#include "../../src/SystemBoot.cpp"

static Display sysDisp;

void setUp(void) {
    resetAllTestMocks();
    rtclock = RTClock();
    storage = Storage();
    storage.begin();
    sysDisp = Display();
    sysDisp.begin();
}

void tearDown(void) {}

void test_init_relay_pins_safe_core3_periman(void) {
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

void test_clear_i2c_bus_preloads_scl_and_generates_stop(void) {
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

void test_recover_i2c_with_throttle_and_wraparound(void) {
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

void test_evaluate_boot_suspension_all_paths(void) {
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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_relay_pins_safe_core3_periman);
    RUN_TEST(test_clear_i2c_bus_preloads_scl_and_generates_stop);
    RUN_TEST(test_recover_i2c_with_throttle_and_wraparound);
    RUN_TEST(test_evaluate_boot_suspension_all_paths);
    return UNITY_END();
}
