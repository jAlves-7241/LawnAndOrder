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
static const uint8_t PEND_DEDUP_MAX  = 8;

// Pause TWDT around synchronous LittleFS full-partition erases, which can exceed WDT_TIMEOUT_S (8 s)
// on slow/aged SPI NOR flash without yielding to loop().
struct ScopedWdtPause {
    bool paused = false;
    ScopedWdtPause() {
        esp_task_wdt_reset();
        paused = (esp_task_wdt_delete(NULL) == 0);
    }
    ~ScopedWdtPause() {
        if (paused) {
            esp_task_wdt_add(NULL);
            esp_task_wdt_reset();
        }
    }
};

uint32_t History::_remountBackoffMs(uint8_t attempts) {
    if (attempts == 0) return 60000UL;    // 1 min
    if (attempts == 1) return 300000UL;   // 5 min
    if (attempts == 2) return 1800000UL;  // 30 min
    return 86400000UL;                    // 24 h cap
}

static bool _isValidCachedEntry(const HistoryEntry& e) {
    if (e.year < DATE_YEAR_MIN || e.year > DATE_YEAR_MAX) return false;
    uint8_t maxD = daysInMonth(e.year, e.month);
    if (maxD == 0 || e.day < 1 || e.day > maxD) return false;
    if (e.hour > 23 || e.min > 59) return false;
    if (e.trigger != WaterTrigger::AUTO &&
        e.trigger != WaterTrigger::MANUAL &&
        e.trigger != WaterTrigger::CUSTOM) return false;
    return true;
}

