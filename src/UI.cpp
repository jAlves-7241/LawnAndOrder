#include "UI.h"
#include "WateringController.h"
#include "RTClock.h"
#include "Scheduler.h"
#include <string.h>
#include <stdio.h>

// ─────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────
static void safe_copy(char* dst, const char* src, size_t max) {
    strncpy(dst, src, max - 1);
    dst[max - 1] = '\0';
}

static void makeItem(MenuItem* it, const char* label, const char* action) {
    safe_copy(it->label,  label,  sizeof(it->label));
    safe_copy(it->action, action, sizeof(it->action));
}

// ─────────────────────────────────────────────────────────
// Constructor / begin
// ─────────────────────────────────────────────────────────
UI::UI(Display& disp, Encoder& enc)
    : _d(disp), _enc(enc),
      _screen(Screen::IDLE), _mid(MenuID::MAIN),
      _cur(0), _off(0), _backMenu(MenuID::MAIN),
      _durValue(5), _durContext(DurContext::CUSTOM_RUN), _durZoneIdx(0),
      _teHour(0), _teMin(0), _teField(0),
      _lastActivity(0), _itemCount(0)
{
    _pendingConfirmTag[0] = '\0';
}

void UI::begin() {
    _lastActivity = millis();
    _renderIdle();
}

// ─────────────────────────────────────────────────────────
// update()
// ─────────────────────────────────────────────────────────
void UI::update() {
    int8_t rot   = _enc.getRotation();
    bool   click = _enc.getClick();

    bool hadInput = (rot != 0 || click);

    // Any input wakes the backlight first; if it was off, swallow the input
    // so the user doesn't accidentally trigger an action while waking.
    if (hadInput && !_d.isBacklightOn()) {
        _d.backlightOn();
        _lastActivity = millis();
        _renderIdle();   // repaint immediately after wake
        return;          // swallow input — next press will act normally
    }

    if (rot != 0) { _lastActivity = millis(); _handleRotation(rot); }
    if (click)    { _lastActivity = millis(); _handleClick(); }

    // Idle timeout — TIME_EDIT is excluded so an in-progress edit isn't lost
    if (_screen != Screen::IDLE && _screen != Screen::TIME_EDIT &&
        (millis() - _lastActivity) >= IDLE_TIMEOUT_MS) {
        _goIdle();
    }

    // Backlight sleep after inactivity (only from idle screen)
    if (_screen == Screen::IDLE && _d.isBacklightOn() &&
        gState.backlight_timeout_ms != BACKLIGHT_TIMEOUT_NEVER) {
        if ((millis() - _lastActivity) >= gState.backlight_timeout_ms) {
            _d.backlightOff();
        }
    }

    // Clock refresh: repaint idle once per second (only when backlight is on)
    static uint32_t lastClockMs = 0;
    if (_screen == Screen::IDLE && _d.isBacklightOn() &&
        (millis() - lastClockMs) >= 1000) {
        lastClockMs = millis();
        _renderIdle();
    }
}

// ─────────────────────────────────────────────────────────
// Input handlers
// ─────────────────────────────────────────────────────────
void UI::_handleRotation(int8_t dir) {
    switch (_screen) {
        case Screen::IDLE:
            _goMenu(MenuID::MAIN);
            break;

        case Screen::MENU:
            if (_itemCount == 0) break;  // guard: empty menu
            _cur = (uint8_t)((_cur + dir + _itemCount) % _itemCount);
            _renderMenu();
            break;

        case Screen::INFO:
        case Screen::DONE:
        case Screen::CONFIRM:
            _goMenu(_backMenu);
            break;

        case Screen::DUR_PICK: {
            int vmin = (_durContext == DurContext::CFG_ZONE) ? 0 : 1;
            int v    = (int)_durValue + dir;
            if (v < vmin) v = vmin;
            if (v > 20)   v = 20;
            _durValue = (uint8_t)v;
            _renderDurPick();
            break;
        }

        case Screen::TIME_EDIT:
            if (_teField == 0) {
                // Editing hour: 0–23, wraps
                _teHour = (uint8_t)((_teHour + dir + 24) % 24);
            } else {
                // Editing minute: 0–59, wraps
                _teMin = (uint8_t)((_teMin + dir + 60) % 60);
            }
            _renderTimeEdit();
            break;
    }
}

