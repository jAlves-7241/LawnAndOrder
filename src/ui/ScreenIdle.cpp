#include "ScreenIdle.h"
#include "../UI.h"
#include "../AppState.h"
#include <stdio.h>

void ScreenIdle::onEnter(UI& ui) {
    _lastWateringActive = false;
    _lastProgress = 255;
    _lastMinute = 255;
    render(ui);
}

void ScreenIdle::handleRotation(UI& ui, int8_t dir) {
    // Rotation doesn't do anything on idle
}

void ScreenIdle::handleClick(UI& ui) {
    ui.goMenu(MenuID::MAIN);
}

void ScreenIdle::update(UI& ui) {
    bool needsRender = false;

    if (_lastWateringActive != gState.watering.active) {
        needsRender = true;
    } else if (gState.watering.active && !gState.watering.is_waiting) {
        if (_lastProgress != gState.watering.progress_pct) {
            needsRender = true;
        }
    }

    if (_lastMinute != gState.now.min) {
        needsRender = true;
    }

    if (needsRender) {
        render(ui);
    }
}

void ScreenIdle::render(UI& ui) {
    fullRender(ui);
}

void ScreenIdle::fullRender(UI& ui) {
    _lastWateringActive = gState.watering.active;
    _lastProgress = gState.watering.progress_pct;
    _lastMinute = gState.now.min;

    char b0[LCD_COLS+1], b3[LCD_COLS+1];

    if (gState.rtc_valid) {
        char tstr[6];
        snprintf(tstr, sizeof(tstr), "%02d:%02d", gState.now.hour, gState.now.min);
        ui.getDisplay().cx(b0, tstr);
    } else {
        ui.getDisplay().cx(b0, "--:--");
    }

    if (gState.watering.active) {
        uint8_t zi = gState.watering.zone_idx;
        if (zi >= NUM_ZONES) zi = 0;
        char zrow[LCD_COLS+1], pb[LCD_COLS+1];
        snprintf(zrow, sizeof(zrow), "Z%d %s", zi + 1, gState.zones[zi].name);
        ui.getDisplay().pbar(pb, _lastProgress);
        ui.getDisplay().setRows(b0, "A regar agora...", zrow, pb);
    } else {
        if (!gState.rtc_valid) {
            ui.getDisplay().setRows(b0, "", "", ui.getDisplay().cx(b3, "! Acertar hora !"));
        } else if (gState.suspended) {
            ui.getDisplay().setRows(b0, "", ui.getDisplay().cx(b3, "Rega Suspensa"), "");
        } else if (gState.mode == AppMode::DESATIVADO) {
            ui.getDisplay().setRows(b0, "", ui.getDisplay().cx(b3, "Rega Desativada"), "");
        } else {
            char nxstr[LCD_COLS+1];
            snprintf(nxstr, sizeof(nxstr), "Proxima: %02d:%02d", gState.next_hour, gState.next_min);
            ui.getDisplay().setRows(b0, "", "", ui.getDisplay().cx(b3, nxstr));
        }
    }
}
