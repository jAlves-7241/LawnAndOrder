#pragma once

#include <stdint.h>
#include <string.h>

class LiquidCrystal_I2C {
public:
    inline static LiquidCrystal_I2C* lastInstance = nullptr;

    uint8_t _cols;
    uint8_t _rows;
    uint8_t cursorCol = 0;
    uint8_t cursorRow = 0;
    bool backlightState = false;
    bool displayState = false;
    uint32_t printCalls = 0;
    uint32_t clearCalls = 0;
    char screen[4][21];

    LiquidCrystal_I2C(uint8_t addr = 0x27, uint8_t cols = 20, uint8_t rows = 4)
        : _cols(cols), _rows(rows) {
        (void)addr;
        lastInstance = this;
        clear();
        clearCalls = 0;
    }

    void init() {
        lastInstance = this;
        displayState = true;
    }

    void backlight() { backlightState = true; }
    void noBacklight() { backlightState = false; }
    void display() { displayState = true; }
    void noDisplay() { displayState = false; }

    void clear() {
        for (int r = 0; r < 4; r++) {
            memset(screen[r], ' ', 20);
            screen[r][20] = '\0';
        }
        cursorCol = 0;
        cursorRow = 0;
        clearCalls++;
    }

    void setCursor(uint8_t col, uint8_t row) {
        cursorCol = col;
        cursorRow = (row < 4) ? row : 3;
    }

    void print(const char* s) {
        printCalls++;
        if (!s || cursorRow >= 4) return;
        for (size_t i = 0; s[i] != '\0' && cursorCol < 20; i++) {
            screen[cursorRow][cursorCol++] = s[i];
        }
        screen[cursorRow][20] = '\0';
    }

    const char* getRow(uint8_t row) const {
        return (row < 4) ? screen[row] : "";
    }

    void createChar(uint8_t location, uint8_t charmap[]) { (void)location; (void)charmap; }
};
