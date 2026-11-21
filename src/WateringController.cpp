#include "i18n.h"
#include "WateringController.h"
#include "Scheduler.h"
#include "Storage.h"
#include "RTClock.h"
#include <string.h>
#include <driver/gpio.h>
#include <RTClib.h>
#include "log.h"

static_assert(ZONE_TEST_DURATION_S > 0 && ZONE_TEST_DURATION_S <= 1200,
              "ZONE_TEST_DURATION_S must stay within 1..1200 s");

const uint8_t WateringController::_relayPins[NUM_ZONES] = {
    PIN_RELAY_1, PIN_RELAY_2, PIN_RELAY_3, PIN_RELAY_4
};

WateringController wateringCtrl;

// ─────────────────────────────────────────────────────────
WateringController::WateringController()
    : _queueLen(0), _queuePos(0),
      _active(false), _isWaiting(false), _isRelayDeadTimeWaiting(false),
      _cycleStartValid(false), _recoveryRetryCount(0), _zoneIdx(0),
      _zoneStartMs(0), _zoneDurationMs(0), _waitStartMs(0), _relayWaitStartMs(0),
      _lastPinRefreshMs(0),
      _runTrigger(WaterTrigger::MANUAL),
      _checkpointedMin(0)
{
    memset(_zoneDurMin, 0, sizeof(_zoneDurMin));
    _cycleStart = {};
}