template <typename Fn>
static void _forEachTrimmedLine(File& f, Fn&& fn, bool* outReadErr = nullptr) {
    if (outReadErr) *outReadErr = false;
    char lineBuf[LINE_BUF];
    size_t linePos = 0;
    bool overflow = false;
    auto flushLine = [&]() {
        if (!overflow && linePos > 0) {
            lineBuf[linePos] = '\0';
            while (linePos > 0 && (lineBuf[linePos - 1] == '\r' ||
                                   lineBuf[linePos - 1] == ' ' ||
                                   lineBuf[linePos - 1] == '\n')) {
                lineBuf[--linePos] = '\0';
            }
            char* p = lineBuf;
            while (*p == ' ') p++;
            if (p[0] != '\0') {
                fn(p);
            }
        }
        linePos = 0;
        overflow = false;
    };
    while (f.available()) {
        esp_task_wdt_reset();
        int cInt = f.read();
        if (cInt < 0) {
            if (outReadErr) *outReadErr = true;
            return;
        }
        char c = (char)cInt;
        if (c == '\n') {
            flushLine();
        } else if (c != '\r') {
            if (linePos < sizeof(lineBuf) - 1) {
                lineBuf[linePos++] = c;
            } else {
                overflow = true;
            }
        }
    }
    flushLine();
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
    _exportLineOverflow = false;
    _rotWriteErr = false;
    _rotLineOverflow = false;
    _rotPendingInFile = false;
    _rotHadPriorPending = false;
    _rotCorruptLines = 0;
    _defHead = 0;
    _defTail = 0;
    _log_suspended = false;
    _log_suppressed_total = 0;
    _log_suppressed_warn_err = 0;
    _lastMountRetryMs = millis();
    if (formatOnFail) _mountRetryAttempts = 0;

    _ready = LittleFS.begin(false);
    if (!_ready) {
        esp_task_wdt_reset();
        // Retry once before considering format
        _ready = LittleFS.begin(false);
    }
    bool formatAttempted = false;
    if (!_ready) {
        if (formatOnFail && !storage.hasHistoryData()) {
            formatAttempted = true;
            {
                ScopedWdtPause wdtPause;
                _ready = LittleFS.begin(true); // nothing to lose
            }
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
        _mountRetryAttempts = 0;
        LittleFS.remove(HIST_TMP_FILE); // Clean up orphaned temp file from a crash

        // Replay any pending line(s) persisted right before a mid-rotation power loss
        bool replayedPending = false;
        bool pendingAlreadyPresent = false;
        File pendFile = LittleFS.open(HIST_PENDING_FILE, "r");
        if (pendFile) {
            char pendLines[PEND_DEDUP_MAX][64] = {};
            char lastValidPend[64] = {};
            uint8_t pendCount = 0;
            uint16_t totalValidPend = 0;
            bool scanReadErr = false;
            _forEachTrimmedLine(pendFile, [&](char* p) {
                HistoryEntry tmp = {};
                if (_lineToEntry(p, tmp)) {
                    char canon[64];
                    if (strlen(p) < sizeof(canon)) {
                        strlcpy(canon, p, sizeof(canon));
                    } else {
                        _entryToLine(tmp, canon, sizeof(canon));
                    }
                    if (pendCount < PEND_DEDUP_MAX) {
                        strlcpy(pendLines[pendCount++], canon, sizeof(pendLines[0]));
                    }
                    strlcpy(lastValidPend, canon, sizeof(lastValidPend));
                    if (totalValidPend < 0xFFFFU) totalValidPend++;
                }
            }, &scanReadErr);

            bool replayWriteFailed = scanReadErr;
            int replayed = 0;
            if (!scanReadErr && totalValidPend > 0) {
                char tailLines[PEND_DEDUP_MAX][64] = {};
                uint8_t tailCount = _readTailLines(tailLines, PEND_DEDUP_MAX);

                if (totalValidPend <= PEND_DEDUP_MAX) {
                    // Match longest prefix of pendLines[] already committed at the tail of HISTORY_FILE.
                    uint8_t overlap = 0;
                    uint8_t maxOv = (pendCount < tailCount) ? pendCount : tailCount;
                    for (uint8_t k = maxOv; k >= 1; k--) {
                        bool allMatch = true;
                        for (uint8_t i = 0; i < k; i++) {
                            if (strcmp(pendLines[i], tailLines[tailCount - k + i]) != 0) {
                                allMatch = false;
                                break;
                            }
                        }
                        if (allMatch) {
                            overlap = k;
                            break;
                        }
                    }
                    if (overlap > 0) {
                        pendingAlreadyPresent = true;
                    }
                    for (uint8_t i = overlap; i < pendCount; i++) {
                        if (_appendCsvLine(pendLines[i])) {
                            replayed++;
                        } else {
                            replayWriteFailed = true;
                            break;
                        }
                    }
                } else if (tailCount > 0 && strcmp(tailLines[tailCount - 1], lastValidPend) == 0) {
                    pendingAlreadyPresent = true;
                } else {
                    bool replayReadErr = false;
                    pendFile.seek(0, SeekSet);
                    _forEachTrimmedLine(pendFile, [&](char* p) {
                        if (replayWriteFailed) return;
                        HistoryEntry tmp = {};
                        if (_lineToEntry(p, tmp)) {
                            if (_appendCsvLine(p)) {
                                replayed++;
                            } else {
                                replayWriteFailed = true;
                            }
                        }
                    }, &replayReadErr);
                    if (replayReadErr) replayWriteFailed = true;
                }
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

        // Tenta carregar da NVS primeiro (a menos que tenhamos recuperado/deduplicado pendentes)
        uint32_t expectedFileSize = 0;
        bool nvsOk = !replayedPending && !pendingAlreadyPresent &&
                     storage.loadHistoryCache(_cache, sizeof(_cache), _lineCount, &expectedFileSize);
        bool fsConsistent = false;
        File f = LittleFS.open(HISTORY_FILE, "r");
        if (f) {
            uint32_t fsize = (uint32_t)f.size();
            f.close();
            if (_lineCount == 0) {
                fsConsistent = (fsize == expectedFileSize);
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
            _revision++;
            LOG_D(TXT_LOG_TAG_HIST, TXT_LOG_HIST_NVS_LOAD, (int)_lineCount);
        } else {
            // Fallback (apenas se NVS estiver vazio ou desatualizado/corrompido)
            _lineCount = _countLines();
            _populateCache();
            // If file holds only unparseable lines, settle to 0 so subsequent boots don't loop rebuilds
            if (_cacheCount == 0 && _lineCount > 0) {
                _lineCount = 0;
            }
            _revision++;
            // Salva na NVS para o proximo boot
            storage.saveHistoryCache(_cache, sizeof(_cache), _lineCount, _currentFileSize());
            LOG_I(TXT_LOG_TAG_HIST, TXT_LOG_HIST_FS_LOAD, (int)_lineCount,
                  replayedPending ? TXT_LOG_HIST_FS_REASON_REPLAY : TXT_LOG_HIST_FS_REASON_REBUILD);
        }
    }
    return _ready;
}

// ─────────────────────────────────────────────────────────
void History::record(const HistoryEntry& entry, bool requireLiveRtc) {
    if (entry.trigger == WaterTrigger::TEST)  return;
    if (!_ready) {
        LOG_W(TXT_LOG_TAG_HIST, TXT_LOG_HIST_NOT_READY);
        return;
    }
    if (requireLiveRtc && !gState.rtc_valid) {
        LOG_W(TXT_LOG_TAG_HIST, TXT_LOG_HIST_SKIP_NO_RTC);
        return;
    }
    if (!_isValidCachedEntry(entry)) {
        return;
    }

    if (_rotState != RotState::IDLE || _exportState != ExportState::IDLE || _defHead != _defTail) {
        if (_rotState != RotState::IDLE) {
            LOG_W(TXT_LOG_TAG_HIST, TXT_LOG_HIST_DISK_BUSY);
        } else {
            LOG_D(TXT_LOG_TAG_HIST, TXT_LOG_HIST_DISK_BUSY);
        }
        uint8_t nextHead = (_defHead + 1) % 4;
        if (nextHead != _defTail) {
            _deferredQueue[_defHead] = entry;
            _deferredRequireRtc[_defHead] = requireLiveRtc;
            _defHead = nextHead;
        } else {
            LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_HIST_Q_FULL);
        }
        return;
    }

    _recordNow(entry);
}

void History::_recordNow(const HistoryEntry& entry) {
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
            _pushCache(entry);
            LOG_I(TXT_LOG_TAG_HIST, TXT_LOG_HIST_LOGGED,
                  serialTriggerName(entry.trigger),
                  (unsigned)entry.year, (unsigned)entry.month, (unsigned)entry.day,
                  (unsigned)entry.hour, (unsigned)entry.min, zSummary);
            if (_rotState != RotState::IDLE) {
                LOG_I(TXT_LOG_TAG_HIST, TXT_LOG_HIST_ROT_START, (unsigned)prevCount);
            } else {
                storage.saveHistoryCache(_cache, sizeof(_cache), _lineCount, _currentFileSize());
            }
        }
        return;   // <-- NAO salvar NVS aqui
    }

    // Caminho sincrono: salvar ficheiro + flush + ler tamanho ja persistido + NVS
    uint32_t fsize = 0;
    if (!_appendCsvLine(line, &fsize)) {
        LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_HIST_FILE_ERR);
        return;
    }
    _lineCount++;

    _pushCache(entry);

    storage.saveHistoryCache(_cache, sizeof(_cache), _lineCount, fsize);
    LOG_I(TXT_LOG_TAG_HIST, TXT_LOG_HIST_LOGGED,
          serialTriggerName(entry.trigger),
          (unsigned)entry.year, (unsigned)entry.month, (unsigned)entry.day,
          (unsigned)entry.hour, (unsigned)entry.min, zSummary);
}

