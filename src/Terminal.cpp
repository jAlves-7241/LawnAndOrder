#include "i18n.h"
#include "Terminal.h"
#include "AppState.h"
#include "History.h"
#include "RTClock.h"
#include "Scheduler.h"
#include "Storage.h"
#include "UI.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Wire.h>

Terminal terminal;
extern UI ui;

// ─────────────────────────────────────────────────────────
// Command Table
// ─────────────────────────────────────────────────────────
// Each entry binds a command name to its handler and its
// help-text string.  _processCommand() loops through this
// table instead of an if/else chain; _cmdHelp() iterates
// the same table to print the help menu, guaranteeing they
// can never get out of sync.
// ─────────────────────────────────────────────────────────

const Terminal::CmdEntry Terminal::CMD_TABLE[] = {
    { "help",           &Terminal::_cmdHelp,          TXT_TERM_HELP_1,  nullptr           },
    { "?",              &Terminal::_cmdHelp,          nullptr,           nullptr           },
    { "status",         &Terminal::_cmdStatus,        TXT_TERM_HELP_2,  nullptr           },
    { "set_time",       &Terminal::_cmdSetTime,       TXT_TERM_HELP_3,  TXT_TERM_HELP_3_EX },
    { "export_config",  &Terminal::_cmdExportConfig,  TXT_TERM_HELP_4,  nullptr           },
    { "import_config",  &Terminal::_cmdImportConfig,  TXT_TERM_HELP_5,  nullptr           },
    { "export_history", &Terminal::_cmdExportHistory, TXT_TERM_HELP_6,  nullptr           },
    { "clear_history",  &Terminal::_cmdClearHistory,  TXT_TERM_HELP_7,  nullptr           },
    { "reboot",         &Terminal::_cmdReboot,        TXT_TERM_HELP_8,  nullptr           },
    { "i2c_scan",       &Terminal::_cmdI2cScan,       TXT_TERM_HELP_9,  nullptr           },
};

const uint8_t Terminal::CMD_COUNT = sizeof(Terminal::CMD_TABLE) / sizeof(Terminal::CMD_TABLE[0]);

// ─────────────────────────────────────────────────────────
// Terminal-aware logging
// ─────────────────────────────────────────────────────────
// Prints a log message on its own line, preserving any
// partial user input and the pending confirmation prompt.
// Uses ANSI escapes to clear and redraw cleanly.
// ─────────────────────────────────────────────────────────

void terminalAwareLog(const char* msg) {
    if (terminal._bufLen > 0 || terminal._pendingClearHistory) {
        // Apagar a linha parcial do utilizador (CR + clear-to-EOL)
        Serial.print("\r\033[K");
        // Imprimir o log na sua própria linha
        Serial.println(msg);

        // Redesenhar o prompt se aplicável
        if (terminal._pendingClearHistory) {
            Serial.print(TXT_TERM_WIPE_CONFIRM);
            Serial.print(" ");
        }

        // Re-ecoar o input parcial para o utilizador continuar a escrever
        if (terminal._bufLen > 0) {
            Serial.write((const uint8_t*)terminal._buffer, terminal._bufLen);
        }
        // Restaurar a posição visual do cursor
        int back = terminal._bufLen - terminal._cursorPos;
        if (back > 0) {
            Serial.printf("\033[%dD", back);
        }
    } else {
        Serial.println(msg);
    }
}

// ─────────────────────────────────────────────────────────
// Utilities
// ─────────────────────────────────────────────────────────

static char *trimWhitespace(char *str) {
    if (!str)
        return nullptr;
    // Trim esquerdo
    while (*str == ' ' || *str == '\t')
        str++;

    size_t len = strlen(str);
    if (len == 0)
        return str;

    // Trim direito
    char *end = str + len - 1;
    while (end > str && (*end == ' ' || *end == '\t')) {
        *end = '\0';
        end--;
    }
    return str;
}