void WateringController::begin() {
    // Defense-in-depth hardware safety: guarantee all relay pins are OUTPUT and OFF
    for (int i = 0; i < NUM_ZONES; i++) {
        gpio_set_level((gpio_num_t)_relayPins[i], RELAY_OFF);
        pinMode(_relayPins[i], OUTPUT);
        digitalWrite(_relayPins[i], RELAY_OFF);
    }
    _queueLen = 0;
    _queuePos = 0;
    _active = false;
    _isWaiting = false;
    _isRelayDeadTimeWaiting = false;
    _cycleStartValid = false;
    _recoveryRetryCount = 0;
    _checkpointedMin = 0;
    _zoneIdx = 0;
    _zoneStartMs = 0;
    _zoneDurationMs = 0;
    _waitStartMs = 0;
    _relayWaitStartMs = 0;
    _lastPinRefreshMs = millis();
    _runTrigger = WaterTrigger::MANUAL;
    memset(_zoneDurMin, 0, sizeof(_zoneDurMin));
    _cycleStart = {};
    _syncState();

    // Tentar resgatar ciclo interrompido por blackout
    RecoveryState rs;
    if (storage.loadRecoveryState(rs) && rs.active) {
        const bool validTrigger = (rs.trigger == WaterTrigger::AUTO ||
                                   rs.trigger == WaterTrigger::MANUAL ||
                                   rs.trigger == WaterTrigger::CUSTOM);
        if (rs.queueLen == 0 || rs.queueLen > NUM_ZONES ||
            rs.queuePos >= rs.queueLen || !validTrigger) {
            LOG_W(TXT_LOG_TAG_WATER, TXT_LOG_REC_STATE_INV);
            rs.active = false;
            rs.retry_count = 0;
            storage.saveRecoveryState(rs);
            return;
        }
        uint8_t seenZonesMask = 0;
        for (uint8_t i = 0; i < rs.queueLen; i++) {
            uint8_t z = rs.queue[i].zone_idx;
            uint32_t durMs = rs.queue[i].duration_ms;
            if (z >= NUM_ZONES || (seenZonesMask & (1U << z)) != 0 ||
                durMs < 60000UL || durMs > 20UL * 60000UL) {
                LOG_W(TXT_LOG_TAG_WATER, TXT_LOG_REC_ZONE_INV);
                rs.active = false;
                rs.retry_count = 0;
                storage.saveRecoveryState(rs);
                return;
            }
            seenZonesMask |= (uint8_t)(1U << z);
        }
        for (uint8_t z = 0; z < NUM_ZONES; z++) {
            if (rs.zone_dur_min[z] > 20) {
                LOG_W(TXT_LOG_TAG_WATER, TXT_LOG_REC_DUR_INV);
                rs.active = false;
                rs.retry_count = 0;
                storage.saveRecoveryState(rs);
                return;
            }
        }

        auto recordPartialRecovery = [](const RecoveryState& rec) {
            if (!gState.rtc_valid) return;
            if (rec.trigger == WaterTrigger::TEST) return;
            if (rec.start_unix_time < 1577836800UL || rec.start_unix_time > 4102444799UL) return;
            bool anyRun = false;
            for (uint8_t z = 0; z < NUM_ZONES; z++) {
                if (rec.zone_dur_min[z] > 0) {
                    anyRun = true;
                    break;
                }
            }
            if (!anyRun) return;
            DateTime startDT = RTClock::utcToLocal(DateTime(rec.start_unix_time));
            if (startDT.year() < DATE_YEAR_MIN || startDT.year() > DATE_YEAR_MAX) return;
            HistoryEntry entry = {};
            entry.year    = startDT.year();
            entry.month   = startDT.month();
            entry.day     = startDT.day();
            entry.hour    = startDT.hour();
            entry.min     = startDT.minute();
            entry.trigger = rec.trigger;
            memcpy(entry.zone_dur, rec.zone_dur_min, sizeof(entry.zone_dur));
            history.record(entry);
        };

        if (rs.retry_count >= MAX_RECOVERY_RETRIES) {
            LOG_E(TXT_LOG_TAG_WATER, TXT_LOG_REC_MAX_RETRY, (unsigned)rs.retry_count);
            recordPartialRecovery(rs);
            rs.active = false;
            rs.retry_count = 0;
            storage.saveRecoveryState(rs);
            return;
        }

        LOG_I(TXT_LOG_TAG_WATER, TXT_LOG_INT_FOUND_INFO,
              serialTriggerName(rs.trigger),
              (unsigned)rs.queue[rs.queuePos].zone_idx + 1U);
        if (!gState.rtc_valid) {
            LOG_W(TXT_LOG_TAG_WATER, TXT_LOG_RTC_INVALID_RESUME);
        } else if (gState.suspended && rs.trigger == WaterTrigger::AUTO) {
            LOG_I(TXT_LOG_TAG_WATER, TXT_LOG_REC_SKIP_SUSP);
        } else if (rs.start_unix_time >= 1577836800UL && rs.start_unix_time <= 4102444799UL) {
            uint64_t remaining_ms = 0;
            for (uint8_t i = rs.queuePos; i < rs.queueLen; i++) {
                remaining_ms += (uint64_t)rs.queue[i].duration_ms;
            }
            if (!scheduler.isCycleExpired(rs.start_unix_time, gState.now, (uint32_t)(remaining_ms / 1000ULL))) {
                // Persistir incremento de tentativas ANTES de energizar reles (evita loop de brownout)
                rs.retry_count++;
                _recoveryRetryCount = rs.retry_count;
                storage.saveRecoveryState(rs);

                // Recuperar estado
                _active = true;
                _cycleStartValid = true;
                _runTrigger = rs.trigger;
                // Reconstruir queue
                _queueLen = rs.queueLen;
                for (uint8_t i = 0; i < _queueLen; i++) {
                    _queue[i].zone_idx = rs.queue[i].zone_idx;
                    _queue[i].duration_ms = rs.queue[i].duration_ms;
                }
                _queuePos = rs.queuePos;
                memcpy(_zoneDurMin, rs.zone_dur_min, sizeof(_zoneDurMin));
                
                // Timestamp original do arranque do ciclo (convertido para local time)
                DateTime utcDT(rs.start_unix_time);
                DateTime startDT = RTClock::utcToLocal(utcDT);
                _cycleStart.year = startDT.year();
                _cycleStart.month = startDT.month();
                _cycleStart.day = startDT.day();
                _cycleStart.hour = startDT.hour();
                _cycleStart.min = startDT.minute();
                _cycleStart.sec = startDT.second();
                _cycleStart.dow = startDT.dayOfTheWeek();
                _cycleStart.unix = utcDT.unixtime();

                _zoneIdx = _queue[_queuePos].zone_idx;
                LOG_I(TXT_LOG_TAG_WATER, TXT_LOG_RESUME_ZONE,
                      (unsigned)_zoneIdx + 1U,
                      (_zoneIdx < NUM_ZONES) ? gState.zones[_zoneIdx].name : "-");
                if (_queuePos > 0) {
                    _isWaiting = true;
                    _waitStartMs = millis();
                    _syncState();
                } else {
                    _startNextZone();
                }
                return; // Impede _syncState de limpar tudo
            }
        }
        // Se chegou aqui, nao e valido retomar: preservar minutos ja regados no historico e limpar NVS
        recordPartialRecovery(rs);
        rs.active = false;
        rs.retry_count = 0;
        storage.saveRecoveryState(rs);
    }
}

