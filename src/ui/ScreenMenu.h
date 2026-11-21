#pragma once
#include "UIScreen.h"
#include "MenuBuilder.h"

class ScreenMenu : public UIScreen {
public:
    void setup(MenuID mid, bool preserveCursor = false);

    void onEnter(UI& ui) override;
    void update(UI& ui) override;
    void handleRotation(UI& ui, int8_t dir) override;
    void handleClick(UI& ui) override;
    void render(UI& ui) override;

private:
    static const uint8_t MAX_ITEMS = MenuBuilder::MAX_ITEMS;

    MenuID _mid = MenuID::MAIN;
    MenuItem _items[MAX_ITEMS] = {};
    uint8_t _itemCount = 0;
    uint8_t _cur = 0;
    uint8_t _off = 0;
    bool _lastWateringActive = false;
    bool _lastSuspended = false;

    void dispatch(UI& ui, const char* action);
};
