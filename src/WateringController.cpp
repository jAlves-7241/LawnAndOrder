#include "i18n.h"
#include "WateringController.h"
#include "Scheduler.h"
#include "Storage.h"
#include "RTClock.h"
#include <string.h>
#include <RTClib.h>
#include "log.h"

const uint8_t WateringController::_relayPins[NUM_ZONES] = {
    PIN_RELAY_1, PIN_RELAY_2, PIN_RELAY_3, PIN_RELAY_4
};

WateringController wateringCtrl;

// ─────────────────────────────────────────────────────────
WateringController::WateringController()
    : _queueLen(0), _queuePos(0),
      _active(false), _isWaiting(false), _isRelayDeadTimeWaiting(false), _zoneIdx(0),
      _zoneStartMs(0), _zoneDurationMs(0), _waitStartMs(0), _relayWaitStartMs(0),
      _runTrigger(WaterTrigger::MANUAL)
{
    memset(_zoneDurMin, 0, sizeof(_zoneDurMin));
    _cycleStart = {};
}

void WateringController::begin() {
    for (int i = 0; i < NUM_ZONES; i++) {
        digitalWrite(_relayPins[i], RELAY_OFF);
        pinMode(_relayPins[i], OUTPUT);
    }
    _syncState();

    // Tentar resgatar ciclo interrompido por blackout
    RecoveryState rs;
    if (storage.loadRecoveryState(rs) && rs.active) {
        if (rs.queueLen > NUM_ZONES || rs.queuePos >= rs.queueLen) {
            LOG_W("REGA", "Recovery state invalid, discarding");
            rs.active = false;
            storage.saveRecoveryState(rs);
            return;
        }
        for (uint8_t i = 0; i < rs.queueLen; i++) {
            if (rs.queue[i].zone_idx >= NUM_ZONES) {
                LOG_W("REGA", "Recovery zone_idx invalid");
                rs.active = false;
                storage.saveRecoveryState(rs);
                return;
            }
        }
        LOG_I("REGA", "Encontrado ciclo interrompido! A avaliar retoma...");
        if (gState.rtc_valid) {
            uint32_t remaining_ms = 0;
            for (uint8_t i = rs.queuePos; i < rs.queueLen; i++) {
                remaining_ms += rs.queue[i].duration_ms;
            }
            if (!scheduler.isCycleExpired(rs.start_unix_time, gState.now, remaining_ms / 1000UL)) {
                // Recuperar estado
                _active = true;
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
                _cycleStart.unix = utcDT.unixtime();

                LOG_I("REGA", TXT_LOG_RESUME_ZONE, _queue[_queuePos].zone_idx + 1);
                _startNextZone();
                return; // Impede _syncState de limpar tudo
            }
        } else {
             LOG_W("REGA", TXT_LOG_RTC_INVALID_RESUME);
        }
        // Se chegou aqui, não é válido retomar, limpa NVS
        rs.active = false;
        storage.saveRecoveryState(rs);
    }
}

// ─────────────────────────────────────────────────────────
// Start commands
// ─────────────────────────────────────────────────────────

void WateringController::startGeneral(WaterTrigger trigger) {
    bool zones[NUM_ZONES];
    uint32_t durations_ms[NUM_ZONES];

    for (int i = 0; i < NUM_ZONES; i++) {
        zones[i]       = gState.zones[i].enabled;
        durations_ms[i] = (uint32_t)gState.zones[i].duration_min * 60000UL;
    }

    stop();
    _queueLen = 0;
    for (int i = 0; i < NUM_ZONES; i++) {
        if (zones[i] && durations_ms[i] > 0)
            _queue[_queueLen++] = { (uint8_t)i, durations_ms[i] };
    }
    if (_queueLen == 0) return;

    _runTrigger    = trigger;
    _cycleStart = gState.now;
    memset(_zoneDurMin, 0, sizeof(_zoneDurMin));
    _queuePos   = 0;
    _active     = true;
    _isWaiting  = false;
    _isRelayDeadTimeWaiting = false;
    
    // Gravação inicial do recovery state (única vez por ciclo)
    if (_runTrigger != WaterTrigger::TEST) {
        RecoveryState rs = {};
        rs.active = true;
        rs.start_unix_time = _cycleStart.unix;
        rs.queuePos = _queuePos;
        rs.queueLen = _queueLen;
        rs.trigger = _runTrigger;
        for (uint8_t i = 0; i < _queueLen; i++) {
            rs.queue[i].zone_idx = _queue[i].zone_idx;
            rs.queue[i].duration_ms = _queue[i].duration_ms;
        }
        memcpy(rs.zone_dur_min, _zoneDurMin, sizeof(_zoneDurMin));
        storage.saveRecoveryState(rs);
    }

    LOG_I("REGA", TXT_START_GEN_WATERING);
    _startNextZone();
}

