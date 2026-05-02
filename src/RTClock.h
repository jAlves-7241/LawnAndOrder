#pragma once
#include <Arduino.h>
#include <RTClib.h>
#include "AppState.h"

// ─────────────────────────────────────────────────────────
// RTClock
//
// Wraps either RTC_DS3231 (real hardware) or RTC_DS1307
// (Wokwi simulation — no DS3231 part available).
// Select at compile time via -DWOKWI_SIM in platformio.ini.
//
// Usage:
//   setup() → rtclock.begin()
//   loop()  → rtclock.update()
// ─────────────────────────────────────────────────────────

#ifdef WOKWI_SIM
  using RTC_Chip = RTC_DS1307;
#else
  using RTC_Chip = RTC_DS3231;
#endif

class RTClock {
public:
    RTClock();

    // Initialise I2C and the RTC chip.
    // Returns true on success; false if the module is not responding.
    bool begin();

    // Call every loop(). Reads the chip into gState.now once per second.
    void update();

    // Set date and time.
    void set(uint16_t year, uint8_t month,  uint8_t day,
             uint8_t  hour, uint8_t minute, uint8_t second);

    // Set only hour and minute, keeping the current date.
    void setTime(uint8_t hour, uint8_t minute);

    // True if the chip was found and the oscillator is running.
    bool isValid() const { return _found && !_lostPower; }

private:
    RTC_Chip _rtc;
    bool     _found;
    bool     _lostPower;
    uint32_t _lastReadMs;

    static void _copyToState(const DateTime& dt);
};

extern RTClock rtclock;
