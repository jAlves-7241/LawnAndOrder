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
    uint8_t duration_min;   // 0–20
};

struct WateringStatus {
    bool    active;
    uint8_t zone_idx;
    uint8_t progress_pct;   // 0–100
};

// Current date/time — updated every second by RTClock::update()
// Named SystemTime to avoid collision with RTClib's DateTime class.
struct SystemTime {
    uint16_t year;    // e.g. 2026
    uint8_t  month;   // 1–12
    uint8_t  day;     // 1–31
    uint8_t  hour;    // 0–23
    uint8_t  min;     // 0–59
    uint8_t  sec;     // 0–59
    uint8_t  dow;     // 0=Sun … 6=Sat
};

// ─────────────────────────────────────────────────────────
// Global state — single source of truth for the whole app
// ─────────────────────────────────────────────────────────

struct AppState {
    Zone           zones[NUM_ZONES];
    AppMode        mode;
    WateringStatus watering;

    // Current time (written by RTClock, read by UI and Scheduler)
    SystemTime now;
    bool       rtc_valid;   // false = RTC lost power or not found

    // Custom manual-run selection (set by UI, consumed by scheduler)
    bool    custom_sel[NUM_ZONES];
    uint8_t custom_dur_min;

    // Schedule
    bool    suspended;
    uint8_t next_hour;
    uint8_t next_min;

    // Backlight — timeout in ms after last activity (BACKLIGHT_TIMEOUT_NEVER = always on)
    uint32_t backlight_timeout_ms;
};

extern AppState gState;

void initAppState();
