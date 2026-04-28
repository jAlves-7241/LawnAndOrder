#include "AppState.h"
#include <string.h>

AppState gState;

void initAppState() {
    // ── Zone defaults ────────────────────────────────────
    const char*  names[NUM_ZONES]   = { "Jardim", "Horta", "Relvado", "Sebe" };
    const bool   enabled[NUM_ZONES] = { true,     true,    true,      false  };
    const uint8_t durs[NUM_ZONES]   = { 30,       30,      20,        0      };

    for (int i = 0; i < NUM_ZONES; i++) {
        strncpy(gState.zones[i].name, names[i], sizeof(gState.zones[i].name) - 1);
        gState.zones[i].name[sizeof(gState.zones[i].name) - 1] = '\0';
        gState.zones[i].enabled      = enabled[i];
        gState.zones[i].duration_min = durs[i];
    }

    // ── Mode & schedule ──────────────────────────────────
    gState.mode      = AppMode::MEDIO;
    gState.suspended = false;
    gState.next_hour = 18;
    gState.next_min  = 0;

    // ── Watering ─────────────────────────────────────────
    gState.watering = { false, 0, 0 };

    // ── Custom run defaults ──────────────────────────────
    for (int i = 0; i < NUM_ZONES; i++) gState.custom_sel[i] = false;
    gState.custom_dur_min = 15;
}
