#pragma once
#include <Arduino.h>
#include "Display.h"
#include "Encoder.h"
#include "AppState.h"

// ─────────────────────────────────────────────────────────
// Screen
// ─────────────────────────────────────────────────────────
enum class Screen : uint8_t {
    IDLE,
    MENU,
    INFO,       // read-only 4-line panel
    CONFIRM,    // yes/no prompt
    DONE,       // execution feedback
    DUR_PICK,   // encoder duration picker (0–20 min, 1-min steps)
    DATE_EDIT,  // encoder day/month/year editor
    TIME_EDIT,  // encoder hour/minute editor
};

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
    TESTES,
    BLSEL,    // backlight timeout selector
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

enum class TimeEditContext : uint8_t {
    RTC,
    CUSTOM_CYCLE, // edits cs.slots[_teCycleIdx]
};

// ─────────────────────────────────────────────────────────
// MenuItem
// ─────────────────────────────────────────────────────────
// Action encoding:
//   "go:<menuID>"                        navigate to menu
//   "go:idle"                            return to idle
//   "info:<l0>|<l1>|<l2>|<l3>|<back>"   info panel
//   "confirm:<l1>|<l2>|<back>|<tag>"     confirm dialog
//   "sel:<modeIdx>"                      select AppMode
//   "cfgz:<zoneIdx>"                     open DUR_PICK for zone config
//   "cz:<zoneIdx>"                       toggle custom zone selection
//   "dur_pick:custom"                    open DUR_PICK for custom run
//   "time_edit"                          open TIME_EDIT screen
//   "horarios"                           computed info screen
struct MenuItem {
    char label[21];   // 20 LCD chars + \0
    char action[64];  // widened to 64 — test confirm strings can reach ~58 chars
};

// ─────────────────────────────────────────────────────────
// UI
// ─────────────────────────────────────────────────────────
class UI {
public:
    UI(Display& disp, Encoder& enc);

    void begin();
    void update();

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
    uint8_t   _durValue;     // 0 = OFF (CFG_ZONE only), 1–20 = minutes
    DurContext _durContext;
    uint8_t   _durZoneIdx;

    // ── Date editor ───────────────────────────────────────
    uint8_t  _deDay;
    uint8_t  _deMonth;
    uint16_t _deYear;
    uint8_t  _deField;  // 0 = day, 1 = month, 2 = year

    // ── Time editor ───────────────────────────────────────
    uint8_t _teHour;   // value being edited
    uint8_t _teMin;
    uint8_t _teField;  // 0 = hour, 1 = minute
    TimeEditContext _teContext;
    uint8_t _teCycleIdx;

    // ── Confirm tag ───────────────────────────────────────
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
    void _renderDateEdit();
    void _renderTimeEdit();

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
    void _showDurPick(uint8_t initial, DurContext ctx, uint8_t zoneIdx = 0);
    void _commitDurPick();
    void _showDateEdit();
    void _commitDateEdit();
    void _showTimeEdit(TimeEditContext ctx, uint8_t cycleIdx = 0);
    void _commitTimeEdit();

    // ── Utilities ─────────────────────────────────────────
    static MenuID      _parseMenuID(const char* s);
    static const char* _modeName(AppMode m);
    static const char* _modeHours(AppMode m);
    uint8_t            _totalZoneDuration();
};
