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
    uint32_t unix;    // seconds since 1970
};

// ─────────────────────────────────────────────────────────
// History
// ─────────────────────────────────────────────────────────

// Trigger type for a watering cycle
enum class WaterTrigger : uint8_t {
    AUTO   = 'A',   // fired by Scheduler
    MANUAL = 'M',   // Rega Geral triggered by user
    CUSTOM = 'C',   // Personalizado triggered by user
    TEST   = 'T',   // test run — not written to history file
};

// One history record — one full watering cycle
struct HistoryEntry {
    uint16_t    year;
    uint8_t     month, day, hour, min;
    uint8_t     zone_dur[NUM_ZONES];  // actual minutes run per zone (0 = skipped)
    WaterTrigger trigger;
};

// ─────────────────────────────────────────────────────────
// Schedule data structures
// ─────────────────────────────────────────────────────────

// When during the day a watering cycle triggers.
struct ScheduleSlot {
    uint8_t hour;    // 0–23
    uint8_t minute;  // 0–59
};

// Which days of the week/month the schedule is active.
enum class DayPattern : uint8_t {
    DAILY,      // every day
    ODD_DAYS,   // odd calendar days  (1, 3, 5 …)
    EVEN_DAYS,  // even calendar days (2, 4, 6 …)
    DOW_MASK,   // specific days of week — see dow_mask below
                // bit 0 = Sun, bit 1 = Mon, … bit 6 = Sat
};

// Full schedule definition for one AppMode.
// DESATIVADO and PERSONALIZADO have slot_count == 0 (no automatic watering).
struct ModeSchedule {
    ScheduleSlot slots[MAX_SLOTS_PER_MODE];
    uint8_t      slot_count;   // 0 = no automatic watering
    DayPattern   day_pattern;
    uint8_t      dow_mask;     // only used when day_pattern == DOW_MASK
};

// Table indexed by (uint8_t)AppMode.
// Declared extern so any module can read it; defined once in AppState.cpp.
extern const ModeSchedule MODE_SCHEDULES[(uint8_t)AppMode::_COUNT];

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
    bool     suspended;
    uint32_t suspended_until;

    uint8_t next_hour;
    uint8_t next_min;

    // Backlight — timeout in ms after last activity (BACKLIGHT_TIMEOUT_NEVER = always on)
    uint32_t backlight_timeout_ms;
};

extern AppState gState;

void initAppState();
