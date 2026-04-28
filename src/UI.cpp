#include "UI.h"
#include <string.h>
#include <stdio.h>

// ─────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────
static void safe_copy(char* dst, const char* src, size_t max) {
    strncpy(dst, src, max - 1);
    dst[max - 1] = '\0';
}

// Fill a MenuItem in place
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
      _lastActivity(0), _itemCount(0)
{}

void UI::begin() {
    _lastActivity = millis();
    _renderIdle();
}

// ─────────────────────────────────────────────────────────
// update() — called every loop()
// ─────────────────────────────────────────────────────────
void UI::update() {
    int8_t rot   = _enc.getRotation();
    bool   click = _enc.getClick();

    if (rot != 0) { _lastActivity = millis(); _handleRotation(rot); }
    if (click)    { _lastActivity = millis(); _handleClick(); }

    // Idle timeout: return to idle screen after inactivity
    if (_screen != Screen::IDLE &&
        (millis() - _lastActivity) >= IDLE_TIMEOUT_MS) {
        _goIdle();
    }

    // Idle clock: refresh every second when nothing else changed
    static uint32_t _lastClockRefresh = 0;
    if (_screen == Screen::IDLE && (millis() - _lastClockRefresh) >= 1000) {
        _lastClockRefresh = millis();
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
            _cur = (_cur + dir + _itemCount) % _itemCount;
            _renderMenu();
            break;

        case Screen::INFO:
        case Screen::DONE:
        case Screen::CONFIRM:
            // Any rotation cancels / goes back
            _goMenu(_backMenu);
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
            // OK — render DONE feedback
            _showDone(_confirmRows[1], _confirmRows[2]);
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

void UI::_showConfirm(const char* l1, const char* l2, MenuID back) {
    char buf[LCD_COLS + 1];
    safe_copy(_confirmRows[0], _d.cx(buf, "-- CONFIRMAR? --"), LCD_COLS + 1);
    safe_copy(_confirmRows[1], _d.fx(buf, l1),                 LCD_COLS + 1);
    safe_copy(_confirmRows[2], _d.fx(buf, l2),                 LCD_COLS + 1);
    safe_copy(_confirmRows[3], _d.cx(buf, "[OK]      [Voltar]"), LCD_COLS + 1);
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

// ─────────────────────────────────────────────────────────
// Action dispatcher
// ─────────────────────────────────────────────────────────
void UI::_dispatch(const char* action) {
    // ── go:<menu|idle> ────────────────────────────────────
    if (strncmp(action, "go:", 3) == 0) {
        const char* target = action + 3;
        if (strcmp(target, "idle") == 0) { _goIdle(); return; }
        _goMenu(_parseMenuID(target));
        return;
    }

    // ── sel:<idx>  — change operating mode ───────────────
    if (strncmp(action, "sel:", 4) == 0) {
        uint8_t idx = (uint8_t)atoi(action + 4);
        if (idx < (uint8_t)AppMode::_COUNT)
            gState.mode = (AppMode)idx;
        _showInfo("MODO SELECIONADO", _modeName(gState.mode),
                  "", "Guardado!  OK", MenuID::MODOS);
        return;
    }

    // ── cfgz:<idx>  — cycle zone config ON→dur+→OFF ──────
    if (strncmp(action, "cfgz:", 5) == 0) {
        uint8_t i = (uint8_t)atoi(action + 5);
        Zone& z = gState.zones[i];
        if (!z.enabled) { z.enabled = true;  z.duration_min = 20; }
        else if (z.duration_min < 60)          z.duration_min += 10;
        else            { z.enabled = false; z.duration_min = 0;  }
        _buildMenu(MenuID::CFG_ZONAS);   // rebuild to reflect new state
        _renderMenu();
        return;
    }

    // ── cz:<idx>  — toggle custom zone selection ──────────
    if (strncmp(action, "cz:", 3) == 0) {
        uint8_t i = (uint8_t)atoi(action + 3);
        gState.custom_sel[i] = !gState.custom_sel[i];
        _buildMenu(MenuID::CUSTOM_ZONAS);
        _renderMenu();
        return;
    }

    // ── cdur:<min>  — set duration & fire custom run ──────
    if (strncmp(action, "cdur:", 5) == 0) {
        uint8_t dur = (uint8_t)atoi(action + 5);
        // Validate: at least one zone selected
        bool any = false;
        for (int i = 0; i < NUM_ZONES; i++) any |= gState.custom_sel[i];
        if (!any) {
            _showInfo("! ATENCAO !", "Seleciona pelo",
                      "menos 1 zona.", "", MenuID::CUSTOM_ZONAS);
            return;
        }
        gState.custom_dur_min = dur;
        // TODO: trigger watering scheduler
        char l1[LCD_COLS + 1] = "Zonas: ";
        for (int i = 0; i < NUM_ZONES; i++) {
            if (gState.custom_sel[i]) {
                char zid[4]; snprintf(zid, sizeof(zid), "Z%d ", i + 1);
                strncat(l1, zid, LCD_COLS - strlen(l1));
            }
        }
        char l2[LCD_COLS + 1];
        snprintf(l2, sizeof(l2), "Duracao: %d min", dur);
        _showDone(l1, l2);
        _backMenu = MenuID::MAIN;
        return;
    }

    // ── info:<l0>|<l1>|<l2>|<l3>|<back> ─────────────────
    if (strncmp(action, "info:", 5) == 0) {
        char buf[128]; safe_copy(buf, action + 5, sizeof(buf));
        char* parts[5] = { nullptr };
        uint8_t n = 0;
        char* tok = strtok(buf, "|");
        while (tok && n < 5) { parts[n++] = tok; tok = strtok(nullptr, "|"); }
        MenuID back = (n >= 5) ? _parseMenuID(parts[4]) : _mid;
        _showInfo(parts[0] ? parts[0] : "",
                  parts[1] ? parts[1] : "",
                  parts[2] ? parts[2] : "",
                  parts[3] ? parts[3] : "", back);
        return;
    }

    // ── confirm:<l1>|<l2>|<back> ─────────────────────────
    if (strncmp(action, "confirm:", 8) == 0) {
        char buf[96]; safe_copy(buf, action + 8, sizeof(buf));
        char* parts[3] = { nullptr };
        uint8_t n = 0;
        char* tok = strtok(buf, "|");
        while (tok && n < 3) { parts[n++] = tok; tok = strtok(nullptr, "|"); }
        MenuID back = (n >= 3) ? _parseMenuID(parts[2]) : _mid;
        _showConfirm(parts[0] ? parts[0] : "",
                     parts[1] ? parts[1] : "", back);
        return;
    }

    // ── horarios  — computed from live state ──────────────
    if (strcmp(action, "horarios") == 0) {
        char zonas_buf[LCD_COLS + 1] = "Zonas: ";
        for (int i = 0; i < NUM_ZONES; i++) {
            if (gState.zones[i].enabled) {
                char zid[4]; snprintf(zid, sizeof(zid), "Z%d ", i + 1);
                strncat(zonas_buf, zid, LCD_COLS - strlen(zonas_buf));
            }
        }
        char hora_buf[LCD_COLS + 1];
        snprintf(hora_buf, sizeof(hora_buf), "Hora: %s",
                 _modeHours(gState.mode));
        char ml[LCD_COLS + 1];
        snprintf(ml, sizeof(ml), "Modo: %s", _modeName(gState.mode));
        _showInfo("HORARIOS ATIVOS", ml, hora_buf, zonas_buf, MenuID::PROG);
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

    char lbuf[21];   // scratch for label building

    switch (mid) {

    case MenuID::MAIN:
        makeItem(it++, "Rega Manual",       "go:manual");
        makeItem(it++, "Programacao",       "go:prog");
        makeItem(it++, "Historico",         "go:hist");
        makeItem(it++, "Definicoes",        "go:def");
        makeItem(it++, "<- Voltar",         "go:idle");
        break;

    case MenuID::MANUAL:
        makeItem(it++, "Rega Geral",        "confirm:Iniciar rega com|params. atuais?|main");
        makeItem(it++, "Personalizado",     "go:czonas");
        makeItem(it++, "<- Voltar",         "go:main");
        break;

    case MenuID::PROG:
        makeItem(it++, "Ver Horarios",      "horarios");
        makeItem(it++, "Alterar Modo",      "go:modos");
        makeItem(it++, "Configurar Zonas",  "go:cfgz");
        makeItem(it++, "Suspender Rega",    "confirm:Suspender rega|por 3 dias?|prog");
        makeItem(it++, "<- Voltar",         "go:main");
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
                snprintf(lbuf, sizeof(lbuf), "[ON]  Z%d %-7s %2dmin",
                         i+1, z.name, z.duration_min);
            else
                snprintf(lbuf, sizeof(lbuf), "[OFF] Z%d %-7s  off",
                         i+1, z.name);
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
            char act[10]; snprintf(act, sizeof(act), "cz:%d", i);
            makeItem(it++, lbuf, act);
        }
        makeItem(it++, "-> Confirmar duracao", "go:cdur");
        makeItem(it++, "<- Voltar",            "go:manual");
        break;

    case MenuID::CUSTOM_DUR:
        { const uint8_t opts[] = { 5,10,15,20,25,30,40,60 };
          for (uint8_t d : opts) {
              snprintf(lbuf, sizeof(lbuf), "  %2d min", d);
              char act[12]; snprintf(act, sizeof(act), "cdur:%d", d);
              makeItem(it++, lbuf, act);
          }
          makeItem(it++, "<- Voltar", "go:czonas");
        }
        break;

    case MenuID::HISTORICO:
        // TODO: pull from NVS / RTC log; static placeholders for now
        makeItem(it++, "26Abr  Z1-3  88min",
                 "info:26 ABR  18:02|Z1 Jardim:  30min|Z2 Horta:   30min|Z3 Relvado: 28min|hist");
        makeItem(it++, "25Abr  Z1-3  85min",
                 "info:25 ABR  18:01|Z1 Jardim:  30min|Z2 Horta:   28min|Z3 Relvado: 27min|hist");
        makeItem(it++, "24Abr  Z1-3  87min",
                 "info:24 ABR  07:00|Z1 Jardim:  29min|Z2 Horta:   30min|Z3 Relvado: 28min|hist");
        makeItem(it++, "<- Voltar", "go:main");
        break;

    case MenuID::DEF:
        makeItem(it++, "Testar Zonas",     "go:testes");
        makeItem(it++, "Acertar Hora",     "info:HORA / DATA|14:32   27/04/2026||Em desenvolvimento|def");
        makeItem(it++, "Brilho LCD",       "info:BRILHO LCD|Nivel atual:  80%|Rode p/ ajustar|Click p/ guardar|def");
        makeItem(it++, "Versao Firmware",  "info:FIRMWARE|" FW_VERSION "|" FW_BUILD_DATE "|ESP32 rev1.0|def");
        makeItem(it++, "Reset Fabrica",    "confirm:Apagar TODAS as|definicoes?|def");
        makeItem(it++, "<- Voltar",        "go:main");
        break;

    case MenuID::TESTES:
        makeItem(it++, "Testar Todas (5s)", "confirm:Testar todas as|zonas, 5s cada?|testes");
        for (int i = 0; i < NUM_ZONES; i++) {
            snprintf(lbuf, sizeof(lbuf), "Z%d %-8s   5s", i+1, gState.zones[i].name);
            char act[48];
            snprintf(act, sizeof(act), "confirm:Ativar Z%d %s|por 5 segundos?|testes",
                     i+1, gState.zones[i].name);
            makeItem(it++, lbuf, act);
        }
        makeItem(it++, "<- Voltar", "go:def");
        break;

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
    char b0[LCD_COLS+1], b2[LCD_COLS+1], b3[LCD_COLS+1];

    // Row 0: time (centred)
    // TODO: replace with RTC once clock module is added
    unsigned long sec = millis() / 1000;
    unsigned long mm  = (sec / 60) % 60;
    unsigned long hh  = (sec / 3600) % 24;
    char time_str[6];
    snprintf(time_str, sizeof(time_str), "%02lu:%02lu", hh, mm);
    _d.cx(b0, time_str);

    // Row 2: active zone count
    uint8_t active = 0;
    for (int i = 0; i < NUM_ZONES; i++) active += gState.zones[i].enabled;

    if (gState.watering.active) {
        char zname[LCD_COLS+1];
        snprintf(zname, sizeof(zname), "  Z%d %s:",
                 gState.watering.zone_idx + 1,
                 gState.zones[gState.watering.zone_idx].name);
        char pb[LCD_COLS+1];
        _d.pbar(pb, gState.watering.progress_pct);
        _d.setRows(b0, nullptr, zname, pb);
    } else {
        char z_str[LCD_COLS+1];
        snprintf(z_str, sizeof(z_str), "%d zonas ativas", active);
        char nx_str[LCD_COLS+1];
        snprintf(nx_str, sizeof(nx_str), "Prox: %02d:%02d",
                 gState.next_hour, gState.next_min);
        _d.setRows(b0, nullptr, _d.cx(b2, z_str), _d.cx(b3, nx_str));
    }
}

void UI::_renderMenu() {
    // Scroll window
    if (_cur < _off)               _off = _cur;
    if (_cur >= _off + MENU_VISIBLE) _off = _cur - MENU_VISIBLE + 1;

    char hbuf[LCD_COLS+1];
    char rows[MENU_VISIBLE][LCD_COLS+1];

    // Build menu name for header from MenuID
    const char* titles[] = {
        "MENU","REGA MANUAL","PROGRAMACAO","ALTERAR MODO",
        "CONFIG ZONAS","SELEC. ZONAS","DURACAO/ZONA",
        "HISTORICO","DEFINICOES","TESTAR ZONAS"
    };
    _d.hdr(hbuf, titles[(uint8_t)_mid]);

    for (uint8_t i = 0; i < MENU_VISIBLE; i++) {
        uint8_t idx = _off + i;
        if (idx < _itemCount) {
            char line[LCD_COLS+1];
            snprintf(line, sizeof(line), "%c%s",
                     (idx == _cur ? '>' : ' '), _items[idx].label);
            _d.fx(rows[i], line);
        } else {
            _d.fx(rows[i], "");
        }
    }

    _d.setRows(hbuf, rows[0], rows[1], rows[2]);
}

void UI::_renderInfo() {
    _d.setRows(_infoRows[0], _infoRows[1], _infoRows[2], _infoRows[3]);
}

void UI::_renderConfirm() {
    _d.setRows(_confirmRows[0], _confirmRows[1],
               _confirmRows[2], _confirmRows[3]);
}

void UI::_renderDone() {
    _d.setRows(_infoRows[0], _infoRows[1], _infoRows[2], _infoRows[3]);
}

// ─────────────────────────────────────────────────────────
// Static utilities
// ─────────────────────────────────────────────────────────
MenuID UI::_parseMenuID(const char* s) {
    if (!s) return MenuID::MAIN;
    if (strcmp(s, "main")   == 0) return MenuID::MAIN;
    if (strcmp(s, "manual") == 0) return MenuID::MANUAL;
    if (strcmp(s, "prog")   == 0) return MenuID::PROG;
    if (strcmp(s, "modos")  == 0) return MenuID::MODOS;
    if (strcmp(s, "cfgz")   == 0) return MenuID::CFG_ZONAS;
    if (strcmp(s, "czonas") == 0) return MenuID::CUSTOM_ZONAS;
    if (strcmp(s, "cdur")   == 0) return MenuID::CUSTOM_DUR;
    if (strcmp(s, "hist")   == 0) return MenuID::HISTORICO;
    if (strcmp(s, "def")    == 0) return MenuID::DEF;
    if (strcmp(s, "testes") == 0) return MenuID::TESTES;
    return MenuID::MAIN;
}

AppMode UI::_parseMode(uint8_t idx) {
    if (idx < (uint8_t)AppMode::_COUNT) return (AppMode)idx;
    return AppMode::MEDIO;
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
    switch (m) {
        case AppMode::INTENSO:    return "07:00 + 18:00";
        case AppMode::MEDIO:      return "18:00";
        case AppMode::FRACO:      return "18:00 (x2/sem)";
        case AppMode::DESATIVADO: return "---";
        default:                  return "definido";
    }
}
