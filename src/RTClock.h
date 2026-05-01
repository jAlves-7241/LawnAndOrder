#pragma once
#include <Arduino.h>
#include <RTClib.h>       // Adafruit RTClib — provides RTC_DS3231
#include "AppState.h"

// ─────────────────────────────────────────────────────────
// RTClock
//
// Thin wrapper around the DS3231 module.
// Reads the chip once per second and writes into gState.now.
// Also exposes set() so the UI can push a new time.
//
// Usage:
//   setup()  → rtclock.begin()
//   loop()   → rtclock.update()
// ─────────────────────────────────────────────────────────
class RTClock {
public:
    RTClock();

    // Initialise I2C and DS3231.
    // Returns true on success; false if the module is not responding.
    // Sets gState.rtc_valid = false if the chip lost power (needs time set).
    bool begin();

    // Call every loop(). Reads the DS3231 into gState.now once per second.
    void update();

    // Set date and time.  The UI calls this after the user edits the time.
    void set(uint16_t year, uint8_t month,  uint8_t day,
             uint8_t  hour, uint8_t minute, uint8_t second);

    // Convenience overload — set only hour and minute, keep current date.
    void setTime(uint8_t hour, uint8_t minute);

    // True if the DS3231 was found and its oscillator is running.
    bool isValid() const { return _found && !_lostPower; }

private:
    RTC_DS3231 _rtc;
    bool       _found;
    bool       _lostPower;
    uint32_t   _lastReadMs;

    // Copy an RTClib DateTime into gState.now and update gState.rtc_valid.
    static void _copyToState(const DateTime& dt);
};

extern RTClock rtclock;