// ─────────────────────────────────────────────────────────
// Start commands
// ─────────────────────────────────────────────────────────

void WateringController::startGeneral(WaterTrigger trigger) {
    QueueEntry candidate[NUM_ZONES];
    uint8_t candidateLen = 0;
#if LOG_LEVEL >= LVL_INFO
    uint32_t totalMin = 0;
#endif

    for (int i = 0; i < NUM_ZONES; i++) {
        uint8_t durMin = (gState.zones[i].duration_min <= 20) ? gState.zones[i].duration_min : 20;
        if (gState.zones[i].enabled && durMin > 0) {
            candidate[candidateLen++] = { (uint8_t)i, (uint32_t)durMin * 60000U };
#if LOG_LEVEL >= LVL_INFO
            totalMin += durMin;
#endif
        }
    }
    if (candidateLen == 0) {
        LOG_W(TXT_LOG_TAG_WATER, TXT_LOG_START_NO_ZONES);
        return;
    }

    stop();
    _queueLen = candidateLen;
    for (uint8_t i = 0; i < _queueLen; i++) {
        _queue[i] = candidate[i];
    }

    _runTrigger    = trigger;
    _cycleStart = gState.now;
    _cycleStartValid = gState.rtc_valid;
    memset(_zoneDurMin, 0, sizeof(_zoneDurMin));
    _queuePos   = 0;
    _active     = true;
    _isWaiting  = false;
    _isRelayDeadTimeWaiting = false;
    _recoveryRetryCount = 0;
    _checkpointedMin = 0;
    
    // Gravacao inicial do recovery state (unica vez por ciclo)
    _saveRecoveryState(true);

    LOG_I(TXT_LOG_TAG_WATER, TXT_LOG_START_GEN,
          serialTriggerName(trigger), (unsigned)_queueLen, (unsigned long)totalMin);
    _startNextZone();
}

void WateringController::startCustom(const bool zones[NUM_ZONES],
                                     uint8_t dur_min) {
    if (dur_min == 0 || dur_min > 20) {
        LOG_W(TXT_LOG_TAG_WATER, TXT_LOG_START_INV_DUR, (unsigned)dur_min);
        return;
    }
    if (!zones) {
        LOG_W(TXT_LOG_TAG_WATER, TXT_LOG_START_NO_ZONES);
        return;
    }
    uint32_t dur_ms = (uint32_t)dur_min * 60000UL;
    if (_buildQueue(zones, dur_ms, WaterTrigger::CUSTOM)) {
#if LOG_LEVEL >= LVL_INFO
        char zList[32] = "";
        size_t pos = 0;
        bool first = true;
        for (int i = 0; i < NUM_ZONES; i++) {
            if (zones[i]) {
                int n = snprintf(zList + pos, sizeof(zList) - pos, "%s%d",
                                 first ? "" : ", ", i + 1);
                if (n > 0 && pos + (size_t)n < sizeof(zList)) pos += (size_t)n;
                first = false;
            }
        }
        LOG_I(TXT_LOG_TAG_WATER, TXT_LOG_START_CUSTOM, zList, (unsigned)dur_min);
#endif
        _startNextZone();
    } else {
        LOG_W(TXT_LOG_TAG_WATER, TXT_LOG_START_NO_ZONES);
    }
}