void History::_drainOneDeferred() {
    if (_rotState != RotState::IDLE || _exportState != ExportState::IDLE) return;
    if (_defHead == _defTail) return;

    HistoryEntry e = _deferredQueue[_defTail];
    bool requireLiveRtc = _deferredRequireRtc[_defTail];
    _defTail = (_defTail + 1) % 4;
    if (e.trigger == WaterTrigger::TEST) return;
    if (!_ready) {
        LOG_W(TXT_LOG_TAG_HIST, TXT_LOG_HIST_NOT_READY);
        return;
    }
    if (requireLiveRtc && !gState.rtc_valid) {
        LOG_W(TXT_LOG_TAG_HIST, TXT_LOG_HIST_SKIP_NO_RTC);
        return;
    }
    if (!_isValidCachedEntry(e)) return;
    _recordNow(e);
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
    // Close active rotation/export streams before unlinking or formatting FS
    const bool wasRotating = (_rotState != RotState::IDLE);
    if (wasRotating) {
        if (_rotSrc) _rotSrc.close();
        if (_rotDst) _rotDst.close();
        _rotState = RotState::IDLE;
        _rotPendingInFile = false;
        _rotHadPriorPending = false;
    }

    if (_exportState != ExportState::IDLE) {
        if (_exportFile) _exportFile.close();
        _exportState = ExportState::IDLE;
        _exportReadErr = false;
        _exportLineOverflow = false;
        _log_suspended = false;
        _log_suppressed_total = 0;
        _log_suppressed_warn_err = 0;
    }

    if (!_ready) {
        if (!forceFormat) {
            LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_HIST_NOT_READY);
            return false;
        }
        {
            ScopedWdtPause wdtPause;
            _ready = LittleFS.begin(true);
        }
        if (!_ready) {
            LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_HIST_FORMAT_FAIL);
            return false;
        }
        _mountRetryAttempts = 0;
        LOG_W(TXT_LOG_TAG_HIST, TXT_LOG_HIST_FORMATTED);
    } else {
        bool unlinkFailed = false;
        bool historyRemoved = true;
        if (LittleFS.exists(HIST_TMP_FILE) && !LittleFS.remove(HIST_TMP_FILE)) {
            unlinkFailed = true;
        }
        if (LittleFS.exists(HISTORY_FILE) && !LittleFS.remove(HISTORY_FILE)) {
            unlinkFailed = true;
            historyRemoved = false;
        }
        // Only unlink pending journal after HISTORY_FILE is confirmed gone so a failed
        // non-forced clear never destroys a mid-rotation pending record.
        if (historyRemoved && LittleFS.exists(HIST_PENDING_FILE) && !LittleFS.remove(HIST_PENDING_FILE)) {
            unlinkFailed = true;
        }
        if (unlinkFailed) {
            if (!forceFormat) {
                if (wasRotating) {
                    _lineCount = _countLines();
                    _populateCache();
                    if (_cacheCount == 0 && _lineCount > 0) {
                        _lineCount = 0;
                    }
                    _revision++;
                    storage.saveHistoryCache(_cache, sizeof(_cache), _lineCount, _currentFileSize());
                }
                LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_HIST_FILE_ERR);
                return false;
            }
            // Directory/file corruption on a mounted FS requires end()+format()
            LittleFS.end();
            {
                ScopedWdtPause wdtPause;
                _ready = LittleFS.format() && LittleFS.begin(false);
            }
            if (!_ready) {
                LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_HIST_FORMAT_FAIL);
                return false;
            }
            _mountRetryAttempts = 0;
            LOG_W(TXT_LOG_TAG_HIST, TXT_LOG_HIST_FORMATTED);
        }
    }

    _lineCount = 0;
    _cacheCount = 0;
    _rotCorruptLines = 0;
    _defHead = 0;
    _defTail = 0;
    _revision++;
    for (int i = 0; i < HISTORY_DISPLAY; i++) _cache[i] = HistoryEntry{};
    storage.saveHistoryCache(_cache, sizeof(_cache), _lineCount, 0);
    LOG_I(TXT_LOG_TAG_HIST, TXT_LOG_HIST_WIPED);
    return true;
}

