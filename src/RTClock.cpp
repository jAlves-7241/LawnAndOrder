#include "RTClock.h"
#include "Storage.h"
#include "log.h"
#include "i18n.h"
#include <Wire.h>

RTClock rtclock;



#if LOG_LEVEL >= LVL_INFO
static const char* DOW_NAMES[] = {
    TXT_SER_DOW_SUN_SHORT, TXT_SER_DOW_MON_SHORT, TXT_SER_DOW_TUE_SHORT, TXT_SER_DOW_WED_SHORT, TXT_SER_DOW_THU_SHORT, TXT_SER_DOW_FRI_SHORT, TXT_SER_DOW_SAT_SHORT
};
#endif

// ─────────────────────────────────────────────────────────
RTClock::RTClock()
    : _found(false), _rtcDriverReady(false), _lostPower(false), _batteryLow(false), _batRecoveryWarned(false), _lastReadMs(0),
      _consecErrors(0),
      _anchorMs(0), _anchorUnix(0), _lastKnownUnix(0),
      _errorCb(nullptr)
{}

bool RTClock::_probeRtcI2c() {
    static const uint8_t DS3231_ADDR = 0x68;
    static const uint8_t DS3231_STATUSREG = 0x0F;
    Wire.beginTransmission(DS3231_ADDR);
    Wire.write(DS3231_STATUSREG);
    return Wire.endTransmission() == 0;
}

bool RTClock::_ensureRtcDriver() {
    if (!_probeRtcI2c()) return false;
    if (!_rtcDriverReady) {
        // Adafruit_I2CDevice::begin() calls Wire.begin(), which on ESP32 Arduino Core
        // returns immediately when Wire.begin(PIN_SDA, PIN_SCL) has already initialized the bus.
        _rtcDriverReady = _rtc.begin();
    }
    return _rtcDriverReady;
}

