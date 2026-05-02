#pragma once
#include <Arduino.h>
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

private:
    bool _ready;

    static void _entryToLine(const HistoryEntry& e, char* buf, size_t len);
    static bool _lineToEntry(const char* line, HistoryEntry& out);
    uint16_t    _countLines() const;
    void        _rotateAndAppend(const char* newLine);
};

extern History history;