bool History::isReady() const { return _ready; }

bool History::hasFileData() const {
    return _ready && (_currentFileSize() > 0);
}

uint16_t History::entryCount() const { return _lineCount; }

// ─────────────────────────────────────────────────────────
// Private
// ─────────────────────────────────────────────────────────

bool History::_writeCsvLine(File& f, const char* line) {
    if (!f || !line) return false;
    const size_t expected = strlen(line) + 2U; // line + "\r\n" from Print::println
    return f.println(line) == expected;
}

bool History::_appendCsvLine(const char* line, uint32_t* outNewSize) const {
    if (!_ready || !line) return false;
    const uint32_t prevSize = _currentFileSize();
    bool needLeadingNewline = false;
    if (prevSize > 0) {
        File rf = LittleFS.open(HISTORY_FILE, "r");
        if (rf) {
            if (rf.seek(prevSize - 1, SeekSet)) {
                int lastByte = rf.read();
                if (lastByte >= 0 && lastByte != '\n') {
                    needLeadingNewline = true;
                }
            }
            rf.close();
        }
    }

    File f = LittleFS.open(HISTORY_FILE, "a");
    if (!f) return false;

    size_t expectedBytes = strlen(line) + 2U;
    bool writeOk = true;
    if (needLeadingNewline) {
        if (f.println("") != 2U) {
            writeOk = false;
        } else {
            expectedBytes += 2U;
        }
    }
    if (writeOk) {
        writeOk = _writeCsvLine(f, line);
    }
    if (writeOk) f.flush();
    f.close();

    const uint32_t newSize = _currentFileSize();
    if (outNewSize) *outNewSize = newSize;
    return writeOk && (newSize >= prevSize + expectedBytes);
}