void WateringController::startCustom(const bool zones[NUM_ZONES],
                                     uint8_t dur_min) {
    uint32_t dur_ms = (uint32_t)dur_min * 60000UL;
    if (_buildQueue(zones, dur_ms, WaterTrigger::CUSTOM)) {
        LOG_I("REGA", "Iniciar rega personalizada - %d min", dur_min);
        _startNextZone();
    }
}

void WateringController::startTest(int8_t zone_idx) {
    uint32_t dur_ms = (uint32_t)ZONE_TEST_DURATION_S * 1000UL;
    bool zones[NUM_ZONES];
    if (zone_idx < 0)
        for (int i = 0; i < NUM_ZONES; i++) zones[i] = true;
    else
        for (int i = 0; i < NUM_ZONES; i++) zones[i] = (i == (int)zone_idx);

    if (_buildQueue(zones, dur_ms, WaterTrigger::TEST)) {
        LOG_I("REGA", "Iniciar teste zona=%d", zone_idx);
        _startNextZone();
    }
}

void WateringController::stop() {
    if (_active) {
        _deactivateAll();
        if (!_isWaiting && !_isRelayDeadTimeWaiting) {
            uint32_t elapsed = millis() - _zoneStartMs;
            // Se correu menos de 30s, registar 0 min (evita "1 min" para interrupções imediatas)
            uint8_t ran_min = 0;
            if (elapsed >= 30000UL) {
                uint32_t raw_min = (elapsed + 30000UL) / 60000UL;
                ran_min = (raw_min > 255) ? 255 : (uint8_t)raw_min;
            }
            if (_runTrigger != WaterTrigger::TEST) {
                if (_zoneIdx < NUM_ZONES) {
                    _zoneDurMin[_zoneIdx] = ran_min;
                }
            }
            LOG_I("REGA", TXT_LOG_ZONE_DEACTIVATED_INT, _zoneIdx + 1, (_zoneIdx < NUM_ZONES) ? gState.zones[_zoneIdx].name : "N/A");
        }
        _active = false;
        _isWaiting = false;
        _isRelayDeadTimeWaiting = false;
        LOG_I("REGA", "Rega interrompida");

        _finishCycle();
    }
    _queueLen = 0;
    _queuePos = 0;
    _syncState();
}

// ─────────────────────────────────────────────────────────
// update()
// ─────────────────────────────────────────────────────────
void WateringController::update() {
    if (!_active) return;

    if (_isWaiting) {
        if (millis() - _waitStartMs >= ZONE_WAIT_DELAY_MS) {
            _isWaiting = false;
            _startNextZone();
        }
        return;
    }

    if (_isRelayDeadTimeWaiting) {
        if (millis() - _relayWaitStartMs >= RELAY_DEADTIME_MS) {
            _isRelayDeadTimeWaiting = false;
            if (_zoneIdx < NUM_ZONES) {
                digitalWrite(_relayPins[_zoneIdx], RELAY_ON);
                _zoneStartMs = millis();
                gState.watering.progress_pct = 0;
                _syncState();
            } else {
                LOG_E("REGA", "zona %d fora dos limites!", _zoneIdx);
            }
        }
        return;
    }

    uint32_t elapsed = millis() - _zoneStartMs;

    if (_zoneDurationMs > 0) {
        uint8_t pct = (uint8_t)min(100UL, (elapsed * 100UL) / _zoneDurationMs);
        gState.watering.progress_pct = pct;
    }

    if (elapsed >= _zoneDurationMs) {
        uint32_t raw_min = _zoneDurationMs / 60000UL;
        if (raw_min < 1) raw_min = 1;
        uint8_t ran_min = (raw_min > 255) ? 255 : (uint8_t)raw_min;
        if (_runTrigger != WaterTrigger::TEST) {
            if (_zoneIdx < NUM_ZONES) {
                _zoneDurMin[_zoneIdx] = ran_min;
            }
        }

        _deactivateAll();
        LOG_I("REGA", TXT_LOG_ZONE_DEACTIVATED, _zoneIdx + 1, (_zoneIdx < NUM_ZONES) ? gState.zones[_zoneIdx].name : "N/A");
        _queuePos++;

        if (_queuePos < _queueLen) {
            _isWaiting = true;
            _waitStartMs = millis();
            LOG_D("REGA", "Zona %d concluida - aguardar %lu s", _zoneIdx + 1, (unsigned long)ZONE_WAIT_DELAY_MS / 1000UL);
            _syncState();
            
            if (_runTrigger != WaterTrigger::TEST) {
                RecoveryState rs;
                rs.active = true;
                rs.start_unix_time = _cycleStart.unix;
                rs.queuePos = _queuePos;
                rs.queueLen = _queueLen;
                rs.trigger = _runTrigger;
                for (uint8_t i = 0; i < _queueLen; i++) {
                    rs.queue[i].zone_idx = _queue[i].zone_idx;
                    rs.queue[i].duration_ms = _queue[i].duration_ms;
                }
                memcpy(rs.zone_dur_min, _zoneDurMin, sizeof(_zoneDurMin));
                storage.saveRecoveryState(rs);
            }
        } else {
            _active = false;
            _queueLen = 0;
            _syncState();
            _finishCycle();
            LOG_I("REGA", "Ciclo concluido");
        }
    }
}

