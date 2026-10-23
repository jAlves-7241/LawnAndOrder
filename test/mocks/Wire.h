#pragma once

#include <stdint.h>
#include <vector>

class TwoWire {
public:
    std::vector<uint8_t> detectedAddresses;

    void begin() {}
    void beginTransmission(uint8_t addr) {
        _lastAddr = addr;
    }

    uint8_t endTransmission() {
        for (uint8_t a : detectedAddresses) {
            if (a == _lastAddr) return 0;
        }
        return 2; // Not found
    }

    void reset() {
        detectedAddresses.clear();
        _lastAddr = 0;
    }

private:
    uint8_t _lastAddr = 0;
};

extern TwoWire Wire;
inline TwoWire Wire;
