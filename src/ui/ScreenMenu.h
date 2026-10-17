#pragma once
#include "UIScreen.h"
#include "MenuBuilder.h"

class ScreenMenu : public UIScreen {
public:
    void setup(MenuID mid);

    void onEnter(UI& ui) override;
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

    void dispatch(UI& ui, const char* action);
};
