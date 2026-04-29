#include "WateringController.h"
#include <string.h>

// ── Relay pin map (matches config.h order) ────────────────
const uint8_t WateringController::_relayPins[NUM_ZONES] = {
    PIN_RELAY_1, PIN_RELAY_2, PIN_RELAY_3, PIN_RELAY_4
};

WateringController wateringCtrl;

// ─────────────────────────────────────────────────────────
WateringController::WateringController()
    : _queueLen(0), _queuePos(0),
      _active(false), _zoneIdx(0),
      _zoneStartMs(0), _zoneDurationMs(0)
{}

void WateringController::begin() {
    for (int i = 0; i < NUM_ZONES; i++) {
        pinMode(_relayPins[i], OUTPUT);
        digitalWrite(_relayPins[i], RELAY_OFF);
    }
    _syncState();
}

// ─────────────────────────────────────────────────────────
// Start commands
// ─────────────────────────────────────────────────────────

void WateringController::startGeneral() {
    bool zones[NUM_ZONES];
    uint32_t durations_ms[NUM_ZONES];

    for (int i = 0; i < NUM_ZONES; i++) {
        zones[i] = gState.zones[i].enabled;
        durations_ms[i] = (uint32_t)gState.zones[i].duration_min * 60000UL;
    }

    stop();  // abort any active cycle first

    _queueLen = 0;
    for (int i = 0; i < NUM_ZONES; i++) {
        if (zones[i] && durations_ms[i] > 0) {
            _queue[_queueLen++] = { (uint8_t)i, durations_ms[i] };
        }
    }

    if (_queueLen == 0) return;  // nothing to run

    _queuePos = 0;
    _active   = true;
    _startNextZone();

    Serial.println("[WATER] startGeneral");
}

void WateringController::startCustom(const bool zones[NUM_ZONES], uint8_t dur_min) {
    uint32_t dur_ms = (uint32_t)dur_min * 60000UL;
    _buildQueue(zones, dur_ms);

    Serial.printf("[WATER] startCustom %d min\n", dur_min);
}

void WateringController::startTest(int8_t zone_idx) {
    uint32_t dur_ms = (uint32_t)ZONE_TEST_DURATION_S * 1000UL;

    bool zones[NUM_ZONES];
    if (zone_idx < 0) {
        // all zones
        for (int i = 0; i < NUM_ZONES; i++) zones[i] = true;
    } else {
        for (int i = 0; i < NUM_ZONES; i++) zones[i] = (i == (int)zone_idx);
    }

    _buildQueue(zones, dur_ms);

    Serial.printf("[WATER] startTest zone=%d\n", zone_idx);
}

void WateringController::stop() {
    if (!_active) return;
    _deactivateAll();
    _active    = false;
    _queueLen  = 0;
    _queuePos  = 0;
    _syncState();
    Serial.println("[WATER] stopped");
}

// ─────────────────────────────────────────────────────────
// update() — called every loop()
// ─────────────────────────────────────────────────────────
void WateringController::update() {
    if (!_active) return;

    uint32_t elapsed = millis() - _zoneStartMs;

    // Update progress in gState so IDLE screen can render it
    if (_zoneDurationMs > 0) {
        uint8_t pct = (uint8_t)min(100UL, (elapsed * 100UL) / _zoneDurationMs);
        gState.watering.progress_pct = pct;
    }

    // Zone finished?
    if (elapsed >= _zoneDurationMs) {
        _deactivateAll();
        _queuePos++;

        if (_queuePos < _queueLen) {
            _startNextZone();
        } else {
            // Cycle complete
            _active = false;
            _queueLen = 0;
            _syncState();
            Serial.println("[WATER] cycle complete");
        }
    }
}

// ─────────────────────────────────────────────────────────
// Private helpers
// ─────────────────────────────────────────────────────────

void WateringController::_buildQueue(const bool zones[NUM_ZONES],
                                     uint32_t dur_ms) {
    stop();
    _queueLen = 0;
    for (int i = 0; i < NUM_ZONES; i++) {
        if (zones[i]) {
            _queue[_queueLen++] = { (uint8_t)i, dur_ms };
        }
    }
    if (_queueLen == 0) return;
    _queuePos = 0;
    _active   = true;
    _startNextZone();
}

void WateringController::_startNextZone() {
    _zoneIdx        = _queue[_queuePos].zone_idx;
    _zoneDurationMs = _queue[_queuePos].duration_ms;
    _zoneStartMs    = millis();
    _activateRelay(_zoneIdx);
    _syncState();

    Serial.printf("[WATER] zone %d (%s) start, dur=%lums\n",
                  _zoneIdx + 1, gState.zones[_zoneIdx].name, _zoneDurationMs);
}

void WateringController::_activateRelay(uint8_t zone_idx) {
    // Belt-and-suspenders: deactivate everything first,
    // then enable only the target zone.
    for (int i = 0; i < NUM_ZONES; i++)
        digitalWrite(_relayPins[i], RELAY_OFF);
    digitalWrite(_relayPins[zone_idx], RELAY_ON);
}

void WateringController::_deactivateAll() {
    for (int i = 0; i < NUM_ZONES; i++)
        digitalWrite(_relayPins[i], RELAY_OFF);
}

void WateringController::_syncState() {
    gState.watering.active       = _active;
    gState.watering.zone_idx     = _active ? _zoneIdx : 0;
    gState.watering.progress_pct = _active ? gState.watering.progress_pct : 0;
}
