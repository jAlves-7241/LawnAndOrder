#include "TestCommon.h"

// Define global mock variables
uint32_t _mock_millis = 1000;
uint32_t _mock_micros = 1000000;
MockSerial Serial;
MockLittleFS LittleFS;

// Terminal-aware log stub
void terminalAwareLog(const char* msg) {
    (void)msg;
}

void resetAllTestMocks() {
    _mock_millis = 1000;
    _mock_micros = 1000000;
    Serial.reset();
    LittleFS.reset();
    Preferences::resetStorage();
    initAppState();
}
