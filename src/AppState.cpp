#include "i18n.h"
#include "AppState.h"
#include <string.h>
#include "log.h"

AppState gState;

// ─────────────────────────────────────────────────────────
// Mode schedule table - indexed by (uint8_t)AppMode
// Built entirely from config.h defines; change those, this follows.
// ─────────────────────────────────────────────────────────
ModeSchedule MODE_SCHEDULES[(uint8_t)AppMode::_COUNT] = {
    // INTENSO - three daily slots
    {
        {{ SCHED_INTENSO_SLOT0_H, SCHED_INTENSO_SLOT0_M },
         { SCHED_INTENSO_SLOT1_H, SCHED_INTENSO_SLOT1_M },
         { SCHED_INTENSO_SLOT2_H, SCHED_INTENSO_SLOT2_M },
         { 0, 0 }},
        3,
        DayPattern::DAILY,
        0, 1
    },
    // MEDIO - two daily slots
    {
        {{ SCHED_MEDIO_SLOT0_H, SCHED_MEDIO_SLOT0_M },
         { SCHED_MEDIO_SLOT1_H, SCHED_MEDIO_SLOT1_M },
         { 0, 0 }, { 0, 0 }},
        2,
        DayPattern::DAILY,
        0, 1
    },
    // FRACO - one daily slot
    {
        {{ SCHED_FRACO_SLOT0_H, SCHED_FRACO_SLOT0_M },
         { 0, 0 }, { 0, 0 }, { 0, 0 }},
        1,
        DayPattern::DAILY,
        0, 1
    },
    // DESATIVADO - no automatic watering
    {
        {{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }},
        0,
        DayPattern::DAILY,
        0, 1
    },
    // PERSONALIZADO - default initial settings
    {
        {{ SCHED_CUSTOM_SLOT0_H, SCHED_CUSTOM_SLOT0_M },
         { 0, 0 }, { 0, 0 }, { 0, 0 }},
        SCHED_CUSTOM_SLOTS,
        DayPattern::EVERY_X_DAYS,
        0, SCHED_CUSTOM_INTERVAL
    },
};

// ─────────────────────────────────────────────────────────
void initAppState() {
    // ── Zone defaults ────────────────────────────────────
    const char*   names[NUM_ZONES]   = { ZONE1_NAME, ZONE2_NAME, ZONE3_NAME, ZONE4_NAME };
    const bool    enabled[NUM_ZONES] = { true,  true,  true,  true };
    const uint8_t durs[NUM_ZONES]    = { ZONE1_DUR, ZONE2_DUR, ZONE3_DUR, ZONE4_DUR };

    for (int i = 0; i < NUM_ZONES; i++) {
        if (i < 4 && names[i] != nullptr) {
            strncpy(gState.zones[i].name, names[i], sizeof(gState.zones[i].name) - 1);
        } else {
            snprintf(gState.zones[i].name, sizeof(gState.zones[i].name), "Zona %d", i + 1);
        }
        gState.zones[i].name[sizeof(gState.zones[i].name) - 1] = '\0';
        gState.zones[i].enabled      = (i < 4) ? enabled[i] : true;
        gState.zones[i].duration_min = (i < 4) ? durs[i] : 10;
    }

    // ── Mode & schedule ──────────────────────────────────
    gState.mode            = AppMode::MEDIO;
    gState.suspended       = false;
    gState.suspended_until = 0;
    gState.custom_ref_day  = 0xFFFFFFFFUL; // sentinel: "not yet set" - Scheduler will initialise on first use
    // next_hour/next_min are computed by Scheduler after RTC is ready.
    // Set a visible placeholder so the IDLE screen is never blank.
    gState.next_hour = SCHED_MEDIO_SLOT0_H;
    gState.next_min  = SCHED_MEDIO_SLOT0_M;

    gState.backlight_timeout_ms = 120000UL;  // default: 2 minutes
    gState.auto_dst = AUTO_DST_DEFAULT;
    gState.setup_done = false;

    // ── RTC ──────────────────────────────────────────────
    gState.rtc_valid = false;
    gState.now = { 2026, 1, 1, 0, 0, 0, 4, 1767225600UL };  // placeholder until RTC is read

    // ── Watering ─────────────────────────────────────────
    gState.watering = { false, false, 0, 0 };

    // ── Custom run defaults ──────────────────────────────
    for (int i = 0; i < NUM_ZONES; i++) gState.custom_sel[i] = false;
    gState.custom_dur_min = 10;
    
    // Reset custom mode to defaults to avoid tainted schedules
    MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO].slot_count = SCHED_CUSTOM_SLOTS;
    MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO].interval_days = SCHED_CUSTOM_INTERVAL;
    MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO].slots[0].hour = SCHED_CUSTOM_SLOT0_H;
    MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO].slots[0].minute = SCHED_CUSTOM_SLOT0_M;

    LOG_I("APP", TXT_LOG_DEFAULTS_LOADED,
          (uint8_t)gState.mode, NUM_ZONES);
}
