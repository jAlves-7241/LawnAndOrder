#pragma once
#include <Arduino.h>

class Terminal {
public:
    Terminal();
    void begin();
    void update(); // Processamento não bloqueante a cada iteração do loop

    friend void terminalAwareLog(const char* msg);
private:
    char _buffer[256];
    uint16_t _bufLen;
    uint16_t _cursorPos;
    bool _pendingClearHistory;
    char _ansi_buf[8];

    bool _in_ansi;
    uint8_t _ansi_bytes;
    bool _ignore_rest;
    char _last_c;

    // Command table (dispatch + help generation)
    struct CmdEntry {
        const char* name;                        // command string (case-insensitive match)
        void (Terminal::*handler)(char* args);    // unified handler
        const char* help;                        // help line (nullptr = alias, skip in help)
        const char* helpExtra;                   // optional second help line (e.g. example)
    };
    static const CmdEntry CMD_TABLE[];
    static const uint8_t  CMD_COUNT;

    // Command processing and dispatch
    void _processCommand(char* cmd);

    // Command handlers (unified signature for table dispatch)
    void _cmdHelp(char* args);
    void _cmdStatus(char* args);
    void _cmdSetTime(char* args);
    void _cmdExportConfig(char* args);
    void _cmdImportConfig(char* args);
    void _cmdExportHistory(char* args);
    void _cmdClearHistory(char* args);
    void _cmdReboot(char* args);
    void _cmdI2cScan(char* args);

    // Input state machine helpers
    void _drainInput();
    bool _feedAnsi(char c);
    void _handleAnsiSequence();
    bool _handleLineEnd(char c);
    bool _handleControlChar(char c);
    void _insertChar(char c);

    // Line-editor primitives
    void _moveCursorToStart();
    void _moveCursorToEnd();
    void _redrawTail();
    void _deleteCharAtCursor();
    void _backspaceAtCursor();
    void _clearLine();
};

extern Terminal terminal;
