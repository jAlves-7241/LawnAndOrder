#include "i18n.h"
#include "History.h"
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "log.h"
#include "Storage.h"

History history;

static const char* HIST_TMP_FILE     = "/hist_tmp.csv";
static const char* HIST_PENDING_FILE = "/hist_pend.txt";

static bool _isValidCachedEntry(const HistoryEntry& e) {
    if (e.year < 2020 || e.year > 2099) return false;
    if (e.month < 1 || e.month > 12 || e.day < 1 || e.day > 31) return false;
    if (e.hour > 23 || e.min > 59) return false;
    if (e.trigger != WaterTrigger::AUTO &&
        e.trigger != WaterTrigger::MANUAL &&
        e.trigger != WaterTrigger::CUSTOM) return false;
    return true;
}

#if LOG_LEVEL >= LVL_INFO
static void _formatEntryZones(const HistoryEntry& entry, char* out, size_t len) {
    if (!out || len == 0) return;
    out[0] = '\0';
    size_t pos = 0;
    bool first = true;
    for (int i = 0; i < NUM_ZONES; i++) {
        if (entry.zone_dur[i] > 0) {
            int n = snprintf(out + pos, len - pos, TXT_SER_ZONE_DUR_FMT,
                             first ? "" : ", ", i + 1, (unsigned)entry.zone_dur[i]);
            if (n > 0 && pos + (size_t)n < len) pos += (size_t)n;
            first = false;
        }
    }
    if (first) {
        snprintf(out, len, "0 min");
    }
}
#endif

