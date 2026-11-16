#include "Display.h"
#include <string.h>
#include <stdio.h>
#include "log.h"
#include "i18n.h"

Display::Display()
    : _lcd(LCD_ADDR, LCD_COLS, LCD_ROWS),
      _backlightOn(false),
      _displayOn(false),
      _lastFullRefresh(0) {
    memset(_shadow, ' ', sizeof(_shadow));
    for (int r = 0; r < LCD_ROWS; r++) {
        _shadow[r][LCD_COLS] = '\0';
        _dirty[r] = true;
    }
}

void Display::begin() {
    _lcd.clearError();
    _lcd.init(false);
    _lcd.backlight();
    _lcd.display();
    _backlightOn = true;
    _displayOn = true;
    _lcd.clear();
    delay(10); // Aguardar que o comando de limpeza termine no hardware real
    memset(_shadow, ' ', sizeof(_shadow));
    for (int r = 0; r < LCD_ROWS; r++) _shadow[r][LCD_COLS] = '\0';
    _invalidateShadow();
    _lastFullRefresh = millis();
    _lastAttemptMs = _lastFullRefresh;
    _lastPeriodicRecoverMs = _lastFullRefresh;
    _busFault = (_lcd.lastError() != 0);
    _consecErrors = _busFault ? 1 : 0;
    _recoverAttempts = 0;
    _hasRecoverAttempt = false;
    _inErrorCallback = false;
    _errorDeferred = false;
    _recoverCooldownMs = 10000UL;
    _commErrWarned = false;
    if (_busFault) {
        LOG_E(TXT_LOG_TAG_LCD, TXT_LOG_LCD_INIT_FAIL);
    } else {
        LOG_I(TXT_LOG_TAG_LCD, TXT_LOG_LCD_READY, LCD_COLS, LCD_ROWS);
    }
}

uint32_t Display::_cooldownForAttempts(uint8_t attempts) {
    if (attempts <= 1) return 10000UL;
    if (attempts == 2) return 60000UL;
    return 300000UL;
}

void Display::recover(bool quiet, bool flushShadow) {
    _recoverExecSeq++;
    _lcd.clearError();
    _lcd.init(false);
    if (_backlightOn) {
        _lcd.backlight();
    } else {
        _lcd.noBacklight();
    }
    
    if (_displayOn) {
        _lcd.display();
    } else {
        _lcd.noDisplay();
    }
    
    _lcd.clear();
    delay(10);
    _invalidateShadow();
    _busFault = (_lcd.lastError() != 0);
    _consecErrors = 0;
    if (!_busFault && _displayOn && flushShadow) {
        for (uint8_t r = 0; r < LCD_ROWS; r++) {
            if (!_flushRow(r)) {
                _invalidateShadow();
                _busFault = true;
                break;
            }
        }
        if (!_busFault) {
            _lastFullRefresh = millis();
        }
    }
    _lastAttemptMs = millis();
    _lastPeriodicRecoverMs = _lastAttemptMs;
    if (_busFault) {
        LOG_E(TXT_LOG_TAG_LCD, TXT_LOG_LCD_REC_FAIL);
    } else {
        _commErrWarned = false;
        _recoverAttempts = 0;
        _hasRecoverAttempt = false;
        _recoverCooldownMs = 10000UL;
        if (!quiet) {
            LOG_I(TXT_LOG_TAG_LCD, TXT_LOG_LCD_RECOVERED);
        }
    }
}

void Display::wake(bool hasRenderer) {
    if (!_displayOn) {
        _displayOn = true;
        _backlightOn = true;
        recover(true, !hasRenderer);
        return;
    }
    backlightOn();
}

bool Display::needsPeriodicRecover(uint32_t intervalMs) const {
    return _displayOn && !_busFault && (millis() - _lastPeriodicRecoverMs) >= intervalMs;
}

void Display::backlightOn() {
    if (_backlightOn) return;
    _lcd.clearError();
    _lcd.backlight();
    _busFault = (_lcd.lastError() != 0);
    _backlightOn = true;
    _invalidateShadow(); // Forçar redesenho completo ao acordar para corrigir glitches físicos
}

void Display::backlightOff() {
    if (!_backlightOn) return;
    _lcd.clearError();
    _lcd.noBacklight();
    _busFault = (_lcd.lastError() != 0);
    if (_busFault) _invalidateShadow();
    _backlightOn = false;
}

void Display::displayOn() {
    if (_displayOn) return;
    _lcd.clearError();
    _lcd.display();
    _busFault = (_lcd.lastError() != 0);
    _displayOn = true;
    // Force redraw because state might be stale
    _invalidateShadow();
}