static const char *getModeStr(AppMode mode) {
    switch (mode) {
    case AppMode::INTENSO:      return TXT_INTENSE;
    case AppMode::MEDIO:        return TXT_MEDIUM;
    case AppMode::FRACO:        return TXT_WEAK;
    case AppMode::DESATIVADO:   return TXT_DISABLED;
    case AppMode::PERSONALIZADO: return TXT_CUSTOM;
    default:                     return TXT_UNKNOWN;
    }
}

// ═════════════════════════════════════════════════════════
// Construction / Init
// ═════════════════════════════════════════════════════════

Terminal::Terminal()
    : _bufLen(0), _cursorPos(0), _pendingClearHistory(false),
      _in_ansi(false), _ansi_bytes(0), _ignore_rest(false), _last_c(0)
{
    memset(_buffer, 0, sizeof(_buffer));
}

void Terminal::begin() {
    _bufLen = 0;
    _cursorPos = 0;
    _in_ansi = false;
    _ansi_bytes = 0;
    _ignore_rest = false;
    _last_c = 0;
    LOG_I("TERM", TXT_LOG_TERM_READY);
}

// ═════════════════════════════════════════════════════════
// Line-Editor Primitives
//
// Small, focused helpers used by the input state machine
// and by terminalAwareLog().  They operate directly on
// _buffer/_bufLen/_cursorPos and emit the matching ANSI
// sequences to keep the visual terminal in sync.
// ═════════════════════════════════════════════════════════

// Move the visual cursor to the start of the input
void Terminal::_moveCursorToStart() {
    if (_cursorPos > 0) {
        Serial.printf("\033[%dD", _cursorPos);
        _cursorPos = 0;
    }
}

// Move the visual cursor to the end of the input
void Terminal::_moveCursorToEnd() {
    if (_cursorPos < _bufLen) {
        Serial.printf("\033[%dC", _bufLen - _cursorPos);
        _cursorPos = _bufLen;
    }
}

// Redraw characters from _cursorPos to _bufLen, then
// move the visual cursor back to _cursorPos.
// Precondition: visual cursor is already at _cursorPos.
void Terminal::_redrawTail() {
    Serial.print("\033[K");
    if (_bufLen > _cursorPos) {
        Serial.write((const uint8_t*)(_buffer + _cursorPos), _bufLen - _cursorPos);
        int back = _bufLen - _cursorPos;
        if (back > 0) {
            Serial.printf("\033[%dD", back);
        }
    }
}

// Delete the character at the current cursor position (Del key)
void Terminal::_deleteCharAtCursor() {
    if (_cursorPos < _bufLen) {
        _bufLen--;
        memmove(&_buffer[_cursorPos], &_buffer[_cursorPos + 1], _bufLen - _cursorPos);
        _redrawTail();
    }
}

// Delete the character before the cursor (Backspace)
void Terminal::_backspaceAtCursor() {
    if (_cursorPos > 0) {
        _cursorPos--;
        _bufLen--;
        memmove(&_buffer[_cursorPos], &_buffer[_cursorPos + 1], _bufLen - _cursorPos);
        Serial.print("\b");
        _redrawTail();
    }
}

// Clear the entire input line visually and logically
void Terminal::_clearLine() {
    if (_bufLen > 0 || _pendingClearHistory) {
        Serial.print("\r\033[K");
        _bufLen = 0;
        _cursorPos = 0;
    }
}

// ═════════════════════════════════════════════════════════
// Input State Machine
//
// The update() loop reads bytes from Serial and dispatches
// each one through a chain of focused handlers.  Each
// handler returns true if it consumed the character.
// ═════════════════════════════════════════════════════════

// Discard all pending serial input (used during history export)
void Terminal::_drainInput() {
    uint8_t budget = 64;
    while (Serial.available() > 0 && budget-- > 0) {
        Serial.read();
    }
    _bufLen = 0;
    _cursorPos = 0;
}

