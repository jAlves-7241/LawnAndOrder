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
    bool _pendingClearHistory;

    bool _in_ansi;
    uint8_t _ansi_bytes;
    bool _ignore_rest;
    char _last_c;

    void _processCommand(char* cmd);
    void _cmdHelp();
    void _cmdStatus();
    void _cmdSetTime(char* args);
    void _cmdExportConfig();
    void _cmdImportConfig(char* hexStr);
    void _cmdExportHistory();
    void _cmdClearHistory();
    void _cmdReboot();
};

extern Terminal terminal;