void UI::_handleClick() {
    switch (_screen) {
        case Screen::IDLE:
            _goMenu(MenuID::MAIN);
            break;

        case Screen::MENU:
            if (_cur < _itemCount)
                _dispatch(_items[_cur].action);
            break;

        case Screen::INFO:
        case Screen::DONE:
            _goMenu(_backMenu);
            break;

        case Screen::CONFIRM:
            _executeConfirmed();
            _showDone(_confirmRows[1], _confirmRows[2]);
            break;

        case Screen::DUR_PICK:
            _commitDurPick();
            break;

        case Screen::TIME_EDIT:
            _commitTimeEdit();
            break;
    }
}

// ─────────────────────────────────────────────────────────
// Navigation helpers
// ─────────────────────────────────────────────────────────
void UI::_goMenu(MenuID mid) {
    _screen = Screen::MENU;
    _mid    = mid;
    _cur    = 0;
    _off    = 0;
    _buildMenu(mid);
    _renderMenu();
}

void UI::_goIdle() {
    _screen = Screen::IDLE;
    _renderIdle();
}

void UI::_showInfo(const char* l0, const char* l1,
                   const char* l2, const char* l3, MenuID back) {
    char buf[LCD_COLS + 1];
    safe_copy(_infoRows[0], _d.cx(buf, l0), LCD_COLS + 1);
    safe_copy(_infoRows[1], _d.fx(buf, l1), LCD_COLS + 1);
    safe_copy(_infoRows[2], _d.fx(buf, l2), LCD_COLS + 1);
    safe_copy(_infoRows[3], _d.fx(buf, l3), LCD_COLS + 1);
    _backMenu = back;
    _screen   = Screen::INFO;
    _renderInfo();
}

void UI::_showConfirm(const char* l1, const char* l2,
                      MenuID back, const char* tag) {
    char buf[LCD_COLS + 1];
    safe_copy(_confirmRows[0], _d.cx(buf, "-- CONFIRMAR? --"),   LCD_COLS + 1);
    safe_copy(_confirmRows[1], _d.fx(buf, l1),                   LCD_COLS + 1);
    safe_copy(_confirmRows[2], _d.fx(buf, l2),                   LCD_COLS + 1);
    safe_copy(_confirmRows[3], _d.cx(buf, "[OK]      [Voltar]"), LCD_COLS + 1);
    safe_copy(_pendingConfirmTag, tag, sizeof(_pendingConfirmTag));
    _backMenu = back;
    _screen   = Screen::CONFIRM;
    _renderConfirm();
}

void UI::_showDone(const char* l1, const char* l2) {
    char buf[LCD_COLS + 1];
    safe_copy(_infoRows[0], _d.cx(buf, "-- EXECUTADO --"),  LCD_COLS + 1);
    safe_copy(_infoRows[1], _d.fx(buf, l1),                 LCD_COLS + 1);
    safe_copy(_infoRows[2], _d.fx(buf, l2),                 LCD_COLS + 1);
    safe_copy(_infoRows[3], _d.cx(buf, "Click p/ voltar"),  LCD_COLS + 1);
    _screen = Screen::DONE;
    _renderDone();
}

void UI::_showDurPick(uint8_t initial, DurContext ctx, uint8_t zoneIdx) {
    // CFG_ZONE allows 0 (= disable); clamp accordingly
    if (ctx == DurContext::CFG_ZONE)
        _durValue = (initial <= 20) ? initial : 5;
    else
        _durValue = (initial >= 1 && initial <= 20) ? initial : 5;
    _durContext  = ctx;
    _durZoneIdx  = zoneIdx;
    _screen      = Screen::DUR_PICK;
    _renderDurPick();
}

