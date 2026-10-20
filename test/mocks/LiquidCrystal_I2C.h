#pragma once

#include <stdint.h>

class LiquidCrystal_I2C {
public:
    LiquidCrystal_I2C(uint8_t addr = 0x27, uint8_t cols = 20, uint8_t rows = 4) {
        (void)addr; (void)cols; (void)rows;
    }
    void init() {}
    void backlight() {}
    void noBacklight() {}
    void clear() {}
    void setCursor(uint8_t col, uint8_t row) { (void)col; (void)row; }
    void print(const char* s) { (void)s; }
    void createChar(uint8_t location, uint8_t charmap[]) { (void)location; (void)charmap; }
};
