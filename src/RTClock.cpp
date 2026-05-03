#include "RTClock.h"

RTClock rtclock;

static const char* DOW_NAMES[] = {
    "Dom","Seg","Ter","Qua","Qui","Sex","Sab"
};

// ─────────────────────────────────────────────────────────
RTClock::RTClock()
    : _found(false), _lostPower(false), _lastReadMs(0)
{}

bool RTClock::begin() {
    if (!_rtc.begin()) {
        Serial.println("[RTC] Modulo nao encontrado — verificar I2C");
        _found = false;
        gState.rtc_valid = false;
        return false;
    }

    _found = true;

#ifdef WOKWI_SIM
    // DS1307 has no lostPower() — assume time is valid in simulation.
    // The Wokwi chip starts at a fixed epoch; any non-zero time is fine.
    _lostPower = false;
    gState.rtc_valid = true;
#else
    // DS3231: lostPower() is true when the oscillator stopped
    // (battery dead / first power-on).
    _lostPower = _rtc.lostPower();
    gState.rtc_valid = !_lostPower;

    if (_lostPower) {
        Serial.println("[RTC] Bateria descarregada ou 1a utilizacao — acertar hora");
        _rtc.adjust(DateTime(2026, 1, 1, 0, 0, 0));
    }
#endif

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

#ifndef WOKWI_SIM
    // Re-check oscillator health — detects mid-run battery failure.
    if (_rtc.lostPower()) {
        if (gState.rtc_valid) {
            gState.rtc_valid = false;
            Serial.println("[RTC] Erro: Falha na bateria em operacao");
        }
    }
#endif

    _copyToState(_rtc.now());
}

// ─────────────────────────────────────────────────────────
void RTClock::set(uint16_t year, uint8_t month,  uint8_t day,
                  uint8_t  hour, uint8_t minute, uint8_t second) {
    if (!_found) return;

    _rtc.adjust(DateTime(year, month, day, hour, minute, second));
    _lostPower       = false;
    gState.rtc_valid = true;

    _copyToState(_rtc.now());

    Serial.printf("[RTC] Hora definida: %04d-%02d-%02d %02d:%02d:%02d\n",
                  year, month, day, hour, minute, second);
}

void RTClock::setTime(uint8_t hour, uint8_t minute) {
    if (!_found) return;
    const SystemTime& t = gState.now;
    set(t.year, t.month, t.day, hour, minute, 0);
}

// ─────────────────────────────────────────────────────────
void RTClock::_copyToState(const DateTime& dt) {
    gState.now.year  = dt.year();
    gState.now.month = dt.month();
    gState.now.day   = dt.day();
    gState.now.hour  = dt.hour();
    gState.now.min   = dt.minute();
    gState.now.sec   = dt.second();
    gState.now.dow   = dt.dayOfTheWeek();
    gState.now.unix  = dt.unixtime();
}
