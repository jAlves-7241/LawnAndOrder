#pragma once
#include "UIScreen.h"
#include "UITypes.h"
#include "../config.h"

// ─────────────────────────────────────────────────────────
// ScreenInfo - Read-only 4-line panel
// ─────────────────────────────────────────────────────────
class ScreenInfo : public UIScreen {
public:
    void setup(const char* l0, const char* l1, const char* l2, const char* l3, MenuID backMenu);

    void onEnter(UI& ui) override;
    void handleRotation(UI& ui, int8_t dir) override;
    void handleClick(UI& ui) override;
    void render(UI& ui) override;

private:
    char _rows[4][LCD_COLS + 1];
    MenuID _backMenu;
};

// ─────────────────────────────────────────────────────────
// ScreenConfirm - Yes/No prompt with side-effect execution
// ─────────────────────────────────────────────────────────
class ScreenConfirm : public UIScreen {
public:
    void setup(const char* l1, const char* l2, MenuID backMenu, const char* tag);

    void onEnter(UI& ui) override;
    void handleRotation(UI& ui, int8_t dir) override;
    void handleClick(UI& ui) override;
    void render(UI& ui) override;

private:
    char _rows[4][LCD_COLS + 1];
    MenuID _backMenu;
    char _tag[16];
    bool _selectionOk;
};

// ─────────────────────────────────────────────────────────
// ScreenDone - Execution feedback
// ─────────────────────────────────────────────────────────
class ScreenDone : public UIScreen {
public:
    void setup(const char* l1, const char* l2);

    void onEnter(UI& ui) override;
    void handleRotation(UI& ui, int8_t dir) override;
    void handleClick(UI& ui) override;
    void render(UI& ui) override;

    void setBackMenu(MenuID back) { _backMenu = back; }

private:
    char _rows[4][LCD_COLS + 1];
    MenuID _backMenu; // inherited dynamically based on previous state
};
