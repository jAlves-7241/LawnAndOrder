#pragma once
#include <Arduino.h>

class UI; // Forward declaration

class UIScreen {
public:
    virtual ~UIScreen() = default;

    // Called when the screen becomes active
    virtual void onEnter(UI& ui) {}
    
    // Called when the screen is being deactivated
    virtual void onExit(UI& ui) {}
    
    // Called periodically (e.g. for time/progress updates)
    virtual void update(UI& ui) {}
    
    // Input handling
    virtual void handleRotation(UI& ui, int8_t dir) {}
    virtual void handleClick(UI& ui) {}
    
    // Initial draw of the screen
    virtual void render(UI& ui) = 0;
};
