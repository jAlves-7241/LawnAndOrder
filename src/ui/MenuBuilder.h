#pragma once
#include <stdint.h>
#include "UITypes.h"

// ─────────────────────────────────────────────────────────
// MenuItem
// ─────────────────────────────────────────────────────────
struct MenuItem {
    char label[21];   // 20 LCD chars + \0
    char action[64];  // widened to 64 - test confirm strings can reach ~58 chars
};

class MenuBuilder {
public:
    static void build(MenuID mid, MenuItem* items, uint8_t& itemCount);
    
    // Utilities moved from UI
    static MenuID parseMenuID(const char* s);
    static const char* modeName(uint8_t m);
    static const char* modeHours(uint8_t m);
    
private:
    static void makeItem(MenuItem* it, const char* label, const char* action);
};
