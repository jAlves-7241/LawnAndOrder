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
        add_item("Rega Manual",       "go:manual");
        add_item("Programacao",       "go:prog");
        add_item("Definicoes",        "go:def");
        add_item("<- Voltar",         "go:idle");
        break;

    case MenuID::MANUAL:
        add_item("Rega Geral",    "confirm:Iniciar rega com|params. atuais?|main|general");
        add_item("Personalizado", "go:czonas");
        add_item("<- Voltar",     "go:main");
        break;

    case MenuID::PROG:
        add_item("Ver Horarios",     "horarios");
        add_item("Alterar Modo",     "go:modos");
        if (gState.mode == AppMode::PERSONALIZADO) {
            add_item("Personalizar", "go:ccustom");
        }
        add_item("Configurar Zonas", "go:cfgz");
        if (gState.suspended)
            add_item("Retomar Rega", "cancel_susp");
        else
            add_item("Suspender Rega", "dur_pick:suspend");
        add_item("<- Voltar",        "go:main");
        break;

    case MenuID::CFG_CUSTOM: {
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        snprintf(lbuf, sizeof(lbuf), "Freq: %d dias", cs.interval_days);
        add_item(lbuf, "dur_pick:freq");
        snprintf(lbuf, sizeof(lbuf), "Ciclos: %d", cs.slot_count);
        add_item(lbuf, "dur_pick:cycles");
        for (int i = 0; i < cs.slot_count; i++) {
            snprintf(lbuf, sizeof(lbuf), "Ciclo %d: %02d:%02d", i + 1, cs.slots[i].hour, cs.slots[i].minute);
            char act[16];
            snprintf(act, sizeof(act), "time_edit:c%d", i);
            add_item(lbuf, act);
        }
        add_item("<- Terminar", "go:prog");
        break;
    }

    case MenuID::SETUP_CUSTOM: {
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        snprintf(lbuf, sizeof(lbuf), "Freq: %d dias", cs.interval_days);
        add_item(lbuf, "dur_pick:freq");
        snprintf(lbuf, sizeof(lbuf), "Ciclos: %d", cs.slot_count);
        add_item(lbuf, "dur_pick:cycles");
        for (int i = 0; i < cs.slot_count; i++) {
            snprintf(lbuf, sizeof(lbuf), "Ciclo %d: %02d:%02d", i + 1, cs.slots[i].hour, cs.slots[i].minute);
            char act[16];
            snprintf(act, sizeof(act), "time_edit:c%d", i);
            add_item(lbuf, act);
        }
        add_item("Avancar >", "setup_advance");
        add_item("<- Voltar", "setup_back");
        break;
    }

    case MenuID::MODOS:
        snprintf(lbuf, sizeof(lbuf), "%s Intenso",      mc(0)); add_item(lbuf, "sel:0");
        snprintf(lbuf, sizeof(lbuf), "%s Medio",         mc(1)); add_item(lbuf, "sel:1");
        snprintf(lbuf, sizeof(lbuf), "%s Fraco",         mc(2)); add_item(lbuf, "sel:2");
        snprintf(lbuf, sizeof(lbuf), "%s Desativado",    mc(3)); add_item(lbuf, "sel:3");
        snprintf(lbuf, sizeof(lbuf), "%s Personalizado", mc(4)); add_item(lbuf, "sel:4");
        add_item("<- Voltar", "go:prog");
        break;

    case MenuID::CFG_ZONAS:
        for (int i = 0; i < NUM_ZONES; i++) {
            Zone& z = gState.zones[i];
            if (z.enabled)
                snprintf(lbuf, sizeof(lbuf), "[ON] Z%d %-6.6s %2dmin", i+1, z.name, z.duration_min);
            else
                snprintf(lbuf, sizeof(lbuf), "[OFF] Z%d %-10s", i+1, z.name);
            char act[12]; snprintf(act, sizeof(act), "cfgz:%d", i);
            add_item(lbuf, act);
        }
        add_item("<- Voltar", "go:prog");
        break;

    case MenuID::CUSTOM_ZONAS:
        for (int i = 0; i < NUM_ZONES; i++) {
            snprintf(lbuf, sizeof(lbuf), "%s Z%d %s",
                     gState.custom_sel[i] ? "[X]" : "[ ]",
                     i+1, gState.zones[i].name);
            char act[8]; snprintf(act, sizeof(act), "cz:%d", i);
            add_item(lbuf, act);
        }
        add_item("-> Definir duracao", "dur_pick:custom");
        add_item("<- Voltar",          "go:manual");
        break;

    case MenuID::HISTORICO: {
        static_assert(NUM_ZONES == 4, "History display hardcodes 4 zones");
        HistoryEntry entries[HISTORY_DISPLAY];
        uint8_t n = history.readLast(HISTORY_DISPLAY, entries);

        if (n == 0) {
            add_item("Sem registos", "");
        } else {
            for (int8_t i = (int8_t)n - 1; i >= 0; i--) {
                const HistoryEntry& e = entries[i];
                static const char* MON[] = {
                    "","Jan","Fev","Mar","Abr","Mai","Jun",
                    "Jul","Ago","Set","Out","Nov","Dez"
                };
                uint8_t m = (e.month <= 12) ? e.month : 0;
                snprintf(lbuf, sizeof(lbuf), "%02d%s %02d:%02d %s",
                         e.day, MON[m], e.hour, e.min,
                         e.trigger == WaterTrigger::AUTO ? "AUTO" :
                         (e.trigger == WaterTrigger::CUSTOM ? "CUSTOM" : "GERAL"));

                char l1[LCD_COLS+1], l2[LCD_COLS+1];
                snprintf(l1, sizeof(l1), "Z1:%dmin  Z2:%dmin", e.zone_dur[0], e.zone_dur[1]);
                snprintf(l2, sizeof(l2), "Z3:%dmin  Z4:%dmin", e.zone_dur[2], e.zone_dur[3]);

                char act[80];
                snprintf(act, sizeof(act),
                         "info:%02d/%02d %02d:%02d|%s|%s| |hist",
                         e.day, m, e.hour, e.min, l1, l2);
                add_item(lbuf, act);
            }
        }
        add_item("<- Voltar", "go:def");
        break;
    }

    case MenuID::DEF: {
        add_item("Testar Zonas",    "go:testes");
        add_item("Historico",       "go:hist");
        add_item("Data/Hora",       "date_edit:rtc");
        add_item("Def. Avancadas",  "go:def_avancado");
        add_item("<- Voltar",       "go:main");
        break;
    }

    case MenuID::DEF_AVANCADO: {
        add_item(gState.auto_dst ? "Fuso Horario: Auto" : "Fuso Horario: Fixo", "toggle_dst");
        const char* blLabel;
        switch (gState.backlight_timeout_ms) {
            case 30000UL:              blLabel = "Ecra: 30 seg"; break;
            case 60000UL:              blLabel = "Ecra:  1 min"; break;
            case 120000UL:             blLabel = "Ecra:  2 min"; break;
            case 300000UL:             blLabel = "Ecra:  5 min"; break;
            case BACKLIGHT_TIMEOUT_NEVER: blLabel = "Ecra: sempre"; break;
            default:                   blLabel = "Ecra: 2 min";  break;
        }
        add_item(blLabel,           "go:blsel");
        add_item("Versao Firmware", "info:FIRMWARE|" FW_VERSION "|" FW_BUILD_DATE "|ESP32 rev1.0|def_avancado");
        add_item("Reset Fabrica",   "confirm:Apagar TODAS as|definicoes?|def_avancado|reset");
        add_item("<- Voltar",       "go:def");
        break;
    }

    case MenuID::TESTES:
        add_item("Testar Todas (5s)", "confirm:Testar todas as|zonas, 5s cada?|testes|test_all");
        for (int i = 0; i < NUM_ZONES; i++) {
            snprintf(lbuf, sizeof(lbuf), "Z%d %-8s   5s", i+1, gState.zones[i].name);
            char act[80];
            snprintf(act, sizeof(act),
                     "confirm:Ativar Z%d %s|por 5 segundos?|testes|test_%d",
                     i+1, gState.zones[i].name, i);
            add_item(lbuf, act);
        }
        add_item("<- Voltar", "go:def");
        break;

    case MenuID::SETUP_MODE:
        add_item("Intenso",       "sel:0");
        add_item("Medio",         "sel:1");
        add_item("Fraco",         "sel:2");
        add_item("Personalizado", "sel:4");
        add_item("Saltar >",      "setup_advance");
        add_item("<- Voltar",     "setup_back");
        break;

    case MenuID::SETUP_ZONES:
        for (int i = 0; i < NUM_ZONES; i++) {
            Zone& z = gState.zones[i];
            if (z.enabled)
                snprintf(lbuf, sizeof(lbuf), "[ON] Z%d %-6s %2dmin", i+1, z.name, z.duration_min);
            else
                snprintf(lbuf, sizeof(lbuf), "[OFF] Z%d %-10s", i+1, z.name);
            char act[12]; snprintf(act, sizeof(act), "cfgz:%d", i);
            add_item(lbuf, act);
        }
        add_item("Terminar >",  "setup_advance");
        add_item("<- Voltar",   "setup_back");
        break;

    case MenuID::BLSEL: {
        auto blmc = [&](uint32_t ms) -> const char* {
            return (gState.backlight_timeout_ms == ms) ? "[*]" : "[ ]";
        };
        snprintf(lbuf, sizeof(lbuf), "%s 30 segundos", blmc(30000UL));
        add_item(lbuf, "bl:30000");
        snprintf(lbuf, sizeof(lbuf), "%s  1 minuto",   blmc(60000UL));
        add_item(lbuf, "bl:60000");
        snprintf(lbuf, sizeof(lbuf), "%s  2 minutos",  blmc(120000UL));
        add_item(lbuf, "bl:120000");
        snprintf(lbuf, sizeof(lbuf), "%s  5 minutos",  blmc(300000UL));
        add_item(lbuf, "bl:300000");
        snprintf(lbuf, sizeof(lbuf), "%s  Sempre",     blmc(BACKLIGHT_TIMEOUT_NEVER));
        add_item(lbuf, "bl:4294967295");
        add_item("<- Voltar", "go:def_avancado");
        break;
    }

    default:
        add_item("<- Voltar", "go:main");
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
        case AppMode::INTENSO:       return "Intenso";
        case AppMode::MEDIO:         return "Medio";
        case AppMode::FRACO:         return "Fraco";
        case AppMode::DESATIVADO:    return "Desativado";
        case AppMode::PERSONALIZADO: return "Personalizado";
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
    for (uint8_t i = 0; i < sched.slot_count; i++) {
        if (i > 0) {
            int n = snprintf(dest + pos, maxLen - pos, "+");
            if (n > 0 && pos + n < maxLen) pos += n;
        }
        int n = snprintf(dest + pos, maxLen - pos, "%02d:%02d", 
                         sched.slots[i].hour, sched.slots[i].minute);
        if (n > 0 && pos + n < maxLen) pos += n;
    }
}
