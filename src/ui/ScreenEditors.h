#pragma once
#include "UIScreen.h"
#include "UITypes.h"
#include <stdint.h>

// ─────────────────────────────────────────────────────────
// ScreenDurPick
// ─────────────────────────────────────────────────────────
class ScreenDurPick : public UIScreen {
public:
    void setup(uint8_t initial, DurContext ctx, uint8_t zoneIdx, MenuID backMenu);

    void onEnter(UI& ui) override;
    void handleRotation(UI& ui, int8_t dir) override;
    void handleClick(UI& ui) override;
    void render(UI& ui) override;

private:
    uint8_t _durValue;
    DurContext _durContext;
    uint8_t _durZoneIdx;
    MenuID _backMenu;

    void _getRange(DurContext ctx, int& vmin, int& vmax);
};

// ─────────────────────────────────────────────────────────
// ScreenDateEdit
// ─────────────────────────────────────────────────────────
class ScreenDateEdit : public UIScreen {
public:
    void setup(MenuID backMenu);

    void onEnter(UI& ui) override;
    void handleRotation(UI& ui, int8_t dir) override;
    void handleClick(UI& ui) override;
    void render(UI& ui) override;

private:
    uint8_t  _deDay;
    uint8_t  _deMonth;
    uint16_t _deYear;
    uint8_t  _deField;  // 0 = day, 1 = month, 2 = year
    MenuID _backMenu;
};

// ─────────────────────────────────────────────────────────
// ScreenTimeEdit
// ─────────────────────────────────────────────────────────
class ScreenTimeEdit : public UIScreen {
public:
    void setup(TimeEditContext ctx, uint8_t cycleIdx, MenuID backMenu);

    void onEnter(UI& ui) override;
    void handleRotation(UI& ui, int8_t dir) override;
    void handleClick(UI& ui) override;
    void render(UI& ui) override;

private:
    uint8_t _teHour;
    uint8_t _teMin;
    uint8_t _teField;  // 0 = hour, 1 = minute
    TimeEditContext _teContext;
    uint8_t _teCycleIdx;
    MenuID _backMenu;
};
