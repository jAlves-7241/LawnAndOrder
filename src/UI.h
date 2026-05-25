#pragma once
#include <Arduino.h>
#include "Display.h"
#include "Encoder.h"
#include "AppState.h"

#include "ui/UIScreen.h"
#include "ui/UITypes.h"
#include "ui/ScreenIdle.h"
#include "ui/ScreenMenu.h"
#include "ui/ScreenCommon.h"
#include "ui/ScreenEditors.h"
#include "ui/ScreenSetup.h"

class UI {
public:
    UI(Display& disp, Encoder& enc);

    void begin();
    void update();

    // Accessors
    Display& getDisplay() { return _d; }
    Encoder& getEncoder() { return _enc; }
    
    // Navigation
    void changeScreen(UIScreen* screen);
    void goIdle();
    void goMenu(MenuID mid);

    // Setup logic
    bool inSetup() const { return _inSetup; }
    void setInSetup(bool val) { _inSetup = val; }
    SetupStep getSetupStep() const { return _setupStep; }
    void advanceSetup();

    void flagConfigChanged() { _configChanged = true; }
    bool executeConfirmed(const char* tag);
    void dispatchAction(const char* action);

    // Get screens
    ScreenIdle& getScreenIdle() { return _screenIdle; }
    ScreenMenu& getScreenMenu() { return _screenMenu; }
    ScreenInfo& getScreenInfo() { return _screenInfo; }
    ScreenConfirm& getScreenConfirm() { return _screenConfirm; }
    ScreenDone& getScreenDone() { return _screenDone; }
    ScreenDurPick& getScreenDurPick() { return _screenDurPick; }
    ScreenDateEdit& getScreenDateEdit() { return _screenDateEdit; }
    ScreenTimeEdit& getScreenTimeEdit() { return _screenTimeEdit; }
    ScreenSetupWelcome& getScreenSetupWelcome() { return _screenSetupWelcome; }

    uint16_t getTotalZoneDuration();
    
    // Cross-screen data access for Date -> Time setup transition
    void setDateCache(uint16_t year, uint8_t month, uint8_t day) {
        _dateYear = year; _dateMonth = month; _dateDay = day;
    }
    uint16_t getDateEditYear() const { return _dateYear; }
    uint8_t getDateEditMonth() const { return _dateMonth; }
    uint8_t getDateEditDay() const { return _dateDay; }

private:
    Display& _d;
    Encoder& _enc;

    UIScreen* _currentScreen;

    ScreenIdle _screenIdle;
    ScreenMenu _screenMenu;
    ScreenInfo _screenInfo;
    ScreenConfirm _screenConfirm;
    ScreenDone _screenDone;
    ScreenDurPick _screenDurPick;
    ScreenDateEdit _screenDateEdit;
    ScreenTimeEdit _screenTimeEdit;
    ScreenSetupWelcome _screenSetupWelcome;

    bool _inSetup;
    SetupStep _setupStep;
    bool _configChanged;
    uint32_t _lastActivity;

    uint16_t _dateYear;
    uint8_t _dateMonth;
    uint8_t _dateDay;
    
    void _startSetup();
};