void Display::displayOff() {
    if (!_displayOn) return;
    _lcd.clearError();
    _lcd.noDisplay();
    _busFault = (_lcd.lastError() != 0);
    if (_busFault) _invalidateShadow();
    _displayOn = false;
}

void Display::_invalidateShadow() {
    // Mark all rows dirty while keeping valid ASCII in _shadow so nullptr rows
    // and periodic full refreshes never emit invalid characters (e.g. 0xFF).
    for (int r = 0; r < LCD_ROWS; r++) _dirty[r] = true;
}

bool Display::_flushRow(uint8_t row) {
    if (row >= LCD_ROWS) return true;
    _lcd.setCursor(0, row);
    _lcd.print(_shadow[row]);
    if (_lcd.lastError() != 0) {
        return false;
    }
    _dirty[row] = false;
    return true;
}

void Display::sync() {
    if (!_displayOn) return;
    if ((millis() - _lastAttemptMs) < 1000UL) return;
    setRows(nullptr, nullptr, nullptr, nullptr);
}

void Display::setRows(const char* r0, const char* r1,
                      const char* r2, const char* r3) {
    if (!_displayOn) return;
    _lastAttemptMs = millis();
    _lcd.clearError();
    bool frameError = false;
    bool wroteAny = false;
    bool fullRowWritten[LCD_ROWS] = { false, false, false, false };
    const char* rows[LCD_ROWS] = { r0, r1, r2, r3 };
    for (int r = 0; r < LCD_ROWS; r++) {
        if (rows[r]) {
            wroteAny = true;
            if (!_writeRow(r, rows[r], &fullRowWritten[r])) {
                frameError = true;
                break;
            }
        } else if (_dirty[r]) {
            wroteAny = true;
            if (!_flushRow((uint8_t)r)) {
                frameError = true;
                break;
            }
            fullRowWritten[r] = true;
        }
    }

    // Forçar atualização total a cada 60 segundos para autocorrigir ruído no bus I2C (glitches físicos)
    if (!frameError) {
        uint32_t now = millis();
        if (now - _lastFullRefresh >= 60000UL) {
            for (int r = 0; r < LCD_ROWS; r++) {
                if (fullRowWritten[r]) continue;
                wroteAny = true;
                if (!_flushRow((uint8_t)r)) {
                    frameError = true;
                    break;
                }
            }
            if (!frameError) {
                _lastFullRefresh = now;
            }
        }
    }

    if (frameError) {
        _invalidateShadow();
        _busFault = true;
        uint32_t now = millis();
        bool inCooldown = (_hasRecoverAttempt && (now - _lastRecoverMs) < _recoverCooldownMs);
        if (!inCooldown) {
            if (_consecErrors < 255) _consecErrors++;
            if (_consecErrors >= 3) {
                if (!_commErrWarned) {
                    LOG_W(TXT_LOG_TAG_LCD, TXT_LOG_LCD_COMM_ERR);
                    _commErrWarned = true;
                }
                _consecErrors = 0;
                const uint32_t seqBefore = _recoverExecSeq;
                _inErrorCallback = true;
                _errorDeferred = false;
                if (_onError) {
                    _onError();
                } else {
                    recover();
                }
                _inErrorCallback = false;

                if (_errorDeferred) {
                    // Shared I2C throttle suppressed bus recovery; retry after 10 s without escalating backoff
                    _lastRecoverMs = now;
                    _hasRecoverAttempt = true;
                    _recoverCooldownMs = 10000UL;
                } else if (_recoverExecSeq != seqBefore && !_busFault) {
                    // recover() succeeded and already reset backoff counters
                } else {
                    _lastRecoverMs = now;
                    _hasRecoverAttempt = true;
                    if (_recoverAttempts < 255) _recoverAttempts++;
                    _recoverCooldownMs = _cooldownForAttempts(_recoverAttempts);
                }
            }
        }
    } else if (wroteAny) {
        _busFault = false;
        _consecErrors = 0;
        _recoverAttempts = 0;
        _hasRecoverAttempt = false;
        _recoverCooldownMs = 10000UL;
        _commErrWarned = false;
    }
}

