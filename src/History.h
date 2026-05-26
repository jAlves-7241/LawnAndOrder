#pragma once
#include <Arduino.h>
#include <FS.h>
#include "AppState.h"   // WaterTrigger, HistoryEntry, NUM_ZONES
#include "config.h"

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

    char _rotLineBuf[64];
    size_t _rotLinePos;
    char _rotPendingLine[128];

    // Cache for UI display
    HistoryEntry _cache[HISTORY_DISPLAY];
    uint8_t      _cacheCount = 0;

    static void _entryToLine(const HistoryEntry& e, char* buf, size_t len);
    static bool _lineToEntry(const char* line, HistoryEntry& out);
    static char* _trimLineBuffer(char* buf, size_t& pos);
    uint16_t    _countLines() const;
    void        _rotateAndAppend(const char* newLine);
    void        _populateCache();
};

extern History history;
