#pragma once
#include <Arduino.h>

class Terminal {
public:
    Terminal();
    void begin();
    void update(); // Processamento não bloqueante a cada iteração do loop

private:
    char _buffer[256];
    uint16_t _bufLen;

    void _processCommand(char* cmd);
    void _cmdHelp();
    void _cmdStatus();
    void _cmdSetTime(char* args);
    void _cmdExportConfig();
    void _cmdImportConfig(char* hexStr);
    void _cmdReboot();
};

extern Terminal terminal;
