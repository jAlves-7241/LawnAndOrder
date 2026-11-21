#include "TestCommon.h"
#include "log.h"

// Define global mock variables
uint32_t _mock_millis = 1000;
uint32_t _mock_micros = 1000000;
uint8_t _mock_gpio[64] = {};
uint8_t _mock_gpio_mode[64] = {};
bool    _mock_gpio_configured[64] = {};
bool    _mock_gpio_glitch_low[64] = {};
bool    _mock_enforce_periman = false;
MockSerial Serial;
MockLittleFS LittleFS;

#ifndef HAS_TERMINAL_AWARE_LOG
// Terminal-aware log stub for tests that do not include Terminal.cpp:
// forward log lines to Serial so unit tests can assert log output.
void terminalAwareLog(const char* msg) {
    Serial.println(msg);
}
#endif

void resetAllTestMocks() {
    _mock_millis = 1000;
    _mock_micros = 1000000;
    memset(_mock_gpio, HIGH, sizeof(_mock_gpio));
    memset(_mock_gpio_mode, 0, sizeof(_mock_gpio_mode));
    memset(_mock_gpio_configured, 0, sizeof(_mock_gpio_configured));
    memset(_mock_gpio_glitch_low, 0, sizeof(_mock_gpio_glitch_low));
    _mock_enforce_periman = false;
    Serial.reset();
    LittleFS.reset();
    Preferences::resetStorage();
    Wire.reset();
    ESP.reset();
    _log_suspended = false;
    _log_suppressed_total = 0;
    _log_suppressed_warn_err = 0;
    initAppState();
}
