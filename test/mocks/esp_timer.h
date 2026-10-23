#pragma once
#include <stdint.h>
#include "Arduino.h"

inline uint64_t esp_timer_get_time() {
    return (uint64_t)micros();
}