bool RTClock::begin() {
    const bool wasValid = gState.rtc_valid;
    if (wasValid && _anchorUnix >= UNIX_MIN_VALID && gState.now.unix == _lastKnownUnix) {
        _incrementSoftwareClock();
    }
    const SystemTime prevTime = gState.now;
    _consecErrors = 0;

    if (!_ensureRtcDriver()) {
        LOG_E(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_NOT_FOUND);
        _found = false;
        if (!wasValid) {
            gState.rtc_valid = false;
        }
        return false;
    }

    _found = true;

    bool readOk = false;
    bool hwLostPower = false;
    DateTime initialUtc = _readRtc(readOk, hwLostPower);
    if (!readOk) {
        LOG_E(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_READ_CORRUPT,
              0, 0, 0, 0, 0, 0);
        _found = false;
        if (wasValid) {
            gState.now = prevTime;
            gState.rtc_valid = true;
        } else {
            gState.rtc_valid = false;
        }
        return false;
    }

    auto commitUtc = [&](const DateTime& dt) -> bool {
        if (!_isValidDateTime(dt)) {
            LOG_E(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_READ_CORRUPT,
                  (int)dt.year(), (int)dt.month(), (int)dt.day(),
                  (int)dt.hour(), (int)dt.minute(), (int)dt.second());
            _found = false;
            if (wasValid) {
                gState.now = prevTime;
                gState.rtc_valid = true;
            } else {
                gState.rtc_valid = false;
            }
            return false;
        }

        DateTime localDt = utcToLocal(dt);
        _copyToState(localDt);
        const uint32_t newUnix = dt.unixtime();
        gState.now.unix = newUnix;
        _lastReadMs = millis();
        _anchorMs = _lastReadMs;
        _anchorUnix = gState.now.unix;
        _lastKnownUnix = gState.now.unix;
        if (!wasValid) {
            _revision++;
        } else if (prevTime.unix >= UNIX_MIN_VALID) {
            int64_t drift = (int64_t)newUnix - (int64_t)prevTime.unix;
            if (drift > 300 || drift < -300) {
                _revision++;
                LOG_W(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_TIME_JUMP);
            }
        }

        LOG_I(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_OK,
              (int)gState.now.year,  (int)gState.now.month,  (int)gState.now.day,
              (int)gState.now.hour,  (int)gState.now.min,    (int)gState.now.sec,
              DOW_NAMES[gState.now.dow % 7]);
        return true;
    };

    _lostPower = hwLostPower;

    if (_lostPower) {
        _batteryLow = true;
        if (wasValid &&
            ((prevTime.unix >= UNIX_MIN_VALID && prevTime.unix <= UNIX_MAX_VALID) ||
             (prevTime.year >= DATE_YEAR_MIN && prevTime.year <= DATE_YEAR_MAX))) {
            LOG_W(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_BAT_FAIL_REC);
            _batRecoveryWarned = true;
            DateTime restoredUtc = (prevTime.unix >= UNIX_MIN_VALID && prevTime.unix <= UNIX_MAX_VALID)
                ? DateTime(prevTime.unix)
                : localToUtc(DateTime(prevTime.year, prevTime.month, prevTime.day,
                                      prevTime.hour, prevTime.min, prevTime.sec));
            bool adjOk = false;
            DateTime verifiedUtc = _adjustAndVerify(restoredUtc, adjOk);
            if (!adjOk) {
                LOG_E(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_READ_CORRUPT,
                      (int)verifiedUtc.year(), (int)verifiedUtc.month(), (int)verifiedUtc.day(),
                      (int)verifiedUtc.hour(), (int)verifiedUtc.minute(), (int)verifiedUtc.second());
                _found = false;
                gState.now = prevTime;
                gState.rtc_valid = true;
                return false;
            }
            _lostPower = false;
            gState.rtc_valid = true;
            return commitUtc(verifiedUtc);
        } else {
            LOG_W(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_BAT_LOW);
            gState.rtc_valid = false;
        }
    } else {
        _batRecoveryWarned = false;
        if (!wasValid) _batteryLow = false;
        gState.rtc_valid = true;
    }
    
    // Proteger o arranque contra barramento I2C preso ou corrompido
    if (_lostPower && !wasValid) {
        if (initialUtc.month() < 1 || initialUtc.month() > 12 || initialUtc.day() < 1 || initialUtc.day() > 31) {
            LOG_E(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_READ_CORRUPT,
                  (int)initialUtc.year(), (int)initialUtc.month(), (int)initialUtc.day(),
                  (int)initialUtc.hour(), (int)initialUtc.minute(), (int)initialUtc.second());
            _found = false;
            gState.rtc_valid = false;
            return false;
        }
        // Chip presente com OSF ativo e relogio ainda nao configurado: preservar OSF ate o utilizador acertar a hora
        if (_isValidDateTime(initialUtc)) {
            _copyToState(utcToLocal(initialUtc));
            gState.now.unix = initialUtc.unixtime();
        } else if (gState.now.year < DATE_YEAR_MIN) {
            _copyToState(DateTime(2026, 1, 1, 0, 0, 0));
            gState.now.unix = UNIX_DEFAULT_FALLBACK;
        }
        _lastReadMs = millis();
        _anchorMs = _lastReadMs;
        _anchorUnix = gState.now.unix;
        _lastKnownUnix = gState.now.unix;
        return true;
    }

    return commitUtc(initialUtc);
}

bool RTClock::probe() {
    if (_found) return true;
    return begin();
}