// ─────────────────────────────────────────────────────────
bool History::begin(bool formatOnFail) {
    if (_rotSrc) _rotSrc.close();
    if (_rotDst) _rotDst.close();
    if (_exportFile) _exportFile.close();
    _rotState = RotState::IDLE;
    _exportState = ExportState::IDLE;
    _exportReadErr = false;
    _pendingCacheSave = false;
    _rotWriteErr = false;
    _rotLineOverflow = false;
    _rotCorruptLines = 0;
    _defHead = 0;
    _defTail = 0;
    _log_suspended = false;
    _log_suppressed_total = 0;
    _log_suppressed_warn_err = 0;
    _lastMountRetryMs = millis();

    _ready = LittleFS.begin(false);
    if (!_ready) {
        // Retry once before considering format
        _ready = LittleFS.begin(false);
    }
    bool formatAttempted = false;
    if (!_ready) {
        if (formatOnFail && !storage.hasHistoryData()) {
            formatAttempted = true;
            _ready = LittleFS.begin(true); // nothing to lose
            if (_ready) {
                LOG_W(TXT_LOG_TAG_HIST, TXT_LOG_HIST_FORMATTED);
            } else {
                LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_HIST_FORMAT_FAIL);
            }
        } else if (formatOnFail) {
            LOG_W(TXT_LOG_TAG_HIST, TXT_LOG_HIST_FS_PRESERVED);
        }
    }

    if (!_ready) {
        if (!formatAttempted) {
            LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_HIST_MOUNT_FAIL);
        }
        _lineCount = 0;
        _cacheCount = 0;
    } else {
        LittleFS.remove(HIST_TMP_FILE); // Clean up orphaned temp file from a crash

        // Replay any pending line persisted right before a mid-rotation power loss
        bool replayedPending = false;
        File pendFile = LittleFS.open(HIST_PENDING_FILE, "r");
        if (pendFile) {
            char lineBuf[LINE_BUF];
            size_t linePos = 0;
            bool overflow = false;
            int replayed = 0;
            bool replayWriteFailed = false;
            auto replayLine = [&](char* p) {
                if (strlen(p) == 0) return;
                HistoryEntry tmp = {};
                if (_lineToEntry(p, tmp)) {
                    File dst = LittleFS.open(HISTORY_FILE, "a");
                    if (dst) {
                        if (dst.println(p) > 0) {
                            replayed++;
                        } else {
                            replayWriteFailed = true;
                        }
                        dst.close();
                    } else {
                        replayWriteFailed = true;
                    }
                }
            };
            while (pendFile.available()) {
                int cInt = pendFile.read();
                if (cInt < 0) break;
                char c = (char)cInt;
                if (c == '\n') {
                    if (!overflow) {
                        char* p = _trimLineBuffer(lineBuf, linePos);
                        replayLine(p);
                    }
                    linePos = 0;
                    overflow = false;
                } else if (c != '\r') {
                    if (linePos < sizeof(lineBuf) - 1) {
                        lineBuf[linePos++] = c;
                    } else {
                        overflow = true;
                    }
                }
            }
            if (!overflow && linePos > 0) {
                char* p = _trimLineBuffer(lineBuf, linePos);
                replayLine(p);
            }
            pendFile.close();
            if (replayWriteFailed) {
                LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_HIST_FILE_ERR);
            } else {
                LittleFS.remove(HIST_PENDING_FILE);
            }
            if (replayed > 0) {
                replayedPending = true;
                LOG_I(TXT_LOG_TAG_HIST, TXT_LOG_HIST_PEND_REPLAY, replayed);
            }
        }

        // Tenta carregar da NVS primeiro (a menos que tenhamos recuperado pendentes)
        uint32_t expectedFileSize = 0;
        bool nvsOk = !replayedPending &&
                     storage.loadHistoryCache(_cache, sizeof(_cache), _lineCount, &expectedFileSize);
        bool fsConsistent = false;
        File f = LittleFS.open(HISTORY_FILE, "r");
        if (f) {
            uint32_t fsize = (uint32_t)f.size();
            f.close();
            if (_lineCount == 0) {
                fsConsistent = (fsize == 0);
            } else if (expectedFileSize > 0) {
                fsConsistent = (fsize == expectedFileSize);
            } else {
                fsConsistent = (fsize > 0);
            }
        } else if (_lineCount == 0 && expectedFileSize == 0) {
            fsConsistent = true;
        }

        bool cacheValid = false;
        uint8_t validCount = 0;
        HistoryEntry tempCache[HISTORY_DISPLAY] = {};
        if (nvsOk && fsConsistent) {
            cacheValid = true;
            for (int i = 0; i < HISTORY_DISPLAY; i++) {
                if (_cache[i].year == 0) {
                    if (_cache[i].month != 0 || _cache[i].day != 0) {
                        cacheValid = false;
                        break;
                    }
                } else if (_isValidCachedEntry(_cache[i])) {
                    tempCache[validCount++] = _cache[i];
                } else {
                    cacheValid = false;
                    break;
                }
            }
            uint8_t maxExpected = (_lineCount < HISTORY_DISPLAY) ? (uint8_t)_lineCount : HISTORY_DISPLAY;
            if ((_lineCount == 0 && validCount != 0) ||
                (_lineCount > 0 && (validCount == 0 || validCount > maxExpected))) {
                cacheValid = false;
            }
        }
        if (cacheValid) {
            // Cache e total recuperados instantaneamente da NVS!
            for (int i = 0; i < HISTORY_DISPLAY; i++) _cache[i] = tempCache[i];
            _cacheCount = validCount;
            LOG_D(TXT_LOG_TAG_HIST, TXT_LOG_HIST_NVS_LOAD, (int)_lineCount);
        } else {
            // Fallback (apenas se NVS estiver vazio ou desatualizado/corrompido)
            _lineCount = _countLines();
            _populateCache();
            // Salva na NVS para o proximo boot
            storage.saveHistoryCache(_cache, sizeof(_cache), _lineCount, _currentFileSize());
            LOG_I(TXT_LOG_TAG_HIST, TXT_LOG_HIST_FS_LOAD, (int)_lineCount,
                  replayedPending ? TXT_LOG_HIST_FS_REASON_REPLAY : TXT_LOG_HIST_FS_REASON_REBUILD);
        }
    }
    return _ready;
}

