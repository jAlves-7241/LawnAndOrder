#include "Encoder.h"

Encoder* volatile Encoder::_inst = nullptr;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// ─────────────────────────────────────────────────────────
Encoder::Encoder(uint8_t pin_clk, uint8_t pin_dt, uint8_t pin_sw)
    : _clk(pin_clk), _dt(pin_dt), _sw(pin_sw), _delta(0), 
      _btn_prev(HIGH), _last_reading(HIGH), _btn_last_ms(0)
{
    _inst = this;
}

void Encoder::begin() {
    pinMode(_clk, INPUT_PULLUP);
    pinMode(_dt,  INPUT_PULLUP);
    pinMode(_sw,  INPUT_PULLUP);

    _btn_prev = (digitalRead(_sw) == LOW);
    _last_reading = _btn_prev;

    attachInterrupt(digitalPinToInterrupt(_clk), _isr, FALLING);
}

void IRAM_ATTR Encoder::_isr() {
    if (!_inst) return;
    static volatile uint32_t last_isr_ms = 0;
    
    portENTER_CRITICAL_ISR(&mux);
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - last_isr_ms < 2) {
        portEXIT_CRITICAL_ISR(&mux);
        return;
    }
    last_isr_ms = now;

    int16_t d = (digitalRead(_inst->_dt) == LOW) ? 1 : -1;
    if (d > 0) {
        if (_inst->_delta < 32000) _inst->_delta += d;
    } else {
        if (_inst->_delta > -32000) _inst->_delta += d;
    }
    portEXIT_CRITICAL_ISR(&mux);
}

int8_t Encoder::getRotation() {
    portENTER_CRITICAL(&mux);
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
    portEXIT_CRITICAL(&mux);

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
