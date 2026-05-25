#include "ScreenCommon.h"
#include "../UI.h"
#include <string.h>

// ─────────────────────────────────────────────────────────
// ScreenInfo
// ─────────────────────────────────────────────────────────
void ScreenInfo::setup(const char* l0, const char* l1, const char* l2, const char* l3, MenuID backMenu) {
    char buf[LCD_COLS + 1];
    strncpy(_rows[0], Display::cx(buf, l0), LCD_COLS + 1);
    strncpy(_rows[1], Display::fx(buf, l1), LCD_COLS + 1);
    strncpy(_rows[2], Display::fx(buf, l2), LCD_COLS + 1);
    strncpy(_rows[3], Display::fx(buf, l3), LCD_COLS + 1);
    _backMenu = backMenu;
}

void ScreenInfo::onEnter(UI& ui) {
    render(ui);
}

void ScreenInfo::handleRotation(UI& ui, int8_t dir) {
    ui.goMenu(_backMenu);
}

void ScreenInfo::handleClick(UI& ui) {
    ui.goMenu(_backMenu);
}

void ScreenInfo::render(UI& ui) {
    ui.getDisplay().setRows(_rows[0], _rows[1], _rows[2], _rows[3]);
}

// ─────────────────────────────────────────────────────────
// ScreenConfirm
// ─────────────────────────────────────────────────────────
void ScreenConfirm::setup(const char* l1, const char* l2, MenuID backMenu, const char* tag) {
    char buf[LCD_COLS + 1];
    strncpy(_rows[0], Display::cx(buf, "-- CONFIRMAR? --"), LCD_COLS + 1);
    strncpy(_rows[1], Display::fx(buf, l1), LCD_COLS + 1);
    strncpy(_rows[2], Display::fx(buf, l2), LCD_COLS + 1);
    strncpy(_rows[3], Display::cx(buf, "[OK]      [Voltar]"), LCD_COLS + 1);
    strncpy(_tag, tag, sizeof(_tag) - 1);
    _tag[sizeof(_tag) - 1] = '\0';
    _backMenu = backMenu;
}

void ScreenConfirm::onEnter(UI& ui) {
    render(ui);
}

void ScreenConfirm::handleRotation(UI& ui, int8_t dir) {
    ui.goMenu(_backMenu);
}

void ScreenConfirm::handleClick(UI& ui) {
    if (!ui.executeConfirmed(_tag)) {
        ui.getScreenDone().setup(_rows[1], _rows[2]);
        ui.getScreenDone().setBackMenu(_backMenu);
        ui.changeScreen(&ui.getScreenDone());
    }
}

void ScreenConfirm::render(UI& ui) {
    ui.getDisplay().setRows(_rows[0], _rows[1], _rows[2], _rows[3]);
}

// ─────────────────────────────────────────────────────────
// ScreenDone
// ─────────────────────────────────────────────────────────
void ScreenDone::setup(const char* l1, const char* l2) {
    char buf[LCD_COLS + 1];
    strncpy(_rows[0], Display::cx(buf, "-- EXECUTADO --"), LCD_COLS + 1);
    strncpy(_rows[1], Display::fx(buf, l1), LCD_COLS + 1);
    strncpy(_rows[2], Display::fx(buf, l2), LCD_COLS + 1);
    strncpy(_rows[3], Display::cx(buf, "Click p/ voltar"), LCD_COLS + 1);
}

void ScreenDone::onEnter(UI& ui) {
    render(ui);
}

void ScreenDone::handleRotation(UI& ui, int8_t dir) {
    ui.goMenu(_backMenu);
}

void ScreenDone::handleClick(UI& ui) {
    ui.goMenu(_backMenu);
}

void ScreenDone::render(UI& ui) {
    ui.getDisplay().setRows(_rows[0], _rows[1], _rows[2], _rows[3]);
}