// ─────────────────────────────────────────────────────────
void History::record(const HistoryEntry& entry) {
    if (entry.trigger == WaterTrigger::TEST)  return;
    if (!_ready) {
        LOG_W(TXT_LOG_TAG_HIST, TXT_LOG_HIST_NOT_READY);
        return;
    }
    if (!gState.rtc_valid) {
        LOG_W(TXT_LOG_TAG_HIST, TXT_LOG_HIST_SKIP_NO_RTC);
        return;
    }

    if (_rotState != RotState::IDLE || _exportState != ExportState::IDLE) {
        if (_rotState != RotState::IDLE) {
            LOG_W(TXT_LOG_TAG_HIST, TXT_LOG_HIST_DISK_BUSY);
        } else {
            LOG_D(TXT_LOG_TAG_HIST, TXT_LOG_HIST_DISK_BUSY);
        }
        uint8_t nextHead = (_defHead + 1) % 4;
        if (nextHead != _defTail) {
            _deferredQueue[_defHead] = entry;
            _defHead = nextHead;
        } else {
            LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_HIST_Q_FULL);
        }
        return;
    }

    char line[LINE_BUF];
    _entryToLine(entry, line, sizeof(line));

#if LOG_LEVEL >= LVL_INFO
    char zSummary[80];
    _formatEntryZones(entry, zSummary, sizeof(zSummary));
#endif

    if (_lineCount >= HISTORY_MAX_ENTRIES) {
        uint16_t prevCount = _lineCount;
        // Caminho assincrono: guardar entrada pendente, NVS sera salvo no FINISHING
        _rotateAndAppend(line);
        if (_rotState != RotState::IDLE || _lineCount > prevCount) {
            // Atualizar cache em RAM ja (para UI imediata), mas NVS so no FINISHING
            if (_cacheCount < HISTORY_DISPLAY) {
                _cache[_cacheCount++] = entry;
            } else {
                for (int i = 0; i < HISTORY_DISPLAY - 1; i++) _cache[i] = _cache[i + 1];
                _cache[HISTORY_DISPLAY - 1] = entry;
            }
            LOG_I(TXT_LOG_TAG_HIST, TXT_LOG_HIST_LOGGED,
                  serialTriggerName(entry.trigger),
                  (unsigned)entry.year, (unsigned)entry.month, (unsigned)entry.day,
                  (unsigned)entry.hour, (unsigned)entry.min, zSummary);
            if (_rotState != RotState::IDLE) {
                _pendingCacheSave = true;
                LOG_I(TXT_LOG_TAG_HIST, TXT_LOG_HIST_ROT_START, (unsigned)prevCount);
            } else {
                storage.saveHistoryCache(_cache, sizeof(_cache), _lineCount, _currentFileSize());
            }
        }
        return;   // <-- NAO salvar NVS aqui
    }

    // Caminho sincrono: salvar ficheiro + NVS imediatamente
    File f = LittleFS.open(HISTORY_FILE, "a");
    if (!f) { LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_HIST_FILE_ERR); return; }
    size_t written = f.println(line);
    uint32_t fsize = (uint32_t)f.size();
    f.close();
    if (written == 0) { LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_HIST_FILE_ERR); return; }
    _lineCount++;

    if (_cacheCount < HISTORY_DISPLAY) {
        _cache[_cacheCount++] = entry;
    } else {
        for (int i = 0; i < HISTORY_DISPLAY - 1; i++) _cache[i] = _cache[i + 1];
        _cache[HISTORY_DISPLAY - 1] = entry;
    }

    storage.saveHistoryCache(_cache, sizeof(_cache), _lineCount, fsize);
    LOG_I(TXT_LOG_TAG_HIST, TXT_LOG_HIST_LOGGED,
          serialTriggerName(entry.trigger),
          (unsigned)entry.year, (unsigned)entry.month, (unsigned)entry.day,
          (unsigned)entry.hour, (unsigned)entry.min, zSummary);
}

