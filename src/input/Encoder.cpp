#include "Encoder.h"

Encoder* Encoder::_inst = nullptr;

Encoder::Encoder(uint8_t clk, uint8_t dt, uint8_t sw)
    : _clk(clk), _dt(dt), _sw(sw),
      _delta(0), _btn_prev(false), _btn_last_ms(0)
{
    _inst = this;
}

void Encoder::begin() {
    pinMode(_clk, INPUT);
    pinMode(_dt,  INPUT);
    pinMode(_sw,  INPUT_PULLUP);
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
    int16_t d = _delta;
    _delta    = 0;
    interrupts();

    if (d > 0) return  1;
    if (d < 0) return -1;
    return 0;
}

bool Encoder::getClick() {
    bool     pressed = (digitalRead(_sw) == LOW);
    uint32_t now     = millis();

    if (pressed && !_btn_prev && (now - _btn_last_ms) >= DEBOUNCE_MS) {
        _btn_prev    = true;
        _btn_last_ms = now;
        return true;
    }
    if (!pressed) _btn_prev = false;
    return false;
}