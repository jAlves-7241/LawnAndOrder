#include "ScreenIdle.h"
#include "../UI.h"
#include "../AppState.h"
#include <stdio.h>
#include "../Scheduler.h"
#include <RTClib.h>

void ScreenIdle::onEnter(UI& ui) {
    _lastWateringActive = false;
    _lastProgress = 255;
    _lastMinute = 255;
    _lastSuspended = false;
    _lastMode = AppMode::MEDIO;
    _lastNextH = 0xFF;
    _lastNextM = 0xFF;
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

    if (_lastSuspended != gState.suspended) needsRender = true;
    if (_lastMode != gState.mode) needsRender = true;
    if (_lastNextH != gState.next_hour || _lastNextM != gState.next_min) needsRender = true;

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
    _lastSuspended = gState.suspended;
    _lastMode = gState.mode;
    _lastNextH = gState.next_hour;
    _lastNextM = gState.next_min;

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
        char zrow[LCD_COLS+1], pb[LCD_COLS+1];
        if (zi >= NUM_ZONES) {
            snprintf(zrow, sizeof(zrow), "Erro: Zona %d", zi + 1);
        } else {
            snprintf(zrow, sizeof(zrow), "Z%d %s", zi + 1, gState.zones[zi].name);
        }
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
            uint32_t nextDay1970 = 0;
            uint8_t h = 0, m = 0;
            if (scheduler.computeNext(gState.mode, gState.now, h, m, &nextDay1970, false)) {
                DateTime localDate(gState.now.year, gState.now.month, gState.now.day, 0, 0, 0);
                uint32_t currentDay1970 = localDate.unixtime() / 86400UL;
                const char* dayStr = "";
                if (nextDay1970 == currentDay1970) {
                    dayStr = "Hoje";
                } else if (nextDay1970 == currentDay1970 + 1) {
                    dayStr = "Amanha";
                } else {
                    static const char* DOW_NAMES[] = {"Domingo", "Segunda", "Terca", "Quarta", "Quinta", "Sexta", "Sabado"};
                    uint8_t dow = (nextDay1970 + 4) % 7;
                    dayStr = DOW_NAMES[dow];
                }
                snprintf(nxstr, sizeof(nxstr), "%s as %02d:%02d", dayStr, h, m);
            } else {
                if (gState.next_hour == 255) {
                    snprintf(nxstr, sizeof(nxstr), "Proxima: --:--");
                } else {
                    snprintf(nxstr, sizeof(nxstr), "Proxima: %02d:%02d", gState.next_hour, gState.next_min);
                }
            }
            ui.getDisplay().setRows(b0, "", "", ui.getDisplay().cx(b3, nxstr));
        }
    }
}
