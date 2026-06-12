#pragma once
#include <Arduino.h>
#include <FS.h>
#include "AppState.h"   // WaterTrigger, HistoryEntry, NUM_ZONES
#include "config.h"

const size_t LINE_BUF = 128;

// ─────────────────────────────────────────────────────────
// History
//
// Appends one CSV line per completed watering cycle to LittleFS.
// TEST cycles are never recorded (filtered by WaterTrigger::TEST).
//
// CSV format:
//   YYYY-MM-DDTHH:MM,<trigger>,dur_z0,dur_z1,dur_z2,dur_z3
//
//   trigger : A (auto) | M (manual/general) | C (custom)
//
// Example:
//   2026-04-26T18:02,M,15,15,10,0
// ─────────────────────────────────────────────────────────

class History {
public:
    bool    begin(bool formatOnFail = true);
    void    record(const HistoryEntry& entry);
    uint8_t readLast(uint8_t count, HistoryEntry out[]) const;
    void    clear();
    uint16_t entryCount() const;

    void    update();

    // Serial export
    void     startExport();
    bool     isExporting() const;
    uint8_t  exportProgress() const;
    uint16_t exportSent() const;

private:
    bool     _ready = false;
    uint16_t _lineCount = 0;   // cached line count - avoids re-reading file on every record()

    enum class RotState { IDLE, COPYING, FINISHING };
    RotState _rotState = RotState::IDLE;

    File _rotSrc;
    File _rotDst;
    uint16_t _rotDiscard;
    uint16_t _rotKeep;
    uint16_t _rotSkipped;
    uint16_t _rotCopied;

    char _rotLineBuf[LINE_BUF];
    size_t _rotLinePos;
    char _rotPendingLine[LINE_BUF];

    // Cache for UI display
    HistoryEntry _cache[HISTORY_DISPLAY];
    uint8_t      _cacheCount = 0;
    
    bool         _pendingCacheSave = false;   // NVS save diferido (rotação async)
    
    HistoryEntry _deferredQueue[4];
    uint8_t      _defHead = 0;
    uint8_t      _defTail = 0;

    // ── Export via Serial (async) ──
    enum class ExportState : uint8_t { IDLE, SENDING, FOOTER };
    ExportState _exportState = ExportState::IDLE;
    File        _exportFile;
    uint16_t    _exportSent;
    uint16_t    _exportTotal;
    char        _exportLineBuf[LINE_BUF];
    size_t      _exportLinePos;

    static void _entryToLine(const HistoryEntry& e, char* buf, size_t len);
    static bool _lineToEntry(const char* line, HistoryEntry& out);
    static char* _trimLineBuffer(char* buf, size_t& pos);
    uint16_t    _countLines() const;
    void        _rotateAndAppend(const char* newLine);
    void        _populateCache();
};

extern History history;