bool History::_appendPendingLine(const char* line) const {
    if (!_ready || !line) return false;
    const uint32_t priorPendSize = _fileSize(HIST_PENDING_FILE);
    bool needPendNewline = false;
    if (priorPendSize > 0) {
        File rf = LittleFS.open(HIST_PENDING_FILE, "r");
        if (rf) {
            if (rf.seek(priorPendSize - 1, SeekSet)) {
                int lastByte = rf.read();
                if (lastByte >= 0 && lastByte != '\n') {
                    needPendNewline = true;
                }
            }
            rf.close();
        }
    }
    File pendFile = LittleFS.open(HIST_PENDING_FILE, "a");
    if (!pendFile) return false;
    bool ok = true;
    if (needPendNewline && pendFile.println("") != 2U) {
        ok = false;
    }
    if (ok) {
        ok = _writeCsvLine(pendFile, line);
    }
    if (ok) pendFile.flush();
    pendFile.close();
    return ok;
}

uint8_t History::_readTailLines(char out[][64], uint8_t maxLines) const {
    if (!_ready || !out || maxLines == 0) return 0;
    File f = LittleFS.open(HISTORY_FILE, "r");
    if (!f) return 0;

    const uint32_t fsize = (uint32_t)f.size();
    if (fsize == 0) {
        f.close();
        return 0;
    }

    const uint32_t tailWindow = (uint32_t)maxLines * LINE_BUF + 128U;
    if (fsize > tailWindow) {
        f.seek(fsize - tailWindow, SeekSet);
        while (f.available()) {
            int c = f.read();
            if (c < 0 || c == '\n') break;
        }
    }

    uint8_t count = 0;
    auto pushLine = [&](const char* line) {
        if (count < maxLines) {
            strlcpy(out[count++], line, 64);
        } else {
            for (uint8_t i = 0; i + 1 < maxLines; i++) {
                memcpy(out[i], out[i + 1], 64);
            }
            strlcpy(out[maxLines - 1], line, 64);
        }
    };

    char curLine[LINE_BUF] = {};
    size_t curPos = 0;
    bool overflow = false;
    char chunk[128];

    auto processLine = [&]() {
        if (!overflow && curPos > 0) {
            char* p = _trimLineBuffer(curLine, curPos);
            HistoryEntry tmp = {};
            if (p[0] != '\0' && _lineToEntry(p, tmp)) {
                char canon[64];
                if (strlen(p) < sizeof(canon)) {
                    strlcpy(canon, p, sizeof(canon));
                } else {
                    _entryToLine(tmp, canon, sizeof(canon));
                }
                pushLine(canon);
            }
        }
        curPos = 0;
        overflow = false;
    };

    while (f.available()) {
        esp_task_wdt_reset();
        int bytesRead = f.read((uint8_t*)chunk, sizeof(chunk));
        if (bytesRead <= 0) break;
        for (int i = 0; i < bytesRead; i++) {
            char c = chunk[i];
            if (c == '\n') {
                processLine();
            } else if (c != '\r') {
                if (curPos < sizeof(curLine) - 1) {
                    curLine[curPos++] = c;
                } else {
                    overflow = true;
                }
            }
        }
    }
    processLine();
    f.close();
    return count;
}

uint32_t History::_fileSize(const char* path) const {
    if (!_ready || !path) return 0;
    File f = LittleFS.open(path, "r");
    if (!f) return 0;
    uint32_t fsize = (uint32_t)f.size();
    f.close();
    return fsize;
}

uint32_t History::_currentFileSize() const {
    return _fileSize(HISTORY_FILE);
}

