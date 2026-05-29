#pragma once
#include "UIScreen.h"
#include "UITypes.h"
#include "../config.h"

// ─────────────────────────────────────────────────────────
// ScreenExport - Progress display for serial history export
// ─────────────────────────────────────────────────────────
class ScreenExport : public UIScreen {
public:
    void setup(MenuID backMenu);

    void onEnter(UI& ui) override;
    void handleRotation(UI& ui, int8_t dir) override;
    void handleClick(UI& ui) override;
    void update(UI& ui) override;
    void render(UI& ui) override;

private:
    MenuID  _backMenu;
    uint8_t _lastPct;
};