void UI::_commitDurPick() {
    if (_durContext == DurContext::CFG_ZONE) {
        if (_durValue == 0) {
            gState.zones[_durZoneIdx].enabled      = false;
            gState.zones[_durZoneIdx].duration_min = 0;
            Serial.printf("[UI] Z%d desativada\n", _durZoneIdx + 1);
        } else {
            gState.zones[_durZoneIdx].enabled      = true;
            gState.zones[_durZoneIdx].duration_min = _durValue;
            Serial.printf("[UI] Z%d dur=%d min\n", _durZoneIdx + 1, _durValue);
        }
        _goMenu(MenuID::CFG_ZONAS);
        return;
    }

    // CUSTOM_RUN
    gState.custom_dur_min = _durValue;
    char zstr[LCD_COLS + 1] = "Zonas: ";
    bool any = false;
    for (int i = 0; i < NUM_ZONES; i++) {
        if (gState.custom_sel[i]) {
            char zid[4]; snprintf(zid, sizeof(zid), "Z%d ", i + 1);
            strncat(zstr, zid, LCD_COLS - strlen(zstr));
            any = true;
        }
    }
    if (!any) {
        _showInfo("! ATENCAO !", "Seleciona pelo",
                  "menos 1 zona.", "", MenuID::CUSTOM_ZONAS);
        return;
    }
    char dstr[LCD_COLS + 1];
    snprintf(dstr, sizeof(dstr), "Duracao: %d min", _durValue);
    _showConfirm(zstr, dstr, MenuID::MANUAL, "custom");
}

void UI::_showTimeEdit() {
    // Seed editor with current RTC time (or 00:00 if RTC not valid)
    _teHour  = gState.rtc_valid ? gState.now.hour : 0;
    _teMin   = gState.rtc_valid ? gState.now.min  : 0;
    _teField = 0;  // start on hour
    _backMenu = MenuID::DEF;
    _screen   = Screen::TIME_EDIT;
    _renderTimeEdit();
}

// Click cycles: hour → minute → commit & save
void UI::_commitTimeEdit() {
    if (_teField == 0) {
        _teField = 1;  // move to minute
        _renderTimeEdit();
    } else {
        // Save to RTC
        if (!rtclock.isValid()) {
            _showInfo("! SEM RTC !", "Modulo nao",
                      "encontrado.", "", MenuID::DEF);
            return;
        }
        rtclock.setTime(_teHour, _teMin);
        char saved[LCD_COLS + 1];
        snprintf(saved, sizeof(saved), "Hora: %02d:%02d", _teHour, _teMin);
        _showDone(saved, "RTC actualizado!");
        _backMenu = MenuID::DEF;
    }
}

// ─────────────────────────────────────────────────────────
// _executeConfirmed — real-world side-effects on OK
// ─────────────────────────────────────────────────────────
void UI::_executeConfirmed() {
    const char* tag = _pendingConfirmTag;

    if (strcmp(tag, "general") == 0) {
        wateringCtrl.startGeneral();

    } else if (strcmp(tag, "custom") == 0) {
        wateringCtrl.startCustom(gState.custom_sel, gState.custom_dur_min);

    } else if (strcmp(tag, "suspend") == 0) {
        gState.suspended = true;
        Serial.println("[UI] rega suspensa 3 dias");

    } else if (strcmp(tag, "reset") == 0) {
        initAppState();
        Serial.println("[UI] reset de fabrica");

    } else if (strcmp(tag, "test_all") == 0) {
        wateringCtrl.startTest(-1);

    } else if (strncmp(tag, "test_", 5) == 0) {
        int8_t z = (int8_t)atoi(tag + 5);
        wateringCtrl.startTest(z);
    }
    // tag == "" → no side-effect needed
}

