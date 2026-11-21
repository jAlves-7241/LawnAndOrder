#include "../i18n.h"
#include "ScreenEditors.h"
#include "../UI.h"
#include "../AppState.h"
#include "../Scheduler.h"
#include "../RTClock.h"
#include "../log.h"
#include <stdio.h>
#include <string.h>

extern Scheduler scheduler;
extern RTClock rtclock;

// ─────────────────────────────────────────────────────────
// ScreenDurPick
// ─────────────────────────────────────────────────────────
void ScreenDurPick::_getRange(DurContext ctx, int& vmin, int& vmax) {
    vmin = 1; vmax = 20;
    if (ctx == DurContext::CFG_ZONE) {
        vmin = 0; vmax = 20;
    } else if (ctx == DurContext::SUSPEND) {
        vmin = 1; vmax = 15;
    } else if (ctx == DurContext::FREQ_DAYS) {
        vmin = 1; vmax = 14;
    } else if (ctx == DurContext::NUM_CYCLES) {
        vmin = 1; vmax = 4;
    }
}

void ScreenDurPick::setup(uint8_t initial, DurContext ctx, uint8_t zoneIdx, MenuID backMenu) {
    int vmin, vmax;
    _getRange(ctx, vmin, vmax);

    _durValue = (initial >= vmin && initial <= vmax) ? initial : (uint8_t)vmin;
    _durContext = ctx;
    _durZoneIdx = zoneIdx;
    _backMenu = backMenu;
}

void ScreenDurPick::onEnter(UI& ui) {
    render(ui);
}

void ScreenDurPick::handleRotation(UI& ui, int8_t dir) {
    int vmin, vmax;
    _getRange(_durContext, vmin, vmax);

    int v = (int)_durValue + dir;
    while (v < vmin) v += (vmax - vmin + 1);
    while (v > vmax) v -= (vmax - vmin + 1);
    _durValue = (uint8_t)v;
    render(ui);
}

