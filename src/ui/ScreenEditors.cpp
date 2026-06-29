#include "../i18n.h"
#include "ScreenEditors.h"
#include "../UI.h"
#include "../AppState.h"
#include "../Scheduler.h"
#include "../RTClock.h"
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
        if (gState.mode == AppMode::PERSONALIZADO && cs.slot_count > 1) {
            uint16_t total_dur = ui.getTotalZoneDuration();
            uint8_t enCount = 0;
            for (int z = 0; z < NUM_ZONES; z++) if (gState.zones[z].enabled) enCount++;
            if (enCount > 1) total_dur += (enCount - 1);
            bool overlap = false;
            for (int i = 0; i < cs.slot_count; i++) {
                for (int j = i + 1; j < cs.slot_count; j++) {
                    uint16_t s1 = cs.slots[i].hour * 60 + cs.slots[i].minute;
                    uint16_t s2 = cs.slots[j].hour * 60 + cs.slots[j].minute;
                    int16_t diff = (int16_t)s1 - (int16_t)s2;
                    if (diff < 0) diff = -diff;
                    if (diff > 720) diff = 1440 - diff;
                    if (diff < total_dur) { overlap = true; break; }
                }
                if (overlap) break;
            }
            if (overlap) {
                gState.zones[_durZoneIdx].enabled      = old_en;
                gState.zones[_durZoneIdx].duration_min = old_dur;
                ui.getScreenInfo().setup(TXT_ERR_OVERLAP, TXT_ERR_DUR_EXCESS,
                          TXT_ERR_FOR_CYCLES, TXT_ERR_REDUCE, _backMenu);
                ui.changeScreen(&ui.getScreenInfo());
                return;
            }
        }

        ui.flagConfigChanged();
        ui.goMenu(_backMenu);
        return;
    }

    if (_durContext == DurContext::FREQ_DAYS) {
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        cs.interval_days = (_durValue > 0) ? _durValue : 1;
        DateTime localDate(gState.now.year, gState.now.month, gState.now.day, 0, 0, 0);
        gState.custom_ref_day = localDate.unixtime() / 86400UL;
        ui.flagConfigChanged();
        scheduler.onModeChanged();
        ui.goMenu(_backMenu);
        return;
    }

    if (_durContext == DurContext::NUM_CYCLES) {
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        uint16_t total_dur = ui.getTotalZoneDuration();
        uint8_t enCount = 0;
        for (int z = 0; z < NUM_ZONES; z++) if (gState.zones[z].enabled) enCount++;
        if (enCount > 1) total_dur += (enCount - 1);
        
        if ((uint32_t)_durValue * total_dur > 1440) {
            ui.getScreenInfo().setup(TXT_ERR_TITLE, TXT_ERR_TOTAL_DUR,
                      TXT_ERR_EXCEEDS_24H, "", _backMenu);
            ui.changeScreen(&ui.getScreenInfo());
            return;
        }
        uint8_t anchor_hour = cs.slots[0].hour;
        uint8_t anchor_min = cs.slots[0].minute;
        cs.slot_count = (_durValue > 0) ? _durValue : 1;
        if (cs.slot_count == 0) cs.slot_count = 1;
        uint16_t interval = 1440 / cs.slot_count;
        for (int i = 0; i < cs.slot_count; i++) {
            uint16_t mins = (anchor_hour * 60 + anchor_min + i * interval) % 1440;
            cs.slots[i].hour   = mins / 60;
            cs.slots[i].minute = mins % 60;
        }
        for (int i = 0; i < cs.slot_count - 1; i++) {
            for (int j = 0; j < cs.slot_count - i - 1; j++) {
                uint16_t t1 = cs.slots[j].hour * 60 + cs.slots[j].minute;
                uint16_t t2 = cs.slots[j+1].hour * 60 + cs.slots[j+1].minute;
                if (t1 > t2) {
                    ScheduleSlot temp = cs.slots[j];
                    cs.slots[j] = cs.slots[j+1];
                    cs.slots[j+1] = temp;
                }
            }
        }
        ui.flagConfigChanged();
        scheduler.onModeChanged();
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
        gState.suspended_until = gState.now.unix + ((uint32_t)_durValue * 86400UL);
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

void ScreenDateEdit::onEnter(UI& ui) {
    render(ui);
}

// Internal helper logic that was static in UI
static uint8_t daysInMonthLocal(uint16_t year, uint8_t month) {
    if (month == 2) {
        bool isLeap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
        return isLeap ? 29 : 28;
    }
    if (month == 4 || month == 6 || month == 9 || month == 11) return 30;
    return 31;
}

void ScreenDateEdit::handleRotation(UI& ui, int8_t dir) {
    if (_deField == 0) {
        uint8_t max_days = daysInMonthLocal(_deYear, _deMonth);
        int d = (int)_deDay + dir;
        while (d < 1) d += max_days;
        while (d > max_days) d -= max_days;
        _deDay = (uint8_t)d;
    } else if (_deField == 1) {
        int m = (int)_deMonth + dir;
        while (m < 1) m += 12;
        while (m > 12) m -= 12;
        _deMonth = (uint8_t)m;
        uint8_t max_days = daysInMonthLocal(_deYear, _deMonth);
        if (_deDay > max_days) _deDay = max_days;
    } else {
        int y = (int)_deYear + dir;
        while (y < DATE_YEAR_MIN) y += DATE_YEAR_SPAN;
        while (y > DATE_YEAR_MAX) y -= DATE_YEAR_SPAN;
        _deYear = (uint16_t)y;
        uint8_t max_days = daysInMonthLocal(_deYear, _deMonth);
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
        if (!rtclock.isPresent()) {
            ui.getScreenInfo().setup(TXT_ERR_NO_RTC, TXT_MODULE_NOT, TXT_FOUND, "", ui.inSetup() ? _backMenu : MenuID::DEF);
            ui.changeScreen(&ui.getScreenInfo());
            return;
        }
        rtclock.set(ui.getDateEditYear(), ui.getDateEditMonth(), ui.getDateEditDay(), _teHour, _teMin, 0);

        if (ui.inSetup()) {
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
        uint16_t total_dur = ui.getTotalZoneDuration();
        uint8_t enCount = 0;
        for (int z = 0; z < NUM_ZONES; z++) if (gState.zones[z].enabled) enCount++;
        if (enCount > 1) total_dur += (enCount - 1); // incluir delays de relés
        
        uint8_t old_h = cs.slots[_teCycleIdx].hour;
        uint8_t old_m = cs.slots[_teCycleIdx].minute;
        cs.slots[_teCycleIdx].hour = _teHour;
        cs.slots[_teCycleIdx].minute = _teMin;
        
        bool overlap = false;
        for (int i = 0; i < cs.slot_count; i++) {
            for (int j = i + 1; j < cs.slot_count; j++) {
                uint16_t s1 = cs.slots[i].hour * 60 + cs.slots[i].minute;
                uint16_t s2 = cs.slots[j].hour * 60 + cs.slots[j].minute;
                int16_t diff = (int16_t)s1 - (int16_t)s2;
                if (diff < 0) diff = -diff;
                if (diff > 720) diff = 1440 - diff;
                if (diff < total_dur) { overlap = true; break; }
            }
            if (overlap) break;
        }
        
        if (overlap) {
            // Reverter alteração
            cs.slots[_teCycleIdx].hour = old_h;
            cs.slots[_teCycleIdx].minute = old_m;
            ui.getScreenInfo().setup(TXT_ERR_OVERLAP, TXT_ERR_CYCLES_CLOSE, TXT_ERR_FOR_DUR, TXT_ERR_ADJUST_TIME, _backMenu);
            ui.changeScreen(&ui.getScreenInfo());
            return;
        }

        // Ensure chronological order
        for (int i = 0; i < cs.slot_count - 1; i++) {
            for (int j = 0; j < cs.slot_count - i - 1; j++) {
                uint16_t t1 = cs.slots[j].hour * 60 + cs.slots[j].minute;
                uint16_t t2 = cs.slots[j+1].hour * 60 + cs.slots[j+1].minute;
                if (t1 > t2) {
                    ScheduleSlot temp = cs.slots[j];
                    cs.slots[j] = cs.slots[j+1];
                    cs.slots[j+1] = temp;
                }
            }
        }

        ui.flagConfigChanged();
        scheduler.onModeChanged();
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

    char vstr[12];
    if (_teField == 0) snprintf(vstr, sizeof(vstr), "[%02d] : %02d", _teHour, _teMin);
    else snprintf(vstr, sizeof(vstr), " %02d  :[%02d]", _teHour, _teMin);
    ui.getDisplay().cx(vbuf, vstr);

    ui.getDisplay().cx(fbuf, _teField == 0 ? TXT_CHOOSE_HOUR : TXT_CHOOSE_MINS);
    ui.getDisplay().cx(hintbuf, _teField == 0 ? TXT_CLICK_FOR_MINS : TXT_CLICK_TO_SAVE);

    ui.getDisplay().setRows(hbuf, vbuf, fbuf, hintbuf);
}