// ─────────────────────────────────────────────────────────
// Private
// ─────────────────────────────────────────────────────────

bool WateringController::_buildQueue(const bool zones[NUM_ZONES],
                                     uint32_t dur_ms,
                                     WaterTrigger trigger) {
    if (dur_ms == 0) return false;
    stop();
    _queueLen = 0;
    for (int i = 0; i < NUM_ZONES; i++) {
        if (zones[i])
            _queue[_queueLen++] = { (uint8_t)i, dur_ms };
    }
    if (_queueLen == 0) return false;

    _runTrigger    = trigger;
    _cycleStart = gState.now;
    memset(_zoneDurMin, 0, sizeof(_zoneDurMin));
    _queuePos   = 0;
    _active     = true;
    _isWaiting  = false;
    _isRelayDeadTimeWaiting = false;

    // Gravação inicial do recovery state (única vez por ciclo)
    if (_runTrigger != WaterTrigger::TEST) {
        RecoveryState rs = {};
        rs.active = true;
        rs.start_unix_time = _cycleStart.unix;
        rs.queuePos = _queuePos;
        rs.queueLen = _queueLen;
        rs.trigger = _runTrigger;
        for (uint8_t i = 0; i < _queueLen; i++) {
            rs.queue[i].zone_idx = _queue[i].zone_idx;
            rs.queue[i].duration_ms = _queue[i].duration_ms;
        }
        memcpy(rs.zone_dur_min, _zoneDurMin, sizeof(_zoneDurMin));
        storage.saveRecoveryState(rs);
    }
    return true;
}

void WateringController::_startNextZone() {
    _zoneIdx        = _queue[_queuePos].zone_idx;
    _zoneDurationMs = _queue[_queuePos].duration_ms;
    _activateRelay();
    gState.watering.progress_pct = 0;
    _syncState();

    if (_zoneDurationMs % 60000UL == 0) {
        LOG_I("REGA", TXT_LOG_ZONE_ACTIVE_MIN,
                      _zoneIdx + 1, (_zoneIdx < NUM_ZONES) ? gState.zones[_zoneIdx].name : "N/A", _zoneDurationMs / 60000UL);
    } else {
        LOG_I("REGA", TXT_LOG_ZONE_ACTIVE_SEC,
                      _zoneIdx + 1, (_zoneIdx < NUM_ZONES) ? gState.zones[_zoneIdx].name : "N/A", _zoneDurationMs / 1000UL);
    }
}

void WateringController::_finishCycle() {
    scheduler.onWateringDone();

    bool anyRun = false;
    for (int i = 0; i < NUM_ZONES; i++) {
        if (_zoneDurMin[i] > 0) anyRun = true;
    }

    if (!anyRun) {
        LOG_I("REGA", "Ciclo sem duracao efetiva. Ignorar historico.");
    } else {
        HistoryEntry entry;
        entry.year    = _cycleStart.year;
        entry.month   = _cycleStart.month;
        entry.day     = _cycleStart.day;
        entry.hour    = _cycleStart.hour;
        entry.min     = _cycleStart.min;
        entry.trigger = _runTrigger;
        memcpy(entry.zone_dur, _zoneDurMin, sizeof(_zoneDurMin));
        history.record(entry);
    }

    // Limpar estado de recuperação na NVS (apenas se não for teste)
    if (_runTrigger != WaterTrigger::TEST) {
        RecoveryState rs = {};
        rs.active = false;
        storage.saveRecoveryState(rs);
    }
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
