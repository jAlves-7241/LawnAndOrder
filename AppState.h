#pragma once
#include <stdint.h>
#include "config.h"

// ─────────────────────────────────────────────────────────
// Types
// ─────────────────────────────────────────────────────────

enum class AppMode : uint8_t {
    INTENSO = 0,   // 07:00 + 18:00, every day
    MEDIO,         // 18:00, every day
    FRACO,         // 18:00, every other day
    DESATIVADO,    // no automatic watering
    PERSONALIZADO, // user-defined (future)
    _COUNT
};

struct Zone {
    char    name[12];       // e.g. "Jardim"
    bool    enabled;
    uint8_t duration_min;   // 0–60
};

struct WateringStatus {
    bool    active;
    uint8_t zone_idx;
    uint8_t progress_pct;   // 0–100
};

// ─────────────────────────────────────────────────────────
// Global state — single source of truth for the whole app
// ─────────────────────────────────────────────────────────

struct AppState {
    Zone           zones[NUM_ZONES];
    AppMode        mode;
    WateringStatus watering;

    // Custom manual-run selection (set by UI, consumed by scheduler)
    bool    custom_sel[NUM_ZONES];
    uint8_t custom_dur_min;

    // Schedule
    bool    suspended;
    uint8_t next_hour;
    uint8_t next_min;
};

extern AppState gState;

void initAppState();