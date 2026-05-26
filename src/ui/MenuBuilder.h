#pragma once
#include <stdint.h>
#include <stddef.h>
#include "UITypes.h"

// ─────────────────────────────────────────────────────────
// MenuItem
// ─────────────────────────────────────────────────────────
struct MenuItem {
    char label[21];   // 20 LCD chars + \0
    char action[80];  // widened to 80 - history confirm strings can reach ~65 chars
};

class MenuBuilder {
public:
    static const uint8_t MAX_ITEMS = 12;
    static void build(MenuID mid, MenuItem* items, uint8_t& itemCount);
    
    // Utilities moved from UI
    static MenuID parseMenuID(const char* s);
    static const char* modeName(uint8_t m);
    static void modeHours(uint8_t m, char* dest, size_t maxLen);
    
private:
    static void makeItem(MenuItem* it, const char* label, const char* action);
};
