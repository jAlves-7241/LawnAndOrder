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
    static const uint8_t MAX_ITEMS = 12;

    MenuID _mid;
    MenuItem _items[MAX_ITEMS];
    uint8_t _itemCount;
    uint8_t _cur;
    uint8_t _off;

    void dispatch(UI& ui, const char* action);
};
