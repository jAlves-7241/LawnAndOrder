#pragma once
#include "UIScreen.h"
#include "../AppState.h"
#include <stdint.h>

class ScreenIdle : public UIScreen {
public:
    ScreenIdle() : _lastWateringActive(false), _lastProgress(255), _lastMinute(255),
                   _lastSuspended(false), _lastMode(AppMode::MEDIO),
                   _lastNextH(0xFF), _lastNextM(0xFF) {}

    void onEnter(UI& ui) override;
    void handleRotation(UI& ui, int8_t dir) override;
    void handleClick(UI& ui) override;
    void update(UI& ui) override;
    void render(UI& ui) override;

private:
    bool _lastWateringActive;
    uint8_t _lastProgress;
    uint8_t _lastMinute;
    bool _lastSuspended;
    AppMode _lastMode;
    uint8_t _lastNextH;
    uint8_t _lastNextM;
    
    // Renders the entire screen
    void fullRender(UI& ui);
};
