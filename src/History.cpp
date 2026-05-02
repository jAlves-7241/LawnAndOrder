#include "History.h"
#include <LittleFS.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

History history;

static const size_t LINE_BUF = 52;  // max CSV line length + \0

// ─────────────────────────────────────────────────────────
bool History::begin(bool formatOnFail) {
    _ready = LittleFS.begin(formatOnFail);
    if (!_ready) {
        Serial.println("[HIST] Erro ao montar LittleFS");
    } else {
        Serial.printf("[HIST] Pronto — %d entradas\n", entryCount());
    }
    return _ready;
}

// ─────────────────────────────────────────────────────────
void History::record(const HistoryEntry& entry) {
    if (!_ready)                          return;
    if (entry.type == WateringType::TEST)  return;
    if (!gState.rtc_valid)                return;

    char line[LINE_BUF];
    _entryToLine(entry, line, sizeof(line));

    if (_countLines() >= HISTORY_MAX_ENTRIES) {
        _rotateAndAppend(line);
    } else {
        File f = LittleFS.open(HISTORY_FILE, "a");
        if (!f) { Serial.println("[HIST] Erro ao abrir ficheiro"); return; }
        f.println(line);
        f.close();
    }

    Serial.printf("[HIST] Registado: %s\n", line);
}

// ─────────────────────────────────────────────────────────
uint8_t History::readLast(uint8_t count, HistoryEntry out[]) const {
    if (!_ready || count == 0) return 0;
    if (count > HISTORY_DISPLAY) count = HISTORY_DISPLAY;

    File f = LittleFS.open(HISTORY_FILE, "r");
    if (!f) return 0;

    // Ring buffer of `count` lines — reads the whole file but only keeps
    // the last `count` non-empty lines. File is at most ~2 KB.
    char lines[HISTORY_DISPLAY][LINE_BUF];
    uint8_t head  = 0;
    uint16_t total = 0;

    while (f.available()) {
        String s = f.readStringUntil('\n');
        s.trim();
        if (s.length() == 0) continue;
        strncpy(lines[head % count], s.c_str(), LINE_BUF - 1);
        lines[head % count][LINE_BUF - 1] = '\0';
        head++;
        total++;
    }
    f.close();

    uint8_t kept   = (total < count) ? (uint8_t)total : count;
    uint8_t filled = 0;

    for (uint8_t i = 0; i < kept; i++) {
        uint8_t slot = (uint8_t)((head - kept + i) % count);
        if (_lineToEntry(lines[slot], out[filled]))
            filled++;
    }
    return filled;
}

// ─────────────────────────────────────────────────────────
void History::clear() {
    if (!_ready) return;
    LittleFS.remove(HISTORY_FILE);
    Serial.println("[HIST] Historico apagado");
}

// ─────────────────────────────────────────────────────────
uint16_t History::entryCount() const {
    return _countLines();
}

// ─────────────────────────────────────────────────────────
// Private
// ─────────────────────────────────────────────────────────

void History::_entryToLine(const HistoryEntry& e, char* buf, size_t len) {
    const char* t = (e.type == WateringType::CUSTOM) ? "CUSTOM" : "GENERAL";
    snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d,%s",
             e.start.year, e.start.month, e.start.day,
             e.start.hour, e.start.min, t);
    for (int i = 0; i < NUM_ZONES; i++) {
        char tmp[5];
        snprintf(tmp, sizeof(tmp), ",%d", e.zone_dur[i]);
        strncat(buf, tmp, len - strlen(buf) - 1);
    }
}

bool History::_lineToEntry(const char* line, HistoryEntry& out) {
    int yr, mo, dy, hh, mm;
    char type[8] = {};

    if (sscanf(line, "%4d-%2d-%2dT%2d:%2d,%7s",
               &yr, &mo, &dy, &hh, &mm, type) != 6) return false;

    out.start = { (uint16_t)yr, (uint8_t)mo, (uint8_t)dy,
                  (uint8_t)hh, (uint8_t)mm, 0, 0 };
    out.type  = (strncmp(type, "CUSTOM", 6) == 0)
                ? WateringType::CUSTOM : WateringType::GENERAL;

    // Advance past first two commas to reach zone durations
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
    while (f.available()) {
        String s = f.readStringUntil('\n');
        if (s.trim().length() > 0) n++;
    }
    f.close();
    return n;
}

void History::_rotateAndAppend(const char* newLine) {
    const uint16_t keep = HISTORY_MAX_ENTRIES - 1;
    // keep * LINE_BUF ≈ 49 * 52 ≈ 2.5 KB — fine on ESP32
    char* buf = (char*)malloc((size_t)keep * LINE_BUF);
    if (!buf) {
        // Fallback: just append (file exceeds max by 1 line — harmless)
        File f = LittleFS.open(HISTORY_FILE, "a");
        if (f) { f.println(newLine); f.close(); }
        return;
    }
    memset(buf, 0, (size_t)keep * LINE_BUF);

    File src = LittleFS.open(HISTORY_FILE, "r");
    uint16_t n = 0;
    bool skipped = false;

    while (src.available() && n < keep) {
        String s = src.readStringUntil('\n');
        s.trim();
        if (s.length() == 0) continue;
        if (!skipped) { skipped = true; continue; }  // discard oldest
        strncpy(buf + n * LINE_BUF, s.c_str(), LINE_BUF - 1);
        n++;
    }
    src.close();

    File dst = LittleFS.open(HISTORY_FILE, "w");
    if (dst) {
        for (uint16_t i = 0; i < n; i++) dst.println(buf + i * LINE_BUF);
        dst.println(newLine);
        dst.close();
    }
    free(buf);
}