// ─────────────────────────────────────────────────────────
// Action dispatcher
// ─────────────────────────────────────────────────────────
void UI::_dispatch(const char* action) {

    // go:<menu|idle>
    if (strncmp(action, "go:", 3) == 0) {
        const char* t = action + 3;
        if (strcmp(t, "idle") == 0) _goIdle();
        else                        _goMenu(_parseMenuID(t));
        return;
    }

    // sel:<idx> — change AppMode
    if (strncmp(action, "sel:", 4) == 0) {
        uint8_t idx = (uint8_t)atoi(action + 4);
        if (idx < (uint8_t)AppMode::_COUNT)
            gState.mode = (AppMode)idx;
        scheduler.onModeChanged();   // recomputes next_hour / next_min immediately
        _showInfo("MODO SELECIONADO", _modeName(gState.mode),
                  "", "Guardado!  OK", MenuID::MODOS);
        return;
    }

    // cfgz:<idx> — open DUR_PICK for zone configuration
    if (strncmp(action, "cfgz:", 5) == 0) {
        uint8_t i = (uint8_t)atoi(action + 5);
        Zone& z = gState.zones[i];
        if (!z.enabled) {
            z.enabled      = true;
            z.duration_min = 5;
        }
        _showDurPick(z.duration_min, DurContext::CFG_ZONE, i);
        return;
    }

    // cz:<idx> — toggle custom zone selection
    if (strncmp(action, "cz:", 3) == 0) {
        uint8_t i = (uint8_t)atoi(action + 3);
        gState.custom_sel[i] = !gState.custom_sel[i];
        _buildMenu(MenuID::CUSTOM_ZONAS);
        _renderMenu();
        return;
    }

    // dur_pick:custom — open DUR_PICK for a custom manual run
    if (strcmp(action, "dur_pick:custom") == 0) {
        bool any = false;
        for (int i = 0; i < NUM_ZONES; i++) any |= gState.custom_sel[i];
        if (!any) {
            _showInfo("! ATENCAO !", "Seleciona pelo",
                      "menos 1 zona.", "", MenuID::CUSTOM_ZONAS);
            return;
        }
        _showDurPick(gState.custom_dur_min, DurContext::CUSTOM_RUN);
        return;
    }

    // time_edit — open the clock editor
    if (strcmp(action, "time_edit") == 0) {
        _showTimeEdit();
        return;
    }

    // info:<l0>|<l1>|<l2>|<l3>|<back>
    if (strncmp(action, "info:", 5) == 0) {
        char buf[128]; safe_copy(buf, action + 5, sizeof(buf));
        char* p[5] = {};
        uint8_t n = 0;
        char* tok = strtok(buf, "|");
        while (tok && n < 5) { p[n++] = tok; tok = strtok(nullptr, "|"); }
        MenuID back = (n >= 5) ? _parseMenuID(p[4]) : _mid;
        _showInfo(p[0]?p[0]:"", p[1]?p[1]:"",
                  p[2]?p[2]:"", p[3]?p[3]:"", back);
        return;
    }

    // confirm:<l1>|<l2>|<back>|<tag>
    if (strncmp(action, "confirm:", 8) == 0) {
        char buf[128]; safe_copy(buf, action + 8, sizeof(buf));
        char* p[4] = {};
        uint8_t n = 0;
        char* tok = strtok(buf, "|");
        while (tok && n < 4) { p[n++] = tok; tok = strtok(nullptr, "|"); }
        MenuID back     = (n >= 3) ? _parseMenuID(p[2]) : _mid;
        const char* tag = (n >= 4) ? p[3] : "";
        _showConfirm(p[0]?p[0]:"", p[1]?p[1]:"", back, tag);
        return;
    }

    // bl:<ms> — set backlight timeout
    if (strncmp(action, "bl:", 3) == 0) {
        uint32_t ms = (uint32_t)strtoul(action + 3, nullptr, 10);
        gState.backlight_timeout_ms = ms;
        _d.backlightOn();            // wake if needed
        _lastActivity = millis();    // reset so it doesn't immediately sleep
        _buildMenu(MenuID::BLSEL);   // rebuild to update [*] marker
        _renderMenu();
        return;
    }

    // horarios — computed from live state
    if (strcmp(action, "horarios") == 0) {
        char zstr[LCD_COLS + 1] = "Zonas: ";
        for (int i = 0; i < NUM_ZONES; i++) {
            if (gState.zones[i].enabled) {
                char zid[4]; snprintf(zid, sizeof(zid), "Z%d ", i + 1);
                strncat(zstr, zid, LCD_COLS - strlen(zstr));
            }
        }
        char mstr[LCD_COLS + 1];
        snprintf(mstr, sizeof(mstr), "Modo: %s", _modeName(gState.mode));
        char hstr[LCD_COLS + 1];
        snprintf(hstr, sizeof(hstr), "Hora: %s", _modeHours(gState.mode));
        _showInfo("HORARIOS ATIVOS", mstr, hstr, zstr, MenuID::PROG);
        return;
    }
}

