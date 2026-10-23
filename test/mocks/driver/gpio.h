#pragma once
#include <stdint.h>
#include "Arduino.h"

typedef int gpio_num_t;

inline int gpio_get_level(gpio_num_t pin) {
    return digitalRead((uint8_t)pin);
}
