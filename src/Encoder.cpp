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
    _inst->_delta += (digitalRead(_inst->_dt) == LOW) ? 1 : -1;
}

int8_t Encoder::getRotation() {
    noInterrupts();
    int16_t raw = _delta;
    _delta = 0;
    interrupts();

    // Clamp to int8_t range to prevent two's-complement direction reversal,
    // while preserving magnitude for fast-scroll acceleration.
    if (raw > 127)  return 127;
    if (raw < -127) return -127;
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
