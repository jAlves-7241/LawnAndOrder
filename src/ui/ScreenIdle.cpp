#include "../i18n.h"
#include "ScreenIdle.h"
#include "../UI.h"
#include "../AppState.h"
#include <stdio.h>
#include "../Scheduler.h"
#include "../RTClock.h"
#include "../History.h"
#include <RTClib.h>

void ScreenIdle::onEnter(UI& ui) {
    _lastWateringActive = false;
    _lastIsWaiting = false;
    _lastProgress = 255;
    _lastMinute = 255;
    _lastSuspended = false;
    _lastMode = AppMode::MEDIO;
    _lastNextH = 0xFF;
    _lastNextM = 0xFF;
    _lastZoneIdx = 0xFF;
    _lastRtcValid = !gState.rtc_valid;
    _lastRtcPresent = !rtclock.isPresent();
    _lastBatteryLow = !rtclock.isBatteryLow();
    _lastHistReady = !history.isReady();
    _lastHistCount = 0xFFFF;
    render(ui);
}

void ScreenIdle::handleRotation(UI& /*ui*/, int8_t /*dir*/) {
    // Rotation doesn't do anything on idle
}

void ScreenIdle::handleClick(UI& ui) {
    ui.goMenu(MenuID::MAIN);
}

void ScreenIdle::update(UI& ui) {
    bool needsRender = false;

    if (_lastWateringActive != gState.watering.active ||
        _lastIsWaiting != gState.watering.is_waiting) {
        needsRender = true;
    } else if (gState.watering.active) {
        if (_lastProgress != gState.watering.progress_pct ||
            _lastZoneIdx != gState.watering.zone_idx) {
            needsRender = true;
        }
    }

    if (_lastMinute != gState.now.min) {
        needsRender = true;
    }

    if (_lastRtcValid != gState.rtc_valid) needsRender = true;
    if (_lastRtcPresent != rtclock.isPresent()) needsRender = true;
    if (_lastBatteryLow != rtclock.isBatteryLow()) needsRender = true;
    if (_lastHistReady != history.isReady() || _lastHistCount != history.entryCount()) needsRender = true;
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
    _lastIsWaiting = gState.watering.is_waiting;
    _lastProgress = gState.watering.progress_pct;
    _lastMinute = gState.now.min;
    _lastSuspended = gState.suspended;
    _lastMode = gState.mode;
    _lastNextH = gState.next_hour;
    _lastNextM = gState.next_min;
    _lastZoneIdx = gState.watering.zone_idx;
    _lastRtcValid = gState.rtc_valid;
    const bool rtcPresent = rtclock.isPresent();
    _lastRtcPresent = rtcPresent;
    const bool batLow = rtclock.isBatteryLow();
    _lastBatteryLow = batLow;
    const bool histReady = history.isReady();
    _lastHistReady = histReady;
    _lastHistCount = history.entryCount();

    char b0[LCD_COLS+1], b1[LCD_COLS+1], b2[LCD_COLS+1], b3[LCD_COLS+1];

    if (gState.rtc_valid) {
        char tstr[8];
        snprintf(tstr, sizeof(tstr), "%02d:%02d", gState.now.hour, gState.now.min);
        ui.getDisplay().cx(b0, tstr);
    } else {
        ui.getDisplay().cx(b0, "--:--");
    }

    if (gState.watering.active) {
        uint8_t zi = gState.watering.zone_idx;
        char zrow[LCD_COLS+1], pb[LCD_COLS+1];
        if (zi >= NUM_ZONES) {
            snprintf(zrow, sizeof(zrow), TXT_IDLE_ERR_ZONE, zi + 1);
        } else {
            snprintf(zrow, sizeof(zrow), "Z%d %s", zi + 1, gState.zones[zi].name);
        }
        ui.getDisplay().pbar(pb, _lastProgress);
        const char* statusLine = gState.watering.is_waiting ? TXT_IDLE_WAITING : TXT_IDLE_WATERING;
        ui.getDisplay().setRows(b0, statusLine, zrow, pb);
    } else {
        const char* warnLine = "";
        if (batLow) {
            warnLine = ui.getDisplay().cx(b1, TXT_IDLE_RTC_BAT);
        } else if (gState.rtc_valid && !rtcPresent) {
            warnLine = ui.getDisplay().cx(b1, TXT_IDLE_RTC_MISSING);
        }

        const char* lastLine = "";
        if (!histReady) {
            lastLine = ui.getDisplay().cx(b2, TXT_IDLE_HIST_ERR);
        } else {
            HistoryEntry lastEntry = {};
            if (history.readLast(1, &lastEntry) == 1) {
                static const char* MON[] = {
                    "", TXT_MONTH_JAN, TXT_MONTH_FEB, TXT_MONTH_MAR, TXT_MONTH_APR, TXT_MONTH_MAY, TXT_MONTH_JUN,
                    TXT_MONTH_JUL, TXT_MONTH_AUG, TXT_MONTH_SEP, TXT_MONTH_OCT, TXT_MONTH_NOV, TXT_MONTH_DEC
                };
                uint8_t m = (lastEntry.month >= 1 && lastEntry.month <= 12) ? lastEntry.month : 0;
                char lstr[LCD_COLS + 1];
                snprintf(lstr, sizeof(lstr), TXT_IDLE_LAST_FMT,
                         (int)lastEntry.day, MON[m], (int)lastEntry.hour, (int)lastEntry.min);
                lastLine = ui.getDisplay().cx(b2, lstr);
            }
        }

        if (!gState.rtc_valid) {
            ui.getDisplay().setRows(b0, warnLine, lastLine, ui.getDisplay().cx(b3, TXT_IDLE_SET_TIME));
        } else if (gState.suspended) {
            ui.getDisplay().setRows(b0, warnLine, lastLine, ui.getDisplay().cx(b3, TXT_IDLE_SUSPENDED));
        } else if (gState.mode == AppMode::DESATIVADO) {
            ui.getDisplay().setRows(b0, warnLine, lastLine, ui.getDisplay().cx(b3, TXT_IDLE_DISABLED));
        } else {
            char nxstr[LCD_COLS+1];
            uint32_t nextDay1970 = 0;
            uint8_t h = 0, m = 0;
            if (scheduler.computeNext(gState.mode, gState.now, h, m, &nextDay1970, false)) {
                DateTime localDate(gState.now.year, gState.now.month, gState.now.day, 0, 0, 0);
                uint32_t currentDay1970 = localDate.unixtime() / 86400UL;
                const char* dayStr = "";
                if (nextDay1970 == currentDay1970) {
                    dayStr = TXT_TODAY;
                } else if (nextDay1970 == currentDay1970 + 1) {
                    dayStr = TXT_TOMORROW;
                } else {
                    static const char* DOW_NAMES[] = {TXT_DOW_SUN, TXT_DOW_MON, TXT_DOW_TUE, TXT_DOW_WED, TXT_DOW_THU, TXT_DOW_FRI, TXT_DOW_SAT};
                    uint8_t dow = (nextDay1970 + 4) % 7;
                    dayStr = DOW_NAMES[dow];
                }
                snprintf(nxstr, sizeof(nxstr), TXT_NEXT_DAY_TIME, dayStr, h, m);
            } else {
                snprintf(nxstr, sizeof(nxstr), TXT_NEXT_NONE);
            }
            ui.getDisplay().setRows(b0, warnLine, lastLine, ui.getDisplay().cx(b3, nxstr));
        }
    }
}
