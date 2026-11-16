#pragma once
#include <Arduino.h>
#include <RTClib.h>
#include "AppState.h"

// ─────────────────────────────────────────────────────────
// RTClock
//
// Wraps RTC_DS3231 (compatible with Wokwi DS1307 simulation as well
// since registers 0x00-0x06 match and 0x0F returns 0x00 on DS1307).
//
// Usage:
//   setup() → rtclock.begin()
//   loop()  → rtclock.update()
// ─────────────────────────────────────────────────────────

class RTClock {
public:
    using ErrorCallback = void (*)();

    RTClock();

    // Initialise I2C and the RTC chip.
    // Returns true on success; false if the module is not responding.
    bool begin();

    // Call every loop(). Reads the RTC about every 30 seconds and advances
    // gState.now with a software clock between hardware reads.
    void update();

    // Set date and time. Returns true if hardware RTC was updated, false if only RAM fallback.
    bool set(uint16_t year, uint8_t month,  uint8_t day,
             uint8_t  hour, uint8_t minute, uint8_t second);

    // Set only hour and minute, keeping the current date.
    void setTime(uint8_t hour, uint8_t minute);

    // Convert UTC DateTime to Local DateTime using DST logic.
    static DateTime utcToLocal(const DateTime& utcDt);

    // Convert Local DateTime to UTC DateTime using DST logic.
    // When afterUtc > 0, resolves the October ambiguous repeated hour relative to afterUtc.
    static DateTime localToUtc(const DateTime& localDt, uint32_t afterUtc = 0);

    // Recompute gState.now local calendar/clock fields from gState.now.unix (e.g. after auto_dst changes).
    void refreshLocalTime();

    // Attempt immediate hardware detection when not yet found (e.g. during Setup Wizard).
    bool probe();

    // True if the chip was found and the oscillator is running.
    bool isValid() const { return _found && !_lostPower; }

    // True if the hardware module is physically detected.
    bool isPresent() const { return _found; }
    // True if RTC power loss (OSF) was detected; clears on set() or clean recovery/boot.
    bool isBatteryLow() const { return _batteryLow; }
    uint32_t revision() const { return _revision; }
    uint32_t manualRevision() const { return _manualRevision; }

    void setErrorCallback(ErrorCallback cb) { _errorCb = cb; }

private:
    RTC_DS3231 _rtc;
    bool     _found;
    bool     _lostPower;
    bool     _batteryLow = false;
    bool     _batRecoveryWarned = false;
    uint32_t _lastReadMs;
    uint32_t _lastRtcRetryMs;
    uint8_t  _consecErrors;
    uint32_t _anchorMs;
    uint32_t _anchorUnix;
    uint32_t _lastKnownUnix;
    uint32_t _revision = 0;
    uint32_t _manualRevision = 0;
    ErrorCallback _errorCb;

    void _incrementSoftwareClock();
    DateTime _readRtc(bool& ok, bool& lostPower);
    DateTime _adjustAndVerify(const DateTime& utcDt, bool& ok);

    static void _copyToState(const DateTime& dt);
    static bool _isEU_DST(const DateTime& dt);
    static bool _isValidDateTime(const DateTime& dt);
};

extern RTClock rtclock;