void WateringController::startTest(int8_t zone_idx) {
    if (zone_idx < -1 || zone_idx >= NUM_ZONES) {
        LOG_W(TXT_LOG_TAG_WATER, TXT_LOG_ZONE_OOB,
              (zone_idx >= 0) ? ((unsigned)zone_idx + 1U) : 0U,
              (unsigned)NUM_ZONES);
        return;
    }
    uint32_t dur_ms = (uint32_t)ZONE_TEST_DURATION_S * 1000UL;
    bool zones[NUM_ZONES];
    if (zone_idx < 0)
        for (int i = 0; i < NUM_ZONES; i++) zones[i] = true;
    else
        for (int i = 0; i < NUM_ZONES; i++) zones[i] = (i == (int)zone_idx);

    if (_buildQueue(zones, dur_ms, WaterTrigger::TEST)) {
        if (zone_idx < 0) {
            LOG_I(TXT_LOG_TAG_WATER, TXT_LOG_START_TEST_ALL,
                  NUM_ZONES, (unsigned long)ZONE_TEST_DURATION_S);
        } else if ((int)zone_idx < NUM_ZONES) {
            LOG_I(TXT_LOG_TAG_WATER, TXT_LOG_START_TEST_ONE,
                  (int)zone_idx + 1, gState.zones[(int)zone_idx].name,
                  (unsigned long)ZONE_TEST_DURATION_S);
        }
        _startNextZone();
    }
}

void WateringController::stop() {
    if (_active) {
        _deactivateAll();
        if (!_isWaiting && !_isRelayDeadTimeWaiting && _zoneIdx < NUM_ZONES) {
            uint32_t elapsed = millis() - _zoneStartMs;
            // Se correu menos de 30s, registar 0 min (evita "1 min" para interrupcoes imediatas)
            uint8_t ran_min = 0;
            if (elapsed >= 30000UL) {
                uint32_t raw_min = (uint32_t)(((uint64_t)elapsed + 30000ULL) / 60000ULL);
                uint32_t max_min = (_zoneDurationMs >= 60000UL) ? (_zoneDurationMs / 60000UL) : 255UL;
                if (raw_min > max_min) raw_min = max_min;
                ran_min = (raw_min > 255UL) ? 255 : (uint8_t)raw_min;
            }
            const uint8_t prevZoneMin = (_runTrigger != WaterTrigger::TEST) ? _zoneDurMin[_zoneIdx] : 0;
            if (_runTrigger != WaterTrigger::TEST) {
                uint16_t total_min = (uint16_t)prevZoneMin + (uint16_t)ran_min;
                _zoneDurMin[_zoneIdx] = (total_min > 20U) ? 20U : (uint8_t)total_min;
            }
            if (elapsed < 60000UL && prevZoneMin == 0) {
                LOG_I(TXT_LOG_TAG_WATER, TXT_LOG_ZONE_DEACTIVATED_INT_SEC,
                      (int)_zoneIdx + 1,
                      gState.zones[_zoneIdx].name,
                      (unsigned long)(elapsed / 1000UL));
            } else {
#if LOG_LEVEL >= LVL_INFO
                uint8_t loggedMin = (_runTrigger != WaterTrigger::TEST)
                    ? _zoneDurMin[_zoneIdx] : ran_min;
                LOG_I(TXT_LOG_TAG_WATER, TXT_LOG_ZONE_DEACTIVATED_INT,
                      (int)_zoneIdx + 1,
                      gState.zones[_zoneIdx].name,
                      (unsigned)loggedMin);
#endif
            }
        } else if ((_isWaiting || _queuePos > 0) && _zoneIdx < NUM_ZONES) {
            // Avoid reporting a transition between zones if stopped during initial dead-time
            LOG_I(TXT_LOG_TAG_WATER, TXT_LOG_STOP_DURING_WAIT,
                  (int)_zoneIdx + 1,
                  gState.zones[_zoneIdx].name);
        }
        _active = false;
        _isWaiting = false;
        _isRelayDeadTimeWaiting = false;
        _recoveryRetryCount = 0;
        _checkpointedMin = 0;
        _queueLen = 0;
        _queuePos = 0;
        _syncState();
        LOG_I(TXT_LOG_TAG_WATER, TXT_LOG_WATER_INT);

        _finishCycle();
    } else {
        _queueLen = 0;
        _queuePos = 0;
        _syncState();
    }
}

