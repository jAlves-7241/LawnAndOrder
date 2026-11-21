#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Minimal Arduino Print base class mock matching Arduino-ESP32 Print.cpp
// short-circuit semantics (stops writing when write(uint8_t) returns 0).
class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t) = 0;

    virtual size_t write(const uint8_t* buffer, size_t size) {
        if (!buffer) return 0;
        size_t n = 0;
        while (size--) {
            if (write(*buffer++)) {
                n++;
            } else {
                break;
            }
        }
        return n;
    }

    size_t write(const char* str) {
        if (!str) return 0;
        return write(reinterpret_cast<const uint8_t*>(str), strlen(str));
    }

    size_t print(const char* str) {
        return write(str);
    }

    size_t print(char c) {
        return write(static_cast<uint8_t>(c));
    }
};
