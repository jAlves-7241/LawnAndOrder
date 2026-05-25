#include "Display.h"
#include <string.h>
#include <stdio.h>
#include "log.h"

Display::Display() : _lcd(LCD_ADDR, LCD_COLS, LCD_ROWS), _backlightOn(false), _displayOn(false) {
    memset(_shadow, ' ', sizeof(_shadow));
    for (int r = 0; r < LCD_ROWS; r++) _shadow[r][LCD_COLS] = '\0';
}

void Display::begin() {
    _lcd.init();
    _lcd.backlight();
    _lcd.display();
    _backlightOn = true;
    _displayOn = true;
    _lcd.clear();
    _invalidateShadow();
    LOG_I("LCD", "Pronto");
}

void Display::backlightOn() {
    if (_backlightOn) return;
    _lcd.backlight();
    _backlightOn = true;
}

void Display::backlightOff() {
    if (!_backlightOn) return;
    _lcd.noBacklight();
    _backlightOn = false;
}

void Display::displayOn() {
    if (_displayOn) return;
    _lcd.display();
    _displayOn = true;
    // Force redraw because state might be stale
    _invalidateShadow();
}

void Display::displayOff() {
    if (!_displayOn) return;
    _lcd.noDisplay();
    _displayOn = false;
}

void Display::_invalidateShadow() {
    // Fill shadow with an impossible value so every row gets rewritten
    memset(_shadow, 0x01, sizeof(_shadow));
    for (int r = 0; r < LCD_ROWS; r++) _shadow[r][LCD_COLS] = '\0';
}

void Display::setRows(const char* r0, const char* r1,
                      const char* r2, const char* r3) {
    const char* rows[LCD_ROWS] = { r0, r1, r2, r3 };
    for (int r = 0; r < LCD_ROWS; r++) {
        if (rows[r]) _writeRow(r, rows[r]);
    }
}

// Only send to LCD if the row content differs from shadow
void Display::_writeRow(uint8_t row, const char* text) {
    if (row >= LCD_ROWS) return;  // bounds guard
    // Build padded version
    char padded[LCD_COLS + 1];
    int len = strlen(text);
    if (len > LCD_COLS) len = LCD_COLS;
    memcpy(padded, text, len);
    memset(padded + len, ' ', LCD_COLS - len);
    padded[LCD_COLS] = '\0';

    if (memcmp(_shadow[row], padded, LCD_COLS) == 0) return;  // no change

    // Detetar limites do delta
    int diffStart = 0;
    while (diffStart < LCD_COLS && _shadow[row][diffStart] == padded[diffStart]) {
        diffStart++;
    }

    int diffEnd = LCD_COLS - 1;
    while (diffEnd >= diffStart && _shadow[row][diffEnd] == padded[diffEnd]) {
        diffEnd--;
    }

    uint8_t writeLen = diffEnd - diffStart + 1;
    memcpy(_shadow[row] + diffStart, padded + diffStart, writeLen);

    // Mover o cursor e escrever apenas a fatia modificada!
    _lcd.setCursor(diffStart, row);
    char chunk[LCD_COLS + 1];
    memcpy(chunk, padded + diffStart, writeLen);
    chunk[writeLen] = '\0';
    _lcd.print(chunk);
}

// ── Static formatting helpers ──────────────────────────────

// Left-aligned, space-padded to LCD_COLS
char* Display::fx(char* buf, const char* s) {
    int len = strlen(s);
    if (len > LCD_COLS) len = LCD_COLS;
    memcpy(buf, s, len);
    memset(buf + len, ' ', LCD_COLS - len);
    buf[LCD_COLS] = '\0';
    return buf;
}

// Centred in LCD_COLS
char* Display::cx(char* buf, const char* s) {
    int len = strlen(s);
    if (len > LCD_COLS) len = LCD_COLS;
    int pad = LCD_COLS - len;
    int lpad = pad / 2;
    memset(buf, ' ', lpad);
    memcpy(buf + lpad, s, len);
    memset(buf + lpad + len, ' ', pad - lpad);
    buf[LCD_COLS] = '\0';
    return buf;
}

// "~~ TITLE ~~" - fills full LCD_COLS with tildes around centred text
char* Display::hdr(char* buf, const char* s) {
    int len = strlen(s);
    if (len > LCD_COLS - 2) len = LCD_COLS - 2;
    int total_tilde = LCD_COLS - len - 2;  // 2 for the spaces
    int lt = total_tilde / 2;
    int rt = total_tilde - lt;
    int pos = 0;
    memset(buf + pos, '~', lt); pos += lt;
    buf[pos++] = ' ';
    memcpy(buf + pos, s, len); pos += len;
    buf[pos++] = ' ';
    memset(buf + pos, '~', rt); pos += rt;
    buf[LCD_COLS] = '\0';
    return buf;
}

// [#####--------] pct%  (total 20 chars)
char* Display::pbar(char* buf, uint8_t pct) {
    if (pct > 100) pct = 100;
    // Layout: "[" + 13 chars bar + "] " + 3 chars pct + "%" = 19 chars → pad to 20
    int filled = (pct * 13) / 100;
    int pos = 0;
    buf[pos++] = '[';
    for (int i = 0; i < 13; i++) buf[pos++] = (i < filled) ? '#' : '-';
    buf[pos++] = ']';
    buf[pos++] = ' ';
    char pct_str[5];
    snprintf(pct_str, sizeof(pct_str), "%3d%%", pct);
    memcpy(buf + pos, pct_str, 4); pos += 4;
    buf[LCD_COLS] = '\0';
    return buf;
}