// ─────────────────────────────────────────────────────────
uint8_t History::readLast(uint8_t count, HistoryEntry out[]) const {
    if (!_ready || !out || count == 0 || _cacheCount == 0) return 0;
    
    // Apenas copiamos da RAM (zero acessos ao disco!)
    uint8_t n = (_cacheCount < count) ? _cacheCount : count;
    for (uint8_t i = 0; i < n; i++) {
        // Copiamos os N mais recentes
        out[i] = _cache[_cacheCount - n + i];
    }
    return n;
}

bool History::clear(bool forceFormat) {
    if (!_ready) {
        if (!forceFormat) {
            LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_HIST_NOT_READY);
            return false;
        }
        _ready = LittleFS.begin(true);
        if (!_ready) {
            LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_HIST_FORMAT_FAIL);
            return false;
        }
        LOG_W(TXT_LOG_TAG_HIST, TXT_LOG_HIST_FORMATTED);
    }
    
    if (_rotState != RotState::IDLE) {
        if (_rotSrc) _rotSrc.close();
        if (_rotDst) _rotDst.close();
        LittleFS.remove(HIST_TMP_FILE);
        _rotState = RotState::IDLE;
    }
    
    if (_exportState != ExportState::IDLE) {
        if (_exportFile) _exportFile.close();
        _exportState = ExportState::IDLE;
        _exportReadErr = false;
        _log_suspended = false;
        _log_suppressed_total = 0;
        _log_suppressed_warn_err = 0;
    }
    
    if (LittleFS.exists(HIST_PENDING_FILE)) {
        LittleFS.remove(HIST_PENDING_FILE);
    }
    if (LittleFS.exists(HISTORY_FILE) && !LittleFS.remove(HISTORY_FILE)) {
        LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_HIST_FILE_ERR);
        return false;
    }
    _lineCount = 0;
    _cacheCount = 0;
    _pendingCacheSave = false;
    _rotCorruptLines = 0;
    _defHead = 0;
    _defTail = 0;
    for (int i = 0; i < HISTORY_DISPLAY; i++) _cache[i] = HistoryEntry{};
    storage.saveHistoryCache(_cache, sizeof(_cache), _lineCount, 0);
    LOG_I(TXT_LOG_TAG_HIST, TXT_LOG_HIST_WIPED);
    return true;
}

bool History::isReady() const { return _ready; }

uint16_t History::entryCount() const { return _lineCount; }

// ─────────────────────────────────────────────────────────
// Private
// ─────────────────────────────────────────────────────────

uint32_t History::_currentFileSize() const {
    if (!_ready) return 0;
    File f = LittleFS.open(HISTORY_FILE, "r");
    if (!f) return 0;
    uint32_t fsize = (uint32_t)f.size();
    f.close();
    return fsize;
}

// CSV: "YYYY-MM-DDTHH:MM,<trigger_char>,d0,d1,d2,d3"
void History::_entryToLine(const HistoryEntry& e, char* buf, size_t len) {
    char tc = (char)e.trigger;   // 'A', 'M', 'C' - already encoded in enum value
    int pos = snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d,%c",
                       e.year, e.month, e.day, e.hour, e.min, tc);
    if (pos < 0 || (size_t)pos >= len) return;
    for (int i = 0; i < NUM_ZONES; i++) {
        int n = snprintf(buf + pos, len - pos, ",%d", e.zone_dur[i]);
        if (n > 0 && (size_t)n < len - pos) pos += n;
        else break;
    }
}

