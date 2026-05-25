#pragma once
#include <stdint.h>

// ─────────────────────────────────────────────────────────
// MenuID
// ─────────────────────────────────────────────────────────
enum class MenuID : uint8_t {
    MAIN,
    MANUAL,
    PROG,
    MODOS,
    CFG_ZONAS,
    CUSTOM_ZONAS,
    CFG_CUSTOM,
    HISTORICO,
    DEF,
    DEF_AVANCADO,
    TESTES,
    BLSEL,    // backlight timeout selector
    SETUP_MODE,   
    SETUP_ZONES,  
    SETUP_CUSTOM, 
    _COUNT
};

// ─────────────────────────────────────────────────────────
// DurContext
// ─────────────────────────────────────────────────────────
enum class DurContext : uint8_t {
    CUSTOM_RUN,  // writes gState.custom_dur_min → confirm screen
    CFG_ZONE,    // writes gState.zones[zoneIdx].duration_min → CFG_ZONAS
    SUSPEND,     // writes gState.suspended_until → IDLE
    FREQ_DAYS,   // writes cs.interval_days (1-14)
    NUM_CYCLES,  // writes cs.slot_count (1-4)
};

// ─────────────────────────────────────────────────────────
// TimeEditContext
// ─────────────────────────────────────────────────────────
enum class TimeEditContext : uint8_t {
    RTC,
    CUSTOM_CYCLE, // edits cs.slots[_teCycleIdx]
};

// ─────────────────────────────────────────────────────────
// SetupStep
// ─────────────────────────────────────────────────────────
enum class SetupStep : uint8_t {
    WELCOME,       // ecrã inicial
    DATE_TIME,     // acertar data/hora
    MODE_SELECT,   // escolher modo de rega
    CUSTOM_CONFIG, // personalizar modo (se aplicavel)
    ZONE_CONFIG,   // configurar zonas
    COMPLETE,      // ecrã final
};
