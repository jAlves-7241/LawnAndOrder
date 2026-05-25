#include "WateringController.h"
#include "Scheduler.h"
#include <string.h>
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
    
    LOG_I("REGA", "Iniciar rega geral");
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
        if (!_isWaiting) {
            LOG_I("REGA", "Zona %d (%s) desactivada (interrompida)", _zoneIdx + 1, gState.zones[_zoneIdx].name);
        }
        _active = false;
        _isWaiting = false;
        _isRelayDeadTimeWaiting = false;
        LOG_I("REGA", "Rega interrompida");
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
        if (millis() - _relayWaitStartMs >= 20) {
            _isRelayDeadTimeWaiting = false;
            if (_zoneIdx < NUM_ZONES) {
                digitalWrite(_relayPins[_zoneIdx], RELAY_ON);
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
        // Record actual duration for this zone (convert ms → min, min 1)
        uint8_t ran_min = (uint8_t)max(1UL, _zoneDurationMs / 60000UL);
        if (_runTrigger != WaterTrigger::TEST)
            _zoneDurMin[_zoneIdx] = ran_min;

        _deactivateAll();
        LOG_I("REGA", "Zona %d (%s) desactivada", _zoneIdx + 1, gState.zones[_zoneIdx].name);
        _queuePos++;

        if (_queuePos < _queueLen) {
            _isWaiting = true;
            _waitStartMs = millis();
            LOG_D("REGA", "Zona %d concluida - aguardar %lu s", _zoneIdx + 1, (unsigned long)ZONE_WAIT_DELAY_MS / 1000UL);
            _syncState();
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
    return true;
}

void WateringController::_startNextZone() {
    _zoneIdx        = _queue[_queuePos].zone_idx;
    _zoneDurationMs = _queue[_queuePos].duration_ms;
    _zoneStartMs    = millis();
    _activateRelay(_zoneIdx);
    _syncState();

    if (_zoneDurationMs % 60000UL == 0) {
        LOG_I("REGA", "Zona %d (%s) activa - dur=%lu min",
                      _zoneIdx + 1, gState.zones[_zoneIdx].name, _zoneDurationMs / 60000UL);
    } else {
        LOG_I("REGA", "Zona %d (%s) activa - dur=%lu s",
                      _zoneIdx + 1, gState.zones[_zoneIdx].name, _zoneDurationMs / 1000UL);
    }
}

void WateringController::_finishCycle() {
    scheduler.onWateringDone();

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

void WateringController::_activateRelay(uint8_t zone_idx) {
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
