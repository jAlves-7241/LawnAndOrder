#pragma once
#include <Arduino.h>
#include "config.h"

// ISR-based rotary encoder driver.
// getRotation() and getClick() are the only methods needed in the main loop.

class Encoder {
public:
    Encoder(uint8_t pin_clk, uint8_t pin_dt, uint8_t pin_sw);
    ~Encoder();

    Encoder(const Encoder&) = delete;
    Encoder& operator=(const Encoder&) = delete;
    Encoder(Encoder&&) = delete;
    Encoder& operator=(Encoder&&) = delete;

    void begin();

    // Returns accumulated rotation delta since last call (clamped to ±127). Resets on read.
    int8_t getRotation();

    // Returns true exactly once per physical click (debounced).
    bool getClick();

private:
    UNIT_TEST_FRIEND
    const uint8_t _clk, _dt, _sw;

    volatile int16_t _delta;    // int16 avoids ISR overflow on fast spins

    bool     _btn_prev;
    bool     _last_reading;
    bool     _attached;
    uint32_t _btn_last_ms;
    volatile uint64_t _last_isr_us;
    volatile bool     _has_last_isr;

    // Static ISR trampoline - only one Encoder instance is supported.
    static void   IRAM_ATTR _isr();
    static Encoder* volatile _inst;
};