void WateringController::deenergizeRelays() {
    _deactivateAll();
}

// ─────────────────────────────────────────────────────────
// update()
// ─────────────────────────────────────────────────────────
void WateringController::update() {
    uint32_t nowMs = millis();
    if (!_active) {
        // Re-assert idle relay outputs every 1 s against EMI transients
        if (nowMs - _lastPinRefreshMs >= 1000UL) {
            _lastPinRefreshMs = nowMs;
            _deactivateAll();
        }
        return;
    }

    if (_isWaiting) {
        if (nowMs - _waitStartMs >= ZONE_WAIT_DELAY_MS) {
            _isWaiting = false;
            _startNextZone();
        } else if (nowMs - _lastPinRefreshMs >= 1000UL) {
            _lastPinRefreshMs = nowMs;
            _deactivateAll();
        }
        return;
    }

    if (_isRelayDeadTimeWaiting) {
        if (nowMs - _relayWaitStartMs >= RELAY_DEADTIME_MS) {
            if (_zoneIdx < NUM_ZONES) {
                _isRelayDeadTimeWaiting = false;
                digitalWrite(_relayPins[_zoneIdx], RELAY_ON);
                _zoneStartMs = millis();
                _lastPinRefreshMs = _zoneStartMs;
                gState.watering.progress_pct = 0;
                _syncState();
                const uint32_t startDurMs = _effectiveZoneDurationMs();
                if (startDurMs % 60000UL == 0) {
                    LOG_I(TXT_LOG_TAG_WATER, TXT_LOG_ZONE_ACTIVE_MIN,
                          (int)_zoneIdx + 1, gState.zones[_zoneIdx].name,
                          (unsigned long)(startDurMs / 60000UL));
                } else {
                    LOG_I(TXT_LOG_TAG_WATER, TXT_LOG_ZONE_ACTIVE_SEC,
                          (int)_zoneIdx + 1, gState.zones[_zoneIdx].name,
                          (unsigned long)(startDurMs / 1000UL));
                }
            } else {
                LOG_E(TXT_LOG_TAG_WATER, TXT_LOG_ZONE_OOB,
                      (unsigned)_zoneIdx + 1U, (unsigned)NUM_ZONES);
                stop();
            }
        }
        return;
    }

    if (nowMs - _lastPinRefreshMs >= 1000UL) {
        _lastPinRefreshMs = nowMs;
        for (uint8_t i = 0; i < NUM_ZONES; i++) {
            digitalWrite(_relayPins[i], (i == _zoneIdx) ? RELAY_ON : RELAY_OFF);
        }
    }

    const uint32_t maxZoneMs = _maxZoneMs();
    const uint32_t effectiveDurationMs = _effectiveZoneDurationMs();

    uint32_t elapsed = nowMs - _zoneStartMs;

    if (elapsed < effectiveDurationMs && _runTrigger != WaterTrigger::TEST && _cycleStartValid) {
        uint32_t totalZoneMin = effectiveDurationMs / 60000UL;
        uint32_t elapsedMin = elapsed / 60000UL;
        if (totalZoneMin > elapsedMin &&
            (elapsedMin - _checkpointedMin) >= RECOVERY_CHECKPOINT_MIN) {
            _checkpointedMin = (uint8_t)elapsedMin;
            if (_queuePos < _queueLen) {
                _queue[_queuePos].duration_ms = (totalZoneMin - elapsedMin) * 60000UL;
            }
            _saveRecoveryState(true);
        }
    }

    if (effectiveDurationMs > 0) {
        uint8_t pct = (uint8_t)min(100ULL, (elapsed * 100ULL) / effectiveDurationMs);
        gState.watering.progress_pct = pct;
    }

    const bool capExceeded = (_zoneDurationMs > maxZoneMs) && (elapsed >= maxZoneMs);
    if (elapsed >= _zoneDurationMs || elapsed >= maxZoneMs) {
        if (capExceeded) {
            LOG_E(TXT_LOG_TAG_WATER, TXT_LOG_ZONE_MAX_CAP,
                  (unsigned)_zoneIdx + 1U, (unsigned long)(maxZoneMs / 1000UL));
        }
        uint32_t raw_min = effectiveDurationMs / 60000UL;
        if (raw_min < 1) raw_min = 1;
        uint8_t ran_min = (raw_min > 255) ? 255 : (uint8_t)raw_min;
        if (_runTrigger != WaterTrigger::TEST) {
            if (_zoneIdx < NUM_ZONES) {
                uint16_t total_min = (uint16_t)_zoneDurMin[_zoneIdx] + (uint16_t)ran_min;
                _zoneDurMin[_zoneIdx] = (total_min > 20U) ? 20U : (uint8_t)total_min;
            }
        }
        _checkpointedMin = 0;

        _deactivateAll();
        if (effectiveDurationMs % 60000UL == 0) {
#if LOG_LEVEL >= LVL_INFO
            uint8_t loggedMin = (_runTrigger != WaterTrigger::TEST && _zoneIdx < NUM_ZONES)
                ? _zoneDurMin[_zoneIdx] : ran_min;
            LOG_I(TXT_LOG_TAG_WATER, TXT_LOG_ZONE_DEACTIVATED,
                  (int)_zoneIdx + 1,
                  (_zoneIdx < NUM_ZONES) ? gState.zones[_zoneIdx].name : "-",
                  (unsigned)loggedMin);
#endif
        } else {
            LOG_I(TXT_LOG_TAG_WATER, TXT_LOG_ZONE_DEACTIVATED_SEC,
                  (int)_zoneIdx + 1,
                  (_zoneIdx < NUM_ZONES) ? gState.zones[_zoneIdx].name : "-",
                  (unsigned long)(effectiveDurationMs / 1000UL));
        }
        _queuePos++;

        if (_queuePos < _queueLen) {
            LOG_D(TXT_LOG_TAG_WATER, TXT_LOG_ZONE_DONE_WAIT,
                  (int)_zoneIdx + 1, (unsigned long)(ZONE_WAIT_DELAY_MS / 1000UL));
            if (_recoveryRetryCount > 0) {
                _recoveryRetryCount = 0;
                LOG_I(TXT_LOG_TAG_WATER, TXT_LOG_REC_STABLE);
            }
            _isWaiting = true;
            _waitStartMs = millis();
            _zoneIdx = _queue[_queuePos].zone_idx;
            gState.watering.progress_pct = 0;
            _syncState();
            
            _saveRecoveryState(true);
        } else {
            if (_recoveryRetryCount > 0) {
                _recoveryRetryCount = 0;
                LOG_I(TXT_LOG_TAG_WATER, TXT_LOG_REC_STABLE);
            }
            _active = false;
            _queueLen = 0;
            _syncState();
            LOG_I(TXT_LOG_TAG_WATER, TXT_LOG_CYCLE_DONE);
            _finishCycle();
        }
    }
}

