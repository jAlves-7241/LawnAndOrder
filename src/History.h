#pragma once
#include <Arduino.h>
#include "AppState.h"
#include "config.h"

// ─────────────────────────────────────────────────────────
// History
//
// Appends one CSV line per completed watering cycle to LittleFS.
// TEST cycles are never recorded.
//
// CSV format (one line per cycle):
//   YYYY-MM-DDTHH:MM,TYPE,dur_z0,dur_z1,dur_z2,dur_z3
//
//   TYPE      : "GENERAL" | "CUSTOM"
//   dur_zN    : minutes actually run for zone N (0 = zone not included)
//
// Example:
//   2026-04-26T18:02,GENERAL,15,15,10,0
//
// When HISTORY_MAX_ENTRIES is reached the oldest entry is dropped
// (file is rewritten — acceptable given small file sizes ≤ ~2 KB).
// ─────────────────────────────────────────────────────────

enum class WateringType : uint8_t { GENERAL, CUSTOM, TEST };

struct HistoryEntry {
    SystemTime   start;                 // timestamp of cycle start
    WateringType type;
    uint8_t      zone_dur[NUM_ZONES];   // minutes per zone (0 = not run)
};

class History {
public:
    // Mount LittleFS. Call once in setup().
    // formatOnFail: reformat partition if mount fails (handles first boot).
    bool begin(bool formatOnFail = true);

    // Append one entry to the CSV. No-op if type == TEST or RTC not valid.
    void record(const HistoryEntry& entry);

    // Read the last `count` entries (oldest first) into `out[]`.
    // Returns the number of entries actually filled.
    uint8_t readLast(uint8_t count, HistoryEntry out[]) const;

    // Delete the history file.
    void clear();

    // Total number of recorded entries currently in the file.
    uint16_t entryCount() const;

private:
    bool _ready;

    // Serialise / deserialise a single CSV line (buf must be ≥ 52 bytes).
    static void   _entryToLine(const HistoryEntry& e, char* buf, size_t len);
    static bool   _lineToEntry(const char* line, HistoryEntry& out);

    uint16_t _countLines() const;
    void     _rotateAndAppend(const char* newLine);
};

extern History history;
