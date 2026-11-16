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
    typedef void (*ErrorCallback)();

    Display();
    void begin();
    void recover(); // Re-initializes without forcing backlight/display ON if they were OFF
    void setErrorCallback(ErrorCallback cb) { _onError = cb; }
    bool hasBusFault() const { return _busFault; }

    // Write all four rows at once.
    // Pass "" to write a blank row. Pass nullptr to leave a row unchanged.
    void setRows(const char* r0, const char* r1,
                 const char* r2, const char* r3);

    // Flush dirty/faulted rows or periodic refresh when display is active.
    void sync();

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
    bool _busFault = false;
    bool _commErrWarned = false;
    uint8_t _consecErrors = 0;
    uint32_t _lastRecoverMs = 0;
    ErrorCallback _onError = nullptr;

    // Shadow buffer - only writes row if content actually changed
    char _shadow[LCD_ROWS][LCD_COLS + 1];
    bool _dirty[LCD_ROWS];
    uint32_t _lastFullRefresh;
    uint32_t _lastAttemptMs = 0;

    bool _flushRow(uint8_t row);
    bool _writeRow(uint8_t row, const char* text, bool* wroteFull = nullptr);
    void _invalidateShadow();  // force full redraw on next setRows()
};
