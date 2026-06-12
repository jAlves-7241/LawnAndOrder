#include "../i18n.h"
#include "MenuBuilder.h"
#include "../AppState.h"
#include "../History.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void MenuBuilder::makeItem(MenuItem* it, const char* label, const char* action) {
    strncpy(it->label, label, sizeof(it->label) - 1);
    it->label[sizeof(it->label) - 1] = '\0';
    strncpy(it->action, action, sizeof(it->action) - 1);
    it->action[sizeof(it->action) - 1] = '\0';
}

void MenuBuilder::build(MenuID mid, MenuItem* items, uint8_t& itemCount) {
    itemCount = 0;
    MenuItem* it = items;

    auto add_item = [&](const char* label, const char* action) {
        if (it - items < MAX_ITEMS) {
            makeItem(it++, label, action);
        }
    };

    auto mc = [&](uint8_t i) -> const char* {
        return ((uint8_t)gState.mode == i) ? "[*]" : "[ ]";
    };

    char lbuf[21];

    switch (mid) {
    case MenuID::MAIN:
        add_item(TXT_MANUAL_WATERING,       "go:manual");
        add_item(TXT_PROG,       "go:prog");
        add_item(TXT_MB_SETTINGS,        "go:def");
        add_item(TXT_BACK,         "go:idle");
        break;

    case MenuID::MANUAL:
        add_item(TXT_MB_GEN_WATERING,    TXT_ACT_CONFIRM_GEN_WATERING);
        add_item(TXT_CUSTOM, "go:czonas");
        add_item(TXT_BACK,     "go:main");
        break;

    case MenuID::PROG:
        add_item(TXT_MB_VIEW_SCHED,     "horarios");
        add_item(TXT_MENU_CHANGE_MODE,     "go:modos");
        if (gState.mode == AppMode::PERSONALIZADO) {
            add_item(TXT_MENU_CUSTOMIZE, "go:ccustom");
        }
        add_item(TXT_CONFIG_ZONES, "go:cfgz");
        if (gState.suspended)
            add_item(TXT_MB_RESUME, "cancel_susp");
        else
            add_item(TXT_MB_SUSPEND, "dur_pick:suspend");
        add_item(TXT_BACK,        "go:main");
        break;

    case MenuID::CFG_CUSTOM: {
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        snprintf(lbuf, sizeof(lbuf), TXT_MB_FREQ_DAYS, cs.interval_days);
        add_item(lbuf, "dur_pick:freq");
        snprintf(lbuf, sizeof(lbuf), TXT_MB_CYCLES, cs.slot_count);
        add_item(lbuf, "dur_pick:cycles");
        for (int i = 0; i < cs.slot_count; i++) {
            snprintf(lbuf, sizeof(lbuf), TXT_MB_CYCLE_TIME, i + 1, cs.slots[i].hour, cs.slots[i].minute);
            char act[16];
            snprintf(act, sizeof(act), "time_edit:c%d", i);
            add_item(lbuf, act);
        }
        add_item(TXT_FINISH_BACK, "go:prog");
        break;
    }

    case MenuID::SETUP_CUSTOM: {
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        snprintf(lbuf, sizeof(lbuf), TXT_MB_FREQ_DAYS, cs.interval_days);
        add_item(lbuf, "dur_pick:freq");
        snprintf(lbuf, sizeof(lbuf), TXT_MB_CYCLES, cs.slot_count);
        add_item(lbuf, "dur_pick:cycles");
        for (int i = 0; i < cs.slot_count; i++) {
            snprintf(lbuf, sizeof(lbuf), TXT_MB_CYCLE_TIME, i + 1, cs.slots[i].hour, cs.slots[i].minute);
            char act[16];
            snprintf(act, sizeof(act), "time_edit:c%d", i);
            add_item(lbuf, act);
        }
        add_item(TXT_MB_ADVANCE, "setup_advance");
        add_item(TXT_BACK, "setup_back");
        break;
    }

    case MenuID::MODOS:
        snprintf(lbuf, sizeof(lbuf), TXT_MB_INTENSE,      mc(0)); add_item(lbuf, "sel:0");
        snprintf(lbuf, sizeof(lbuf), TXT_MB_MEDIUM,         mc(1)); add_item(lbuf, "sel:1");
        snprintf(lbuf, sizeof(lbuf), TXT_MB_WEAK,         mc(2)); add_item(lbuf, "sel:2");
        snprintf(lbuf, sizeof(lbuf), TXT_MB_DISABLED,    mc(3)); add_item(lbuf, "sel:3");
        snprintf(lbuf, sizeof(lbuf), TXT_MB_CUSTOM, mc(4)); add_item(lbuf, "sel:4");
        add_item(TXT_BACK, "go:prog");
        break;

    case MenuID::CFG_ZONAS:
        for (int i = 0; i < NUM_ZONES; i++) {
            Zone& z = gState.zones[i];
            if (z.enabled)
                snprintf(lbuf, sizeof(lbuf), TXT_MB_ZONE_ON, i+1, z.name, z.duration_min);
            else
                snprintf(lbuf, sizeof(lbuf), TXT_MB_ZONE_OFF, i+1, z.name);
            char act[12]; snprintf(act, sizeof(act), "cfgz:%d", i);
            add_item(lbuf, act);
        }
        add_item(TXT_BACK, "go:prog");
        break;

    case MenuID::CUSTOM_ZONAS:
        for (int i = 0; i < NUM_ZONES; i++) {
            snprintf(lbuf, sizeof(lbuf), TXT_MB_ZONE_CUSTOM,
                     gState.custom_sel[i] ? "[X]" : "[ ]",
                     i+1, gState.zones[i].name);
            char act[8]; snprintf(act, sizeof(act), "cz:%d", i);
            add_item(lbuf, act);
        }
        add_item(TXT_SET_DURATION, "dur_pick:custom");
        add_item(TXT_BACK,          "go:manual");
        break;

    case MenuID::HISTORICO: {
        static_assert(NUM_ZONES == 4, "History display hardcodes 4 zones");
        HistoryEntry entries[HISTORY_DISPLAY];
        uint8_t n = history.readLast(HISTORY_DISPLAY, entries);

        if (n == 0) {
            add_item(TXT_NO_RECORDS, "");
        } else {
            for (int8_t i = (int8_t)n - 1; i >= 0; i--) {
                const HistoryEntry& e = entries[i];
                static const char* MON[] = {
                    "", TXT_MONTH_JAN, TXT_MONTH_FEB, TXT_MONTH_MAR, TXT_MONTH_APR, TXT_MONTH_MAY, TXT_MONTH_JUN,
                    TXT_MONTH_JUL, TXT_MONTH_AUG, TXT_MONTH_SEP, TXT_MONTH_OCT, TXT_MONTH_NOV, TXT_MONTH_DEC
                };
                uint8_t m = (e.month <= 12) ? e.month : 0;
                snprintf(lbuf, sizeof(lbuf), "%02d%s %02d:%02d %s",
                         e.day, MON[m], e.hour, e.min,
                         e.trigger == WaterTrigger::AUTO ? "AUTO" :
                         (e.trigger == WaterTrigger::CUSTOM ? "CUSTOM" : "GERAL"));

                char l1[LCD_COLS+1], l2[LCD_COLS+1];
                snprintf(l1, sizeof(l1), TXT_MB_Z1_Z2, e.zone_dur[0], e.zone_dur[1]);
                snprintf(l2, sizeof(l2), TXT_MB_Z3_Z4, e.zone_dur[2], e.zone_dur[3]);

                char act[80];
                snprintf(act, sizeof(act),
                         "info:%02d/%02d %02d:%02d|%s|%s| |hist",
                         e.day, m, e.hour, e.min, l1, l2);
                add_item(lbuf, act);
            }
        }
        add_item(TXT_BACK, "go:def");
        break;
    }

    case MenuID::DEF: {
        add_item(TXT_TEST_ZONES,    "go:testes");
        add_item(TXT_HISTORY,       "go:hist");
        add_item(TXT_DATETIME,       "date_edit:rtc");
        add_item(TXT_ADVANCED_SETTINGS,  "go:def_avancado");
        add_item(TXT_BACK,       "go:main");
        break;
    }

    case MenuID::DEF_AVANCADO: {
        add_item(gState.auto_dst ? TXT_TZ_AUTO : TXT_TZ_FIXED, "toggle_dst");
        const char* blLabel;
        switch (gState.backlight_timeout_ms) {
            case 30000UL:              blLabel = TXT_BL_30S; break;
            case 60000UL:              blLabel = TXT_BL_1M; break;
            case 120000UL:             blLabel = TXT_BL_2M; break;
            case 300000UL:             blLabel = TXT_BL_5M; break;
            case BACKLIGHT_TIMEOUT_NEVER: blLabel = TXT_BL_ALWAYS; break;
            default:                   blLabel = TXT_BL_2M_DEF;  break;
        }
        add_item(blLabel,           "go:blsel");
        add_item(TXT_FIRMWARE_VER, "info:FIRMWARE|" FW_VERSION "|" FW_BUILD_DATE "|ESP32 rev1.0|def_avancado");
        add_item(TXT_FACTORY_RESET,   TXT_ACT_CONFIRM_RESET);
        add_item(TXT_BACK,       "go:def");
        break;
    }

    case MenuID::TESTES:
        add_item(TXT_TEST_ALL_5S, TXT_ACT_CONFIRM_TEST_ALL);
        for (int i = 0; i < NUM_ZONES; i++) {
            char safe_name[16];
            strncpy(safe_name, gState.zones[i].name, sizeof(safe_name));
            safe_name[sizeof(safe_name)-1] = '\0';
            for (int k = 0; safe_name[k]; k++) if (safe_name[k] == '|') safe_name[k] = '/';
            snprintf(lbuf, sizeof(lbuf), "Z%d %-8s   5s", i+1, safe_name);
            char act[80];
            snprintf(act, sizeof(act),
                     TXT_ACT_CONFIRM_TEST_ZONE,
                     i+1, safe_name, i);
            add_item(lbuf, act);
        }
        add_item(TXT_BACK, "go:def");
        break;

    case MenuID::SETUP_MODE:
        add_item(TXT_INTENSE,       "sel:0");
        add_item(TXT_MEDIUM,         "sel:1");
        add_item(TXT_WEAK,         "sel:2");
        add_item(TXT_CUSTOM, "sel:4");
        add_item(TXT_SKIP,      "setup_advance");
        add_item(TXT_BACK,     "setup_back");
        break;

    case MenuID::SETUP_ZONES:
        for (int i = 0; i < NUM_ZONES; i++) {
            Zone& z = gState.zones[i];
            if (z.enabled)
                snprintf(lbuf, sizeof(lbuf), TXT_MB_ZONE_ON, i+1, z.name, z.duration_min);
            else
                snprintf(lbuf, sizeof(lbuf), TXT_MB_ZONE_OFF, i+1, z.name);
            char act[12]; snprintf(act, sizeof(act), "cfgz:%d", i);
            add_item(lbuf, act);
        }
        add_item(TXT_FINISH,  "setup_advance");
        add_item(TXT_BACK,   "setup_back");
        break;

    case MenuID::BLSEL: {
        auto blmc = [&](uint32_t ms) -> const char* {
            return (gState.backlight_timeout_ms == ms) ? "[*]" : "[ ]";
        };
        snprintf(lbuf, sizeof(lbuf), TXT_MB_BL_30S, blmc(30000UL));
        add_item(lbuf, "bl:30000");
        snprintf(lbuf, sizeof(lbuf), TXT_MB_BL_1M,   blmc(60000UL));
        add_item(lbuf, "bl:60000");
        snprintf(lbuf, sizeof(lbuf), TXT_MB_BL_2M,  blmc(120000UL));
        add_item(lbuf, "bl:120000");
        snprintf(lbuf, sizeof(lbuf), TXT_MB_BL_5M,  blmc(300000UL));
        add_item(lbuf, "bl:300000");
        snprintf(lbuf, sizeof(lbuf), TXT_MB_BL_ALWAYS,     blmc(BACKLIGHT_TIMEOUT_NEVER));
        add_item(lbuf, "bl:4294967295");
        add_item(TXT_BACK, "go:def_avancado");
        break;
    }

    default:
        add_item(TXT_BACK, "go:main");
        break;
    }

    itemCount = (uint8_t)(it - items);
}

