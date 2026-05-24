#include "History.h"
#include <LittleFS.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "log.h"

History history;

static const size_t LINE_BUF = 52;

// ─────────────────────────────────────────────────────────
bool History::begin(bool formatOnFail) {
    _ready = LittleFS.begin(formatOnFail);
    if (!_ready) {
        LOG_E("HIST", "Falha ao montar LittleFS");
        _lineCount = 0;
        _cacheCount = 0;
    } else {
        _lineCount = _countLines();
        _populateCache();
        LOG_I("HIST", "Pronto - %d entradas", _lineCount);
    }
    return _ready;
}

// ─────────────────────────────────────────────────────────
void History::record(const HistoryEntry& entry) {
    if (!_ready)                              return;
    if (entry.trigger == WaterTrigger::TEST)  return;
    if (!gState.rtc_valid)                    return;

    char line[LINE_BUF];
    _entryToLine(entry, line, sizeof(line));

    if (_lineCount >= HISTORY_MAX_ENTRIES) {
        _rotateAndAppend(line);
        // O _lineCount é ajustado internamente pelo _rotateAndAppend
    } else {
        File f = LittleFS.open(HISTORY_FILE, "a");
        if (!f) { LOG_E("HIST", "Falha ao abrir ficheiro"); return; }
        f.println(line);
        f.close();
        _lineCount++;
    }

    // Atualizar o cache em memória (shift left se estiver cheio)
    if (_cacheCount < HISTORY_DISPLAY) {
        _cache[_cacheCount++] = entry;
    } else {
        for (int i = 0; i < HISTORY_DISPLAY - 1; i++) {
            _cache[i] = _cache[i + 1];
        }
        _cache[HISTORY_DISPLAY - 1] = entry;
    }

    LOG_I("HIST", "Registado: %s", line);
}

// ─────────────────────────────────────────────────────────
uint8_t History::readLast(uint8_t count, HistoryEntry out[]) const {
    if (!_ready || count == 0 || _cacheCount == 0) return 0;
    
    // Apenas copiamos da RAM (zero acessos ao disco!)
    uint8_t n = (_cacheCount < count) ? _cacheCount : count;
    for (uint8_t i = 0; i < n; i++) {
        // Copiamos os N mais recentes
        out[i] = _cache[_cacheCount - n + i];
    }
    return n;
}

void History::clear() {
    if (!_ready) return;
    LittleFS.remove(HISTORY_FILE);
    _lineCount = 0;
    _cacheCount = 0;
    LOG_I("HIST", "Historico apagado");
}

uint16_t History::entryCount() const { return _lineCount; }

// ─────────────────────────────────────────────────────────
// Private
// ─────────────────────────────────────────────────────────

// CSV: "YYYY-MM-DDTHH:MM,<trigger_char>,d0,d1,d2,d3"
void History::_entryToLine(const HistoryEntry& e, char* buf, size_t len) {
    char tc = (char)e.trigger;   // 'A', 'M', 'C' - already encoded in enum value
    snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d,%c",
             e.year, e.month, e.day, e.hour, e.min, tc);
    for (int i = 0; i < NUM_ZONES; i++) {
        char tmp[5];
        snprintf(tmp, sizeof(tmp), ",%d", e.zone_dur[i]);
        strncat(buf, tmp, len - strlen(buf) - 1);
    }
}

bool History::_lineToEntry(const char* line, HistoryEntry& out) {
    int yr, mo, dy, hh, mm;
    char tc = 0;

    // "YYYY-MM-DDTHH:MM,C,d0,..."
    if (sscanf(line, "%4d-%2d-%2dT%2d:%2d,%c",
               &yr, &mo, &dy, &hh, &mm, &tc) != 6) return false;

    out.year  = (uint16_t)yr;
    out.month = (uint8_t)mo;
    out.day   = (uint8_t)dy;
    out.hour  = (uint8_t)hh;
    out.min   = (uint8_t)mm;

    // Validate trigger char
    if (tc == 'A' || tc == 'M' || tc == 'C' || tc == 'T')
        out.trigger = (WaterTrigger)tc;
    else
        out.trigger = WaterTrigger::MANUAL;

    // Parse zone durations - after the second comma
    const char* p = line;
    int commas = 0;
    while (*p && commas < 2) { if (*p++ == ',') commas++; }
    for (int i = 0; i < NUM_ZONES; i++) {
        out.zone_dur[i] = (uint8_t)atoi(p);
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
    return true;
}

uint16_t History::_countLines() const {
    if (!_ready) return 0;
    File f = LittleFS.open(HISTORY_FILE, "r");
    if (!f) return 0;
    uint16_t n = 0;
    uint8_t buf[256];
    while (f.available()) {
        int len = f.read(buf, sizeof(buf));
        for (int i = 0; i < len; i++) {
            if (buf[i] == '\n') n++;
        }
    }
    f.close();
    return n;
}

void History::_populateCache() {
    _cacheCount = 0;
    if (!_ready || _lineCount == 0) return;

    File f = LittleFS.open(HISTORY_FILE, "r");
    if (!f) return;

    char line[LINE_BUF];
    while (f.available()) {
        int len = f.readBytesUntil('\n', line, LINE_BUF - 1);
        if (len > 0 && line[len-1] == '\r') len--;
        line[len] = '\0';
        if (len == 0) continue;

        HistoryEntry temp;
        if (_lineToEntry(line, temp)) {
            if (_cacheCount < HISTORY_DISPLAY) {
                _cache[_cacheCount++] = temp;
            } else {
                for (int i = 0; i < HISTORY_DISPLAY - 1; i++) {
                    _cache[i] = _cache[i+1];
                }
                _cache[HISTORY_DISPLAY - 1] = temp;
            }
        }
    }
    f.close();
}

void History::_rotateAndAppend(const char* newLine) {
    // Para otimizar leituras/escritas e não estar sempre a rodar a cada rega,
    // descartamos os 10% mais antigos de uma vez.
    uint16_t discard = HISTORY_MAX_ENTRIES / 10;
    if (discard == 0) discard = 1;
    uint16_t keep = HISTORY_MAX_ENTRIES - discard;

    File src = LittleFS.open(HISTORY_FILE, "r");
    if (!src) {
        // Se falhar, tentamos apenas adicionar
        File f = LittleFS.open(HISTORY_FILE, "a");
        if (f) { f.println(newLine); f.close(); }
        return;
    }

    File dst = LittleFS.open("/hist_tmp.csv", "w");
    if (!dst) {
        src.close();
        return;
    }

    uint16_t n = 0;
    uint16_t skipped = 0;

    // Copiar linha a linha, evitando carregar o ficheiro todo na RAM
    while (src.available()) {
        String s = src.readStringUntil('\n');
        s.trim();
        if (s.length() == 0) continue;

        if (skipped < discard) {
            skipped++;
            continue;
        }

        dst.println(s);
        n++;
        // Limite de segurança para não ultrapassar a quota planeada
        if (n >= keep) break;
    }
    src.close();

    // Adicionar a nova entrada no final
    dst.println(newLine);
    dst.close();

    // Substituir o original pelo novo ficheiro processado.
    // LittleFS.rename() suporta overwrite - não é necessário remove() prévio.
    // Evita janela de perda de dados se o rename falhar.
    if (!LittleFS.rename("/hist_tmp.csv", HISTORY_FILE)) {
        LOG_E("HIST", "rename falhou - manter original");
        LittleFS.remove("/hist_tmp.csv");
        return;
    }

    _lineCount = n + 1; // O contador volta para trás, tendo espaço para crescer novamente
}
