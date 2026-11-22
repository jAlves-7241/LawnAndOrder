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
    uint32_t initWireCalls = 0;
    uint8_t mockI2cError = 0;
    uint8_t _i2cErr = 0;
    char screen[4][21];

    LiquidCrystal_I2C(uint8_t addr = 0x27, uint8_t cols = 20, uint8_t rows = 4)
        : _cols(cols), _rows(rows) {
        (void)addr;
        lastInstance = this;
        clear();
        clearCalls = 0;
    }

    void init() {
        init(false, true);
    }

    void init(bool initWire, bool displayOn = true) {
        lastInstance = this;
        if (initWire) initWireCalls++;
        _i2cErr = mockI2cError;
        if (_i2cErr != 0) return;
        displayState = displayOn;
        clear();
    }

    uint8_t lastError() const { return _i2cErr; }
    void clearError() { _i2cErr = 0; }

    void backlight() {
        if (_i2cErr != 0) return;
        if (mockI2cError != 0) {
            _i2cErr = mockI2cError;
            return;
        }
        backlightState = true;
    }
    void noBacklight() {
        if (_i2cErr != 0) return;
        if (mockI2cError != 0) {
            _i2cErr = mockI2cError;
            return;
        }
        backlightState = false;
    }
    void display() {
        if (_i2cErr != 0) return;
        if (mockI2cError != 0) {
            _i2cErr = mockI2cError;
            return;
        }
        displayState = true;
    }
    void noDisplay() {
        if (_i2cErr != 0) return;
        if (mockI2cError != 0) {
            _i2cErr = mockI2cError;
            return;
        }
        displayState = false;
    }

    void clear() {
        if (_i2cErr != 0) return;
        if (mockI2cError != 0) {
            _i2cErr = mockI2cError;
            return;
        }
        for (int r = 0; r < 4; r++) {
            memset(screen[r], ' ', 20);
            screen[r][20] = '\0';
        }
        cursorCol = 0;
        cursorRow = 0;
        clearCalls++;
    }

    void setCursor(uint8_t col, uint8_t row) {
        if (_i2cErr != 0) return;
        if (mockI2cError != 0) {
            _i2cErr = mockI2cError;
            return;
        }
        cursorCol = col;
        cursorRow = (row < 4) ? row : 3;
    }

    void print(const char* s) {
        if (_i2cErr != 0) return;
        if (mockI2cError != 0) {
            _i2cErr = mockI2cError;
            printCalls++;
            return;
        }
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
