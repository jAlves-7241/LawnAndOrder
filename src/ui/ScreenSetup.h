#pragma once
#include "UIScreen.h"
#include "UITypes.h"

class ScreenSetupWelcome : public UIScreen {
public:
    void setup(bool isCompleteScreen);

    void onEnter(UI& ui) override;
    void handleRotation(UI& ui, int8_t dir) override;
    void handleClick(UI& ui) override;
    void render(UI& ui) override;

private:
    bool _isComplete;
};
