#pragma once
#include <Arduino.h>
#include "Display.h"
#include "Encoder.h"
#include "AppState.h"

// ─────────────────────────────────────────────────────────
// Screen — which logical screen is active
// ─────────────────────────────────────────────────────────
enum class Screen : uint8_t {
    IDLE,
    MENU,
    INFO,       // read-only info panel (4 lines)
    CONFIRM,    // yes/no prompt
    DONE,       // execution feedback
};

// ─────────────────────────────────────────────────────────
// MenuID — identifies each menu page
// ─────────────────────────────────────────────────────────
enum class MenuID : uint8_t {
    MAIN,
    MANUAL,
    PROG,
    MODOS,
    CFG_ZONAS,
    CUSTOM_ZONAS,
    CUSTOM_DUR,
    HISTORICO,
    DEF,
    TESTES,
    _COUNT
};

// ─────────────────────────────────────────────────────────
// MenuItem
// ─────────────────────────────────────────────────────────
// action  — compact encoded string:
//   "go:<MenuID>"      → navigate to menu
//   "go:idle"          → return to idle
//   "info:<l0>|<l1>…"  → show info panel
//   "confirm:<l1>|<l2>|<backMenu>" → show confirm dialog
//   "sel:<modeIdx>"    → select AppMode
//   "cfgz:<zoneIdx>"   → toggle/bump zone config
//   "cz:<zoneIdx>"     → toggle custom zone selection
//   "cdur:<minutes>"   → set custom duration & execute

struct MenuItem {
    char    label[21];   // fits one LCD row (20 chars + \0)
    char    action[48];
};

// ─────────────────────────────────────────────────────────
// UI class
// ─────────────────────────────────────────────────────────
class UI {
public:
    UI(Display& disp, Encoder& enc);

    void begin();
    void update();   // call every loop iteration

private:
    Display& _d;
    Encoder& _enc;

    // ── FSM state ─────────────────────────────────────────
    Screen  _screen;
    MenuID  _mid;
    uint8_t _cur;          // cursor index within current menu
    uint8_t _off;          // scroll offset
    MenuID  _backMenu;     // where CONFIRM/INFO/DONE returns to

    // Static panels (filled by _showInfo / _showConfirm)
    char _infoRows[4][LCD_COLS + 1];
    char _confirmRows[4][LCD_COLS + 1];

    // Idle timeout
    uint32_t _lastActivity;

    // ── Menu tables (rebuilt when state changes) ──────────
    // We use a flat array per menu.  Menus are rebuilt via
    // _buildMenu() only when needed — not every frame.
    static const uint8_t MAX_ITEMS = 12;
    MenuItem _items[MAX_ITEMS];
    uint8_t  _itemCount;

    void _buildMenu(MenuID mid);

    // ── Renderers ─────────────────────────────────────────
    void _renderIdle();
    void _renderMenu();
    void _renderInfo();
    void _renderConfirm();
    void _renderDone();

    // ── Input handlers ────────────────────────────────────
    void _handleRotation(int8_t dir);
    void _handleClick();

    // ── Action dispatcher ─────────────────────────────────
    void _dispatch(const char* action);

    // ── Navigation helpers ────────────────────────────────
    void _goMenu(MenuID mid);
    void _goIdle();
    void _showInfo(const char* l0, const char* l1,
                   const char* l2, const char* l3,
                   MenuID back);
    void _showConfirm(const char* l1, const char* l2, MenuID back);
    void _showDone(const char* l1, const char* l2);

    // ── Utilities ─────────────────────────────────────────
    static MenuID   _parseMenuID(const char* s);
    static AppMode  _parseMode(uint8_t idx);
    static const char* _modeName(AppMode m);
    static const char* _modeHours(AppMode m);
};