void ScreenDurPick::handleClick(UI& ui) {
    if (_durContext == DurContext::CFG_ZONE) {
        if (_durZoneIdx >= NUM_ZONES) return;
        uint8_t old_dur = gState.zones[_durZoneIdx].duration_min;
        bool    old_en  = gState.zones[_durZoneIdx].enabled;
        
        gState.zones[_durZoneIdx].enabled      = (_durValue > 0);
        gState.zones[_durZoneIdx].duration_min = _durValue;

        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        if (gState.mode == AppMode::PERSONALIZADO &&
            hasCustomSlotOverlap(cs, computeMinCustomSpacing())) {
            gState.zones[_durZoneIdx].enabled      = old_en;
            gState.zones[_durZoneIdx].duration_min = old_dur;
            ui.getScreenInfo().setup(TXT_ERR_OVERLAP, TXT_ERR_DUR_EXCESS,
                      TXT_ERR_FOR_CYCLES, TXT_ERR_REDUCE, _backMenu);
            ui.changeScreen(&ui.getScreenInfo());
            return;
        }

        LOG_I(TXT_LOG_TAG_UI, TXT_LOG_UI_ZONE_SET,
              (int)_durZoneIdx + 1,
              gState.zones[_durZoneIdx].name,
              gState.zones[_durZoneIdx].enabled ? TXT_SER_ENABLED : TXT_SER_DISABLED,
              (unsigned)gState.zones[_durZoneIdx].duration_min);
        ui.flagConfigChanged();
        ui.goMenu(_backMenu);
        return;
    }

    if (_durContext == DurContext::FREQ_DAYS) {
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        uint8_t newInterval = (_durValue > 0) ? _durValue : 1;
        if (newInterval == cs.interval_days &&
            (!gState.rtc_valid || gState.custom_ref_day != 0xFFFFFFFFUL)) {
            ui.goMenu(_backMenu);
            return;
        }
        cs.interval_days = newInterval;
        if (!gState.rtc_valid) { gState.custom_ref_day = 0xFFFFFFFFUL; } else {
            DateTime localDate(gState.now.year, gState.now.month, gState.now.day, 0, 0, 0);
            gState.custom_ref_day = localDate.unixtime() / 86400UL;
        }
        LOG_I(TXT_LOG_TAG_SCHEDULE, TXT_LOG_UI_INTERVAL_SET, (unsigned)cs.interval_days);
        ui.flagConfigChanged();
        if (!ui.inSetup()) {
            scheduler.onModeChanged(false);
        }
        ui.goMenu(_backMenu);
        return;
    }

    if (_durContext == DurContext::NUM_CYCLES) {
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        if (_durValue == cs.slot_count) {
            ui.goMenu(_backMenu);
            return;
        }
        uint16_t min_spacing = computeMinCustomSpacing();
        if ((uint32_t)_durValue * (uint32_t)min_spacing > 1440UL) {
            ui.getScreenInfo().setup(TXT_ERR_TITLE, TXT_ERR_TOTAL_DUR,
                      TXT_ERR_EXCEEDS_24H, "", _backMenu);
            ui.changeScreen(&ui.getScreenInfo());
            return;
        }
        cs.slot_count = (_durValue > 0) ? _durValue : 1;
        respaceCustomSlots(cs);
        LOG_I(TXT_LOG_TAG_SCHEDULE, TXT_LOG_UI_CYCLES_SET, (unsigned)cs.slot_count);
        ui.flagConfigChanged();
        if (!ui.inSetup()) {
            scheduler.onModeChanged(false);
        }
        ui.goMenu(_backMenu);
        return;
    }

    if (_durContext == DurContext::SUSPEND) {
        if (!gState.rtc_valid) {
            ui.getScreenInfo().setup(TXT_ERR_NO_RTC, TXT_NO_VALID_TIME, TXT_ERR_NOT_POSSIBLE, TXT_ERR_SUSPEND, MenuID::PROG);
            ui.changeScreen(&ui.getScreenInfo());
            return;
        }
        gState.suspended = true;
        uint64_t until = (uint64_t)gState.now.unix + ((uint64_t)_durValue * 86400ULL);
        if (until > 4102444799ULL) until = 4102444799ULL;
        gState.suspended_until = (uint32_t)until;
        LOG_I(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SUSP_SET, (int)_durValue);
        ui.flagConfigChanged();
        char msg[21];
        snprintf(msg, sizeof(msg), TXT_PAUSE_DAYS, _durValue);
        ui.getScreenDone().setup(TXT_SUSPENDED_TITLE, msg);
        ui.getScreenDone().setBackMenu(MenuID::MAIN);
        ui.changeScreen(&ui.getScreenDone());
        return;
    }

    if (_durContext == DurContext::CUSTOM_RUN) {
        gState.custom_dur_min = _durValue;
        char zstr[LCD_COLS + 1];
        snprintf(zstr, sizeof(zstr), "%s", TXT_ZONES_PREFIX);
        bool any = false;
        for (int i = 0; i < NUM_ZONES; i++) {
            if (gState.custom_sel[i]) {
                char zid[4]; snprintf(zid, sizeof(zid), "Z%d ", i + 1);
                size_t len = strlen(zstr);
                if (len < sizeof(zstr) - 1) {
                    strncat(zstr, zid, sizeof(zstr) - len - 1);
                }
                any = true;
            }
        }
        if (!any) {
            ui.getScreenInfo().setup(TXT_ATTENTION, TXT_SELECT_AT_LEAST, TXT_ONE_ZONE, "", MenuID::CUSTOM_ZONAS);
            ui.changeScreen(&ui.getScreenInfo());
            return;
        }
        char dstr[LCD_COLS + 1];
        snprintf(dstr, sizeof(dstr), TXT_DUR_MIN_FMT, _durValue);
        ui.getScreenConfirm().setup(zstr, dstr, MenuID::MANUAL, "custom");
        ui.changeScreen(&ui.getScreenConfirm());
        return;
    }
}