MenuID MenuBuilder::parseMenuID(const char* s) {
    if (!strcmp(s,"main"))   return MenuID::MAIN;
    if (!strcmp(s,"manual")) return MenuID::MANUAL;
    if (!strcmp(s,"prog"))   return MenuID::PROG;
    if (!strcmp(s,"modos"))  return MenuID::MODOS;
    if (!strcmp(s,"cfgz"))   return MenuID::CFG_ZONAS;
    if (!strcmp(s,"czonas")) return MenuID::CUSTOM_ZONAS;
    if (!strcmp(s,"ccustom")) return MenuID::CFG_CUSTOM;
    if (!strcmp(s,"hist"))   return MenuID::HISTORICO;
    if (!strcmp(s,"def"))    return MenuID::DEF;
    if (!strcmp(s,"def_avancado")) return MenuID::DEF_AVANCADO;
    if (!strcmp(s,"testes")) return MenuID::TESTES;
    if (!strcmp(s,"blsel"))  return MenuID::BLSEL;
    if (!strcmp(s,"smode"))  return MenuID::SETUP_MODE;
    if (!strcmp(s,"szones")) return MenuID::SETUP_ZONES;
    if (!strcmp(s,"scustom")) return MenuID::SETUP_CUSTOM;
    return MenuID::MAIN;
}

