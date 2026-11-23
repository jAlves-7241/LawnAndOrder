#pragma once
#include "UIScreen.h"
#include "../AppState.h"
#include <stdint.h>

class ScreenIdle : public UIScreen {
public:
    ScreenIdle() : _lastWateringActive(false), _lastIsWaiting(false), _lastProgress(255),
                   _lastYear(0), _lastMonth(0), _lastDay(0), _lastHour(255), _lastMinute(255),
                   _lastSuspended(false), _lastMode(AppMode::MEDIO),
                   _lastNextH(0xFF), _lastNextM(0xFF),
                   _lastZoneIdx(0xFF), _lastRtcValid(false), _lastRtcPresent(false),
                   _lastBatteryLow(false), _lastHistReady(false), _lastHistCount(0xFFFF),
                   _lastHasEnabledZones(false), _lastSkippedSlot(false),
                   _lastSkippedHour(0xFF), _lastSkippedMin(0xFF) {}

    void onEnter(UI& ui) override;
    void handleRotation(UI& ui, int8_t dir) override;
    void handleClick(UI& ui) override;
    void update(UI& ui) override;
    void render(UI& ui) override;

private:
    UNIT_TEST_FRIEND
    bool _lastWateringActive;
    bool _lastIsWaiting;
    uint8_t _lastProgress;
    uint16_t _lastYear;
    uint8_t _lastMonth;
    uint8_t _lastDay;
    uint8_t _lastHour;
    uint8_t _lastMinute;
    bool _lastSuspended;
    AppMode _lastMode;
    uint8_t _lastNextH;
    uint8_t _lastNextM;
    uint8_t _lastZoneIdx;
    bool _lastRtcValid;
    bool _lastRtcPresent;
    bool _lastBatteryLow;
    bool _lastHistReady;
    uint16_t _lastHistCount;
    bool _lastHasEnabledZones;
    bool _lastSkippedSlot;
    uint8_t _lastSkippedHour;
    uint8_t _lastSkippedMin;
    
    // Renders the entire screen
    void fullRender(UI& ui);
};