// Feed a character into the ANSI sequence accumulator.
// Returns true if the character was consumed (either as
// part of an ongoing sequence, or as the ESC that starts
// one).  When a complete sequence is detected, calls
// _handleAnsiSequence() before returning.
bool Terminal::_feedAnsi(char c) {
    // Start of a new escape sequence
    if (c == 27) {
        _in_ansi = true;
        _ansi_bytes = 0;
        _last_c = c;
        return true;
    }

    if (!_in_ansi) return false;

    // Accumulate bytes
    if (_ansi_bytes < sizeof(_ansi_buf)) {
        _ansi_buf[_ansi_bytes] = c;
    }

    // Abort sequences that are too long (protection against invalid sequences)
    if (_ansi_bytes >= sizeof(_ansi_buf) - 1) {
        _in_ansi = false;
        _last_c = c;
        return true;
    }

    _ansi_bytes++;

    // Check for sequence terminator
    bool is_terminator = false;
    if (_ansi_bytes == 1) {
        // After ESC, expect '[' or 'O'; anything else terminates immediately
        if (c != '[' && c != 'O') {
            is_terminator = true;
        }
    } else {
        // CSI / SS3 sequences terminate on an alpha character or '~'
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '~') {
            is_terminator = true;
        }
    }

    if (is_terminator) {
        _in_ansi = false;
        _handleAnsiSequence();
    }

    _last_c = c;
    return true;
}

// Decode a completed ANSI escape sequence from _ansi_buf
// and execute the corresponding line-editor action.
void Terminal::_handleAnsiSequence() {
    char terminator = _ansi_buf[_ansi_bytes - 1];

    // ── CSI sequences (ESC [ ...) ──
    if (_ansi_bytes >= 2 && _ansi_buf[0] == '[') {

        // Two-byte CSI: ESC [ <letter>
        if (_ansi_bytes == 2) {
            switch (terminator) {
            case 'D': // Left arrow
                if (_cursorPos > 0) {
                    _cursorPos--;
                    Serial.print("\033[D");
                }
                return;
            case 'C': // Right arrow
                if (_cursorPos < _bufLen) {
                    _cursorPos++;
                    Serial.print("\033[C");
                }
                return;
            case 'H': // Home
                _moveCursorToStart();
                return;
            case 'F': // End
                _moveCursorToEnd();
                return;
            }
        }

        // Three-byte CSI: ESC [ <digit> ~
        if (_ansi_bytes == 3 && terminator == '~') {
            switch (_ansi_buf[1]) {
            case '3': // Delete
                _deleteCharAtCursor();
                return;
            case '1': // Home (alternate)
            case '7': // Home (rxvt)
                _moveCursorToStart();
                return;
            case '4': // End (alternate)
            case '8': // End (rxvt)
                _moveCursorToEnd();
                return;
            }
        }
    }

    // ── SS3 sequences (ESC O ...) ──
    if (_ansi_bytes == 2 && _ansi_buf[0] == 'O') {
        switch (terminator) {
        case 'H': // Home
            _moveCursorToStart();
            return;
        case 'F': // End
            _moveCursorToEnd();
            return;
        }
    }

    // Unknown sequence — silently ignore
}

// Handle end-of-line characters (CR, LF, CRLF).
// Returns true if the character was consumed.
bool Terminal::_handleLineEnd(char c) {
    // Ignorar \n se vier imediatamente após \r (CRLF)
    if (c == '\n' && _last_c == '\r') {
        _last_c = c;
        return true;
    }

    if (c == '\n' || c == '\r') {
        _ignore_rest = false;
        if (_bufLen > 0 || _pendingClearHistory) {
            _buffer[_bufLen] = '\0';
            _bufLen = 0;
            _cursorPos = 0;
            _processCommand(_buffer);
        }
        _last_c = c;
        return true;
    }

    return false;
}