// ─────────────────────────────────────────────────────────
// Menu builder
// ─────────────────────────────────────────────────────────
void UI::_buildMenu(MenuID mid) {
    _mid       = mid;
    _itemCount = 0;
    MenuItem* it = _items;

    auto mc = [&](uint8_t i) -> const char* {
        return ((uint8_t)gState.mode == i) ? "[*]" : "[ ]";
    };

    char lbuf[21];

    switch (mid) {

    case MenuID::MAIN:
        makeItem(it++, "Rega Manual",       "go:manual");
        makeItem(it++, "Programacao",       "go:prog");
        makeItem(it++, "Definicoes",        "go:def");
        makeItem(it++, "<- Voltar",         "go:idle");
        break;

    case MenuID::MANUAL:
        makeItem(it++, "Rega Geral",    "confirm:Iniciar rega com|params. atuais?|main|general");
        makeItem(it++, "Personalizado", "go:czonas");
        makeItem(it++, "<- Voltar",     "go:main");
        break;

    case MenuID::PROG:
        makeItem(it++, "Ver Horarios",     "horarios");
        makeItem(it++, "Alterar Modo",     "go:modos");
        makeItem(it++, "Configurar Zonas", "go:cfgz");
        makeItem(it++, "Suspender Rega",   "confirm:Suspender rega|por 3 dias?|prog|suspend");
        makeItem(it++, "<- Voltar",        "go:main");
        break;

    case MenuID::MODOS:
        snprintf(lbuf, sizeof(lbuf), "%s Intenso",      mc(0)); makeItem(it++, lbuf, "sel:0");
        snprintf(lbuf, sizeof(lbuf), "%s Medio",         mc(1)); makeItem(it++, lbuf, "sel:1");
        snprintf(lbuf, sizeof(lbuf), "%s Fraco",         mc(2)); makeItem(it++, lbuf, "sel:2");
        snprintf(lbuf, sizeof(lbuf), "%s Desativado",    mc(3)); makeItem(it++, lbuf, "sel:3");
        snprintf(lbuf, sizeof(lbuf), "%s Personalizado", mc(4)); makeItem(it++, lbuf, "sel:4");
        makeItem(it++, "<- Voltar", "go:prog");
        break;

    case MenuID::CFG_ZONAS:
        for (int i = 0; i < NUM_ZONES; i++) {
            Zone& z = gState.zones[i];
            if (z.enabled)
                snprintf(lbuf, sizeof(lbuf), "[ON]  Z%d %-6s %2dmin",
                         i+1, z.name, z.duration_min);
            else
                snprintf(lbuf, sizeof(lbuf), "[OFF] Z%d %-10s", i+1, z.name);
            char act[12]; snprintf(act, sizeof(act), "cfgz:%d", i);
            makeItem(it++, lbuf, act);
        }
        makeItem(it++, "<- Voltar", "go:prog");
        break;

    case MenuID::CUSTOM_ZONAS:
        for (int i = 0; i < NUM_ZONES; i++) {
            snprintf(lbuf, sizeof(lbuf), "%s Z%d %s",
                     gState.custom_sel[i] ? "[X]" : "[ ]",
                     i+1, gState.zones[i].name);
            char act[8]; snprintf(act, sizeof(act), "cz:%d", i);
            makeItem(it++, lbuf, act);
        }
        makeItem(it++, "-> Definir duracao", "dur_pick:custom");
        makeItem(it++, "<- Voltar",          "go:manual");
        break;

    case MenuID::HISTORICO:
        // TODO: replace with entries read from LittleFS CSV once storage module is added
        makeItem(it++, "26Abr  Z1-4  50min",
                 "info:26 ABR  18:02|Z1+2: 15+15min|Z3+4: 10+10min||hist");
        makeItem(it++, "25Abr  Z1-4  48min",
                 "info:25 ABR  18:01|Z1+2: 15+13min|Z3+4: 10+10min||hist");
        makeItem(it++, "24Abr  Z1-4  50min",
                 "info:24 ABR  07:00|Z1+2: 15+15min|Z3+4: 10+10min||hist");
        makeItem(it++, "<- Voltar", "go:def");
        break;

    case MenuID::DEF: {
        makeItem(it++, "Testar Zonas",    "go:testes");
        makeItem(it++, "Historico",       "go:hist");
        makeItem(it++, "Acertar Hora",    "time_edit");
        // Backlight timeout — show current setting in label
        const char* blLabel;
        switch (gState.backlight_timeout_ms) {
            case 30000UL:              blLabel = "Ecra: 30 seg"; break;
            case 60000UL:              blLabel = "Ecra:  1 min"; break;
            case 120000UL:             blLabel = "Ecra:  2 min"; break;
            case 300000UL:             blLabel = "Ecra:  5 min"; break;
            case BACKLIGHT_TIMEOUT_NEVER: blLabel = "Ecra: sempre"; break;
            default:                   blLabel = "Ecra: 2 min";  break;
        }
        makeItem(it++, blLabel,           "go:blsel");
        makeItem(it++, "Versao Firmware", "info:FIRMWARE|" FW_VERSION "|" FW_BUILD_DATE "|ESP32 rev1.0|def");
        makeItem(it++, "Reset Fabrica",   "confirm:Apagar TODAS as|definicoes?|def|reset");
        makeItem(it++, "<- Voltar",       "go:main");
        break;
    }

    case MenuID::TESTES:
        makeItem(it++, "Testar Todas (5s)",
                 "confirm:Testar todas as|zonas, 5s cada?|testes|test_all");
        for (int i = 0; i < NUM_ZONES; i++) {
            snprintf(lbuf, sizeof(lbuf), "Z%d %-8s   5s", i+1, gState.zones[i].name);
            char act[64];
            snprintf(act, sizeof(act),
                     "confirm:Ativar Z%d %s|por 5 segundos?|testes|test_%d",
                     i+1, gState.zones[i].name, i);
            makeItem(it++, lbuf, act);
        }
        makeItem(it++, "<- Voltar", "go:def");
        break;

    case MenuID::BLSEL: {
        // Each item sets the timeout via "bl:<ms>" action.
        // Active option is marked with [*].
        auto blmc = [&](uint32_t ms) -> const char* {
            return (gState.backlight_timeout_ms == ms) ? "[*]" : "[ ]";
        };
        snprintf(lbuf, sizeof(lbuf), "%s 30 segundos", blmc(30000UL));
        makeItem(it++, lbuf, "bl:30000");
        snprintf(lbuf, sizeof(lbuf), "%s  1 minuto",   blmc(60000UL));
        makeItem(it++, lbuf, "bl:60000");
        snprintf(lbuf, sizeof(lbuf), "%s  2 minutos",  blmc(120000UL));
        makeItem(it++, lbuf, "bl:120000");
        snprintf(lbuf, sizeof(lbuf), "%s  5 minutos",  blmc(300000UL));
        makeItem(it++, lbuf, "bl:300000");
        snprintf(lbuf, sizeof(lbuf), "%s  Sempre",     blmc(BACKLIGHT_TIMEOUT_NEVER));
        makeItem(it++, lbuf, "bl:4294967295");
        makeItem(it++, "<- Voltar", "go:def");
        break;
    }

    default:
        makeItem(it++, "<- Voltar", "go:main");
        break;
    }

    _itemCount = (uint8_t)(it - _items);
}