// Only send to LCD if the row content differs from shadow
bool Display::_writeRow(uint8_t row, const char* text, bool* wroteFull) {
    if (wroteFull) *wroteFull = false;
    if (row >= LCD_ROWS) return true;  // bounds guard
    if (!text) text = "";
    // Build padded version
    char padded[LCD_COLS + 1];
    size_t rawLen = strlen(text);
    int len = (rawLen > LCD_COLS) ? LCD_COLS : (int)rawLen;
    memcpy(padded, text, len);
    memset(padded + len, ' ', LCD_COLS - len);
    padded[LCD_COLS] = '\0';

    if (_dirty[row]) {
        memcpy(_shadow[row], padded, LCD_COLS + 1);
        if (!_flushRow(row)) {
            return false;
        }
        if (wroteFull) *wroteFull = true;
        return true;
    }

    if (memcmp(_shadow[row], padded, LCD_COLS) == 0) return true;  // no change

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
    if (_lcd.lastError() != 0) {
        _dirty[row] = true;
        return false;
    }
    if (wroteFull && diffStart == 0 && diffEnd == LCD_COLS - 1) {
        *wroteFull = true;
    }
    return true;
}

// ── Static formatting helpers ──────────────────────────────

// Left-aligned, space-padded to LCD_COLS
char* Display::_fx(char* buf, size_t n, const char* s) {
    if (n == 0) return buf;
    if (!s) s = "";
    int len = strlen(s);
    if (len > LCD_COLS) len = LCD_COLS;
    if (len > (int)n - 1) len = n - 1;
    memcpy(buf, s, len);
    int pad = LCD_COLS - len;
    if (pad > (int)n - 1 - len) pad = n - 1 - len;
    memset(buf + len, ' ', pad);
    buf[len + pad] = '\0';
    return buf;
}

// Centred in LCD_COLS
char* Display::_cx(char* buf, size_t n, const char* s) {
    if (n == 0) return buf;
    if (!s) s = "";
    int maxCols = min((int)LCD_COLS, (int)n - 1);
    if (maxCols <= 0) {
        buf[0] = '\0';
        return buf;
    }
    int len = strlen(s);
    if (len > maxCols) len = maxCols;
    int pad = maxCols - len;
    int lpad = pad / 2;
    memset(buf, ' ', lpad);
    memcpy(buf + lpad, s, len);
    int rpad = pad - lpad;
    memset(buf + lpad + len, ' ', rpad);
    buf[lpad + len + rpad] = '\0';
    return buf;
}

// "~~ TITLE ~~" - fills full LCD_COLS with tildes around centred text
char* Display::_hdr(char* buf, size_t n, const char* s) {
    if (n == 0) return buf;
    if (!s) s = "";
    int maxCols = min((int)LCD_COLS, (int)n - 1);
    if (maxCols <= 0) {
        buf[0] = '\0';
        return buf;
    }
    int len = strlen(s);
    if (len > maxCols - 2) len = maxCols - 2;
    if (len < 0) len = 0;
    int total_tilde = maxCols - len - 2;  // 2 for the spaces
    if (total_tilde < 0) total_tilde = 0;
    int lt = total_tilde / 2;
    int rt = total_tilde - lt;
    int pos = 0;
    if (lt > 0) {
        memset(buf + pos, '~', lt);
        pos += lt;
    }
    if (pos < maxCols) buf[pos++] = ' ';
    if (len > 0) {
        memcpy(buf + pos, s, len);
        pos += len;
    }
    if (pos < maxCols) buf[pos++] = ' ';
    if (rt > 0 && pos < maxCols) {
        if (rt > maxCols - pos) rt = maxCols - pos;
        memset(buf + pos, '~', rt);
        pos += rt;
    }
    buf[pos] = '\0';
    return buf;
}

// [#####--------] pct%  (total 20 chars)
char* Display::_pbar(char* buf, size_t n, uint8_t pct) {
    if (n == 0) return buf;
    if (pct > 100) pct = 100;
    const int barWidth = LCD_COLS - 7;
    int filled = (pct * barWidth) / 100;
    int pos = 0;
    if (pos < (int)n - 1) buf[pos++] = '[';
    for (int i = 0; i < barWidth; i++) {
        if (pos < (int)n - 1) buf[pos++] = (i < filled) ? '#' : '-';
    }
    if (pos < (int)n - 1) buf[pos++] = ']';
    if (pos < (int)n - 1) buf[pos++] = ' ';
    char pct_str[6];
    snprintf(pct_str, sizeof(pct_str), "%3d%%", pct);
    int pLen = strlen(pct_str);
    if (pLen > (int)n - 1 - pos) pLen = n - 1 - pos;
    memcpy(buf + pos, pct_str, pLen); pos += pLen;
    buf[pos] = '\0';
    return buf;
}
