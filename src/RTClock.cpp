#include "RTClock.h"
#include "log.h"

RTClock rtclock;



static const char* DOW_NAMES[] = {
    "Dom","Seg","Ter","Qua","Qui","Sex","Sab"
};

// ─────────────────────────────────────────────────────────
RTClock::RTClock()
    : _found(false), _lostPower(false), _lastReadMs(0), _errorCb(nullptr)
{}

bool RTClock::begin() {
    if (!_rtc.begin()) {
        LOG_E("RTC", "Modulo nao encontrado - verificar I2C");
        _found = false;
        gState.rtc_valid = false;
        return false;
    }

    _found = true;

#ifdef WOKWI_SIM
    // DS1307 has no lostPower() - assume time is valid in simulation.
    // The Wokwi chip starts at a fixed epoch; any non-zero time is fine.
    _lostPower = false;
    gState.rtc_valid = true;
#else
    // DS3231: lostPower() is true when the oscillator stopped
    // (battery dead / first power-on).
    _lostPower = _rtc.lostPower();
    gState.rtc_valid = !_lostPower;

    if (_lostPower) {
        LOG_W("RTC", "Bateria descarregada ou 1a utilizacao - acertar hora");
        _rtc.adjust(DateTime(2026, 1, 1, 0, 0, 0));
    }
#endif

    _copyToState(_rtc.now());

    LOG_I("RTC", "OK - %04d-%02d-%02d %02d:%02d:%02d (%s)",
          gState.now.year,  gState.now.month,  gState.now.day,
          gState.now.hour,  gState.now.min,    gState.now.sec,
          DOW_NAMES[gState.now.dow % 7]);
    return true;
}

// ─────────────────────────────────────────────────────────
void RTClock::update() {
    if (!_found) return;

    uint32_t nowMs = millis();
    if ((nowMs - _lastReadMs) < 1000UL) return;
    _lastReadMs = nowMs;

#ifndef WOKWI_SIM
    // Re-check oscillator health - detects mid-run battery failure.
    if (_rtc.lostPower()) {
        if (gState.rtc_valid) {
            gState.rtc_valid = false;
            LOG_E("RTC", "Falha na bateria em operacao");
        }
    }
#endif

    DateTime utcDt = _rtc.now();

#ifndef WOKWI_SIM
    // Detetar barramento I2C preso ou corrompido (ano fora dos limites de 2020-2099)
    static uint8_t consecErrors = 0;
    if (utcDt.year() < 2020 || utcDt.year() > 2099) {
        consecErrors++;
        if (consecErrors >= 3) {
            LOG_E("RTC", "I2C com leituras corrompidas (%04d-%02d-%02d). A recuperar barramento...",
                  utcDt.year(), utcDt.month(), utcDt.day());
            if (_errorCb) {
                _errorCb();
            }
            consecErrors = 0;
        }
        return; // Aborta o update para proteger o gState contra dados corrompidos
    } else {
        consecErrors = 0;
    }
#endif

    DateTime localDt = utcDt;

    if (gState.auto_dst && _isEU_DST(utcDt)) {
        // Apply +1 hour offset
        localDt = DateTime(utcDt.unixtime() + 3600);
    }

    _copyToState(localDt);

    // Override the Unix timestamp with the actual UTC timestamp.
    // This makes time deltas (e.g., suspension) immune to DST transitions.
    gState.now.unix = utcDt.unixtime();
}

// ─────────────────────────────────────────────────────────
void RTClock::set(uint16_t year, uint8_t month,  uint8_t day,
                  uint8_t  hour, uint8_t minute, uint8_t second) {
    if (!_found) return;

    // Preservar a suspensão mantendo a diferença de tempo absoluta
    uint32_t oldUnixUTC = gState.now.unix;

    // A hora recebida do utilizador é HORA LOCAL.
    DateTime localDt(year, month, day, hour, minute, second);
    DateTime utcDt = localDt;

    // Converter para UTC se o DST estiver ativo e aplicável a esta hora
    if (gState.auto_dst) {
        // Fast heuristic for Local -> UTC DST check
        bool isDstLocal = false;
        if (month > 3 && month < 10) isDstLocal = true;
        else if (month == 3 || month == 10) {
            uint8_t ls = 31 - DateTime(year, month, 31, 0, 0, 0).dayOfTheWeek();
            if (month == 3) {
                if (day > ls || (day == ls && hour >= 2)) isDstLocal = true;
            } else {
                if (day < ls || (day == ls && hour < 2)) isDstLocal = true;
            }
        }
        if (isDstLocal) {
            utcDt = DateTime(localDt.unixtime() - 3600);
        }
    }

    _rtc.adjust(utcDt);
    _lostPower       = false;
    gState.rtc_valid = true;

    // Forçar releitura para gState
    _lastReadMs = 0;
    update();

    // Reajustar o `suspended_until` com base no delta UTC (imune a saltos de fuso)
    if (gState.suspended && gState.suspended_until > 0 && oldUnixUTC > 0) {
        int64_t delta = (int64_t)gState.now.unix - (int64_t)oldUnixUTC;
        int64_t newUntil = (int64_t)gState.suspended_until + delta;
        // Se a nova hora ultrapassou a meta de suspensão, ou overflow negativo
        if (newUntil <= (int64_t)gState.now.unix || newUntil < 0) {
            gState.suspended = false;
            gState.suspended_until = 0;
        } else {
            gState.suspended_until = (uint32_t)newUntil;
        }
    }

    LOG_I("RTC", "Hora (Local) definida: %04d-%02d-%02d %02d:%02d:%02d",
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

// Retorna true se a hora (UTC) cai no período de horário de verão europeu
bool RTClock::_isEU_DST(const DateTime& dt) {
    uint8_t m = dt.month();
    if (m > 3 && m < 10) return true;
    if (m < 3 || m > 10) return false;
    
    // Calcular o último domingo do mês (março ou outubro)
    uint8_t ls = 31 - DateTime(dt.year(), m, 31, 0, 0, 0).dayOfTheWeek();
    
    if (m == 3) {
        // DST começa no último domingo de março à 01:00 UTC
        if (dt.day() > ls || (dt.day() == ls && dt.hour() >= 1)) return true;
    } else {
        // DST termina no último domingo de outubro à 01:00 UTC
        if (dt.day() < ls || (dt.day() == ls && dt.hour() < 1)) return true;
    }
    return false;
}