void History::_pushCache(const HistoryEntry& entry) {
    if (_cacheCount < HISTORY_DISPLAY) {
        _cache[_cacheCount++] = entry;
    } else {
        for (int i = 0; i < HISTORY_DISPLAY - 1; i++) {
            _cache[i] = _cache[i + 1];
        }
        _cache[HISTORY_DISPLAY - 1] = entry;
    }
    _revision++;
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

    // Exact prefix: "YYYY-MM-DDTHH:MM,<trigger>," (19 chars) + at least 1 zone char
    if (strlen(line) < 20) return false;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 7) {
            if (line[i] != '-') return false;
        } else if (i == 10) {
            if (line[i] != 'T') return false;
        } else if (i == 13) {
            if (line[i] != ':') return false;
        } else if (line[i] < '0' || line[i] > '9') {
            return false;
        }
    }
    if (line[16] != ',' || line[18] != ',') return false;

    int yr = (line[0] - '0') * 1000 + (line[1] - '0') * 100 +
             (line[2] - '0') * 10   + (line[3] - '0');
    int mo = (line[5] - '0') * 10   + (line[6] - '0');
    int dy = (line[8] - '0') * 10   + (line[9] - '0');
    int hh = (line[11] - '0') * 10  + (line[12] - '0');
    int mm = (line[14] - '0') * 10  + (line[15] - '0');
    char tc = line[17];

    // Rejeitar valores cronológicos impossíveis (incluindo dias por mês e ano bissexto)
    if (yr < DATE_YEAR_MIN || yr > DATE_YEAR_MAX || mo < 1 || mo > 12 ||
        hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
    uint8_t maxD = daysInMonth((uint16_t)yr, (uint8_t)mo);
    if (dy < 1 || dy > (int)maxD) return false;

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

    // Parse zone durations - right after the second comma
    const char* p = line + 19;
    if (*p == '\0') return false;
    for (int i = 0; i < NUM_ZONES; i++) {
        if (*p) {
            if (*p != '-' && (*p < '0' || *p > '9')) return false;
            char* endp = nullptr;
            long val = strtol(p, &endp, 10);
            if (endp == p) return false;
            if (i == NUM_ZONES - 1) {
                if (*endp != '\0') return false;
            } else if (*endp == ',') {
                if (endp[1] == '\0') return false;
            } else if (*endp != '\0') {
                return false;
            }
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            out.zone_dur[i] = (uint8_t)val;
            p = (*endp == ',') ? (endp + 1) : endp;
        } else {
            out.zone_dur[i] = 0;
        }
    }
    return *p == '\0';
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
    const uint32_t fsize = (uint32_t)f.size();
    const uint32_t tailSize = HISTORY_DISPLAY * LINE_BUF + 128;

    auto scanFrom = [&](bool tailOnly) {
        _cacheCount = 0;
        for (int i = 0; i < HISTORY_DISPLAY; i++) _cache[i] = HistoryEntry{};

        if (tailOnly && fsize > tailSize) {
            f.seek(fsize - tailSize, SeekSet);
            // Descartar a primeira linha que possivelmente está cortada a meio
            while (f.available()) {
                int c = f.read();
                if (c < 0 || c == '\n') break;
            }
        } else {
            f.seek(0, SeekSet);
        }

        char chunk[256];
        char line[LINE_BUF];
        size_t linePos = 0;
        bool lineOverflow = false;

        while (f.available()) {
            esp_task_wdt_reset();
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
                            _pushCache(temp);
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
                    _pushCache(temp);
                }
            }
        }
    };

    const bool useTail = (fsize > tailSize);
    scanFrom(useTail);
    if (useTail && _cacheCount < HISTORY_DISPLAY) {
        // Fallback: tail window held corrupt/overlong lines — rescan full file
        scanFrom(false);
    }

    f.close();
}

