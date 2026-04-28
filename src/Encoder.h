#pragma once
#include <Arduino.h>
#include "config.h"

// ISR-based rotary encoder driver.
// getRotation() and getClick() are the only methods needed in the main loop.

class Encoder {
public:
    Encoder(uint8_t pin_clk, uint8_t pin_dt, uint8_t pin_sw);

    void begin();

    // Returns -1, 0, or +1 accumulated since last call. Resets on read.
    int8_t getRotation();

    // Returns true exactly once per physical click (debounced).
    bool getClick();

private:
    const uint8_t _clk, _dt, _sw;

    volatile int16_t _delta;    // int16 avoids ISR overflow on fast spins

    bool     _btn_prev;
    uint32_t _btn_last_ms;

    // Static ISR trampoline — only one Encoder instance is supported.
    static void   IRAM_ATTR _isr();
    static Encoder* _inst;
};