// Handle control characters: Backspace, Del (127),
// Ctrl+C (0x03), Ctrl+U (0x15).
// Returns true if the character was consumed.
bool Terminal::_handleControlChar(char c) {
    // Cancelar/limpar linha atual (Ctrl+C ou Ctrl+U)
    if (c == 3 || c == 21) {
        _clearLine();
        if (_pendingClearHistory) {
            _pendingClearHistory = false;
            Serial.println(TXT_TERM_CANCELLED);
        }
        _last_c = c;
        return true;
    }

    // Backspace / Delete
    if (c == '\b' || c == 127) {
        _backspaceAtCursor();
        _last_c = c;
        return true;
    }

    return false;
}

// Insert a printable ASCII character at the cursor position.
// Handles buffer overflow gracefully.
void Terminal::_insertChar(char c) {
    // Skip if we are in overflow-ignore mode
    if (_ignore_rest) {
        _last_c = c;
        return;
    }

    // Only printable ASCII
    if (c < 32 || c > 126) {
        _last_c = c;
        return;
    }

    if (_bufLen < sizeof(_buffer) - 1) {
        // Shift right to make room
        memmove(&_buffer[_cursorPos + 1], &_buffer[_cursorPos], _bufLen - _cursorPos);
        _buffer[_cursorPos++] = c;
        _bufLen++;
        // Echo the inserted character
        Serial.write(c);
        // Redraw the tail (characters after the cursor)
        if (_cursorPos < _bufLen) {
            Serial.write((const uint8_t*)(_buffer + _cursorPos), _bufLen - _cursorPos);
            Serial.printf("\033[%dD", _bufLen - _cursorPos);
        }
    } else {
        // Buffer overflow
        Serial.print("\r\033[K");
        Serial.println(TXT_TERM_ERR_OVERFLOW);
        _bufLen = 0;
        _cursorPos = 0;
        _ignore_rest = true;
    }

    _last_c = c;
}

// ═════════════════════════════════════════════════════════
// Main Update Loop
// ═════════════════════════════════════════════════════════

void Terminal::update() {
    // Salvaguarda crítica: se a exportação de histórico estiver ativa,
    // silenciar o terminal
    if (history.isExporting()) {
        _drainInput();
        return;
    }

    // Leitura não bloqueante de caracteres com limite
    uint8_t budget = 64;
    while (Serial.available() > 0 && budget-- > 0) {
        char c = Serial.read();

        if (_feedAnsi(c))          continue;
        if (_handleLineEnd(c))     continue;
        if (_handleControlChar(c)) continue;
        _insertChar(c);
    }
}

// ═════════════════════════════════════════════════════════
// Command Processing & Dispatch
// ═════════════════════════════════════════════════════════

void Terminal::_processCommand(char *cmd) {
    // Nova linha no terminal após submissão de comando
    Serial.println();

    char *trimmed = trimWhitespace(cmd);

    // Handle pending clear_history confirmation
    if (_pendingClearHistory) {
        _pendingClearHistory = false;
        if (trimmed && (strcasecmp(trimmed, "y") == 0 || strcasecmp(trimmed, "yes") == 0 ||
            strcasecmp(trimmed, "s") == 0 || strcasecmp(trimmed, "sim") == 0)) {
            Serial.println(TXT_TERM_WIPING);
            history.clear();
            // Reposicionar ecrã físico para Idle para refletir o histórico limpo
            // imediatamente
            ui.goIdle();
            Serial.println(TXT_TERM_WIPE_OK);
        } else {
            Serial.println(TXT_TERM_CANCELLED);
        }
        return;
    }

    if (!trimmed || *trimmed == '\0') {
        return;
    }

    // Separar o comando dos argumentos (suporta espaço ou tabulação)
    char *args = strpbrk(trimmed, " \t");
    if (args) {
        *args = '\0';
        args++;
        args = trimWhitespace(args);
    }

    // Lookup in command table
    for (uint8_t i = 0; i < CMD_COUNT; i++) {
        if (strcasecmp(trimmed, CMD_TABLE[i].name) == 0) {
            (this->*CMD_TABLE[i].handler)(args);
            return;
        }
    }

    Serial.printf(TXT_TERM_UNKNOWN_CMD, trimmed);
}

