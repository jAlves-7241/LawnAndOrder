#include "../i18n.h"
#include "ScreenMenu.h"
#include "../UI.h"
#include "../History.h"
#include <string.h>

void ScreenMenu::setup(MenuID mid, bool preserveCursor) {
    bool keepPos = preserveCursor && (mid == _mid);
    _mid = mid;
    if (!keepPos) {
        _cur = 0;
        _off = 0;
    }
    _lastWateringActive = gState.watering.active;
    _lastSuspended = gState.suspended;
    _lastHistRev = history.revision();
    MenuBuilder::build(mid, _items, _itemCount);
    if (keepPos) {
        if (_itemCount == 0) {
            _cur = 0;
            _off = 0;
        } else {
            if (_cur >= _itemCount) _cur = _itemCount - 1;
            if (_itemCount <= MENU_VISIBLE) {
                _off = 0;
            } else if (_off > _itemCount - MENU_VISIBLE) {
                _off = _itemCount - MENU_VISIBLE;
            }
        }
    }
}

void ScreenMenu::onEnter(UI& ui) {
    render(ui);
}

void ScreenMenu::update(UI& ui) {
    if ((_mid == MenuID::MAIN && _lastWateringActive != gState.watering.active) ||
        (_mid == MenuID::PROG && _lastSuspended != gState.suspended) ||
        (_mid == MenuID::HISTORICO && _lastHistRev != history.revision())) {
        char prevAction[sizeof(_items[0].action)] = {};
        if (_itemCount > 0 && _cur < _itemCount) {
            strlcpy(prevAction, _items[_cur].action, sizeof(prevAction));
        }
        _lastWateringActive = gState.watering.active;
        _lastSuspended = gState.suspended;
        _lastHistRev = history.revision();
        MenuBuilder::build(_mid, _items, _itemCount);
        if (_itemCount == 0) {
            _cur = 0;
            _off = 0;
        } else {
            // Keep cursor anchored to the same menu action when dynamic items (e.g. "stop_now") appear/disappear
            if (prevAction[0] != '\0') {
                for (uint8_t i = 0; i < _itemCount; i++) {
                    if (strcmp(_items[i].action, prevAction) == 0) {
                        _cur = i;
                        break;
                    }
                }
            }
            if (_cur >= _itemCount) _cur = _itemCount - 1;
            if (_cur < _off) _off = _cur;
            if (_cur >= _off + MENU_VISIBLE) _off = _cur - MENU_VISIBLE + 1;
            if (_itemCount <= MENU_VISIBLE) {
                _off = 0;
            } else if (_off > _itemCount - MENU_VISIBLE) {
                _off = _itemCount - MENU_VISIBLE;
            }
        }
        render(ui);
    }
}

void ScreenMenu::handleRotation(UI& ui, int8_t dir) {
    if (_itemCount == 0) return;
    
#if MENU_WRAP_AROUND
    int new_cur = (int)_cur + dir;
    while (new_cur < 0) new_cur += _itemCount;
    while (new_cur >= _itemCount) new_cur -= _itemCount;
    if (new_cur != _cur) {
        _cur = new_cur;
        render(ui);
    }
#else
    int new_cur = (int)_cur + dir;
    if (new_cur < 0) new_cur = 0;
    if (new_cur >= _itemCount) new_cur = _itemCount - 1;
    if (new_cur != _cur) {
        _cur = new_cur;
        render(ui);
    }
#endif
}

void ScreenMenu::handleClick(UI& ui) {
    if (_itemCount == 0 || _cur >= _itemCount) return;
    char actionCopy[sizeof(_items[0].action)];
    strlcpy(actionCopy, _items[_cur].action, sizeof(actionCopy));
    dispatch(ui, actionCopy);
}

void ScreenMenu::render(UI& ui) {
    if (_cur < _off)                 _off = _cur;
    if (_cur >= _off + MENU_VISIBLE) _off = _cur - MENU_VISIBLE + 1;
    if (_itemCount <= MENU_VISIBLE) {
        _off = 0;
    } else if (_off > _itemCount - MENU_VISIBLE) {
        _off = _itemCount - MENU_VISIBLE;
    }

    auto menuTitle = [](MenuID m) -> const char* {
        switch (m) {
            case MenuID::MAIN:         return TXT_MENU_MAIN;
            case MenuID::MANUAL:       return TXT_MANUAL_WATERING;
            case MenuID::PROG:         return TXT_PROG;
            case MenuID::MODOS:        return TXT_MENU_CHANGE_MODE;
            case MenuID::CFG_ZONAS:    return TXT_CONFIG_ZONES;
            case MenuID::CUSTOM_ZONAS: return TXT_CHOOSE_ZONES;
            case MenuID::CFG_CUSTOM:   return TXT_MENU_CUSTOMIZE;
            case MenuID::HISTORICO:    return TXT_HISTORY;
            case MenuID::DEF:          return TXT_MENU_SETTINGS;
            case MenuID::DEF_AVANCADO: return TXT_ADVANCED_SETTINGS;
            case MenuID::TESTES:       return TXT_TEST_ZONES;
            case MenuID::BLSEL:        return TXT_MENU_SCREEN_TIME;
            case MenuID::SETUP_MODE:   return TXT_WATERING_MODE;
            case MenuID::SETUP_ZONES:  return TXT_CONFIG_ZONES_SHORT;
            case MenuID::SETUP_CUSTOM: return TXT_MODE_CUSTOM_SHORT;
            default:                   return TXT_MENU;
        }
    };

    char hbuf[LCD_COLS+1];
    ui.getDisplay().hdr(hbuf, menuTitle(_mid));

    char rows[MENU_VISIBLE][LCD_COLS+1];
    for (uint8_t i = 0; i < MENU_VISIBLE; i++) {
        uint8_t idx = _off + i;
        if (idx < _itemCount) {
            char line[LCD_COLS+2];
            snprintf(line, sizeof(line), "%c%s",
                     idx == _cur ? '>' : ' ', _items[idx].label);
            ui.getDisplay().fx(rows[i], line);
        } else {
            ui.getDisplay().fx(rows[i], "");
        }
    }
    static_assert(MENU_VISIBLE == 3, "ScreenMenu requires exactly 3 visible rows");
    ui.getDisplay().setRows(hbuf, rows[0], rows[1], rows[2]);
}

void ScreenMenu::dispatch(UI& ui, const char* action) {
    ui.dispatchAction(action);
}
