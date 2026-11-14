#include "RTClock.h"
#include "Storage.h"
#include "log.h"
#include "i18n.h"

RTClock rtclock;



#if LOG_LEVEL >= LVL_INFO
static const char* DOW_NAMES[] = {
    TXT_SER_DOW_SUN_SHORT, TXT_SER_DOW_MON_SHORT, TXT_SER_DOW_TUE_SHORT, TXT_SER_DOW_WED_SHORT, TXT_SER_DOW_THU_SHORT, TXT_SER_DOW_FRI_SHORT, TXT_SER_DOW_SAT_SHORT
};
#endif

// ─────────────────────────────────────────────────────────
RTClock::RTClock()
    : _found(false), _lostPower(false), _batRecoveryWarned(false), _lastReadMs(0),
      _lastRtcRetryMs(0), _consecErrors(0),
      _anchorMs(0), _anchorUnix(0), _lastKnownUnix(0),
      _errorCb(nullptr)
{}

bool RTClock::begin() {
    const bool wasValid = gState.rtc_valid;
    const SystemTime prevTime = gState.now;
    _consecErrors = 0;

    if (!_rtc.begin()) {
        LOG_E(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_NOT_FOUND);
        _found = false;
        if (!wasValid) {
            gState.rtc_valid = false;
        }
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

    if (_lostPower) {
        if (wasValid && prevTime.year >= 2020 && prevTime.year <= 2099) {
            LOG_W(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_BAT_FAIL_REC);
            DateTime restoredUtc = (prevTime.unix >= 1577836800UL)
                ? DateTime(prevTime.unix)
                : localToUtc(DateTime(prevTime.year, prevTime.month, prevTime.day,
                                      prevTime.hour, prevTime.min, prevTime.sec));
            _rtc.adjust(restoredUtc);
            _lostPower = false;
            _batRecoveryWarned = false;
            gState.rtc_valid = true;
        } else {
            LOG_W(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_BAT_LOW);
            gState.rtc_valid = false;
        }
    } else {
        _batRecoveryWarned = false;
        gState.rtc_valid = true;
    }
#endif

    DateTime utcDt = _rtc.now();
    
    // Proteger o arranque contra barramento I2C preso ou corrompido
    if (_lostPower && !wasValid) {
        if (utcDt.month() < 1 || utcDt.month() > 12 || utcDt.day() < 1 || utcDt.day() > 31) {
            LOG_E(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_READ_CORRUPT,
                  (int)utcDt.year(), (int)utcDt.month(), (int)utcDt.day(),
                  (int)utcDt.hour(), (int)utcDt.minute(), (int)utcDt.second());
            _found = false;
            gState.rtc_valid = false;
            return false;
        }
        // Chip presente com OSF ativo e relogio ainda nao configurado: preservar OSF ate o utilizador acertar a hora
        if (_isValidDateTime(utcDt)) {
            _copyToState(utcToLocal(utcDt));
            gState.now.unix = utcDt.unixtime();
        } else if (gState.now.year < DATE_YEAR_MIN) {
            _copyToState(DateTime(2026, 1, 1, 0, 0, 0));
            gState.now.unix = 1767225600UL;
        }
        _lastReadMs = millis();
        _anchorMs = _lastReadMs;
        _anchorUnix = gState.now.unix;
        _lastKnownUnix = gState.now.unix;
        return true;
    }

    if (!_isValidDateTime(utcDt)) {
        LOG_E(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_READ_CORRUPT,
              (int)utcDt.year(), (int)utcDt.month(), (int)utcDt.day(),
              (int)utcDt.hour(), (int)utcDt.minute(), (int)utcDt.second());
        _found = false;
        if (wasValid) {
            gState.now = prevTime;
            gState.rtc_valid = true;
        } else {
            gState.rtc_valid = false;
        }
        return false;
    }

    DateTime localDt = utcToLocal(utcDt);

    _copyToState(localDt);
    gState.now.unix = utcDt.unixtime();
    _lastReadMs = millis();
    _anchorMs = _lastReadMs;
    _anchorUnix = gState.now.unix;
    _lastKnownUnix = gState.now.unix;

    LOG_I(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_OK,
          (int)gState.now.year,  (int)gState.now.month,  (int)gState.now.day,
          (int)gState.now.hour,  (int)gState.now.min,    (int)gState.now.sec,
          DOW_NAMES[gState.now.dow % 7]);
    return true;
}

bool RTClock::probe() {
    if (_found) return true;
    return begin();
}

void RTClock::refreshLocalTime() {
    if (gState.now.unix < 1577836800UL) return;
    DateTime localDt = utcToLocal(DateTime(gState.now.unix));
    _copyToState(localDt);
}

// ─────────────────────────────────────────────────────────
void RTClock::update() {
    uint32_t nowMs = millis();
    if (_log_suspended || (nowMs - _lastReadMs) < 30000UL) {
        _incrementSoftwareClock();
        return; // 30s (ajustado de 1s para reduzir jitter no bus I2C)
    }
    _lastReadMs = nowMs;

    if (!_found) {
        // Retry hotplug detection at most once per 30s update interval
        if (nowMs - _lastRtcRetryMs >= 30000UL) {
            _lastRtcRetryMs = nowMs;
            LOG_D(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_HOT_DETECT);
            if (_rtc.begin()) {
                _found = true;
                bool wasValid = gState.rtc_valid;
                #ifdef WOKWI_SIM
                    _lostPower = false;
                #else
                    _lostPower = _rtc.lostPower();
                #endif
                
                LOG_I(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_HOT_FOUND);
                
                DateTime utcDt = _rtc.now();
                if (!_lostPower && _isValidDateTime(utcDt)) {
                    _batRecoveryWarned = false;
                    gState.rtc_valid = true;
                    DateTime localDt = utcToLocal(utcDt);
                    _copyToState(localDt);
                    gState.now.unix = utcDt.unixtime();
                    _anchorMs = nowMs;
                    _anchorUnix = gState.now.unix;
                    _lastKnownUnix = gState.now.unix;
                    _revision++;
                    LOG_I(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_HOT_SYNC,
                          (int)gState.now.year, (int)gState.now.month, (int)gState.now.day,
                          (int)gState.now.hour, (int)gState.now.min, (int)gState.now.sec);

                } else if (wasValid) {
                    // Se o RTC tem hora invalida mas o utilizador ja configurou hora no software
                    if (gState.now.unix >= 1577836800UL) {
                        _rtc.adjust(DateTime(gState.now.unix));
                        _lostPower = false;
                        _batRecoveryWarned = false;
                        _revision++;
                        LOG_I(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_HOT_RESTORED);
                    } else {
                        set(gState.now.year, gState.now.month, gState.now.day,
                            gState.now.hour, gState.now.min, gState.now.sec);
                    }
                } else {
                    gState.rtc_valid = false;
                    if (utcDt.month() < 1 || utcDt.month() > 12 || utcDt.day() < 1 || utcDt.day() > 31) {
                        LOG_E(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_READ_CORRUPT,
                              (int)utcDt.year(), (int)utcDt.month(), (int)utcDt.day(),
                              (int)utcDt.hour(), (int)utcDt.minute(), (int)utcDt.second());
                        _found = false;
                    } else if (_lostPower) {
                        LOG_W(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_BAT_LOW);
                    } else {
                        LOG_E(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_READ_CORRUPT,
                              (int)utcDt.year(), (int)utcDt.month(), (int)utcDt.day(),
                              (int)utcDt.hour(), (int)utcDt.minute(), (int)utcDt.second());
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

    bool recoveredPowerLoss = false;
#ifndef WOKWI_SIM
    // Re-check oscillator health - detects mid-run battery failure or EMI reset.
    if (_rtc.lostPower()) {
        if (gState.rtc_valid) {
            recoveredPowerLoss = true;
            if (!_batRecoveryWarned) {
                LOG_W(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_BAT_FAIL_REC);
                _batRecoveryWarned = true;
            }
            if (gState.now.unix >= 1577836800UL) {
                DateTime restoredUtc(gState.now.unix);
                _rtc.adjust(restoredUtc);
                _lostPower = false;
            } else {
                set(gState.now.year, gState.now.month, gState.now.day,
                    gState.now.hour, gState.now.min, gState.now.sec);
                _batRecoveryWarned = true;
            }
            _lastReadMs = nowMs;   // internal recovery must not defeat the 30 s throttle
        } else {
            if (!_lostPower) {
                LOG_E(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_BAT_FAIL_RAM);
            }
            _lostPower = true;
            _incrementSoftwareClock();
            return;
        }
    }
#endif

    DateTime utcDt = _rtc.now();

#ifndef WOKWI_SIM
    // Detetar barramento I2C preso ou corrompido (campos fora dos limites validos)
    if (!_isValidDateTime(utcDt)) {
        _consecErrors++;
        if (_consecErrors >= 3) {
            LOG_E(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_I2C_CORRUPT,
                  (int)utcDt.year(), (int)utcDt.month(), (int)utcDt.day(),
                  (int)utcDt.hour(), (int)utcDt.minute(), (int)utcDt.second());
            _consecErrors = 0;
            _found = false; // Forca a re-detecao a quente (hotplug) no proximo ciclo
            if (_errorCb) {
                _errorCb();
            }
        } else {
            // Retry faster with backoff (~5s) instead of waiting 30s
            _lastReadMs = nowMs - (30000UL - 5000UL);
        }
        _incrementSoftwareClock(); // Impede que o relogio congele no display durante falhas I2C
        return; // Aborta o update para proteger o gState contra dados corrompidos
    } else {
        _consecErrors = 0;
    }
#endif

    _lostPower = false;
    if (!recoveredPowerLoss) {
        _batRecoveryWarned = false;
    }

    uint32_t newUnix = utcDt.unixtime();
    if (gState.rtc_valid && !recoveredPowerLoss && _anchorUnix >= 1577836800UL) {
        uint32_t elapsedSec = (nowMs - _anchorMs) / 1000UL;
        uint32_t expectedUnix = _anchorUnix + elapsedSec;
        int64_t drift = (int64_t)newUnix - (int64_t)expectedUnix;
        if (drift > 300 || drift < -300) {
            _revision++;
            LOG_W(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_TIME_JUMP);
        }
    }
    gState.rtc_valid = true;

    DateTime localDt = utcToLocal(utcDt);
    _copyToState(localDt);

    // Override the Unix timestamp with the actual UTC timestamp.
    // This makes time deltas (e.g., suspension) immune to DST transitions.
    gState.now.unix = newUnix;
    _anchorMs = nowMs;
    _anchorUnix = gState.now.unix;
    _lastKnownUnix = gState.now.unix;
}

// ─────────────────────────────────────────────────────────
bool RTClock::set(uint16_t year, uint8_t month,  uint8_t day,
                  uint8_t  hour, uint8_t minute, uint8_t second) {
    
    // Filtro contra underflow/campos invalidos (corrupcao por dados manuais ou software)
    if (year < DATE_YEAR_MIN || year > DATE_YEAR_MAX) {
        LOG_W(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_INVALID_SET,
              (unsigned)year, (unsigned)month, (unsigned)day,
              (unsigned)hour, (unsigned)minute, (unsigned)second);
        return false;
    }
    DateTime localDt(year, month, day, hour, minute, second);
    if (!_isValidDateTime(localDt)) {
        LOG_W(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_INVALID_SET,
              (unsigned)year, (unsigned)month, (unsigned)day,
              (unsigned)hour, (unsigned)minute, (unsigned)second);
        return false;
    }

    // Preservar a suspensao mantendo a diferenca de tempo absoluta
    uint32_t oldUnixUTC = gState.now.unix;
    bool wasRtcValid = gState.rtc_valid;

    // A hora recebida do utilizador e HORA LOCAL.
    DateTime utcDt = localToUtc(localDt);
    DateTime effectiveLocal = utcToLocal(utcDt);

    bool hwOk = false;
    if (_found) {
        #ifdef WOKWI_SIM
            _rtc.adjust(utcDt);
            hwOk = true;
        #else
            if (_rtc.begin()) {
                _rtc.adjust(utcDt);
                hwOk = true;
            } else {
                _found = false;
            }
        #endif
    } else {
        LOG_D(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_SAVE_ATTEMPT);
        if (_rtc.begin()) {
            _found = true;
            _rtc.adjust(utcDt);
            hwOk = true;
        }
    }

    if (hwOk) {
        _lostPower = false;
        _batRecoveryWarned = false;
        _lastReadMs = millis() - 30000UL;
    } else {
        _lastRtcRetryMs = millis();
    }
    gState.rtc_valid = true;

    // Atualizar a RAM imediatamente (importante para fallback software)
    _copyToState(effectiveLocal);
    gState.now.unix = utcDt.unixtime();
    _anchorMs = millis();
    _anchorUnix = gState.now.unix;
    _lastKnownUnix = gState.now.unix;

    // Reajustar o `suspended_until`
    if (gState.suspended && gState.suspended_until > 0) {
        if (wasRtcValid && oldUnixUTC > 0) {
            // Se o relogio anterior ja era valido, reajustar com base no delta UTC (imune a saltos de fuso)
            int64_t delta = (int64_t)gState.now.unix - (int64_t)oldUnixUTC;
            int64_t newUntil = (int64_t)gState.suspended_until + delta;
            // Se a nova hora ultrapassou a meta de suspensao, ou overflow negativo
            if (newUntil > UINT32_MAX) {
                gState.suspended_until = UINT32_MAX;
            } else if (newUntil <= (int64_t)gState.now.unix || newUntil < 0) {
                gState.suspended = false;
                gState.suspended_until = 0;
                LOG_I(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SUSP_CLR_TIME_SET);
            } else {
                gState.suspended_until = (uint32_t)newUntil;
            }
            storage.save();
        } else {
            // Se o relogio anterior era invalido (placeholder de boot), avaliar o prazo contra a nova hora real
            if (gState.now.unix >= gState.suspended_until) {
                gState.suspended = false;
                gState.suspended_until = 0;
                storage.save();
                LOG_I(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SUSP_EXP_CLR);
            }
        }
    }

    if (hwOk) {
        LOG_I(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_TIME_SET,
              (int)effectiveLocal.year(), (int)effectiveLocal.month(),
              (int)effectiveLocal.day(),  (int)effectiveLocal.hour(),
              (int)effectiveLocal.minute(), (int)effectiveLocal.second());
    } else {
        LOG_W(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_TIME_SET_SW,
              (int)effectiveLocal.year(), (int)effectiveLocal.month(),
              (int)effectiveLocal.day(),  (int)effectiveLocal.hour(),
              (int)effectiveLocal.minute(), (int)effectiveLocal.second());
    }
    _revision++;
    return hwOk;
}

void RTClock::setTime(uint8_t hour, uint8_t minute) {
    const SystemTime& t = gState.now;
    set(t.year, t.month, t.day, hour, minute, 0);
}

void RTClock::_incrementSoftwareClock() {
    // Ancorar ao millis() para não perder segundos durante stalls do loop
    // Recalibrar âncora se o unix foi alterado externamente (ex: set())
    if (_anchorUnix == 0 || gState.now.unix != _lastKnownUnix) {
        _anchorMs      = millis();
        _anchorUnix    = gState.now.unix;
        _lastKnownUnix = gState.now.unix;
    }

    uint32_t currentUnix = gState.now.unix;
    if (currentUnix < 1577836800UL) { // Menor que 2020-01-01
        currentUnix    = 1767225600UL; // 2026-01-01 00:00:00 UTC
        _anchorMs      = millis();
        _anchorUnix    = currentUnix;
        _lastKnownUnix = currentUnix;
    }
    
    // Avançar a âncora a cada segundo para evitar o overflow de 49.7 dias do millis()
    uint32_t elapsedMs = millis() - _anchorMs;
    if (elapsedMs >= 1000) {
        uint32_t elapsedSec = elapsedMs / 1000;
        _anchorUnix += elapsedSec;
        _anchorMs   += elapsedSec * 1000;
    }
    
    currentUnix = _anchorUnix;
    if (currentUnix == gState.now.unix) return; // Sem avanço

    DateTime utcDt(currentUnix);
    DateTime localDt = utcToLocal(utcDt);
    _copyToState(localDt);
    gState.now.unix = currentUnix;
    _lastKnownUnix  = currentUnix;
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

bool RTClock::_isValidDateTime(const DateTime& dt) {
    uint16_t y  = dt.year();
    uint8_t  m  = dt.month();
    uint8_t  d  = dt.day();
    uint8_t  hh = dt.hour();
    uint8_t  mm = dt.minute();
    uint8_t  ss = dt.second();
    if (y < DATE_YEAR_MIN || y > DATE_YEAR_MAX) return false;
    if (m < 1 || m > 12) return false;
    static const uint8_t kDaysInMonth[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint8_t maxD = kDaysInMonth[m - 1];
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0))) {
        maxD = 29;
    }
    if (d < 1 || d > maxD) return false;
    if (hh > 23 || mm > 59 || ss > 59) return false;
    return true;
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
    uint32_t localUnix = (uint32_t)((int64_t)utcDt.unixtime() + (int64_t)TIMEZONE_OFFSET * 3600LL);
    if (gState.auto_dst && _isEU_DST(utcDt)) {
        localUnix += 3600UL;
    }
    return DateTime(localUnix);
}

DateTime RTClock::localToUtc(const DateTime& localDt, uint32_t afterUtc) {
    uint32_t stdUtcUnix = (uint32_t)((int64_t)localDt.unixtime() - (int64_t)TIMEZONE_OFFSET * 3600LL);
    if (!gState.auto_dst) {
        return DateTime(stdUtcUnix);
    }

    uint32_t dstUtcUnix = stdUtcUnix - 3600UL;
    bool dstAtDst = _isEU_DST(DateTime(dstUtcUnix));
    bool dstAtStd = _isEU_DST(DateTime(stdUtcUnix));

    if (dstAtDst && dstAtStd) {
        return DateTime(dstUtcUnix);
    }
    if (!dstAtDst && !dstAtStd) {
        return DateTime(stdUtcUnix);
    }
    if (dstAtDst && !dstAtStd) {
        // Ambiguous October fall-back hour (occurs twice: first at dstUtcUnix, then at stdUtcUnix).
        if (afterUtc > 0 && dstUtcUnix <= afterUtc) {
            return DateTime(stdUtcUnix);
        }
        return DateTime(dstUtcUnix);
    }
    // Spring-forward gap (!dstAtDst && dstAtStd): map into valid post-transition UTC.
    return DateTime(stdUtcUnix);
}
