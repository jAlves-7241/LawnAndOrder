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
    uint8_t _durValue = 0;
    DurContext _durContext = DurContext::CUSTOM_RUN;
    uint8_t _durZoneIdx = 0;
    MenuID _backMenu = MenuID::MAIN;

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
    uint8_t  _deDay = 1;
    uint8_t  _deMonth = 1;
    uint16_t _deYear = 2026;
    uint8_t  _deField = 0;  // 0 = day, 1 = month, 2 = year
    MenuID _backMenu = MenuID::MAIN;
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
    uint8_t _teHour = 0;
    uint8_t _teMin = 0;
    uint8_t _teField = 0;  // 0 = hour, 1 = minute
    TimeEditContext _teContext = TimeEditContext::RTC;
    uint8_t _teCycleIdx = 0;
    MenuID _backMenu = MenuID::MAIN;
};