void ScreenDurPick::render(UI& ui) {
    char hbuf[LCD_COLS + 1], vbuf[LCD_COLS + 1], h1[LCD_COLS + 1], h2[LCD_COLS + 1];
    const char* title = TXT_SET_VALUE;
    const char* unit  = TXT_MINUTES;

    switch (_durContext) {
        case DurContext::CFG_ZONE: title = TXT_ZONE_DURATION; break;
        case DurContext::SUSPEND: title = TXT_PAUSE_DAYS_TITLE; unit = TXT_DAYS; break;
        case DurContext::FREQ_DAYS: title = TXT_INTERVAL; unit = TXT_DAYS; break;
        case DurContext::NUM_CYCLES: title = TXT_DAILY_CYCLES; unit = TXT_CYCLES; break;
        default: title = TXT_MANUAL_WATERING; break;
    }

    ui.getDisplay().hdr(hbuf, title);

    char vstr[20];
    if (_durContext == DurContext::CFG_ZONE && _durValue == 0)
        snprintf(vstr, sizeof(vstr), TXT_DEACTIVATED);
    else
        snprintf(vstr, sizeof(vstr), "%d %s", _durValue, unit);

    ui.getDisplay().cx(vbuf, vstr);
    ui.getDisplay().cx(h1, TXT_ROTATE_TO_CHANGE);
    ui.getDisplay().cx(h2, TXT_CLICK_TO_SAVE);
    ui.getDisplay().setRows(hbuf, vbuf, h1, h2);
}

// ─────────────────────────────────────────────────────────
// ScreenDateEdit
// ─────────────────────────────────────────────────────────
void ScreenDateEdit::setup(MenuID backMenu) {
    _deDay   = gState.rtc_valid ? gState.now.day : 1;
    _deMonth = gState.rtc_valid ? gState.now.month : 1;
    _deYear  = gState.rtc_valid ? gState.now.year : 2026;
    _deField = 0; // começar no dia
    _backMenu = backMenu;
}

void ScreenDateEdit::refreshFromClock(UI& ui) {
    setup(_backMenu);
    render(ui);
}

void ScreenDateEdit::onEnter(UI& ui) {
    render(ui);
}

void ScreenDateEdit::handleRotation(UI& ui, int8_t dir) {
    if (_deField == 0) {
        uint8_t max_days = daysInMonth(_deYear, _deMonth);
        int d = (int)_deDay + dir;
        while (d < 1) d += max_days;
        while (d > max_days) d -= max_days;
        _deDay = (uint8_t)d;
    } else if (_deField == 1) {
        int m = (int)_deMonth + dir;
        while (m < 1) m += 12;
        while (m > 12) m -= 12;
        _deMonth = (uint8_t)m;
        uint8_t max_days = daysInMonth(_deYear, _deMonth);
        if (_deDay > max_days) _deDay = max_days;
    } else {
        int y = (int)_deYear + dir;
        while (y < DATE_YEAR_MIN) y += DATE_YEAR_SPAN;
        while (y > DATE_YEAR_MAX) y -= DATE_YEAR_SPAN;
        _deYear = (uint16_t)y;
        uint8_t max_days = daysInMonth(_deYear, _deMonth);
        if (_deDay > max_days) _deDay = max_days;
    }
    render(ui);
}

void ScreenDateEdit::handleClick(UI& ui) {
    if (_deField < 2) {
        _deField++;
        render(ui);
    } else {
        // Transit to time editing using UI helper
        ui.getScreenTimeEdit().setup(TimeEditContext::RTC, 0, _backMenu);
        ui.setDateCache(_deYear, _deMonth, _deDay); // Passing the date cache across
        ui.changeScreen(&ui.getScreenTimeEdit());
    }
}