// ─────────────────────────────────────────────────────────
// Renderers
// ─────────────────────────────────────────────────────────
void UI::_renderIdle() {
    char b0[LCD_COLS+1], b3[LCD_COLS+1];

    // Row 0: time from RTC (or warning if RTC not ready)
    if (gState.rtc_valid) {
        char tstr[6];
        snprintf(tstr, sizeof(tstr), "%02d:%02d",
                 gState.now.hour, gState.now.min);
        _d.cx(b0, tstr);
    } else {
        _d.cx(b0, "--:--");
    }

    if (gState.watering.active) {
        uint8_t zi = gState.watering.zone_idx;
        if (zi >= NUM_ZONES) zi = 0;  // bounds guard
        char zrow[LCD_COLS+1], pb[LCD_COLS+1];
        snprintf(zrow, sizeof(zrow), "  Z%d %s:", zi + 1, gState.zones[zi].name);
        _d.pbar(pb, gState.watering.progress_pct);
        _d.setRows(b0, "", zrow, pb);
    } else {
        // Row 3: next scheduled watering
        // Show RTC warning on row 3 if clock not set
        if (!gState.rtc_valid) {
            _d.setRows(b0, "", "", _d.cx(b3, "! Acertar hora !"));
        } else {
            char nxstr[LCD_COLS+1];
            snprintf(nxstr, sizeof(nxstr), "Prox: %02d:%02d",
                     gState.next_hour, gState.next_min);
            _d.setRows(b0, "", "", _d.cx(b3, nxstr));
        }
    }
}

