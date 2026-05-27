#include "History.h"
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "log.h"
#include "Storage.h"

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
        LittleFS.remove("/hist_tmp.csv"); // Clean up orphaned temp file from a crash
        // Tenta carregar da NVS primeiro
        if (storage.loadHistoryCache(_cache, sizeof(_cache), _lineCount)) {
            // Cache e total recuperados instantaneamente da NVS!
            // Compactar cache para eliminar possíveis lacunas/gaps resultantes de corrupção
            uint8_t validCount = 0;
            HistoryEntry tempCache[HISTORY_DISPLAY] = {};
            for (int i = 0; i < HISTORY_DISPLAY; i++) {
                if (_cache[i].year > 0) {
                    tempCache[validCount++] = _cache[i];
                }
            }
            memcpy(_cache, tempCache, sizeof(_cache));
            _cacheCount = validCount;
            LOG_I("HIST", "Carregado via NVS - %d entradas", _lineCount);
        } else {
            // Fallback (apenas se NVS estiver vazio ou corrompido)
            _lineCount = _countLines();
            _populateCache();
            // Salva na NVS para o próximo boot
            storage.saveHistoryCache(_cache, sizeof(_cache), _lineCount);
            LOG_I("HIST", "Carregado via LittleFS (Fallback) - %d entradas", _lineCount);
        }
    }
    return _ready;
}

// ─────────────────────────────────────────────────────────
void History::record(const HistoryEntry& entry) {
    if (!_ready)                              return;
    if (entry.trigger == WaterTrigger::TEST)  return;
    if (!gState.rtc_valid)                    return;

    if (_rotState != RotState::IDLE) {
        LOG_W("HIST", "Rotacao pendente, forçar conclusao sincrona...");
        uint32_t bailMs = millis();
        while (_rotState != RotState::IDLE) {
            esp_task_wdt_reset();
            update();
            if (millis() - bailMs > 5000) {
                LOG_E("HIST", "Rotation stuck, aborting");
                if (_rotSrc) _rotSrc.close();
                if (_rotDst) _rotDst.close();
                LittleFS.remove("/hist_tmp.csv");
                _rotState = RotState::IDLE;
                break;
            }
        }
    }

    char line[LINE_BUF];
    _entryToLine(entry, line, sizeof(line));

    if (_lineCount >= HISTORY_MAX_ENTRIES) {
        // Caminho assíncrono: guardar entrada pendente, NVS será salvo no FINISHING
        _rotateAndAppend(line);
        _pendingCacheSave = true;
        // Atualizar cache em RAM já (para UI imediata), mas NVS só no FINISHING
        if (_cacheCount < HISTORY_DISPLAY) {
            _cache[_cacheCount++] = entry;
        } else {
            for (int i = 0; i < HISTORY_DISPLAY - 1; i++) _cache[i] = _cache[i + 1];
            _cache[HISTORY_DISPLAY - 1] = entry;
        }
        LOG_I("HIST", "Rotacao iniciada, NVS diferido");
        return;   // <-- NÃO salvar NVS aqui
    }

    // Caminho síncrono: salvar ficheiro + NVS imediatamente (comportamento atual)
    File f = LittleFS.open(HISTORY_FILE, "a");
    if (!f) { LOG_E("HIST", "Falha ao abrir ficheiro"); return; }
    f.println(line);
    f.close();
    _lineCount++;

    if (_cacheCount < HISTORY_DISPLAY) {
        _cache[_cacheCount++] = entry;
    } else {
        for (int i = 0; i < HISTORY_DISPLAY - 1; i++) _cache[i] = _cache[i + 1];
        _cache[HISTORY_DISPLAY - 1] = entry;
    }

    storage.saveHistoryCache(_cache, sizeof(_cache), _lineCount);
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
    
    if (_rotState != RotState::IDLE) {
        if (_rotSrc) _rotSrc.close();
        if (_rotDst) _rotDst.close();
        LittleFS.remove("/hist_tmp.csv");
        _rotState = RotState::IDLE;
    }
    
    LittleFS.remove(HISTORY_FILE);
    _lineCount = 0;
    _cacheCount = 0;
    memset(_cache, 0, sizeof(_cache));
    storage.saveHistoryCache(_cache, sizeof(_cache), _lineCount);
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
        strlcat(buf, tmp, len);
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
        if (*p) {
            out.zone_dur[i] = (uint8_t)atoi(p);
            while (*p && *p != ',') p++;
            if (*p == ',') p++;
        } else {
            out.zone_dur[i] = 0;
        }
    }
    return true;
}

