#include <unity.h>
#include "TestCommon.h"
#include "../../test/mocks/TestCommon.cpp"
bool _log_suspended = false;
#include "../../src/AppState.cpp"

#define private public
#include "Encoder.h"
#undef private

#include "../../src/Encoder.cpp"

static Encoder encoder(PIN_CLK, PIN_DT, PIN_SW);

void setUp(void) {
    resetAllTestMocks();
    digitalWrite(PIN_CLK, HIGH);
    digitalWrite(PIN_DT, HIGH);
    digitalWrite(PIN_SW, HIGH);
    encoder.begin();
    encoder._delta = 0;
}

void tearDown(void) {}

void test_encoder_click_debounce_and_single_trigger(void) {
    // Initial state: button released (HIGH)
    TEST_ASSERT_FALSE(encoder.getClick());

    // 1. Press button (goes LOW)
    digitalWrite(PIN_SW, LOW);

    // Immediate read (< DEBOUNCE_MS=50ms) -> must NOT trigger yet
    TEST_ASSERT_FALSE(encoder.getClick());

    // Advance 20ms (< 50ms) -> still false
    advanceMockMillis(20);
    TEST_ASSERT_FALSE(encoder.getClick());

    // Advance another 35ms (total 55ms > 50ms) -> triggers exactly once
    advanceMockMillis(35);
    TEST_ASSERT_TRUE(encoder.getClick());

    // While holding button down -> subsequent reads return false
    advanceMockMillis(100);
    TEST_ASSERT_FALSE(encoder.getClick());
    advanceMockMillis(500);
    TEST_ASSERT_FALSE(encoder.getClick());

    // 2. Release button (goes HIGH)
    digitalWrite(PIN_SW, HIGH);
    TEST_ASSERT_FALSE(encoder.getClick());
    advanceMockMillis(60);
    TEST_ASSERT_FALSE(encoder.getClick()); // Release must not fire a click

    // 3. Press again -> second click triggers after debounce
    digitalWrite(PIN_SW, LOW);
    TEST_ASSERT_FALSE(encoder.getClick());
    advanceMockMillis(60);
    TEST_ASSERT_TRUE(encoder.getClick());
}

void test_encoder_rotation_delta_and_clamping(void) {
    // Initial delta should be 0
    TEST_ASSERT_EQUAL_INT8(0, encoder.getRotation());

    // 1. Positive step via ISR
    digitalWrite(PIN_DT, LOW); // DT == 0 means d = +1
    advanceMockMicros(3000);
    Encoder::_isr();
    TEST_ASSERT_EQUAL_INT8(1, encoder.getRotation());
    // Second read should be 0 (resets on read)
    TEST_ASSERT_EQUAL_INT8(0, encoder.getRotation());

    // 2. Negative step via ISR
    digitalWrite(PIN_DT, HIGH); // DT == 1 means d = -1
    advanceMockMicros(3000);
    Encoder::_isr();
    TEST_ASSERT_EQUAL_INT8(-1, encoder.getRotation());
    TEST_ASSERT_EQUAL_INT8(0, encoder.getRotation());

    // 3. Positive clamping (> 127) with preserved remainder
    encoder._delta = 150;
    int8_t rot = encoder.getRotation();
    TEST_ASSERT_EQUAL_INT8(127, rot);
    TEST_ASSERT_EQUAL_INT16(23, encoder._delta); // 150 - 127 = 23 remainder

    rot = encoder.getRotation();
    TEST_ASSERT_EQUAL_INT8(23, rot);
    TEST_ASSERT_EQUAL_INT16(0, encoder._delta);

    // 4. Negative clamping (< -127) with preserved remainder
    encoder._delta = -200;
    rot = encoder.getRotation();
    TEST_ASSERT_EQUAL_INT8(-127, rot);
    TEST_ASSERT_EQUAL_INT16(-73, encoder._delta); // -200 - (-127) = -73

    rot = encoder.getRotation();
    TEST_ASSERT_EQUAL_INT8(-73, rot);
    TEST_ASSERT_EQUAL_INT16(0, encoder._delta);
}

void test_encoder_isr_rate_limiting(void) {
    digitalWrite(PIN_DT, LOW);
    encoder._delta = 0;

    // First pulse at t=1000000us
    setMockMicros(1000000);
    Encoder::_isr();
    TEST_ASSERT_EQUAL_INT16(1, encoder._delta);

    // Second pulse only 500us later (< 2000us threshold) -> must be IGNORED
    advanceMockMicros(500);
    Encoder::_isr();
    TEST_ASSERT_EQUAL_INT16(1, encoder._delta);

    // Another pulse 1000us later (total 1500us < 2000us) -> must still be IGNORED
    advanceMockMicros(1000);
    Encoder::_isr();
    TEST_ASSERT_EQUAL_INT16(1, encoder._delta);

    // Pulse 2500us after first pulse (elapsed > 2000us) -> accepted!
    setMockMicros(1000000 + 2500);
    Encoder::_isr();
    TEST_ASSERT_EQUAL_INT16(2, encoder._delta);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_encoder_click_debounce_and_single_trigger);
    RUN_TEST(test_encoder_rotation_delta_and_clamping);
    RUN_TEST(test_encoder_isr_rate_limiting);
    return UNITY_END();
}