void ScreenDateEdit::render(UI& ui) {
    char hbuf[LCD_COLS + 1], vbuf[LCD_COLS + 1], fbuf[LCD_COLS + 1], hintbuf[LCD_COLS + 1];
    ui.getDisplay().hdr(hbuf, TXT_DATETIME);

    char vstr[21];
    if (_deField == 0) snprintf(vstr, sizeof(vstr), "[%02d] / %02d / %04d", _deDay, _deMonth, _deYear);
    else if (_deField == 1) snprintf(vstr, sizeof(vstr), " %02d /[%02d]/ %04d", _deDay, _deMonth, _deYear);
    else snprintf(vstr, sizeof(vstr), " %02d / %02d /[%04d]", _deDay, _deMonth, _deYear);
    ui.getDisplay().cx(vbuf, vstr);

    if (_deField == 0) ui.getDisplay().cx(fbuf, TXT_CHOOSE_DAY);
    else if (_deField == 1) ui.getDisplay().cx(fbuf, TXT_CHOOSE_MONTH);
    else ui.getDisplay().cx(fbuf, TXT_CHOOSE_YEAR);

    if (_deField == 0) ui.getDisplay().cx(hintbuf, TXT_CLICK_FOR_MONTH);
    else if (_deField == 1) ui.getDisplay().cx(hintbuf, TXT_CLICK_FOR_YEAR);
    else ui.getDisplay().cx(hintbuf, TXT_CLICK_FOR_HOUR);

    ui.getDisplay().setRows(hbuf, vbuf, fbuf, hintbuf);
}

// ─────────────────────────────────────────────────────────
// ScreenTimeEdit
// ─────────────────────────────────────────────────────────
void ScreenTimeEdit::setup(TimeEditContext ctx, uint8_t cycleIdx, MenuID backMenu) {
    _teContext  = ctx;
    _teCycleIdx = (cycleIdx < MAX_SLOTS_PER_MODE) ? cycleIdx : 0;

    if (ctx == TimeEditContext::RTC) {
        _teHour   = gState.rtc_valid ? gState.now.hour : 0;
        _teMin    = gState.rtc_valid ? gState.now.min  : 0;
    } else {
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        _teHour   = cs.slots[_teCycleIdx].hour;
        _teMin    = cs.slots[_teCycleIdx].minute;
    }

    _teField = 0;  // start on hour
    _backMenu = backMenu;
}

void ScreenTimeEdit::refreshFromClock(UI& ui) {
    if (_teContext != TimeEditContext::RTC) return;
    if (gState.rtc_valid) {
        ui.setDateCache(gState.now.year, gState.now.month, gState.now.day);
    }
    setup(_teContext, _teCycleIdx, _backMenu);
    render(ui);
}

void ScreenTimeEdit::onEnter(UI& ui) {
    render(ui);
}

void ScreenTimeEdit::handleRotation(UI& ui, int8_t dir) {
    if (_teField == 0) {
        int h = (int)_teHour + dir;
        while (h < 0) h += 24;
        _teHour = (uint8_t)(h % 24);
    } else {
        int m = (int)_teMin + dir;
        while (m < 0) m += 60;
        _teMin = (uint8_t)(m % 60);
    }
    render(ui);
}

