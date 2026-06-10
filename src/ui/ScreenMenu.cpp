#include "../i18n.h"
#include "ScreenMenu.h"
#include "../UI.h"
#include <string.h>

void ScreenMenu::setup(MenuID mid) {
    _mid = mid;
    _cur = 0;
    _off = 0;
    MenuBuilder::build(mid, _items, _itemCount);
}

void ScreenMenu::onEnter(UI& ui) {
    render(ui);
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
    dispatch(ui, _items[_cur].action);
}

void ScreenMenu::render(UI& ui) {
    if (_cur < _off)                 _off = _cur;
    if (_cur >= _off + MENU_VISIBLE) _off = _cur - MENU_VISIBLE + 1;

    auto menuTitle = [](MenuID m) -> const char* {
        switch (m) {
            case MenuID::MAIN:         return TXT_MENU_MAIN;
            case MenuID::MANUAL:       return TXT_MANUAL_WATERING;
            case MenuID::PROG:         return TXT_PROG;
            case MenuID::MODOS:        return "Alterar Modo";
            case MenuID::CFG_ZONAS:    return TXT_CONFIG_ZONES;
            case MenuID::CUSTOM_ZONAS: return TXT_CHOOSE_ZONES;
            case MenuID::CFG_CUSTOM:   return "Personalizar";
            case MenuID::HISTORICO:    return TXT_HISTORY;
            case MenuID::DEF:          return "Definicoes";
            case MenuID::DEF_AVANCADO: return TXT_ADVANCED_SETTINGS;
            case MenuID::TESTES:       return TXT_TEST_ZONES;
            case MenuID::BLSEL:        return "Tempo Ecra";
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
