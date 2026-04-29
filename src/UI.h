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
    DUR_PICK,   // encoder-driven duration picker (1–20 min, 1-min steps)
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
    HISTORICO,
    DEF,
    TESTES,
    _COUNT
};

// ─────────────────────────────────────────────────────────
// DurContext — what the duration picker commits to on click
// ─────────────────────────────────────────────────────────
enum class DurContext : uint8_t {
    CUSTOM_RUN,  // writes gState.custom_dur_min → confirm screen
    CFG_ZONE,    // writes gState.zones[zoneIdx].duration_min → back to CFG_ZONAS
};

// ─────────────────────────────────────────────────────────
// MenuItem
// ─────────────────────────────────────────────────────────
// Action encoding:
//   "go:<menuID>"                       navigate to menu
//   "go:idle"                           return to idle
//   "info:<l0>|<l1>|<l2>|<l3>|<back>"  info panel
//   "confirm:<l1>|<l2>|<back>|<tag>"    confirm dialog
//   "sel:<modeIdx>"                     select AppMode
//   "cfgz:<zoneIdx>"                    toggle zone / open DUR_PICK
//   "cz:<zoneIdx>"                      toggle custom zone selection
//   "dur_pick:custom"                   open DUR_PICK for custom run
//   "horarios"                          computed info screen

struct MenuItem {
    char label[21];   // 20 LCD chars + \0
    char action[56];
};

// ─────────────────────────────────────────────────────────
// UI
// ─────────────────────────────────────────────────────────
class UI {
public:
    UI(Display& disp, Encoder& enc);

    void begin();
    void update();  // call every loop() iteration

private:
    Display& _d;
    Encoder& _enc;

    // ── FSM ───────────────────────────────────────────────
    Screen  _screen;
    MenuID  _mid;
    uint8_t _cur;
    uint8_t _off;
    MenuID  _backMenu;

    // ── Static display buffers ────────────────────────────
    char _infoRows[4][LCD_COLS + 1];
    char _confirmRows[4][LCD_COLS + 1];

    // ── Duration picker ───────────────────────────────────
    uint8_t   _durValue;     // selected minutes 1–20
    DurContext _durContext;
    uint8_t   _durZoneIdx;   // relevant only for CFG_ZONE context

    // ── Confirm tag ───────────────────────────────────────
    // Keywords: "general" | "custom" | "test_all" | "test_N" |
    //           "suspend" | "reset"  | ""
    char _pendingConfirmTag[16];

    // ── Idle timeout ──────────────────────────────────────
    uint32_t _lastActivity;

    // ── Menu table ────────────────────────────────────────
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
    void _renderDurPick();

    // ── Input ─────────────────────────────────────────────
    void _handleRotation(int8_t dir);
    void _handleClick();

    // ── Dispatcher & navigation ───────────────────────────
    void _dispatch(const char* action);
    void _executeConfirmed();
    void _goMenu(MenuID mid);
    void _goIdle();
    void _showInfo(const char* l0, const char* l1,
                   const char* l2, const char* l3, MenuID back);
    void _showConfirm(const char* l1, const char* l2,
                      MenuID back, const char* tag = "");
    void _showDone(const char* l1, const char* l2);
    void _showDurPick(uint8_t initial, DurContext ctx,
                      uint8_t zoneIdx = 0);
    void _commitDurPick();   // called on click inside DUR_PICK

    // ── Utilities ─────────────────────────────────────────
    static MenuID      _parseMenuID(const char* s);
    static const char* _modeName(AppMode m);
    static const char* _modeHours(AppMode m);
};
