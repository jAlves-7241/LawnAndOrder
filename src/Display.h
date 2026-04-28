#pragma once
#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include "config.h"

// ─────────────────────────────────────────────────────────
// Display
// Thin wrapper around LiquidCrystal_I2C.
// Key design choice: NEVER call lcd.clear() after init —
// instead rewrite only the rows that changed, avoiding flicker.
// ─────────────────────────────────────────────────────────

class Display {
public:
    Display();
    void begin();

    // Write all four rows at once. Pass nullptr to skip a row.
    void setRows(const char* r0, const char* r1,
                 const char* r2, const char* r3);

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

    // Shadow buffer — only writes row if content actually changed
    char _shadow[LCD_ROWS][LCD_COLS + 1];

    void _writeRow(uint8_t row, const char* text);
};