// ═════════════════════════════════════════════════════════
// Command Handlers
// ═════════════════════════════════════════════════════════

void Terminal::_cmdHelp(char* /*args*/) {
    Serial.println(
        "======================================================================");
    Serial.println(TXT_TERM_WELCOME_HDR);
    Serial.println(
        "======================================================================");
    Serial.println(TXT_TERM_HELP_TITLE);

    for (uint8_t i = 0; i < CMD_COUNT; i++) {
        if (CMD_TABLE[i].help) {
            Serial.println(CMD_TABLE[i].help);
            if (CMD_TABLE[i].helpExtra) {
                Serial.println(CMD_TABLE[i].helpExtra);
            }
        }
    }

    Serial.println(
        "======================================================================");
}

void Terminal::_cmdStatus(char* /*args*/) {
    Serial.println(TXT_SYS_STATUS_HDR);

    // 1. Relógio / RTC
    if (gState.rtc_valid) {
        Serial.printf(TXT_TERM_STAT_LOCAL,
                      gState.now.year, gState.now.month, gState.now.day,
                      gState.now.hour, gState.now.min, gState.now.sec);
        Serial.printf(TXT_TERM_STAT_UTC, (unsigned long)gState.now.unix);
        Serial.println(TXT_TERM_STAT_RTC_OK);
    } else {
        Serial.printf(TXT_TERM_STAT_LOCAL_SW,
                      gState.now.year, gState.now.month, gState.now.day,
                      gState.now.hour, gState.now.min, gState.now.sec);
        Serial.println(TXT_TERM_STAT_RTC_ERR);
    }

    // 2. Estado Geral
    Serial.printf(TXT_TERM_STAT_MODE, getModeStr(gState.mode));

    if (gState.suspended) {
        uint32_t remaining = (gState.suspended_until > gState.now.unix)
                                 ? (gState.suspended_until - gState.now.unix)
                                 : 0;
        uint32_t hours = remaining / 3600;
        uint32_t mins  = (remaining % 3600) / 60;
        Serial.printf(TXT_TERM_STAT_SUSP, hours, mins);
    } else if (gState.mode == AppMode::DESATIVADO) {
        Serial.println(TXT_TERM_STAT_INACTIVE);
    } else {
        Serial.printf(TXT_TERM_STAT_ACTIVE,
                      gState.next_hour, gState.next_min);
    }

    // 3. Zonas
    Serial.println(TXT_TERM_STAT_ZONES);
    for (int i = 0; i < NUM_ZONES; i++) {
        Serial.printf(TXT_TERM_STAT_Z_FMT, i + 1,
                      gState.zones[i].enabled ? TXT_ON : TXT_OFF,
                      gState.zones[i].duration_min, gState.zones[i].name);
    }

    // 4. Rega em execução
    Serial.print(TXT_TERM_STAT_ACT);
    if (gState.watering.active) {
        if (gState.watering.is_waiting) {
            Serial.printf(TXT_LOG_WAIT_ZONE_TRANS,
                          gState.watering.zone_idx + 1);
        } else {
            Serial.printf(TXT_LOG_WATERING_PROG,
                          gState.watering.zone_idx + 1, gState.watering.progress_pct);
        }
    } else {
        Serial.println(TXT_TERM_STAT_ACT_NO);
    }

    Serial.println(TXT_TERM_STAT_SEP);
}

