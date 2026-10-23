#include "TestCommon.h"

// Define global mock variables
uint32_t _mock_millis = 1000;
uint32_t _mock_micros = 1000000;
uint8_t _mock_gpio[64] = {};
MockSerial Serial;
MockLittleFS LittleFS;

#ifndef HAS_TERMINAL_AWARE_LOG
// Terminal-aware log stub for tests that do not include Terminal.cpp
void terminalAwareLog(const char* msg) {
    (void)msg;
}
#endif

void resetAllTestMocks() {
    _mock_millis = 1000;
    _mock_micros = 1000000;
    memset(_mock_gpio, HIGH, sizeof(_mock_gpio));
    Serial.reset();
    LittleFS.reset();
    Preferences::resetStorage();
    Wire.reset();
    ESP.reset();
    _log_suspended = false;
    initAppState();
}
