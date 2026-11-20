#pragma once
#include <stdint.h>
#include "Arduino.h"
#include "soc/gpio_struct.h"

inline int gpio_ll_get_level(gpio_dev_t* hw, uint32_t gpio_num) {
    (void)hw;
    return digitalRead((uint8_t)gpio_num);
}
