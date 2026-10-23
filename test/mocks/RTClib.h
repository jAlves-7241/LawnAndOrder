#pragma once

#include <stdint.h>
#include <time.h>
#include "Arduino.h"

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

    DateTime(const DateTime& copy) {
        _unix = copy._unix;
        _y = copy._y;
        _m = copy._m;
        _d = copy._d;
        _hh = copy._hh;
        _mm = copy._mm;
        _ss = copy._ss;
    }

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
    bool begin() { return true; }
    bool lostPower() { return false; }
    void adjust(const DateTime& dt) { _current = dt; }
    DateTime now() { return _current; }
private:
    DateTime _current = DateTime(2026, 1, 1, 0, 0, 0);
};

class RTC_DS1307 {
public:
    bool begin() { return true; }
    void adjust(const DateTime& dt) { _current = dt; }
    DateTime now() { return _current; }
private:
    DateTime _current = DateTime(2026, 1, 1, 0, 0, 0);
};
