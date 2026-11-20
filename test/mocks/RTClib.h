#pragma once

#include <stdint.h>
#include <time.h>
#include "Arduino.h"
#include "Wire.h"

#define SECONDS_PER_DAY 86400UL
#define SECONDS_FROM_1970_TO_2000 946684800UL

class TimeSpan {
public:
    TimeSpan(int32_t seconds = 0) : _seconds(seconds) {}
    TimeSpan(int32_t days, int32_t hours, int32_t minutes, int32_t seconds) {
        _seconds = days * 86400L + hours * 3600L + minutes * 60L + seconds;
    }
    int32_t totalseconds() const { return _seconds; }
private:
    int32_t _seconds;
};

class DateTime {
public:
    DateTime(uint32_t t = SECONDS_FROM_1970_TO_2000) {
        _unix = t;
        _breakdown(t);
    }

    DateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour = 0, uint8_t min = 0, uint8_t sec = 0) {
        _y = year;
        _m = month;
        _d = day;
        _hh = hour;
        _mm = min;
        _ss = sec;
        _unix = _toUnix(year, month, day, hour, min, sec);
    }

    DateTime(const DateTime& copy) = default;
    DateTime& operator=(const DateTime& copy) = default;

    uint16_t year() const { return _y; }
    uint8_t month() const { return _m; }
    uint8_t day() const { return _d; }
    uint8_t hour() const { return _hh; }
    uint8_t minute() const { return _mm; }
    uint8_t second() const { return _ss; }

    uint8_t dayOfTheWeek() const {
        return (uint8_t)(((_unix / SECONDS_PER_DAY) + 4) % 7);
    }

    uint32_t unixtime() const { return _unix; }

    DateTime operator+(const TimeSpan& span) const {
        return DateTime(_unix + span.totalseconds());
    }

    DateTime operator-(const TimeSpan& span) const {
        return DateTime(_unix - span.totalseconds());
    }

    bool operator<(const DateTime& right) const { return _unix < right._unix; }
    bool operator<=(const DateTime& right) const { return _unix <= right._unix; }
    bool operator>(const DateTime& right) const { return _unix > right._unix; }
    bool operator>=(const DateTime& right) const { return _unix >= right._unix; }
    bool operator==(const DateTime& right) const { return _unix == right._unix; }
    bool operator!=(const DateTime& right) const { return _unix != right._unix; }

private:
    uint32_t _unix;
    uint16_t _y;
    uint8_t  _m, _d, _hh, _mm, _ss;

    static bool _isLeapYear(uint16_t y) {
        return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    }

    static uint8_t _daysInMonth(uint16_t y, uint8_t m) {
        static const uint8_t dim[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        if (m == 2 && _isLeapYear(y)) return 29;
        if (m >= 1 && m <= 12) return dim[m - 1];
        return 30;
    }

    static uint32_t _toUnix(uint16_t y, uint8_t m, uint8_t d, uint8_t hh, uint8_t mm, uint8_t ss) {
        uint32_t days = 0;
        for (uint16_t year = 1970; year < y; year++) {
            days += _isLeapYear(year) ? 366 : 365;
        }
        for (uint8_t mon = 1; mon < m; mon++) {
            days += _daysInMonth(y, mon);
        }
        days += (d - 1);
        return days * SECONDS_PER_DAY + (uint32_t)hh * 3600UL + (uint32_t)mm * 60UL + (uint32_t)ss;
    }

    void _breakdown(uint32_t u) {
        _ss = u % 60;
        u /= 60;
        _mm = u % 60;
        u /= 60;
        _hh = u % 24;
        uint32_t days = u / 24;

        uint16_t y = 1970;
        while (true) {
            uint16_t dy = _isLeapYear(y) ? 366 : 365;
            if (days >= dy) {
                days -= dy;
                y++;
            } else {
                break;
            }
        }
        _y = y;

        uint8_t m = 1;
        while (true) {
            uint8_t dm = _daysInMonth(_y, m);
            if (days >= dm) {
                days -= dm;
                m++;
            } else {
                break;
            }
        }
        _m = m;
        _d = (uint8_t)(days + 1);
    }
};

class RTC_DS3231 {
public:
    inline static RTC_DS3231* activeInstance = nullptr;

