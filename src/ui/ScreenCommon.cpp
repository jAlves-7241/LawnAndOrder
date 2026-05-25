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
    strncpy(_tag, tag, sizeof(_tag) - 1);
    _tag[sizeof(_tag) - 1] = '\0';
    _backMenu = backMenu;
    _selectionOk = false;
}

void ScreenConfirm::onEnter(UI& ui) {
    render(ui);
}

void ScreenConfirm::handleRotation(UI& ui, int8_t dir) {
    _selectionOk = !_selectionOk;
    render(ui);
}

void ScreenConfirm::handleClick(UI& ui) {
    if (_selectionOk) {
        if (!ui.executeConfirmed(_tag)) {
            if (!strcmp(_tag, "general") || !strcmp(_tag, "custom")) {
                ui.getScreenDone().setup("Rega iniciada", "");
            } else if (!strncmp(_tag, "test_", 5)) {
                ui.getScreenDone().setup("Teste iniciado", "");
            } else {
                ui.getScreenDone().setup(_rows[1], _rows[2]);
            }
            ui.getScreenDone().setBackMenu(_backMenu);
            ui.changeScreen(&ui.getScreenDone());
        }
    } else {
        ui.goMenu(_backMenu);
    }
}

void ScreenConfirm::render(UI& ui) {
    char l3[LCD_COLS + 1];
    snprintf(l3, sizeof(l3), "%-10s %-9s", _selectionOk ? ">OK" : " OK", _selectionOk ? " Voltar" : ">Voltar");
    ui.getDisplay().setRows(_rows[0], _rows[1], _rows[2], l3);
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
