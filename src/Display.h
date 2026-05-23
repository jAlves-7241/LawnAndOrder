#pragma once
#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include "config.h"

// ─────────────────────────────────────────────────────────
// Display
// Thin wrapper around LiquidCrystal_I2C.
// Key design choice: NEVER call lcd.clear() after init -
// instead rewrite only the rows that changed, avoiding flicker.
// ─────────────────────────────────────────────────────────

class Display {
public:
    Display();
    void begin();

    // Write all four rows at once.
    // Pass "" to write a blank row. Pass nullptr to leave a row unchanged.
    void setRows(const char* r0, const char* r1,
                 const char* r2, const char* r3);

    // Backlight control.
    void backlightOn();
    void backlightOff();
    bool isBacklightOn() const { return _backlightOn; }

    // Character display control (pixels).
    void displayOn();
    void displayOff();
    bool isDisplayOn() const { return _displayOn; }

    // ── Formatting helpers (return into caller-supplied buf) ──
    // Left-pad with spaces to LCD_COLS
    static char* fx(char* buf, const char* s);

    // Centre-align in LCD_COLS
    static char* cx(char* buf, const char* s);

    // "~~ TITLE ~~" header bar
    static char* hdr(char* buf, const char* s);

    // Progress bar: [#####--------]  42%
    static char* pbar(char* buf, uint8_t pct);

private:
    LiquidCrystal_I2C _lcd;
    bool _backlightOn;
    bool _displayOn;

    // Shadow buffer - only writes row if content actually changed
    char _shadow[LCD_ROWS][LCD_COLS + 1];

    void _writeRow(uint8_t row, const char* text);
    void _invalidateShadow();  // force full redraw on next setRows()
};
