#include "Encoder.h"

Encoder* Encoder::_inst = nullptr;

Encoder::Encoder(uint8_t clk, uint8_t dt, uint8_t sw)
    : _clk(clk), _dt(dt), _sw(sw),
      _delta(0), _btn_prev(false), _last_reading(false), _btn_last_ms(0)
{
    _inst = this;
}

void Encoder::begin() {
    pinMode(_clk, INPUT);
    pinMode(_dt,  INPUT);
    pinMode(_sw,  INPUT_PULLUP);
    _btn_last_ms = millis();
    attachInterrupt(digitalPinToInterrupt(_clk), _isr, RISING);
}

// On the rising edge of CLK, DT state encodes direction:
//   DT == LOW  → clockwise  (+1)
//   DT == HIGH → counter-cw (-1)
void IRAM_ATTR Encoder::_isr() {
    if (!_inst) return;
    int16_t d = (digitalRead(_inst->_dt) == LOW) ? 1 : -1;
    if (d > 0) {
        if (_inst->_delta < 32000) _inst->_delta += d;
    } else {
        if (_inst->_delta > -32000) _inst->_delta += d;
    }
}

int8_t Encoder::getRotation() {
    noInterrupts();
    int16_t raw = _delta;
    if (raw > 127) {
        _delta = raw - 127;
        raw = 127;
    } else if (raw < -127) {
        _delta = raw + 127;
        raw = -127;
    } else {
        _delta = 0;
    }
    interrupts();

    return (int8_t)raw;
}

bool Encoder::getClick() {
    bool     reading = (digitalRead(_sw) == LOW);
    uint32_t now     = millis();

    if (reading != _last_reading) {
        _btn_last_ms = now;
    }

    if ((now - _btn_last_ms) > DEBOUNCE_MS) {
        if (reading != _btn_prev) {
            _btn_prev = reading;
            if (_btn_prev) {
                _last_reading = reading;
                return true;
            }
        }
    }

    _last_reading = reading;
    return false;
}
