#include "ScreenCommon.h"
#include "../UI.h"
#include "../i18n.h"
#include <string.h>

// ─────────────────────────────────────────────────────────
// ScreenInfo
// ─────────────────────────────────────────────────────────
void ScreenInfo::setup(const char* l0, const char* l1, const char* l2, const char* l3,
                       MenuID backMenu, UIScreen* backScreen) {
    char buf[LCD_COLS + 1];
    snprintf(_rows[0], sizeof(_rows[0]), "%s", Display::cx(buf, l0));
    snprintf(_rows[1], sizeof(_rows[1]), "%s", Display::fx(buf, l1));
    snprintf(_rows[2], sizeof(_rows[2]), "%s", Display::fx(buf, l2));
    snprintf(_rows[3], sizeof(_rows[3]), "%s", Display::fx(buf, l3));
    _backMenu = backMenu;
    _backScreen = backScreen;
}

void ScreenInfo::onEnter(UI& ui) {
    _enterMs = millis();
    render(ui);
}

void ScreenInfo::handleRotation(UI& ui, int8_t /*dir*/) {
    if (millis() - _enterMs < 350UL) return;
    if (_backScreen) ui.changeScreen(_backScreen);
    else ui.goMenu(_backMenu);
}

void ScreenInfo::handleClick(UI& ui) {
    if (_backScreen) ui.changeScreen(_backScreen);
    else ui.goMenu(_backMenu);
}

void ScreenInfo::render(UI& ui) {
    ui.getDisplay().setRows(_rows[0], _rows[1], _rows[2], _rows[3]);
}

// ─────────────────────────────────────────────────────────
// ScreenConfirm
// ─────────────────────────────────────────────────────────
void ScreenConfirm::setup(const char* l1, const char* l2, MenuID backMenu, const char* tag) {
    char buf[LCD_COLS + 1];
    snprintf(_rows[0], sizeof(_rows[0]), "%s", Display::cx(buf, TXT_CONFIRM_TITLE));
    snprintf(_rows[1], sizeof(_rows[1]), "%s", Display::fx(buf, l1));
    snprintf(_rows[2], sizeof(_rows[2]), "%s", Display::fx(buf, l2));
    if (!tag) tag = "";
    strncpy(_tag, tag, sizeof(_tag) - 1);
    _tag[sizeof(_tag) - 1] = '\0';
    _backMenu = backMenu;
    _selectionOk = false;
}

void ScreenConfirm::onEnter(UI& ui) {
    _enterMs = millis();
    render(ui);
}

void ScreenConfirm::handleRotation(UI& ui, int8_t /*dir*/) {
    if (millis() - _enterMs < 350UL) return;
    _selectionOk = !_selectionOk;
    render(ui);
}

void ScreenConfirm::handleClick(UI& ui) {
    if (_selectionOk) {
        if (!ui.executeConfirmed(_tag)) {
            if (!strcmp(_tag, "general") || !strcmp(_tag, "custom")) {
                ui.getScreenDone().setup(TXT_WATERING_STARTED, "", MenuID::MAIN);
            } else if (!strncmp(_tag, "test_", 5)) {
                ui.getScreenDone().setup(TXT_TEST_STARTED, "", MenuID::MAIN);
            } else {
                ui.getScreenDone().setup(_rows[1], _rows[2], _backMenu);
            }
            ui.changeScreen(&ui.getScreenDone());
        }
    } else {
        if (!strcmp(_tag, "skip_clock") && ui.inSetup()) {
            ui.getScreenDateEdit().setup(MenuID::MAIN);
            ui.changeScreen(&ui.getScreenDateEdit());
            return;
        }
        ui.goMenu(_backMenu);
    }
}

void ScreenConfirm::render(UI& ui) {
    char l3[LCD_COLS + 1];
    int w1 = LCD_COLS / 2;
    int w2 = LCD_COLS - w1 - 1;
    snprintf(l3, sizeof(l3), "%-*s %-*s", w1, _selectionOk ? TXT_CONFIRM_OK_SEL : TXT_CONFIRM_OK, w2, _selectionOk ? TXT_CONFIRM_BACK : TXT_CONFIRM_BACK_SEL);
    ui.getDisplay().setRows(_rows[0], _rows[1], _rows[2], l3);
}

// ─────────────────────────────────────────────────────────
// ScreenDone
// ─────────────────────────────────────────────────────────
void ScreenDone::setup(const char* l1, const char* l2, MenuID back) {
    char buf[LCD_COLS + 1];
    snprintf(_rows[0], sizeof(_rows[0]), "%s", Display::cx(buf, TXT_DONE_TITLE));
    snprintf(_rows[1], sizeof(_rows[1]), "%s", Display::fx(buf, l1));
    snprintf(_rows[2], sizeof(_rows[2]), "%s", Display::fx(buf, l2));
    snprintf(_rows[3], sizeof(_rows[3]), "%s", Display::cx(buf, TXT_CLICK_TO_BACK));
    _backMenu = back;
}

void ScreenDone::onEnter(UI& ui) {
    _enterMs = millis();
    render(ui);
}

void ScreenDone::handleRotation(UI& ui, int8_t /*dir*/) {
    if (millis() - _enterMs < 350UL) return;
    ui.goMenu(_backMenu);
}

void ScreenDone::handleClick(UI& ui) {
    ui.goMenu(_backMenu);
}

void ScreenDone::render(UI& ui) {
    ui.getDisplay().setRows(_rows[0], _rows[1], _rows[2], _rows[3]);
}