void UI::_renderMenu() {
    if (_cur < _off)                 _off = _cur;
    if (_cur >= _off + MENU_VISIBLE) _off = _cur - MENU_VISIBLE + 1;

    auto menuTitle = [](MenuID m) -> const char* {
        switch (m) {
            case MenuID::MAIN:         return "MENU";
            case MenuID::MANUAL:       return "REGA MANUAL";
            case MenuID::PROG:         return "PROGRAMACAO";
            case MenuID::MODOS:        return "ALTERAR MODO";
            case MenuID::CFG_ZONAS:    return "CONFIG ZONAS";
            case MenuID::CUSTOM_ZONAS: return "SELEC. ZONAS";
            case MenuID::HISTORICO:    return "HISTORICO";
            case MenuID::DEF:          return "DEFINICOES";
            case MenuID::TESTES:       return "TESTAR ZONAS";
            case MenuID::BLSEL:        return "TEMPO ECRA";
            default:                   return "MENU";
        }
    };

    char hbuf[LCD_COLS+1];
    _d.hdr(hbuf, menuTitle(_mid));

    char rows[MENU_VISIBLE][LCD_COLS+1];
    for (uint8_t i = 0; i < MENU_VISIBLE; i++) {
        uint8_t idx = _off + i;
        if (idx < _itemCount) {
            char line[LCD_COLS+2];
            snprintf(line, sizeof(line), "%c%s",
                     idx == _cur ? '>' : ' ', _items[idx].label);
            _d.fx(rows[i], line);
        } else {
            _d.fx(rows[i], "");
        }
    }
    _d.setRows(hbuf, rows[0], rows[1], rows[2]);
}

void UI::_renderInfo()    { _d.setRows(_infoRows[0],    _infoRows[1],    _infoRows[2],    _infoRows[3]);    }
void UI::_renderConfirm() { _d.setRows(_confirmRows[0], _confirmRows[1], _confirmRows[2], _confirmRows[3]); }
void UI::_renderDone()    { _d.setRows(_infoRows[0],    _infoRows[1],    _infoRows[2],    _infoRows[3]);    }