void History::_rotateAndAppend(const char* newLine) {
    // Bloqueio forçado síncrono removido (código morto) - _rotState é garantido ser IDLE aqui.

    uint16_t step = HISTORY_MAX_ENTRIES / 10;
    if (step == 0) step = 1;
    uint16_t rotKeep = (HISTORY_MAX_ENTRIES > step) ? (HISTORY_MAX_ENTRIES - step) : 1;

    auto fallbackAppend = [&]() {
        if (_appendCsvLine(newLine)) {
            _lineCount++;
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

    // Count actual valid CSV entries so corrupt/blank lines are scrubbed without
    // consuming the discard budget of genuine watering records.
    uint16_t validLines = 0;
    bool scanReadErr = false;
    _forEachTrimmedLine(_rotSrc, [&](char* p) {
        HistoryEntry tmp = {};
        if (_lineToEntry(p, tmp) && validLines < 0xFFFFU) {
            validLines++;
        }
    }, &scanReadErr);
    if (scanReadErr || !_rotSrc.seek(0, SeekSet)) {
        _rotSrc.close();
        fallbackAppend();
        return;
    }
    _rotDiscard = (validLines > rotKeep) ? (uint16_t)(validLines - rotKeep) : 0U;

    _rotDst = LittleFS.open(HIST_TMP_FILE, "w");
    if (!_rotDst) {
        _rotSrc.close();
        fallbackAppend();
        return;
    }

    // Append pending line before starting async copy so power loss (or an earlier
    // preserved pending entry after a failed clear/rotation) cannot be truncated/lost.
    _rotHadPriorPending = (_fileSize(HIST_PENDING_FILE) > 0);
    _rotPendingInFile = _appendPendingLine(newLine);

    strlcpy(_rotPendingLine, newLine, sizeof(_rotPendingLine));
    _rotSkipped = 0;
    _rotCopied = 0;
    _rotCorruptLines = 0;
    _rotExpectedBytes = 0;
    _rotLinePos = 0;
    _rotWriteErr = false;
    _rotLineOverflow = false;
    _rotState = RotState::COPYING;
}

void History::flushDeferredBeforeReboot() {
    if (!_ready || _defHead == _defTail) return;
    while (_defHead != _defTail && _rotState == RotState::IDLE && _exportState == ExportState::IDLE) {
        _drainOneDeferred();
    }
    while (_defHead != _defTail) {
        HistoryEntry e = _deferredQueue[_defTail];
        bool requireLiveRtc = _deferredRequireRtc[_defTail];
        _defTail = (_defTail + 1) % 4;
        if (e.trigger == WaterTrigger::TEST) continue;
        if (requireLiveRtc && !gState.rtc_valid) continue;
        if (!_isValidCachedEntry(e)) continue;
        char line[LINE_BUF];
        _entryToLine(e, line, sizeof(line));
        _appendPendingLine(line);
    }
}

void History::update() {
    if (!_ready) {
        if (!gState.watering.active) {
            uint32_t nowMs = millis();
            if (nowMs - _lastMountRetryMs >= _remountBackoffMs(_mountRetryAttempts)) {
                _lastMountRetryMs = nowMs;
                if (!begin(false)) {
                    if (_mountRetryAttempts < 255) _mountRetryAttempts++;
                }
            }
        }
        return;
    }
    if (_rotState == RotState::IDLE && _exportState == ExportState::IDLE) {
        _drainOneDeferred();
        return;
    }

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
                                if (!_writeCsvLine(_rotDst, p)) {
                                    _rotWriteErr = true;
                                    _rotState = RotState::FINISHING;
                                    return;
                                }
                                _rotExpectedBytes += (uint32_t)(strlen(p) + 2U);
                                _rotCopied++;
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
                                if (!_writeCsvLine(_rotDst, p)) {
                                    _rotWriteErr = true;
                                } else {
                                    _rotExpectedBytes += (uint32_t)(strlen(p) + 2U);
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
        uint16_t pendingWritten = 0;
        if (!_rotWriteErr) {
            if (_rotHadPriorPending || _rotPendingInFile) {
                File pf = LittleFS.open(HIST_PENDING_FILE, "r");
                if (pf) {
                    bool pendReadErr = false;
                    _forEachTrimmedLine(pf, [&](char* p) {
                        if (_rotWriteErr) return;
                        HistoryEntry tmp = {};
                        if (_lineToEntry(p, tmp)) {
                            if (!_writeCsvLine(_rotDst, p)) {
                                _rotWriteErr = true;
                            } else {
                                _rotExpectedBytes += (uint32_t)(strlen(p) + 2U);
                                pendingWritten++;
                            }
                        }
                    }, &pendReadErr);
                    pf.close();
                    if (pendReadErr || (_rotPendingInFile && pendingWritten == 0)) {
                        if (_rotHadPriorPending) {
                            _rotWriteErr = true; // Preserve original file and prior pending journal
                        } else {
                            _rotPendingInFile = false; // Fall back to in-RAM _rotPendingLine
                        }
                    }
                } else {
                    if (_rotHadPriorPending) {
                        _rotWriteErr = true;
                    } else {
                        _rotPendingInFile = false;
                    }
                }
            }
            if (!_rotWriteErr && !_rotPendingInFile) {
                if (!_writeCsvLine(_rotDst, _rotPendingLine)) {
                    _rotWriteErr = true;
                } else {
                    _rotExpectedBytes += (uint32_t)(strlen(_rotPendingLine) + 2U);
                    pendingWritten++;
                }
            }
            if (!_rotWriteErr) {
                _rotDst.flush();
            }
        }
        _rotDst.close();

        if (!_rotWriteErr && _fileSize(HIST_TMP_FILE) != _rotExpectedBytes) {
            _rotWriteErr = true;
        }

        if (!_rotWriteErr && LittleFS.rename(HIST_TMP_FILE, HISTORY_FILE)) {
            LittleFS.remove(HIST_PENDING_FILE);
            _lineCount = _rotCopied + pendingWritten;
            if (_rotHadPriorPending) {
                // Prior pending entries were merged into HISTORY_FILE — resync RAM cache order
                _populateCache();
            }

            // Salvar NVS agora que _lineCount e correto E cache ja foi atualizado
            uint32_t fsize = _currentFileSize();
            storage.saveHistoryCache(_cache, sizeof(_cache), _lineCount, fsize);
            if (_rotCorruptLines > 0) {
                LOG_W(TXT_LOG_TAG_HIST, TXT_LOG_HIST_ROT_CORRUPT_SUM,
                      (unsigned)_rotCorruptLines);
            }
            LOG_I(TXT_LOG_TAG_HIST, TXT_LOG_HIST_ROT_DONE, (int)_lineCount);
        } else {
            LOG_E(TXT_LOG_TAG_HIST, TXT_LOG_HIST_RENAME_FAIL);
            LittleFS.remove(HIST_TMP_FILE);
            bool allFallbackOk = true;
            if (_rotHadPriorPending) {
                File pf = LittleFS.open(HIST_PENDING_FILE, "r");
                if (pf) {
                    bool pendReadErr = false;
                    _forEachTrimmedLine(pf, [&](char* p) {
                        HistoryEntry tmp = {};
                        if (_lineToEntry(p, tmp) && !_appendCsvLine(p)) {
                            allFallbackOk = false;
                        }
                    }, &pendReadErr);
                    pf.close();
                    if (pendReadErr) allFallbackOk = false;
                } else {
                    allFallbackOk = false;
                }
                if (!_rotPendingInFile && !_appendCsvLine(_rotPendingLine)) {
                    allFallbackOk = false;
                }
            } else {
                allFallbackOk = _appendCsvLine(_rotPendingLine);
            }
            if (allFallbackOk) {
                LittleFS.remove(HIST_PENDING_FILE);
            }
            // Rebuild consistent state from the actual file
            _lineCount = _countLines();
            _populateCache();
            if (_cacheCount == 0 && _lineCount > 0) {
                _lineCount = 0;
            }
            storage.saveHistoryCache(_cache, sizeof(_cache), _lineCount, _currentFileSize());
        }
        _rotPendingInFile = false;
        _rotHadPriorPending = false;
        _rotState = RotState::IDLE;
        // Drenar apenas UMA entrada por ciclo para evitar re-entrancia;
        // restantes entradas drenam uma por ciclo seguinte em update().
        _drainOneDeferred();
    }

    // ── Export via Serial (async) ──
    if (_exportState == ExportState::SENDING) {
        if (Serial.availableForWrite() < 64) return;
        char chunk[64];
        int bytesRead = _exportFile.read((uint8_t*)chunk, sizeof(chunk));

        if (bytesRead > 0) {
            for (int i = 0; i < bytesRead; i++) {
                char c = chunk[i];
                if (c == '\n') {
                    if (!_exportLineOverflow) {
                        char* p = _trimLineBuffer(_exportLineBuf, _exportLinePos);
                        HistoryEntry tmp = {};
                        if (p[0] != '\0' && _lineToEntry(p, tmp)) {
                            Serial.println(p);
                            _exportSent++;
                        }
                    } else {
                        _exportLinePos = 0;
                    }
                    _exportLineOverflow = false;
                } else if (c != '\r') {
                    if (_exportLinePos < sizeof(_exportLineBuf) - 1) {
                        _exportLineBuf[_exportLinePos++] = c;
                    } else {
                        _exportLineOverflow = true;
                    }
                }
            }
        } else {
            if (bytesRead < 0 || _exportFile.available() > 0) {
                _exportReadErr = true;
                _exportLinePos = 0; // discard incomplete partial line
                _exportLineOverflow = false;
            } else if (_exportLinePos > 0) {
                if (!_exportLineOverflow) {
                    // EOF — process last line if no trailing newline
                    char* p = _trimLineBuffer(_exportLineBuf, _exportLinePos);
                    HistoryEntry tmp = {};
                    if (p[0] != '\0' && _lineToEntry(p, tmp)) {
                        Serial.println(p);
                        _exportSent++;
                    }
                } else {
                    _exportLinePos = 0;
                }
                _exportLineOverflow = false;
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
        _drainOneDeferred();
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
    _exportLinePos = 0;
    _exportReadErr = false;
    _exportLineOverflow = false;

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

bool History::isRotating() const {
    return _rotState != RotState::IDLE;
}

uint16_t History::exportSent() const {
    return _exportSent;
}