void ScreenTimeEdit::handleClick(UI& ui) {
    if (_teField == 0) {
        _teField = 1;
        render(ui);
        return;
    }

    if (_teContext == TimeEditContext::RTC) {
        if (!rtclock.isPresent() && !rtclock.probe()) {
            LOG_W(TXT_LOG_TAG_CLOCK, TXT_LOG_RTC_NOT_FOUND);
            if (ui.inSetup()) {
                ui.getScreenConfirm().setup(TXT_SETUP_NO_RTC, TXT_SETUP_SKIP_TIME,
                                            MenuID::SETUP_MODE, "skip_clock");
                ui.changeScreen(&ui.getScreenConfirm());
            } else {
                ui.getScreenInfo().setup(TXT_ERR_NO_RTC, TXT_MODULE_NOT,
                                         TXT_FOUND, "", MenuID::DEF);
                ui.changeScreen(&ui.getScreenInfo());
            }
            return;
        }

        bool hwOk = rtclock.set(ui.getDateEditYear(), ui.getDateEditMonth(),
                    ui.getDateEditDay(), _teHour, _teMin, 0, false);
        if (!hwOk) {
            if (ui.inSetup()) {
                ui.getScreenConfirm().setup(TXT_SETUP_NO_RTC, TXT_SETUP_SKIP_TIME,
                                            MenuID::SETUP_MODE, "skip_clock");
                ui.changeScreen(&ui.getScreenConfirm());
            } else {
                ui.getScreenInfo().setup(TXT_ERR_NO_RTC, TXT_MODULE_NOT,
                                         TXT_FOUND, "", MenuID::DEF);
                ui.changeScreen(&ui.getScreenInfo());
            }
            return;
        }
        if (!ui.inSetup()) {
            scheduler.onModeChanged(false);
        } else {
            ui.advanceSetup();
            return;
        }

        char saved[LCD_COLS + 1];
        snprintf(saved, sizeof(saved), TXT_TIME_FMT, _teHour, _teMin);
        ui.getScreenDone().setup(saved, TXT_RTC_UPDATED);
        ui.getScreenDone().setBackMenu(MenuID::DEF);
        ui.changeScreen(&ui.getScreenDone());
    } else {
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];

        // Validar sobreposições antes de guardar
        uint8_t old_h = cs.slots[_teCycleIdx].hour;
        uint8_t old_m = cs.slots[_teCycleIdx].minute;
        cs.slots[_teCycleIdx].hour = _teHour;
        cs.slots[_teCycleIdx].minute = _teMin;

        if (hasCustomSlotOverlap(cs, computeMinCustomSpacing())) {
            // Reverter alteração
            cs.slots[_teCycleIdx].hour = old_h;
            cs.slots[_teCycleIdx].minute = old_m;
            ui.getScreenInfo().setup(TXT_ERR_OVERLAP, TXT_ERR_CYCLES_CLOSE,
                                     TXT_ERR_FOR_DUR, TXT_ERR_ADJUST_TIME,
                                     _backMenu);
            ui.changeScreen(&ui.getScreenInfo());
            return;
        }

        // Ensure chronological order
        sortCustomSlots(cs);
#if LOG_LEVEL >= LVL_INFO
        uint8_t loggedIdx = _teCycleIdx;
        for (uint8_t i = 0; i < cs.slot_count; i++) {
            if (cs.slots[i].hour == _teHour && cs.slots[i].minute == _teMin) {
                loggedIdx = i;
                break;
            }
        }
        LOG_I(TXT_LOG_TAG_SCHEDULE, TXT_LOG_UI_SLOT_SET,
              (int)loggedIdx + 1, (unsigned)_teHour, (unsigned)_teMin);
#endif
        ui.flagConfigChanged();
        if (!ui.inSetup()) {
            scheduler.onModeChanged(false);
        }
        ui.goMenu(_backMenu);
    }
}

void ScreenTimeEdit::render(UI& ui) {
    char hbuf[LCD_COLS + 1], vbuf[LCD_COLS + 1], fbuf[LCD_COLS + 1], hintbuf[LCD_COLS + 1];
    const char* title = TXT_DATETIME;
    char cbuf[21];

    if (_teContext == TimeEditContext::CUSTOM_CYCLE) {
        snprintf(cbuf, sizeof(cbuf), TXT_CYCLE_TIME_FMT, _teCycleIdx + 1);
        title = cbuf;
    }

    ui.getDisplay().hdr(hbuf, title);

    char vstr[16];
    if (_teField == 0) snprintf(vstr, sizeof(vstr), "[%02d] : %02d", _teHour, _teMin);
    else snprintf(vstr, sizeof(vstr), " %02d  :[%02d]", _teHour, _teMin);
    ui.getDisplay().cx(vbuf, vstr);

    ui.getDisplay().cx(fbuf, _teField == 0 ? TXT_CHOOSE_HOUR : TXT_CHOOSE_MINS);
    ui.getDisplay().cx(hintbuf, _teField == 0 ? TXT_CLICK_FOR_MINS : TXT_CLICK_TO_SAVE);

    ui.getDisplay().setRows(hbuf, vbuf, fbuf, hintbuf);
}