void UI::_renderDurPick() {
    char hbuf[LCD_COLS+1], vbuf[LCD_COLS+1], h1[LCD_COLS+1], h2[LCD_COLS+1];

    if (_durContext == DurContext::CFG_ZONE) {
        char htxt[20];
        snprintf(htxt, sizeof(htxt), "DUR Z%d %s",
                 _durZoneIdx+1, gState.zones[_durZoneIdx].name);
        _d.hdr(hbuf, htxt);
    } else {
        _d.hdr(hbuf, "DURACAO / ZONA");
    }

    char vstr[10];
    if (_durContext == DurContext::CFG_ZONE && _durValue == 0)
        snprintf(vstr, sizeof(vstr), "OFF");
    else
        snprintf(vstr, sizeof(vstr), "%d min", _durValue);
    _d.cx(vbuf, vstr);

    _d.cx(h1, "rode p/ ajustar");
    _d.cx(h2, "click p/ confirmar");
    _d.setRows(hbuf, vbuf, h1, h2);
}

void UI::_renderTimeEdit() {
    char hbuf[LCD_COLS+1], vbuf[LCD_COLS+1], fbuf[LCD_COLS+1], hintbuf[LCD_COLS+1];

    _d.hdr(hbuf, "ACERTAR HORA");

    // Show HH:MM with brackets around the active field
    char vstr[12];
    if (_teField == 0)
        snprintf(vstr, sizeof(vstr), "[%02d] : %02d", _teHour, _teMin);
    else
        snprintf(vstr, sizeof(vstr), " %02d  :[%02d]", _teHour, _teMin);
    _d.cx(vbuf, vstr);

    // Field indicator
    _d.cx(fbuf, _teField == 0 ? "^ horas" : "^ minutos");

    // Context-sensitive hint
    if (_teField == 0)
        _d.cx(hintbuf, "click -> minutos");
    else
        _d.cx(hintbuf, "click -> guardar");

    _d.setRows(hbuf, vbuf, fbuf, hintbuf);
}

// ─────────────────────────────────────────────────────────
// Utilities
// ─────────────────────────────────────────────────────────
MenuID UI::_parseMenuID(const char* s) {
    if (!s)                  return MenuID::MAIN;
    if (!strcmp(s,"main"))   return MenuID::MAIN;
    if (!strcmp(s,"manual")) return MenuID::MANUAL;
    if (!strcmp(s,"prog"))   return MenuID::PROG;
    if (!strcmp(s,"modos"))  return MenuID::MODOS;
    if (!strcmp(s,"cfgz"))   return MenuID::CFG_ZONAS;
    if (!strcmp(s,"czonas")) return MenuID::CUSTOM_ZONAS;
    if (!strcmp(s,"hist"))   return MenuID::HISTORICO;
    if (!strcmp(s,"def"))    return MenuID::DEF;
    if (!strcmp(s,"testes")) return MenuID::TESTES;
    if (!strcmp(s,"blsel"))  return MenuID::BLSEL;
    return MenuID::MAIN;
}

const char* UI::_modeName(AppMode m) {
    switch (m) {
        case AppMode::INTENSO:       return "Intenso";
        case AppMode::MEDIO:         return "Medio";
        case AppMode::FRACO:         return "Fraco";
        case AppMode::DESATIVADO:    return "Desativado";
        case AppMode::PERSONALIZADO: return "Personalizado";
        default:                     return "?";
    }
}

const char* UI::_modeHours(AppMode m) {
    // Build display string from the live schedule table so it always
    // reflects the defines in config.h, even if they are changed.
    static char buf[16];
    const ModeSchedule& sched = MODE_SCHEDULES[(uint8_t)m];

    if (sched.slot_count == 0) return "---";

    if (sched.slot_count == 1) {
        snprintf(buf, sizeof(buf), "%02d:%02d",
                 sched.slots[0].hour, sched.slots[0].minute);
        if (sched.day_pattern == DayPattern::ODD_DAYS ||
            sched.day_pattern == DayPattern::EVEN_DAYS) {
            strncat(buf, " alt.", sizeof(buf) - strlen(buf) - 1);
        }
        return buf;
    }

    // Two slots
    snprintf(buf, sizeof(buf), "%02d:%02d+%02d:%02d",
             sched.slots[0].hour, sched.slots[0].minute,
             sched.slots[1].hour, sched.slots[1].minute);
    return buf;
}