// ─────────────────────────────────────────────────────────
// Private
// ─────────────────────────────────────────────────────────

uint32_t WateringController::_maxZoneMs() const {
    const uint32_t hardCapMs = 20UL * 60000UL;
    if (_runTrigger == WaterTrigger::TEST) {
        const uint32_t testCapMs = (uint32_t)ZONE_TEST_DURATION_S * 1000UL;
        return (testCapMs <= hardCapMs) ? testCapMs : hardCapMs;
    }
    return hardCapMs;
}

uint32_t WateringController::_effectiveZoneDurationMs() const {
    const uint32_t maxZoneMs = _maxZoneMs();
    return (_zoneDurationMs <= maxZoneMs) ? _zoneDurationMs : maxZoneMs;
}

bool WateringController::_buildQueue(const bool zones[NUM_ZONES],
                                     uint32_t dur_ms,
                                     WaterTrigger trigger) {
    if (!zones || dur_ms == 0) return false;
    if (trigger != WaterTrigger::TEST && dur_ms > 20UL * 60000UL) return false;

    QueueEntry candidate[NUM_ZONES];
    uint8_t candidateLen = 0;
    for (int i = 0; i < NUM_ZONES; i++) {
        if (zones[i]) {
            candidate[candidateLen++] = { (uint8_t)i, dur_ms };
        }
    }
    if (candidateLen == 0) return false;

    stop();
    _queueLen = candidateLen;
    for (uint8_t i = 0; i < _queueLen; i++) {
        _queue[i] = candidate[i];
    }

    _runTrigger    = trigger;
    _cycleStart = gState.now;
    _cycleStartValid = gState.rtc_valid;
    memset(_zoneDurMin, 0, sizeof(_zoneDurMin));
    _queuePos   = 0;
    _active     = true;
    _isWaiting  = false;
    _isRelayDeadTimeWaiting = false;
    _recoveryRetryCount = 0;
    _checkpointedMin = 0;

    // Gravacao inicial do recovery state (unica vez por ciclo)
    _saveRecoveryState(true);
    return true;
}

