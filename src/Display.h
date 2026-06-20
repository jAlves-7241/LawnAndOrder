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
    void recover(); // Re-initializes without forcing backlight/display ON if they were OFF

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
    // Helpers para formatação de texto. Usam templates para deduzir em segurança o tamanho do array de destino.
    template<size_t N> static char* fx(char (&buf)[N], const char* s) { return _fx(buf, N, s); }
    template<size_t N> static char* cx(char (&buf)[N], const char* s) { return _cx(buf, N, s); }
    template<size_t N> static char* hdr(char (&buf)[N], const char* s) { return _hdr(buf, N, s); }
    template<size_t N> static char* pbar(char (&buf)[N], uint8_t pct) { return _pbar(buf, N, pct); }

private:
    static char* _fx(char* buf, size_t n, const char* s);
    static char* _cx(char* buf, size_t n, const char* s);
    static char* _hdr(char* buf, size_t n, const char* s);
    static char* _pbar(char* buf, size_t n, uint8_t pct);
    LiquidCrystal_I2C _lcd;
    bool _backlightOn;
    bool _displayOn;

    // Shadow buffer - only writes row if content actually changed
    char _shadow[LCD_ROWS][LCD_COLS + 1];

    void _writeRow(uint8_t row, const char* text);
    void _invalidateShadow();  // force full redraw on next setRows()
};
