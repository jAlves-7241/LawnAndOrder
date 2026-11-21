#pragma once
#include <stdint.h>
#include "Arduino.h"

typedef int gpio_num_t;

inline int gpio_get_level(gpio_num_t pin) {
    return digitalRead((uint8_t)pin);
}

inline int gpio_set_level(gpio_num_t pin, uint32_t level) {
    if (pin >= 0 && pin < 64) {
        _mock_gpio[pin] = level ? HIGH : LOW;
    }
    return 0;
}