bool History::_lineToEntry(const char* line, HistoryEntry& out) {
    if (!line) return false;
    int yr, mo, dy, hh, mm;
    char tc = 0;

    // "YYYY-MM-DDTHH:MM,C,d0,..."
    if (sscanf(line, "%4d-%2d-%2dT%2d:%2d,%c",
               &yr, &mo, &dy, &hh, &mm, &tc) != 6) return false;

    // Rejeitar valores cronológicos impossíveis
    if (yr < 2020 || yr > 2099 || mo < 1 || mo > 12 ||
        dy < 1 || dy > 31 || hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;

    out.year  = (uint16_t)yr;
    out.month = (uint8_t)mo;
    out.day   = (uint8_t)dy;
    out.hour  = (uint8_t)hh;
    out.min   = (uint8_t)mm;

    // Validate trigger char (match _isValidCachedEntry: reject 'T' or invalid triggers)
    if (tc == 'A' || tc == 'M' || tc == 'C')
        out.trigger = (WaterTrigger)tc;
    else
        return false;

    // Parse zone durations - after the second comma
    const char* p = line;
    int commas = 0;
    while (*p && commas < 2) { if (*p++ == ',') commas++; }
    for (int i = 0; i < NUM_ZONES; i++) {
        if (*p) {
            char* endp = nullptr;
            long val = strtol(p, &endp, 10);
            if (endp == p || val < 0) val = 0;
            if (val > 255) val = 255;
            out.zone_dur[i] = (uint8_t)val;
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
    uint32_t n = 0;
    bool hasNonEmptyTail = false;
    uint8_t buf[256];
    while (f.available()) {
        esp_task_wdt_reset();
        int len = f.read(buf, sizeof(buf));
        if (len <= 0) break;
        for (int i = 0; i < len; i++) {
            if (buf[i] == '\n') {
                if (n < 0xFFFFUL) n++;
                hasNonEmptyTail = false;
            } else if (buf[i] != '\r' && buf[i] != ' ') {
                hasNonEmptyTail = true;
            }
        }
    }
    if (hasNonEmptyTail && n < 0xFFFFUL) n++;
    f.close();
    return (uint16_t)n;
}

void History::_populateCache() {
    _cacheCount = 0;
    if (!_ready || _lineCount == 0) return;

    File f = LittleFS.open(HISTORY_FILE, "r");
    if (!f) return;

    // Otimização O(1): se o ficheiro for longo, saltar para o fim
    uint32_t fsize = f.size();
    uint32_t tailSize = HISTORY_DISPLAY * LINE_BUF + 128;
    if (fsize > tailSize) {
        f.seek(fsize - tailSize, SeekSet);
        // Descartar a primeira linha que possivelmente está cortada a meio
        while (f.available()) {
            if (f.read() == '\n') break;
        }
    }

    char chunk[256];
    char line[LINE_BUF];
    size_t linePos = 0;
    bool lineOverflow = false;

    while (f.available()) {
        int bytesRead = f.read((uint8_t*)chunk, sizeof(chunk));
        if (bytesRead <= 0) break;

        for (int i = 0; i < bytesRead; i++) {
            char c = chunk[i];
            if (c == '\n') {
                if (lineOverflow) {
                    linePos = 0;
                    lineOverflow = false;
                    continue;
                }
                char* p = _trimLineBuffer(line, linePos);
                if (strlen(p) > 0) {
                    HistoryEntry temp = {};
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
            } else if (c != '\r') {
                if (linePos < sizeof(line) - 1) {
                    line[linePos++] = c;
                } else {
                    lineOverflow = true;
                }
            }
        }
    }

    // Process final line if file does not end with '\n'
    if (linePos > 0 && !lineOverflow) {
        char* p = _trimLineBuffer(line, linePos);
        if (strlen(p) > 0) {
            HistoryEntry temp = {};
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
    // Bloqueio forçado síncrono removido (código morto) - _rotState é garantido ser IDLE aqui.

    uint16_t step = HISTORY_MAX_ENTRIES / 10;
    if (step == 0) step = 1;
    _rotKeep = (HISTORY_MAX_ENTRIES > step) ? (HISTORY_MAX_ENTRIES - step) : 1;
    // If prior fallbacks grew _lineCount past HISTORY_MAX_ENTRIES, discard all excess
    // oldest lines so newest entries are never truncated at the tail.
    if (_lineCount > _rotKeep) {
        _rotDiscard = _lineCount - _rotKeep;
    } else {
        _rotDiscard = step;
    }

    auto fallbackAppend = [&]() {
        File f = LittleFS.open(HISTORY_FILE, "a");
        bool wrote = false;
        if (f) {
            if (f.println(newLine) > 0) {
                _lineCount++;
                wrote = true;
            }
            f.close();
        }
        if (wrote) {
            LOG_W(TXT_LOG_TAG_HIST, TXT_LOG_HIST_ROT_OPEN_FAIL);
        } else {
            LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_HIST_FILE_ERR);
        }
    };

    _rotSrc = LittleFS.open(HISTORY_FILE, "r");
    if (!_rotSrc) {
        fallbackAppend();
        return;
    }

    _rotDst = LittleFS.open(HIST_TMP_FILE, "w");
    if (!_rotDst) {
        _rotSrc.close();
        fallbackAppend();
        return;
    }

    // Persist pending line before starting async copy so power loss cannot drop it
    File pendFile = LittleFS.open(HIST_PENDING_FILE, "w");
    if (pendFile) {
        pendFile.println(newLine);
        pendFile.close();
    }

    strlcpy(_rotPendingLine, newLine, sizeof(_rotPendingLine));
    _rotSkipped = 0;
    _rotCopied = 0;
    _rotCorruptLines = 0;
    _rotLinePos = 0;
    _rotWriteErr = false;
    _rotLineOverflow = false;
    _rotState = RotState::COPYING;
}

void History::update() {
    if (!_ready) {
        uint32_t nowMs = millis();
        if (nowMs - _lastMountRetryMs >= 86400000UL) {
            _lastMountRetryMs = nowMs;
            begin(false);
        }
        return;
    }
    if (_rotState == RotState::IDLE && _exportState == ExportState::IDLE) return;

    if (_rotState == RotState::COPYING) {
        char chunk[256];
        int bytesRead = _rotSrc.read((uint8_t*)chunk, sizeof(chunk));
        
        if (bytesRead > 0) {
            for (int i = 0; i < bytesRead; i++) {
                char c = chunk[i];
                if (c == '\n') {
                    if (_rotLineOverflow) {
                        _rotLinePos = 0;
                        _rotLineOverflow = false;
                        if (_rotCorruptLines < 0xFFFFU) _rotCorruptLines++;
                        continue;
                    }
                    char* p = _trimLineBuffer(_rotLineBuf, _rotLinePos);
                    
                    if (strlen(p) > 0) {
                        HistoryEntry temp = {};
                        if (_lineToEntry(p, temp)) {
                            if (_rotSkipped < _rotDiscard) {
                                _rotSkipped++;
                            } else {
                                if (_rotDst.println(p) == 0) {
                                    _rotWriteErr = true;
                                    _rotState = RotState::FINISHING;
                                    return;
                                }
                                _rotCopied++;
                                if (_rotCopied >= _rotKeep) {
                                    _rotState = RotState::FINISHING;
                                    return; // yield
                                }
                            }
                        } else {
                            if (_rotCorruptLines < 0xFFFFU) _rotCorruptLines++;
                        }
                    }
                } else if (c != '\r') {
                    if (_rotLinePos < sizeof(_rotLineBuf) - 1) {
                        _rotLineBuf[_rotLinePos++] = c;
                    } else {
                        _rotLineOverflow = true;
                    }
                }
            }
        } else {
            if (bytesRead < 0 || _rotSrc.available() > 0) {
                _rotWriteErr = true;
                _rotLinePos = 0;
                _rotLineOverflow = false;
            } else {
                // EOF
                if (_rotLineOverflow) {
                    _rotLinePos = 0;
                    _rotLineOverflow = false;
                    if (_rotCorruptLines < 0xFFFFU) _rotCorruptLines++;
                } else if (_rotLinePos > 0) {
                    char* p = _trimLineBuffer(_rotLineBuf, _rotLinePos);
                    if (strlen(p) > 0) {
                        HistoryEntry temp = {};
                        if (_lineToEntry(p, temp)) {
                            if (_rotSkipped < _rotDiscard) {
                                _rotSkipped++;
                            } else {
                                if (_rotDst.println(p) == 0) {
                                    _rotWriteErr = true;
                                } else {
                                    _rotCopied++;
                                }
                            }
                        } else {
                            if (_rotCorruptLines < 0xFFFFU) _rotCorruptLines++;
                        }
                    }
                }
            }
            _rotState = RotState::FINISHING;
        }
        return; // yield back to loop
    }

    if (_rotState == RotState::FINISHING) {
        _rotSrc.close();
        if (!_rotWriteErr && _rotDst.println(_rotPendingLine) == 0) {
            _rotWriteErr = true;
        }
        _rotDst.close();

        if (!_rotWriteErr && LittleFS.rename(HIST_TMP_FILE, HISTORY_FILE)) {
            LittleFS.remove(HIST_PENDING_FILE);
            _lineCount = _rotCopied + 1;

            // Salvar NVS agora que _lineCount e correto E cache ja foi atualizado
            uint32_t fsize = _currentFileSize();
            _pendingCacheSave = false;
            storage.saveHistoryCache(_cache, sizeof(_cache), _lineCount, fsize);
            if (_rotCorruptLines > 0) {
                LOG_W(TXT_LOG_TAG_HIST, TXT_LOG_HIST_ROT_CORRUPT_SUM,
                      (unsigned)_rotCorruptLines);
            }
            LOG_I(TXT_LOG_TAG_HIST, TXT_LOG_HIST_ROT_DONE, (int)_lineCount);
        } else {
            LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_HIST_RENAME_FAIL);
            LittleFS.remove(HIST_TMP_FILE);
            _pendingCacheSave = false;
            File f = LittleFS.open(HISTORY_FILE, "a");
            if (f) {
                if (f.println(_rotPendingLine) > 0) {
                    LittleFS.remove(HIST_PENDING_FILE);
                }
                f.close();
            }
            // Rebuild consistent state from the actual file
            _lineCount = _countLines();
            _populateCache();
            storage.saveHistoryCache(_cache, sizeof(_cache), _lineCount, _currentFileSize());
        }
        _rotState = RotState::IDLE;
        // Drenar apenas UMA entrada por ciclo para evitar re-entrancia
        // (record() pode desencadear nova rotacao se o ficheiro estiver cheio)
        if (_defHead != _defTail) {
            HistoryEntry e = _deferredQueue[_defTail];
            _defTail = (_defTail + 1) % 4;
            record(e);
        }
    }

    // ── Export via Serial (async) ──
    if (_exportState == ExportState::SENDING) {
        char chunk[256];
        int bytesRead = _exportFile.read((uint8_t*)chunk, sizeof(chunk));

        if (bytesRead > 0) {
            for (int i = 0; i < bytesRead; i++) {
                char c = chunk[i];
                if (c == '\n') {
                    _exportLineBuf[_exportLinePos] = '\0';
                    // Trim CR
                    while (_exportLinePos > 0 && _exportLineBuf[_exportLinePos - 1] == '\r') {
                        _exportLineBuf[--_exportLinePos] = '\0';
                    }
                    if (_exportLinePos > 0) {
                        Serial.println(_exportLineBuf);
                        _exportSent++;
                    }
                    _exportLinePos = 0;
                } else if (_exportLinePos < sizeof(_exportLineBuf) - 1) {
                    _exportLineBuf[_exportLinePos++] = c;
                }
            }
        } else {
            if (bytesRead < 0 || _exportFile.available() > 0) {
                _exportReadErr = true;
                _exportLinePos = 0; // discard incomplete partial line
            } else if (_exportLinePos > 0) {
                // EOF — process last line if no trailing newline
                _exportLineBuf[_exportLinePos] = '\0';
                while (_exportLinePos > 0 && _exportLineBuf[_exportLinePos - 1] == '\r') {
                    _exportLineBuf[--_exportLinePos] = '\0';
                }
                if (_exportLinePos > 0) {
                    Serial.println(_exportLineBuf);
                    _exportSent++;
                }
                _exportLinePos = 0;
            }
            _exportState = ExportState::FOOTER;
        }
        return; // yield
    }

    if (_exportState == ExportState::FOOTER) {
        _exportFile.close();
        Serial.printf(TXT_TERM_HIST_EXPORT_END, _exportSent);
        Serial.println();
        _log_suspended = false;
        if (_exportReadErr) {
            _exportReadErr = false;
            LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_HIST_FILE_ERR);
        }
        if (_log_suppressed_warn_err > 0) {
#if LOG_LEVEL >= LVL_WARN
            LOG_W(TXT_LOG_TAG_HIST, TXT_LOG_EXPORT_SUPPRESSED,
                  (unsigned)_log_suppressed_total,
                  (unsigned)_log_suppressed_warn_err);
#elif LOG_LEVEL == LVL_ERROR
            LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_EXPORT_SUPPRESSED,
                  (unsigned)_log_suppressed_total,
                  (unsigned)_log_suppressed_warn_err);
#endif
        } else if (_log_suppressed_total > 0) {
            LOG_I(TXT_LOG_TAG_HIST, TXT_LOG_EXPORT_SUPPRESSED_INFO,
                  (unsigned)_log_suppressed_total);
        }
        _log_suppressed_total = 0;
        _log_suppressed_warn_err = 0;
        _exportState = ExportState::IDLE;
        
        // Drenar apenas UMA entrada por ciclo (mesmo motivo da rotacao)
        if (_defHead != _defTail) {
            HistoryEntry e = _deferredQueue[_defTail];
            _defTail = (_defTail + 1) % 4;
            record(e);
        }
    }
}

// ─────────────────────────────────────────────────────────
// Serial Export
// ─────────────────────────────────────────────────────────

bool History::startExport(bool announce) {
    if (!_ready) {
        LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_HIST_NOT_READY);
        return false;
    }
    if (_lineCount == 0) return false;
    if (_exportState != ExportState::IDLE) return false;
    if (_rotState != RotState::IDLE) {
        LOG_W(TXT_LOG_TAG_HIST, TXT_LOG_NO_EXPORT_ROT);
        return false;
    }

    _exportFile = LittleFS.open(HISTORY_FILE, "r");
    if (!_exportFile) {
        LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_HIST_EXPORT_FAIL);
        return false;
    }

    _exportSent = 0;
    _exportTotal = _lineCount;
    _exportLinePos = 0;
    _exportReadErr = false;

    if (announce) {
        Serial.println(TXT_LOG_START_EXPORT);
    }

    // Suspender logs temporariamente para evitar corromper o output CSV no serial
    _log_suppressed_total = 0;
    _log_suppressed_warn_err = 0;
    _log_suspended = true;

    // Print CSV header
    Serial.println();
    Serial.println(TXT_TERM_HIST_HDR);
    Serial.println(TXT_TERM_HIST_CSV_HDR);

    _exportState = ExportState::SENDING;
    return true;
}

bool History::isExporting() const {
    return _exportState != ExportState::IDLE;
}

uint8_t History::exportProgress() const {
    if (_exportTotal == 0 || _exportSent >= _exportTotal) return 100;
    return (uint8_t)(((uint32_t)_exportSent * 100UL) / _exportTotal);
}

uint16_t History::exportSent() const {
    return _exportSent;
}