char* History::_trimLineBuffer(char* buf, size_t& pos) {
    buf[pos] = '\0';
    // Trim right
    while (pos > 0 && (buf[pos - 1] == '\r' || buf[pos - 1] == ' ' || buf[pos - 1] == '\n')) {
        buf[--pos] = '\0';
    }
    // Trim left
    char* p = buf;
    while (*p == ' ') p++;
    
    pos = 0; // Reset pos for next line
    return p;
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

    char chunk[256];
    char line[LINE_BUF];
    size_t linePos = 0;

    while (f.available()) {
        int bytesRead = f.read((uint8_t*)chunk, sizeof(chunk));
        if (bytesRead <= 0) break;

        for (int i = 0; i < bytesRead; i++) {
            char c = chunk[i];
            if (c == '\n') {
                char* p = _trimLineBuffer(line, linePos);
                if (strlen(p) > 0) {
                    HistoryEntry temp;
                    if (_lineToEntry(p, temp)) {
                        if (_cacheCount < HISTORY_DISPLAY) {
                            _cache[_cacheCount++] = temp;
                        } else {
                            for (int k = 0; k < HISTORY_DISPLAY - 1; k++) {
                                _cache[k] = _cache[k+1];
                            }
                            _cache[HISTORY_DISPLAY - 1] = temp;
                        }
                    }
                }
            } else if (c != '\r' && linePos < sizeof(line) - 1) {
                line[linePos++] = c;
            }
        }
    }

    // Process final line if file does not end with '\n'
    if (linePos > 0) {
        char* p = _trimLineBuffer(line, linePos);
        if (strlen(p) > 0) {
            HistoryEntry temp;
            if (_lineToEntry(p, temp)) {
                if (_cacheCount < HISTORY_DISPLAY) {
                    _cache[_cacheCount++] = temp;
                } else {
                    for (int k = 0; k < HISTORY_DISPLAY - 1; k++) {
                        _cache[k] = _cache[k+1];
                    }
                    _cache[HISTORY_DISPLAY - 1] = temp;
                }
            }
        }
    }

    f.close();
}

void History::_rotateAndAppend(const char* newLine) {
    if (_rotState != RotState::IDLE) {
        LOG_W("HIST", "Rotacao forçada sincrona!");
        uint32_t bailMs = millis();
        while (_rotState != RotState::IDLE) {
            esp_task_wdt_reset();
            update();
            if (millis() - bailMs > 5000) {
                LOG_E("HIST", "Rotation stuck, aborting");
                if (_rotSrc) _rotSrc.close();
                if (_rotDst) _rotDst.close();
                LittleFS.remove("/hist_tmp.csv");
                _rotState = RotState::IDLE;
                break;
            }
        }
    }

    _rotDiscard = HISTORY_MAX_ENTRIES / 10;
    if (_rotDiscard == 0) _rotDiscard = 1;
    _rotKeep = HISTORY_MAX_ENTRIES - _rotDiscard;

    _rotSrc = LittleFS.open(HISTORY_FILE, "r");
    if (!_rotSrc) {
        // Se falhar, tentamos apenas adicionar
        File f = LittleFS.open(HISTORY_FILE, "a");
        if (f) { f.println(newLine); f.close(); }
        return;
    }

    _rotDst = LittleFS.open("/hist_tmp.csv", "w");
    if (!_rotDst) {
        _rotSrc.close();
        return;
    }

    strlcpy(_rotPendingLine, newLine, sizeof(_rotPendingLine));
    _rotSkipped = 0;
    _rotCopied = 0;
    _rotLinePos = 0;
    _rotState = RotState::COPYING;
}

void History::update() {
    if (_rotState == RotState::IDLE) return;

    if (_rotState == RotState::COPYING) {
        char chunk[256];
        int bytesRead = _rotSrc.read((uint8_t*)chunk, sizeof(chunk));
        
        if (bytesRead > 0) {
            for (int i = 0; i < bytesRead; i++) {
                char c = chunk[i];
                if (c == '\n') {
                    char* p = _trimLineBuffer(_rotLineBuf, _rotLinePos);
                    
                    if (strlen(p) > 0) {
                        HistoryEntry temp;
                        if (_lineToEntry(p, temp)) {
                            if (_rotSkipped < _rotDiscard) {
                                _rotSkipped++;
                            } else {
                                _rotDst.println(p);
                                _rotCopied++;
                                if (_rotCopied >= _rotKeep) {
                                    _rotState = RotState::FINISHING;
                                    return; // yield
                                }
                            }
                        } else {
                            LOG_W("HIST", "Linha corrompida expurgada do disco.");
                        }
                    }
                } else if (c != '\r' && _rotLinePos < sizeof(_rotLineBuf) - 1) {
                    _rotLineBuf[_rotLinePos++] = c;
                }
            }
        } else {
            // EOF
            if (_rotLinePos > 0) {
                char* p = _trimLineBuffer(_rotLineBuf, _rotLinePos);
                if (strlen(p) > 0) {
                    HistoryEntry temp;
                    if (_lineToEntry(p, temp)) {
                        if (_rotSkipped < _rotDiscard) {
                            _rotSkipped++;
                        } else {
                            _rotDst.println(p);
                            _rotCopied++;
                        }
                    } else {
                        LOG_W("HIST", "Linha corrompida expurgada do disco.");
                    }
                }
            }
            _rotState = RotState::FINISHING;
        }
        return; // yield back to loop
    }

    if (_rotState == RotState::FINISHING) {
        _rotSrc.close();
        _rotDst.println(_rotPendingLine);
        _rotDst.close();

        if (LittleFS.rename("/hist_tmp.csv", HISTORY_FILE)) {
            _lineCount = _rotCopied + 1;

            // Salvar NVS agora que _lineCount é correto E cache já foi atualizado
            if (_pendingCacheSave) {
                _pendingCacheSave = false;
                storage.saveHistoryCache(_cache, sizeof(_cache), _lineCount);
                LOG_I("HIST", "Rotacao concluida + NVS atualizado. Entradas: %d", _lineCount);
            } else {
                storage.saveHistoryCache(_cache, sizeof(_cache), _lineCount);
                LOG_I("HIST", "Rotacao concluida. Entradas: %d", _lineCount);
            }
        } else {
            LOG_E("HIST", "rename falhou - manter original");
            LittleFS.remove("/hist_tmp.csv");
            _pendingCacheSave = false;
        }
        _rotState = RotState::IDLE;
    }
}