    bool mockPresent = true;
    bool mockLostPower = false;
    bool mockI2cReadFail = false;
    bool mockAdjustFail = false;
    bool mockRawOverride = false;
    uint8_t mockRawStatus = 0x00;
    uint8_t mockRawTime[7] = {};

    RTC_DS3231() {
        _registerWire();
    }
    RTC_DS3231(const RTC_DS3231& other)
        : mockPresent(other.mockPresent),
          mockLostPower(other.mockLostPower),
          mockI2cReadFail(other.mockI2cReadFail),
          mockAdjustFail(other.mockAdjustFail),
          mockRawOverride(other.mockRawOverride),
          mockRawStatus(other.mockRawStatus),
          _current(other._current),
          _regPtr(other._regPtr) {
        memcpy(mockRawTime, other.mockRawTime, sizeof(mockRawTime));
        _registerWire();
    }
    RTC_DS3231& operator=(const RTC_DS3231& other) {
        if (this != &other) {
            mockPresent = other.mockPresent;
            mockLostPower = other.mockLostPower;
            mockI2cReadFail = other.mockI2cReadFail;
            mockAdjustFail = other.mockAdjustFail;
            mockRawOverride = other.mockRawOverride;
            mockRawStatus = other.mockRawStatus;
            memcpy(mockRawTime, other.mockRawTime, sizeof(mockRawTime));
            _current = other._current;
            _regPtr = other._regPtr;
        }
        _registerWire();
        return *this;
    }
    ~RTC_DS3231() {
        if (activeInstance == this) {
            activeInstance = nullptr;
        }
    }

    bool begin() {
        _registerWire();
        return mockPresent;
    }
    bool lostPower() { return mockLostPower; }
    void adjust(const DateTime& dt) {
        if (mockAdjustFail) return;
        _current = dt;
        mockLostPower = false;
        mockRawOverride = false;
    }
    DateTime now() { return _current; }
    void resetMock() {
        mockPresent = true;
        mockLostPower = false;
        mockI2cReadFail = false;
        mockAdjustFail = false;
        mockRawOverride = false;
        mockRawStatus = 0x00;
        memset(mockRawTime, 0, sizeof(mockRawTime));
        _current = DateTime(2026, 1, 1, 0, 0, 0);
        _regPtr = 0;
        _registerWire();
    }
private:
    DateTime _current = DateTime(2026, 1, 1, 0, 0, 0);
    uint8_t _regPtr = 0;

    static uint8_t _bin2bcd(uint8_t v) {
        return (uint8_t)(v + 6U * (v / 10U));
    }

    void _registerWire() {
        activeInstance = this;
        Wire.setSlaveHandlers(0x68, &_wireTx, &_wireRx);
    }

    static uint8_t _wireTx(uint8_t /*addr*/, const uint8_t* txBuf, size_t txLen) {
        RTC_DS3231* inst = activeInstance;
        if (!inst || !inst->mockPresent) return 2;
        if (txLen > 0) {
            inst->_regPtr = txBuf[0];
        }
        return 0;
    }

    static size_t _wireRx(uint8_t /*addr*/, uint8_t* rxBuf, size_t qty) {
        RTC_DS3231* inst = activeInstance;
        if (!inst || !inst->mockPresent || inst->mockI2cReadFail) return 0;
        if (inst->_regPtr == 0x0F && qty == 1) {
            rxBuf[0] = inst->mockRawOverride ? inst->mockRawStatus
                                             : (inst->mockLostPower ? 0x80U : 0x00U);
            return 1;
        }
        if (inst->_regPtr == 0x00 && qty == 7) {
            if (inst->mockRawOverride) {
                memcpy(rxBuf, inst->mockRawTime, 7);
            } else {
                const DateTime& dt = inst->_current;
                rxBuf[0] = _bin2bcd(dt.second());
                rxBuf[1] = _bin2bcd(dt.minute());
                rxBuf[2] = _bin2bcd(dt.hour());
                rxBuf[3] = (uint8_t)((dt.dayOfTheWeek() == 0) ? 7 : dt.dayOfTheWeek());
                rxBuf[4] = _bin2bcd(dt.day());
                rxBuf[5] = _bin2bcd(dt.month());
                uint16_t y = (dt.year() >= 2000) ? (dt.year() - 2000U) : 0U;
                rxBuf[6] = _bin2bcd((uint8_t)(y % 100U));
            }
            return 7;
        }
        return 0;
    }
};