void RTClock::refreshLocalTime() {
    if (gState.now.unix < UNIX_MIN_VALID) return;
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
        // Retry hotplug detection once per 30s update interval (already gated by _lastReadMs above)
        LOG_D(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_HOT_DETECT);
        if (_ensureRtcDriver()) {
            bool wasValid = gState.rtc_valid;
            bool readOk = false;
            bool hwLostPower = false;
            DateTime utcDt = _readRtc(readOk, hwLostPower);
            if (!readOk) {
                LOG_E(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_READ_CORRUPT,
                      0, 0, 0, 0, 0, 0);
                _found = false;
            } else {
                _found = true;
                _lostPower = hwLostPower;

                LOG_I(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_HOT_FOUND);

                if (!_lostPower && _isValidDateTime(utcDt)) {
                    _batRecoveryWarned = false;
                    if (!wasValid) _batteryLow = false;
                    const uint32_t newUnix = utcDt.unixtime();
                    if (wasValid && _anchorUnix >= UNIX_MIN_VALID) {
                        uint32_t elapsedSec = (nowMs - _anchorMs) / 1000UL;
                        uint32_t expectedUnix = _anchorUnix + elapsedSec;
                        int64_t drift = (int64_t)newUnix - (int64_t)expectedUnix;
                        if (drift > 300 || drift < -300) {
                            LOG_W(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_TIME_JUMP);
                        }
                    }
                    gState.rtc_valid = true;
                    DateTime localDt = utcToLocal(utcDt);
                    _copyToState(localDt);
                    gState.now.unix = newUnix;
                    _anchorMs = nowMs;
                    _anchorUnix = gState.now.unix;
                    _lastKnownUnix = gState.now.unix;
                    _revision++;
                    LOG_I(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_HOT_SYNC,
                          (int)gState.now.year, (int)gState.now.month, (int)gState.now.day,
                          (int)gState.now.hour, (int)gState.now.min, (int)gState.now.sec);

                } else if (_lostPower && wasValid) {
                    // Restore RAM time only when hardware oscillator genuinely stopped
                    _batteryLow = true;
                    if (!_batRecoveryWarned) {
                        LOG_W(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_BAT_FAIL_REC);
                        _batRecoveryWarned = true;
                    }
                    if (_anchorUnix >= UNIX_MIN_VALID && gState.now.unix == _lastKnownUnix) {
                        _incrementSoftwareClock();
                    }
                    DateTime restoredUtc = (gState.now.unix >= UNIX_MIN_VALID)
                        ? DateTime(gState.now.unix)
                        : localToUtc(DateTime(gState.now.year, gState.now.month, gState.now.day,
                                              gState.now.hour, gState.now.min, gState.now.sec));
                    bool adjOk = false;
                    DateTime verifiedUtc = _adjustAndVerify(restoredUtc, adjOk);
                    if (adjOk) {
                        _lostPower = false;
                        DateTime localDt = utcToLocal(verifiedUtc);
                        _copyToState(localDt);
                        gState.now.unix = verifiedUtc.unixtime();
                        _anchorMs = nowMs;
                        _anchorUnix = gState.now.unix;
                        _lastKnownUnix = gState.now.unix;
                        _revision++;
                        LOG_I(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_HOT_RESTORED);
                    } else {
                        _found = false;
                    }
                } else if (!_lostPower) {
                    // Invalid calendar values without OSF indicate a corrupt read
                    LOG_E(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_READ_CORRUPT,
                          (int)utcDt.year(), (int)utcDt.month(), (int)utcDt.day(),
                          (int)utcDt.hour(), (int)utcDt.minute(), (int)utcDt.second());
                    _found = false;
                } else {
                    _batteryLow = true;
                    gState.rtc_valid = false;
                    if (utcDt.month() < 1 || utcDt.month() > 12 || utcDt.day() < 1 || utcDt.day() > 31) {
                        LOG_E(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_READ_CORRUPT,
                              (int)utcDt.year(), (int)utcDt.month(), (int)utcDt.day(),
                              (int)utcDt.hour(), (int)utcDt.minute(), (int)utcDt.second());
                        _found = false;
                    } else {
                        LOG_W(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_BAT_LOW);
                    }
                }
            }
        }

        // Se continua nao encontrado, mantem relogio por software
        if (!gState.rtc_valid) {
            // Garantir que temos uma data de arranque coerente em RAM
            if (gState.now.year < DATE_YEAR_MIN) {
                gState.now.year = 2026;
                gState.now.month = 1;
                gState.now.day = 1;
                gState.now.hour = 0;
                gState.now.min = 0;
                gState.now.sec = 0;
                gState.now.dow = 4; // Quinta-feira
                gState.now.unix = UNIX_DEFAULT_FALLBACK;
            }
        }
        _incrementSoftwareClock();
        return;
    }

    auto handleReadError = [&](const DateTime& badDt) {
        (void)badDt;
        _consecErrors++;
        if (_consecErrors >= 3) {
            LOG_E(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_I2C_CORRUPT,
                  (int)badDt.year(), (int)badDt.month(), (int)badDt.day(),
                  (int)badDt.hour(), (int)badDt.minute(), (int)badDt.second());
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
    };

    bool readOk = false;
    bool hwLostPower = false;
    DateTime rawUtc = _readRtc(readOk, hwLostPower);
    if (!readOk) {
        handleReadError(DateTime(2000, 1, 1, 0, 0, 0));
        return;
    }

    auto applyUtc = [&](const DateTime& utcDt, bool recoveredPowerLoss) {
        // Detetar campos fora dos limites validos
        if (!_isValidDateTime(utcDt)) {
            handleReadError(utcDt);
            return; // Aborta o update para proteger o gState contra dados corrompidos
        } else {
            _consecErrors = 0;
        }

        const bool wasRtcValid = gState.rtc_valid;
        _lostPower = false;
        if (!recoveredPowerLoss) {
            _batRecoveryWarned = false;
            if (!wasRtcValid) {
                _batteryLow = false;
            }
        }

        uint32_t newUnix = utcDt.unixtime();
        if (!wasRtcValid) {
            _revision++;
        } else if (!recoveredPowerLoss && _anchorUnix >= UNIX_MIN_VALID) {
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
    };

    // Re-check oscillator health only after a verified I2C read succeeds.
    if (hwLostPower) {
        _batteryLow = true;
        if (gState.rtc_valid) {
            if (!_batRecoveryWarned) {
                LOG_W(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_BAT_FAIL_REC);
                _batRecoveryWarned = true;
            }
            if (_anchorUnix >= UNIX_MIN_VALID && gState.now.unix == _lastKnownUnix) {
                _incrementSoftwareClock();
            }
            DateTime restoredUtc = (gState.now.unix >= UNIX_MIN_VALID)
                ? DateTime(gState.now.unix)
                : localToUtc(DateTime(gState.now.year, gState.now.month, gState.now.day,
                                      gState.now.hour, gState.now.min, gState.now.sec));
            _lastReadMs = nowMs;   // internal recovery must not defeat the 30 s throttle
            bool adjOk = false;
            DateTime verifiedUtc = _adjustAndVerify(restoredUtc, adjOk);
            if (!adjOk) {
                handleReadError(verifiedUtc);
                return;
            }
            _lostPower = false;
            applyUtc(verifiedUtc, true);
            return;
        } else {
            if (!_lostPower) {
                LOG_E(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_BAT_FAIL_RAM);
            }
            _lostPower = true;
            _consecErrors = 0;
            _incrementSoftwareClock();
            return;
        }
    }

    applyUtc(rawUtc, false);
}

// ─────────────────────────────────────────────────────────
bool RTClock::set(uint16_t year, uint8_t month,  uint8_t day,
                  uint8_t  hour, uint8_t minute, uint8_t second,
                  bool allowSwFallback) {
    
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
    if (!_isValidDateTime(utcDt)) {
        LOG_W(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_INVALID_SET,
              (unsigned)year, (unsigned)month, (unsigned)day,
              (unsigned)hour, (unsigned)minute, (unsigned)second);
        return false;
    }
    DateTime effectiveLocal = utcToLocal(utcDt);

    bool hwOk = false;
    if (_found) {
        if (_ensureRtcDriver()) {
            _adjustAndVerify(utcDt, hwOk);
        }
        if (!hwOk) _found = false;
    } else {
        LOG_D(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_SAVE_ATTEMPT);
        if (_ensureRtcDriver()) {
            _adjustAndVerify(utcDt, hwOk);
            if (hwOk) _found = true;
        }
    }

    if (!hwOk && !allowSwFallback) {
        _lastReadMs = millis();
        return false;
    }

    if (hwOk) {
        _lostPower = false;
        _batteryLow = false;
        _batRecoveryWarned = false;
        _lastReadMs = millis();
    } else {
        _lastReadMs = millis();
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
            if (newUntil > (int64_t)UNIX_MAX_VALID) {
                gState.suspended_until = UNIX_MAX_VALID;
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
    _manualRevision++;
    return hwOk;
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
    if (currentUnix < UNIX_MIN_VALID) { // Menor que 2020-01-01
        currentUnix    = UNIX_DEFAULT_FALLBACK; // 2026-01-01 00:00:00 UTC
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

DateTime RTClock::_readRtc(bool& ok, bool& lostPower) {
    ok = false;
    static const uint8_t DS3231_ADDR = 0x68;
    static const uint8_t DS3231_STATUSREG = 0x0F;

    Wire.beginTransmission(DS3231_ADDR);
    Wire.write(DS3231_STATUSREG);
    if (Wire.endTransmission() != 0) return DateTime(2000, 1, 1, 0, 0, 0);
    if (Wire.requestFrom(DS3231_ADDR, (size_t)1) != 1) return DateTime(2000, 1, 1, 0, 0, 0);
    int stat = Wire.read();
    if (stat < 0) return DateTime(2000, 1, 1, 0, 0, 0);
    // Bits 6..4 of DS3231 status reg 0x0F are always 0 (and 0x00 on Wokwi DS1307);
    // non-zero (e.g. 0xFF floating bus) indicates a corrupt I2C read, not valid OSF.
    if (((uint8_t)stat & 0x70U) != 0) return DateTime(2000, 1, 1, 0, 0, 0);
    lostPower = ((uint8_t)stat & 0x80U) != 0;

    Wire.beginTransmission(DS3231_ADDR);
    Wire.write((uint8_t)0x00);
    if (Wire.endTransmission() != 0) return DateTime(2000, 1, 1, 0, 0, 0);
    if (Wire.requestFrom(DS3231_ADDR, (size_t)7) != 7) return DateTime(2000, 1, 1, 0, 0, 0);
    uint8_t buf[7];
    for (int i = 0; i < 7; i++) {
        int b = Wire.read();
        if (b < 0) return DateTime(2000, 1, 1, 0, 0, 0);
        buf[i] = (uint8_t)b;
    }

    auto validBcd = [](uint8_t v) -> bool {
        return (v & 0x0FU) <= 9U && (v >> 4) <= 9U;
    };
    auto bcd2bin = [](uint8_t v) -> uint8_t {
        return (uint8_t)(v - 6U * (v >> 4));
    };

    bool hrOk = false;
    uint8_t hour24 = 0;
    if ((buf[2] & 0x80U) == 0) {
        if ((buf[2] & 0x40U) != 0) {
            // 12-hour mode: bit 5 is PM, bits 4..0 are 01..12 BCD
            uint8_t hr12Bcd = buf[2] & 0x1FU;
            if (validBcd(hr12Bcd)) {
                uint8_t hr12 = bcd2bin(hr12Bcd);
                if (hr12 >= 1 && hr12 <= 12) {
                    bool isPm = (buf[2] & 0x20U) != 0;
                    hour24 = (uint8_t)((hr12 % 12U) + (isPm ? 12U : 0U));
                    hrOk = true;
                }
            }
        } else {
            uint8_t hr24Bcd = buf[2] & 0x3FU;
            if (validBcd(hr24Bcd)) {
                hour24 = bcd2bin(hr24Bcd);
                hrOk = (hour24 <= 23U);
            }
        }
    }

    const bool bcdOk = hrOk && validBcd(buf[0] & 0x7FU) && validBcd(buf[1]) &&
                       validBcd(buf[4]) && validBcd(buf[5] & 0x7FU) && validBcd(buf[6]);
    if (!bcdOk) {
        if (!lostPower) return DateTime(2000, 1, 1, 0, 0, 0);
        ok = true;
        return DateTime(2000, 1, 1, 0, 0, 0);
    }

    ok = true;
    return DateTime((uint16_t)bcd2bin(buf[6]) + 2000U,
                    bcd2bin(buf[5] & 0x7FU),
                    bcd2bin(buf[4]),
                    hour24,
                    bcd2bin(buf[1]),
                    bcd2bin(buf[0] & 0x7FU));
}

DateTime RTClock::_adjustAndVerify(const DateTime& utcDt, bool& ok) {
    _rtc.adjust(utcDt);
    bool readOk = false;
    bool checkLost = false;
    DateTime checkDt = _readRtc(readOk, checkLost);
    if (!readOk || checkLost || !_isValidDateTime(checkDt)) {
        ok = false;
        return checkDt;
    }
    int64_t diff = (int64_t)checkDt.unixtime() - (int64_t)utcDt.unixtime();
    if (diff < -2 || diff > 2) {
        ok = false;
        return checkDt;
    }
    ok = true;
    return checkDt;
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
    uint8_t maxD = daysInMonth(y, m);
    if (maxD == 0 || d < 1 || d > maxD) return false;
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
