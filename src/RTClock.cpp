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

    DateTime utcDt = _rtc.now();
    
    // Proteger o arranque contra barramento I2C preso ou corrompido
    if (utcDt.year() < 2020 || utcDt.year() > 2099) {
        LOG_E("RTC", "Leitura de arranque corrompida (%04d-%02d-%02d)",
              utcDt.year(), utcDt.month(), utcDt.day());
        _found = false;
        gState.rtc_valid = false;
        return false;
    }

    DateTime localDt = utcDt;
    if (gState.auto_dst && _isEU_DST(utcDt)) {
        localDt = DateTime(utcDt.unixtime() + 3600);
    }

    _copyToState(localDt);
    gState.now.unix = utcDt.unixtime();

    LOG_I("RTC", "OK - %04d-%02d-%02d %02d:%02d:%02d (%s)",
          gState.now.year,  gState.now.month,  gState.now.day,
          gState.now.hour,  gState.now.min,    gState.now.sec,
          DOW_NAMES[gState.now.dow % 7]);
    return true;
}

// ─────────────────────────────────────────────────────────
void RTClock::update() {
    uint32_t nowMs = millis();
    if ((nowMs - _lastReadMs) < 30000UL) {
        _incrementSoftwareClock();
        return; // 30s (ajustado de 1s para reduzir jitter no bus I2C)
    }
    _lastReadMs = nowMs;

    if (!_found) {
        // Tentar redetetar o RTC a quente (hotplug) a cada 10 segundos
        static uint32_t lastRtcRetryMs = 0;
        if (nowMs - lastRtcRetryMs >= 10000UL) {
            lastRtcRetryMs = nowMs;
            LOG_I("RTC", "A tentar detetar RTC a quente...");
            if (_rtc.begin()) {
                _found = true;
                #ifdef WOKWI_SIM
                    _lostPower = false;
                    if (!gState.rtc_valid) gState.rtc_valid = true;
                #else
                    _lostPower = _rtc.lostPower();
                    if (!gState.rtc_valid) gState.rtc_valid = !_lostPower;
                #endif
                
                LOG_I("RTC", "Modulo detetado a quente (hotplug) com sucesso!");
                
                DateTime utcDt = _rtc.now();
                if (utcDt.year() >= 2020 && utcDt.year() <= 2099 && !_lostPower) {
                    DateTime localDt = utcDt;
                    if (gState.auto_dst && _isEU_DST(utcDt)) {
                        localDt = DateTime(utcDt.unixtime() + 3600);
                    }
                    _copyToState(localDt);
                    gState.now.unix = utcDt.unixtime();
                    LOG_I("RTC", "Sincronizado com o RTC a quente: %04d-%02d-%02d %02d:%02d:%02d",
                          gState.now.year, gState.now.month, gState.now.day,
                          gState.now.hour, gState.now.min, gState.now.sec);
                } else {
                    // Se o RTC tem hora inválida mas o utilizador já configurou hora no software
                    if (gState.rtc_valid) {
                        set(gState.now.year, gState.now.month, gState.now.day,
                            gState.now.hour, gState.now.min, gState.now.sec);
                    }
                }
            }
        }

        // Se continua nao encontrado, mantem relogio por software
        if (!gState.rtc_valid) {
            // Garantir que temos uma data de arranque coerente em RAM
            if (gState.now.year < 2020) {
                gState.now.year = 2026;
                gState.now.month = 1;
                gState.now.day = 1;
                gState.now.hour = 0;
                gState.now.min = 0;
                gState.now.sec = 0;
                gState.now.dow = 4; // Quinta-feira
                gState.now.unix = 1767225600UL;
            }
        }
        _incrementSoftwareClock();
        return;
    }

#ifndef WOKWI_SIM
    // Re-check oscillator health - detects mid-run battery failure or EMI reset.
    if (_rtc.lostPower()) {
        if (gState.rtc_valid) {
            LOG_W("RTC", "Pilha falhou/Reset EMI detetado. A tentar restaurar RTC via RAM...");
            set(gState.now.year, gState.now.month, gState.now.day,
                gState.now.hour, gState.now.min, gState.now.sec);
        } else {
            LOG_E("RTC", "Falha na bateria e hora da RAM invalida");
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
            _found = false; // Força a re-deteção a quente (hotplug) no próximo ciclo
        }
        _incrementSoftwareClock(); // Impede que o relógio congele no display durante falhas I2C
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
    
    // Filtro contra underflow do epoch (corrupção por dados manuais ou software)
    if (year < 2020 || year > 2099) return;

    // Preservar a suspensão mantendo a diferença de tempo absoluta
    uint32_t oldUnixUTC = gState.now.unix;

    // A hora recebida do utilizador é HORA LOCAL.
    DateTime localDt(year, month, day, hour, minute, second);
    DateTime utcDt = localDt;

    // Converter para UTC se o DST estiver ativo e aplicável a esta hora
    if (gState.auto_dst) {
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

    if (_found) {
        _rtc.adjust(utcDt);
    } else {
        LOG_I("RTC", "A tentar gravar hora no RTC...");
        if (_rtc.begin()) {
            _found = true;
            _rtc.adjust(utcDt);
            LOG_I("RTC", "RTC ligado e hora gravada com sucesso!");
        }
    }

    _lostPower       = false;
    gState.rtc_valid = true;

    // Atualizar a RAM imediatamente (importante para fallback software)
    _copyToState(localDt);
    gState.now.unix = utcDt.unixtime();

    // Forçar releitura para gState
    _lastReadMs = 0;

    // Reajustar o `suspended_until` com base no delta UTC (imune a saltos de fuso)
    if (gState.suspended && gState.suspended_until > 0 && oldUnixUTC > 0) {
        int64_t delta = (int64_t)gState.now.unix - (int64_t)oldUnixUTC;
        int64_t newUntil = (int64_t)gState.suspended_until + delta;
        // Se a nova hora ultrapassou a meta de suspensão, ou overflow negativo
        if (newUntil > UINT32_MAX) {
            gState.suspended_until = UINT32_MAX;
        } else if (newUntil <= (int64_t)gState.now.unix || newUntil < 0) {
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
    const SystemTime& t = gState.now;
    set(t.year, t.month, t.day, hour, minute, 0);
}

void RTClock::_incrementSoftwareClock() {
    // Ancorar ao millis() para não perder segundos durante stalls do loop
    static uint32_t anchorMs   = millis();
    static uint32_t anchorUnix = gState.now.unix;
    static uint32_t lastKnownUnix = gState.now.unix;
    
    // Recalibrar âncora se o unix foi alterado externamente (ex: set())
    if (gState.now.unix != lastKnownUnix) {
        anchorMs   = millis();
        anchorUnix = gState.now.unix;
        lastKnownUnix = gState.now.unix;
    }

    uint32_t currentUnix = gState.now.unix;
    if (currentUnix < 1577836800UL) { // Menor que 2020-01-01
        currentUnix = 1767225600UL;   // 2026-01-01 00:00:00 UTC
        anchorMs   = millis();
        anchorUnix = currentUnix;
    }
    
    // Avançar a âncora a cada segundo para evitar o overflow de 49.7 dias do millis()
    uint32_t elapsedMs = millis() - anchorMs;
    if (elapsedMs >= 1000) {
        uint32_t elapsedSec = elapsedMs / 1000;
        anchorUnix += elapsedSec;
        anchorMs += elapsedSec * 1000;
    }
    
    currentUnix = anchorUnix;
    if (currentUnix == gState.now.unix) return; // Sem avanço

    DateTime utcDt(currentUnix);
    DateTime localDt = utcDt;
    if (gState.auto_dst && _isEU_DST(utcDt)) {
        localDt = DateTime(utcDt.unixtime() + 3600);
    }
    _copyToState(localDt);
    gState.now.unix = currentUnix;
    lastKnownUnix = currentUnix;
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

DateTime RTClock::utcToLocal(const DateTime& utcDt) {
    if (gState.auto_dst && _isEU_DST(utcDt)) {
        return DateTime(utcDt.unixtime() + 3600);
    }
    return utcDt;
}