void WateringController::_startNextZone() {
    _zoneIdx         = _queue[_queuePos].zone_idx;
    _zoneDurationMs  = _queue[_queuePos].duration_ms;
    _checkpointedMin = 0;
    _activateRelay();
    gState.watering.progress_pct = 0;
    _syncState();
}

void WateringController::_finishCycle() {
    bool anyRun = false;
    for (int i = 0; i < NUM_ZONES; i++) {
        if (_zoneDurMin[i] > 0) anyRun = true;
    }

    if (anyRun && _runTrigger != WaterTrigger::TEST)
        scheduler.onWateringDone();

    if (!anyRun && _runTrigger != WaterTrigger::TEST) {
        LOG_I(TXT_LOG_TAG_WATER, TXT_LOG_CYCLE_NO_DUR);
    } else if (anyRun) {
        if (!_cycleStartValid) {
            LOG_W(TXT_LOG_TAG_HIST, TXT_LOG_HIST_SKIP_NO_RTC);
        } else {
            HistoryEntry entry = {};
            entry.year    = _cycleStart.year;
            entry.month   = _cycleStart.month;
            entry.day     = _cycleStart.day;
            entry.hour    = _cycleStart.hour;
            entry.min     = _cycleStart.min;
            entry.trigger = _runTrigger;
            memcpy(entry.zone_dur, _zoneDurMin, sizeof(_zoneDurMin));
            history.record(entry);
        }
    }

    // Limpar estado de recuperacao na NVS (apenas se nao for teste)
    _saveRecoveryState(false);
}

void WateringController::_saveRecoveryState(bool active) {
    if (_runTrigger == WaterTrigger::TEST) return;
    if (active && !_cycleStartValid) return;
    RecoveryState rs = {};
    rs.active = active;
    rs.retry_count = active ? _recoveryRetryCount : 0;
    if (active) {
        rs.start_unix_time = _cycleStart.unix;
        rs.queuePos = _queuePos;
        rs.queueLen = _queueLen;
        rs.trigger = _runTrigger;
        for (uint8_t i = 0; i < _queueLen; i++) {
            rs.queue[i].zone_idx = _queue[i].zone_idx;
            rs.queue[i].duration_ms = _queue[i].duration_ms;
        }
        memcpy(rs.zone_dur_min, _zoneDurMin, sizeof(_zoneDurMin));
        if (_zoneIdx < NUM_ZONES && _checkpointedMin > 0) {
            uint16_t accum = (uint16_t)rs.zone_dur_min[_zoneIdx] + (uint16_t)_checkpointedMin;
            rs.zone_dur_min[_zoneIdx] = (accum > 20U) ? 20U : (uint8_t)accum;
        }
    }
    storage.saveRecoveryState(rs);
}

void WateringController::_activateRelay() {
    for (int i = 0; i < NUM_ZONES; i++)
        digitalWrite(_relayPins[i], RELAY_OFF);

    _isRelayDeadTimeWaiting = true;
    _relayWaitStartMs = millis();
}

void WateringController::_deactivateAll() {
    for (int i = 0; i < NUM_ZONES; i++)
        digitalWrite(_relayPins[i], RELAY_OFF);
}

void WateringController::_syncState() {
    gState.watering.active     = _active;
    gState.watering.is_waiting = _isWaiting;
    gState.watering.zone_idx   = _active ? _zoneIdx : 0;
    if (!_active) gState.watering.progress_pct = 0;
}
