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

const char* serialModeName(AppMode m) {
    switch (m) {
        case AppMode::INTENSO:       return TXT_TERM_MODE_INTENSE;
        case AppMode::MEDIO:         return TXT_TERM_MODE_MEDIUM;
        case AppMode::FRACO:         return TXT_TERM_MODE_WEAK;
        case AppMode::DESATIVADO:    return TXT_TERM_MODE_DISABLED;
        case AppMode::PERSONALIZADO: return TXT_TERM_MODE_CUSTOM;
        default:                     return TXT_TERM_MODE_UNKNOWN;
    }
}

const char* serialTriggerName(WaterTrigger t) {
    switch (t) {
        case WaterTrigger::AUTO:   return TXT_SER_TRIG_AUTO;
        case WaterTrigger::MANUAL: return TXT_SER_TRIG_MANUAL;
        case WaterTrigger::CUSTOM: return TXT_SER_TRIG_CUSTOM;
        case WaterTrigger::TEST:   return TXT_SER_TRIG_TEST;
        default:                   return TXT_TERM_MODE_UNKNOWN;
    }
}

const char* i2cDeviceName(uint8_t addr) {
#if LCD_ADDR != 0x27 && LCD_ADDR != 0x3F
    if (addr == LCD_ADDR) return TXT_I2C_DEV_LCD;
#endif
    if (addr == 0x27 || addr == 0x3F) return TXT_I2C_DEV_LCD;
    if (addr == 0x68)                 return TXT_I2C_DEV_RTC;
    if (addr >= 0x50 && addr <= 0x57) return TXT_I2C_DEV_EEPROM;
    return TXT_I2C_DEV_UNKNOWN;
}

uint8_t daysInMonth(uint16_t year, uint8_t month) {
    if (month < 1 || month > 12) return 0;
    static const uint8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) {
        return 29;
    }
    return kDays[month - 1];
}

uint16_t computeMinCustomSpacing() {
    uint16_t total = 0;
    uint8_t enCount = 0;
    for (int z = 0; z < NUM_ZONES; z++) {
        if (gState.zones[z].enabled) {
            total += gState.zones[z].duration_min;
            enCount++;
        }
    }
    if (enCount > 1) total += (uint16_t)(enCount - 1);
    return (total > 0) ? total : 1;
}

void sortCustomSlots(ModeSchedule& sched) {
    uint8_t count = (sched.slot_count <= MAX_SLOTS_PER_MODE) ? sched.slot_count : MAX_SLOTS_PER_MODE;
    for (uint8_t i = 0; i + 1 < count; i++) {
        for (uint8_t j = (uint8_t)(i + 1); j < count; j++) {
            uint16_t ti = (uint16_t)sched.slots[i].hour * 60U + sched.slots[i].minute;
            uint16_t tj = (uint16_t)sched.slots[j].hour * 60U + sched.slots[j].minute;
            if (tj < ti) {
                ScheduleSlot tmp = sched.slots[i];
                sched.slots[i] = sched.slots[j];
                sched.slots[j] = tmp;
            }
        }
    }
}

bool hasCustomSlotOverlap(const ModeSchedule& sched, uint16_t minSpacingMin) {
    uint8_t count = (sched.slot_count <= MAX_SLOTS_PER_MODE) ? sched.slot_count : MAX_SLOTS_PER_MODE;
    if (count <= 1) return false;
    const bool wrapMidnight = !(sched.day_pattern == DayPattern::EVERY_X_DAYS && sched.interval_days > 1);
    for (uint8_t i = 0; i < count; i++) {
        for (uint8_t j = (uint8_t)(i + 1); j < count; j++) {
            uint16_t s1 = (uint16_t)sched.slots[i].hour * 60U + sched.slots[i].minute;
            uint16_t s2 = (uint16_t)sched.slots[j].hour * 60U + sched.slots[j].minute;
            int16_t diff = (int16_t)s1 - (int16_t)s2;
            if (diff < 0) diff = (int16_t)(-diff);
            if (wrapMidnight && diff > 720) diff = (int16_t)(1440 - diff);
            if ((uint16_t)diff < minSpacingMin) return true;
        }
    }
    return false;
}

void respaceCustomSlots(ModeSchedule& sched) {
    if (sched.slot_count == 0 || sched.slot_count > MAX_SLOTS_PER_MODE) {
        sched.slot_count = 1;
    }
    uint16_t baseMins = (uint16_t)sched.slots[0].hour * 60U + sched.slots[0].minute;
    uint16_t stepMins = 1440U / sched.slot_count;
    for (uint8_t i = 1; i < sched.slot_count; i++) {
        uint16_t nextMins = (uint16_t)((baseMins + (uint16_t)i * stepMins) % 1440U);
        sched.slots[i].hour   = (uint8_t)(nextMins / 60U);
        sched.slots[i].minute = (uint8_t)(nextMins % 60U);
    }
    for (uint8_t i = sched.slot_count; i < MAX_SLOTS_PER_MODE; i++) {
        sched.slots[i].hour   = 0;
        sched.slots[i].minute = 0;
    }
    sortCustomSlots(sched);
}

// ─────────────────────────────────────────────────────────
void initAppState() {
    // ── Zone defaults ────────────────────────────────────
    const char*   names[NUM_ZONES]   = { ZONE1_NAME, ZONE2_NAME, ZONE3_NAME, ZONE4_NAME };
    const uint8_t durs[NUM_ZONES]    = { ZONE1_DUR, ZONE2_DUR, ZONE3_DUR, ZONE4_DUR };

    for (uint8_t i = 0; i < NUM_ZONES; i++) {
        if (names[i] != nullptr) {
            strncpy(gState.zones[i].name, names[i], sizeof(gState.zones[i].name) - 1);
        } else {
            snprintf(gState.zones[i].name, sizeof(gState.zones[i].name), TXT_ZONE_PREFIX, (int)(i + 1));
        }
        gState.zones[i].name[sizeof(gState.zones[i].name) - 1] = '\0';
        gState.zones[i].enabled      = (durs[i] > 0);
        gState.zones[i].duration_min = durs[i];
    }

    // ── Mode & schedule ──────────────────────────────────
    gState.mode            = AppMode::DESATIVADO;
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
    gState.now = { 2026, 1, 1, 0, 0, 0, 4, UNIX_DEFAULT_FALLBACK };  // placeholder until RTC is read

    // ── Watering ─────────────────────────────────────────
    gState.watering = { false, false, 0, 0 };

    // ── Custom run defaults ──────────────────────────────
    for (int i = 0; i < NUM_ZONES; i++) gState.custom_sel[i] = false;
    gState.custom_dur_min = 10;
    
    // Reset custom mode to defaults to avoid tainted schedules
    MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO].slot_count = SCHED_CUSTOM_SLOTS;
    MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO].interval_days = SCHED_CUSTOM_INTERVAL;
    MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO].day_pattern = DayPattern::EVERY_X_DAYS;
    MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO].dow_mask = 0;
    MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO].slots[0].hour = SCHED_CUSTOM_SLOT0_H;
    MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO].slots[0].minute = SCHED_CUSTOM_SLOT0_M;
    for(int i = 1; i < MAX_SLOTS_PER_MODE; i++) {
        MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO].slots[i].hour = 0;
        MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO].slots[i].minute = 0;
    }

    LOG_D(TXT_LOG_TAG_SYS, TXT_LOG_DEFAULTS_LOADED,
          serialModeName(gState.mode), NUM_ZONES);
}
