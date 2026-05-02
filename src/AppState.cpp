#include "AppState.h"
#include <string.h>

AppState gState;

// ─────────────────────────────────────────────────────────
// Mode schedule table — indexed by (uint8_t)AppMode
// Built entirely from config.h defines; change those, this follows.
// ─────────────────────────────────────────────────────────
extern const ModeSchedule MODE_SCHEDULES[(uint8_t)AppMode::_COUNT] = {
    // INTENSO — two daily slots
    {
        {{ SCHED_INTENSO_SLOT0_H, SCHED_INTENSO_SLOT0_M },
         { SCHED_INTENSO_SLOT1_H, SCHED_INTENSO_SLOT1_M }},
        2,
        DayPattern::DAILY,
        0
    },
    // MEDIO — one daily slot
    {
        {{ SCHED_MEDIO_SLOT0_H, SCHED_MEDIO_SLOT0_M },
         { 0, 0 }},
        1,
        DayPattern::DAILY,
        0
    },
    // FRACO — one slot, odd calendar days only
    {
        {{ SCHED_FRACO_SLOT0_H, SCHED_FRACO_SLOT0_M },
         { 0, 0 }},
        1,
        DayPattern::ODD_DAYS,
        0
    },
    // DESATIVADO — no automatic watering
    {
        {{ 0, 0 }, { 0, 0 }},
        0,
        DayPattern::DAILY,
        0
    },
    // PERSONALIZADO — no slots until user defines them (future)
    {
        {{ 0, 0 }, { 0, 0 }},
        0,
        DayPattern::DAILY,
        0
    },
};

// ─────────────────────────────────────────────────────────
void initAppState() {
    // ── Zone defaults ────────────────────────────────────
    const char*   names[NUM_ZONES]   = { "Jardim", "Horta", "Relvado", "Sebe" };
    const bool    enabled[NUM_ZONES] = { true,  true,  true,  true };
    const uint8_t durs[NUM_ZONES]    = { 15,    15,    10,    10   };

    for (int i = 0; i < NUM_ZONES; i++) {
        strncpy(gState.zones[i].name, names[i], sizeof(gState.zones[i].name) - 1);
        gState.zones[i].name[sizeof(gState.zones[i].name) - 1] = '\0';
        gState.zones[i].enabled      = enabled[i];
        gState.zones[i].duration_min = durs[i];
    }

    // ── Mode & schedule ──────────────────────────────────
    gState.mode      = AppMode::MEDIO;
    gState.suspended = false;
    // next_hour/next_min are computed by Scheduler after RTC is ready.
    // Set a visible placeholder so the IDLE screen is never blank.
    gState.next_hour = SCHED_MEDIO_SLOT0_H;
    gState.next_min  = SCHED_MEDIO_SLOT0_M;

    gState.backlight_timeout_ms = 120000UL;  // default: 2 minutes

    // ── RTC ──────────────────────────────────────────────
    gState.rtc_valid = false;
    gState.now = { 2026, 1, 1, 0, 0, 0, 4 };  // placeholder until RTC is read

    // ── Watering ─────────────────────────────────────────
    gState.watering = { false, 0, 0 };

    // ── Custom run defaults ──────────────────────────────
    for (int i = 0; i < NUM_ZONES; i++) gState.custom_sel[i] = false;
    gState.custom_dur_min = 10;
}
