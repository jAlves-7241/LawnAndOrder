#include "RTClock.h"

RTClock rtclock;

// Day-of-week lookup used for Serial logging only
static const char* DOW_NAMES[] = {
    "Dom","Seg","Ter","Qua","Qui","Sex","Sab"
};

// ─────────────────────────────────────────────────────────
RTClock::RTClock()
    : _found(false), _lostPower(false), _lastReadMs(0)
{}

// ─────────────────────────────────────────────────────────
bool RTClock::begin() {
    if (!_rtc.begin()) {
        Serial.println("[RTC] DS3231 nao encontrado — verificar ligacoes I2C");
        _found = false;
        gState.rtc_valid = false;
        return false;
    }

    _found = true;

    // lostPower() returns true when the oscillator stopped (battery dead /
    // first power-on).  In that case the time is wrong and the user must set it.
    _lostPower = _rtc.lostPower();
    gState.rtc_valid = !_lostPower;

    if (_lostPower) {
        Serial.println("[RTC] Bateria descarregada ou primeira utilizacao — acertar hora");
        // Write a safe placeholder so the chip doesn't run at 2000-01-01 00:00
        // (RTClib default after lost-power).  The user will correct it via UI.
        _rtc.adjust(DateTime(2026, 1, 1, 0, 0, 0));
    }

    // Force an immediate read into gState
    _copyToState(_rtc.now());

    Serial.printf("[RTC] OK — %04d-%02d-%02d %02d:%02d:%02d (%s)\n",
                  gState.now.year,  gState.now.month,  gState.now.day,
                  gState.now.hour,  gState.now.min,    gState.now.sec,
                  DOW_NAMES[gState.now.dow % 7]);

    return true;
}

// ─────────────────────────────────────────────────────────
void RTClock::update() {
    if (!_found) return;

    if ((millis() - _lastReadMs) < 1000UL) return;
    _lastReadMs = millis();

    // Re-check oscillator health: lostPower() detects a mid-run battery failure.
    // If true, time is now unreliable — clear rtc_valid so the UI shows "--:--".
    if (_rtc.lostPower()) {
        if (gState.rtc_valid) {
            gState.rtc_valid = false;
            Serial.println("[RTC] Bateria falhou em operacao — acertar hora");
        }
    }

    _copyToState(_rtc.now());
}

// ─────────────────────────────────────────────────────────
void RTClock::set(uint16_t year, uint8_t month,  uint8_t day,
                  uint8_t  hour, uint8_t minute, uint8_t second) {
    if (!_found) return;

    _rtc.adjust(DateTime(year, month, day, hour, minute, second));
    _lostPower       = false;
    gState.rtc_valid = true;

    // Immediately refresh gState so the UI doesn't flicker
    _copyToState(_rtc.now());

    Serial.printf("[RTC] Hora definida: %04d-%02d-%02d %02d:%02d:%02d\n",
                  year, month, day, hour, minute, second);
}

// ─────────────────────────────────────────────────────────
void RTClock::setTime(uint8_t hour, uint8_t minute) {
    if (!_found) return;

    // Keep the current date, only update H:M (seconds reset to 0)
    const SystemTime& t = gState.now;
    set(t.year, t.month, t.day, hour, minute, 0);
}

// ─────────────────────────────────────────────────────────
// Private
// ─────────────────────────────────────────────────────────
void RTClock::_copyToState(const DateTime& dt) {
    gState.now.year  = dt.year();
    gState.now.month = dt.month();
    gState.now.day   = dt.day();
    gState.now.hour  = dt.hour();
    gState.now.min   = dt.minute();
    gState.now.sec   = dt.second();
    // RTClib dayOfTheWeek(): 0=Sun … 6=Sat — matches our convention
    gState.now.dow   = dt.dayOfTheWeek();
}
