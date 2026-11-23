#pragma once
#include <stdint.h>

class Display;
class Encoder;
class RTClock;
class UI;

struct I2CRecoveryState {
    uint32_t lastRecoverMs = 0;
    bool     hasRecovered  = false;
};

struct SystemBootContext {
    Display& display;
    Encoder& encoder;
    UI&      ui;
    void   (*recoverI2c)();
    void   (*afterI2cInit)(); // optional hook invoked after Wire.begin() + LCD power-up delay
};

// Pre-load RELAY_OFF in hardware GPIO latches before configuring OUTPUT pins
// so ESP32 Arduino Core 3.x (periman) cannot emit a LOW glitch on boot.
void initRelayPinsSafe();

// Attempt I2C bus unstick via up to 9 open-drain SCL clocks + STOP condition.
// Returns true if SDA is HIGH after recovery.
bool clearI2CBus();

// Configure Wire transaction timeout (50 ms).
void configureI2CTimeout();

// Throttled (10 s) full I2C bus + RTC + LCD recovery.
void recoverI2CWith(Display& disp, RTClock& clk, I2CRecoveryState& state);

// Evaluate persisted suspension state against live RTC time on boot.
void evaluateBootSuspension();

// Canonical full-system initialization and cooperative loop tick shared by main.cpp and tests.
void bootSystem(const SystemBootContext& ctx);
void tickSystem(UI& ui);