void Terminal::_cmdSetTime(char *args) {
    if (!args || *args == '\0') {
        Serial.println(TXT_TERM_ERR_SET_TIME_ARGS);
        return;
    }

    int yr, mo, dy, hh, mm, ss;
    if (sscanf(args, "%d-%d-%d %d:%d:%d", &yr, &mo, &dy, &hh, &mm, &ss) != 6) {
        Serial.println(TXT_TERM_ERR_FORMAT);
        return;
    }

    // Validações básicas de limites cronológicos
    if (yr < DATE_YEAR_MIN || yr > DATE_YEAR_MAX) {
        Serial.printf(TXT_TERM_ERR_YEAR, DATE_YEAR_MIN, DATE_YEAR_MAX);
        return;
    }
    if (mo < 1 || mo > 12) {
        Serial.println(TXT_TERM_ERR_MONTH);
        return;
    }
    // Validação avançada de dias por mês (incluindo ano bissexto)
    int maxDays = 31;
    if (mo == 2) {
        bool isLeap = ((yr % 4 == 0 && yr % 100 != 0) || (yr % 400 == 0));
        maxDays = isLeap ? 29 : 28;
    } else if (mo == 4 || mo == 6 || mo == 9 || mo == 11) {
        maxDays = 30;
    }

    if (dy < 1 || dy > maxDays) {
        Serial.printf(TXT_TERM_ERR_DAY, dy, mo, yr, maxDays);
        return;
    }
    if (hh < 0 || hh > 23) {
        Serial.println(TXT_TERM_ERR_HOUR);
        return;
    }
    if (mm < 0 || mm > 59) {
        Serial.println(TXT_TERM_ERR_MIN);
        return;
    }
    if (ss < 0 || ss > 59) {
        Serial.println(TXT_TERM_ERR_SEC);
        return;
    }

    // Acertar a hora local
    rtclock.set((uint16_t)yr, (uint8_t)mo, (uint8_t)dy, (uint8_t)hh, (uint8_t)mm,
                (uint8_t)ss);

    // Atualizar o scheduler imediatamente baseado no novo relógio
    scheduler.onModeChanged();

    Serial.printf(TXT_TERM_TIME_SET, yr, mo, dy, hh, mm, ss);
}

void Terminal::_cmdExportConfig(char* /*args*/) {
    char hexStr[sizeof(AppConfigBlob) * 2 + 1];
    storage.exportConfigHex(hexStr, sizeof(hexStr));

    Serial.println(TXT_TERM_EXPORT_HDR);
    Serial.println(TXT_TERM_EXPORT_COPY);
    Serial.println(hexStr);
    Serial.println(TXT_TERM_EXPORT_SEP);
}

void Terminal::_cmdImportConfig(char *args) {
    if (!args || *args == '\0') {
        Serial.println(TXT_TERM_ERR_IMP_FMT);
        return;
    }

    if (storage.importConfigHex(args)) {
        // Sucesso na importação - recalcular agendamento do Scheduler
        scheduler.onModeChanged();
        // Reposicionar ecrã físico para Idle para refletir as novas configurações
        // imediatamente
        ui.goIdle();
        Serial.println(TXT_TERM_IMP_OK);
    } else {
        Serial.println(TXT_TERM_ERR_IMP_BLOB);
    }
}

void Terminal::_cmdExportHistory(char* /*args*/) {
    if (history.entryCount() == 0) {
        Serial.println(TXT_TERM_HIST_EMPTY);
        return;
    }
    Serial.println(TXT_LOG_START_EXPORT);
    history.startExport();
}

void Terminal::_cmdClearHistory(char* /*args*/) {
    Serial.println(TXT_TERM_WIPE_WARN);
    Serial.print(TXT_TERM_WIPE_CONFIRM);
    Serial.print(" ");
    _pendingClearHistory = true;
}

void Terminal::_cmdReboot(char* /*args*/) {
    Serial.println(TXT_TERM_REBOOT);
    Serial.flush();
    delay(100);
    ESP.restart();
}

void Terminal::_cmdI2cScan(char* /*args*/) {
    byte i2cCount = 0;
    for (byte i = 8; i < 120; i++) {
        Wire.beginTransmission(i);
        if (Wire.endTransmission() == 0) {
            Serial.printf(TXT_LOG_I2C_SCAN_FOUND, i);
            Serial.println();
            i2cCount++;
            delay(1);
        }
    }
    if (i2cCount == 0) {
        Serial.println(TXT_LOG_I2C_SCAN_NONE);
    } else {
        Serial.printf(TXT_LOG_I2C_SCAN_DONE, i2cCount);
        Serial.println();
    }
}