const char* MenuBuilder::modeName(uint8_t m) {
    switch ((AppMode)m) {
        case AppMode::INTENSO:       return TXT_INTENSE;
        case AppMode::MEDIO:         return TXT_MEDIUM;
        case AppMode::FRACO:         return TXT_WEAK;
        case AppMode::DESATIVADO:    return TXT_DISABLED;
        case AppMode::PERSONALIZADO: return TXT_CUSTOM;
        default:                     return "?";
    }
}

void MenuBuilder::modeHours(uint8_t m, char* dest, size_t maxLen) {
    if (m >= (uint8_t)AppMode::_COUNT) {
        snprintf(dest, maxLen, "?");
        return;
    }
    const ModeSchedule& sched = MODE_SCHEDULES[m];

    if (sched.slot_count == 0) {
        snprintf(dest, maxLen, "---");
        return;
    }

    size_t pos = 0;
    for (uint8_t i = 0; i < sched.slot_count && pos < maxLen - 1; i++) {
        if (i > 0) {
            int n = snprintf(dest + pos, maxLen - pos, "+");
            if (n > 0) pos += ((size_t)n < maxLen - pos) ? n : (maxLen - pos - 1);
        }
        if (pos < maxLen - 1) {
            int n = snprintf(dest + pos, maxLen - pos, "%02d:%02d", 
                             sched.slots[i].hour, sched.slots[i].minute);
            if (n > 0) pos += ((size_t)n < maxLen - pos) ? n : (maxLen - pos - 1);
        }
    }
}
