#include "UI.h"
#include "WateringController.h"
#include "RTClock.h"
#include "Scheduler.h"
#include "Storage.h"
#include "History.h"
#include <string.h>
#include <stdio.h>
#include "log.h"

// ─────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────
static void safe_copy(char* dst, const char* src, size_t max) {
    strncpy(dst, src, max - 1);
    dst[max - 1] = '\0';
}

static void makeItem(MenuItem* it, const char* label, const char* action) {
    safe_copy(it->label,  label,  sizeof(it->label));
    safe_copy(it->action, action, sizeof(it->action));
}

// Retorna o número de dias de um determinado mês (inclui anos bissextos)
static uint8_t daysInMonth(uint16_t year, uint8_t month) {
    if (month == 2) {
        bool isLeap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
        return isLeap ? 29 : 28;
    }
    if (month == 4 || month == 6 || month == 9 || month == 11) return 30;
    return 31;
}

// ─────────────────────────────────────────────────────────
// Constructor / begin
// ─────────────────────────────────────────────────────────
UI::UI(Display& disp, Encoder& enc)
    : _d(disp), _enc(enc),
      _screen(Screen::IDLE), _mid(MenuID::MAIN),
      _cur(0), _off(0), _backMenu(MenuID::MAIN),
      _durValue(5), _durContext(DurContext::CUSTOM_RUN), _durZoneIdx(0),
      _deDay(1), _deMonth(1), _deYear(2026), _deField(0),
      _teHour(0), _teMin(0), _teField(0),
      _teContext(TimeEditContext::RTC), _teCycleIdx(0),
      _lastActivity(0), _itemCount(0),
      _setupStep(SetupStep::WELCOME), _inSetup(false),
      _configChanged(false)
{
    _pendingConfirmTag[0] = '\0';
}

void UI::begin() {
    _lastActivity = millis();
    if (!gState.setup_done) {
        _startSetup();
    } else {
        _renderIdle();
    }
}

// ─────────────────────────────────────────────────────────
void UI::_startSetup() {
    _inSetup   = true;
    _setupStep = SetupStep::WELCOME;
    _screen    = Screen::SETUP_WELCOME;
    _renderSetupWelcome();
}

void UI::_advanceSetup() {
    switch (_setupStep) {
        case SetupStep::WELCOME:
            _setupStep = SetupStep::DATE_TIME;
            _showDateEdit();
            break;

        case SetupStep::DATE_TIME:
            _setupStep = SetupStep::MODE_SELECT;
            _goMenu(MenuID::SETUP_MODE);
            break;

        case SetupStep::MODE_SELECT:
            // Só chega aqui via "Saltar >" → salta modo E zonas
            _setupStep = SetupStep::COMPLETE;
            _screen    = Screen::SETUP_WELCOME;
            _renderSetupComplete();
            break;

        case SetupStep::CUSTOM_CONFIG:
            _setupStep = SetupStep::ZONE_CONFIG;
            _goMenu(MenuID::SETUP_ZONES);
            break;

        case SetupStep::ZONE_CONFIG:
            _setupStep = SetupStep::COMPLETE;
            _screen    = Screen::SETUP_WELCOME;
            _renderSetupComplete();
            break;

        case SetupStep::COMPLETE:
            LOG_I("UI", "Setup Wizard concluido");
            _inSetup = false;
            gState.setup_done = true;
            storage.save();
            scheduler.onModeChanged();
            _goIdle();
            break;
    }
}

// ─────────────────────────────────────────────────────────
// update()
// ─────────────────────────────────────────────────────────
void UI::update() {
    if (_screen == Screen::IDLE && _configChanged) {
        storage.save();
        _configChanged = false;
    }

    int8_t rot   = _enc.getRotation();
    bool   click = _enc.getClick();

    bool hadInput = (rot != 0 || click);

    // Any input wakes the display first; if it was off, swallow the input
    // so the user doesn't accidentally trigger an action while waking.
    if (hadInput && (!_d.isBacklightOn() || !_d.isDisplayOn())) {
        _d.displayOn();
        _d.backlightOn();
        _lastActivity = millis();
        _renderIdle();   // repaint immediately after wake
        return;          // swallow input - next press will act normally
    }

    if (rot != 0) { _lastActivity = millis(); _handleRotation(rot); }
    if (click)    { _lastActivity = millis(); _handleClick(); }

    // Idle timeout - applies to all screens, any unsaved edits are discarded
    if (_screen != Screen::IDLE && !_inSetup &&
        (millis() - _lastActivity) >= IDLE_TIMEOUT_MS) {
        _goIdle();
    }

    // Backlight sleep after inactivity (only from idle screen)
    if (_screen == Screen::IDLE && !_inSetup && gState.backlight_timeout_ms != BACKLIGHT_TIMEOUT_NEVER) {
        uint32_t elapsed = millis() - _lastActivity;
        
        if (_d.isBacklightOn() && elapsed >= gState.backlight_timeout_ms) {
            _d.backlightOff();
        }
        
        if (_d.isDisplayOn() && elapsed >= (gState.backlight_timeout_ms + DISPLAY_OFF_DELAY_MS)) {
            _d.displayOff();
        }
    }

    // Clock refresh: repaint idle screen periodically
    static uint32_t lastClockMs = 0;
    static uint8_t lastMin = 0xFF;
    
    if (_screen == Screen::IDLE && _d.isDisplayOn()) {
        bool needsRedraw = false;
        uint32_t nowMs = millis();
        if (gState.watering.active) {
            // When watering is active, redraw every second to update progress/durations
            if (nowMs - lastClockMs >= 1000) {
                needsRedraw = true;
            }
        } else {
            // When idle and not watering, only redraw on minute boundary to reduce I2C traffic
            if (gState.now.min != lastMin) {
                needsRedraw = true;
            }
        }

        if (needsRedraw) {
            lastClockMs = nowMs;
            lastMin = gState.now.min;
            _renderIdle();
        }
    }
}

// ─────────────────────────────────────────────────────────
// Input handlers
// ─────────────────────────────────────────────────────────
void UI::_handleRotation(int8_t dir) {
    switch (_screen) {
        case Screen::IDLE:
            _goMenu(MenuID::MAIN);
            break;

        case Screen::MENU: {
            if (_itemCount == 0) break;  // guard: empty menu
            int newCur = (int)_cur + dir;
            while (newCur < 0) newCur += _itemCount;
            _cur = (uint8_t)(newCur % _itemCount);
            _renderMenu();
            break;
        }

        case Screen::INFO:
        case Screen::DONE:
        case Screen::CONFIRM:
            _goMenu(_backMenu);
            break;

        case Screen::DUR_PICK: {
            int vmin = 1;
            int vmax = 20;

            if (_durContext == DurContext::CFG_ZONE) {
                vmin = 0; vmax = 20;
            } else if (_durContext == DurContext::SUSPEND) {
                vmin = 1; vmax = 15;
            } else if (_durContext == DurContext::FREQ_DAYS) {
                vmin = 1; vmax = 14;
            } else if (_durContext == DurContext::NUM_CYCLES) {
                vmin = 1; vmax = 4;
            }

            int v = (int)_durValue + dir;
            while (v < vmin) v += (vmax - vmin + 1);
            while (v > vmax) v -= (vmax - vmin + 1);
            _durValue = (uint8_t)v;
            _renderDurPick();
            break;
        }

        case Screen::DATE_EDIT:
            if (_deField == 0) {
                // Dia: wrap consoante o mês
                uint8_t max_days = daysInMonth(_deYear, _deMonth);
                int d = (int)_deDay + dir;
                while (d < 1) d += max_days;
                while (d > max_days) d -= max_days;
                _deDay = (uint8_t)d;
            } else if (_deField == 1) {
                // Mês: wrap 1-12
                int m = (int)_deMonth + dir;
                while (m < 1) m += 12;
                while (m > 12) m -= 12;
                _deMonth = (uint8_t)m;
                // Re-validar o dia se o mês encolheu
                uint8_t max_days = daysInMonth(_deYear, _deMonth);
                if (_deDay > max_days) _deDay = max_days;
            } else {
                // Ano: limite parametrizado
                int y = (int)_deYear + dir;
                while (y < DATE_YEAR_MIN) y += DATE_YEAR_SPAN;
                while (y > DATE_YEAR_MAX) y -= DATE_YEAR_SPAN;
                _deYear = (uint16_t)y;
                uint8_t max_days = daysInMonth(_deYear, _deMonth);
                if (_deDay > max_days) _deDay = max_days;
            }
            _renderDateEdit();
            break;

        case Screen::TIME_EDIT:
            if (_teField == 0) {
                // Editing hour: 0–23, wraps
                int h = (int)_teHour + dir;
                while (h < 0) h += 24;
                _teHour = (uint8_t)(h % 24);
            } else {
                // Editing minute: 0–59, wraps
                int m = (int)_teMin + dir;
                while (m < 0) m += 60;
                _teMin = (uint8_t)(m % 60);
            }
            _renderTimeEdit();
            break;

        case Screen::SETUP_WELCOME:
            // Ignorar rotação - apenas click avança
            break;
    }
}

void UI::_handleClick() {
    switch (_screen) {
        case Screen::SETUP_WELCOME:
            _advanceSetup();
            break;

        case Screen::IDLE:
            _goMenu(MenuID::MAIN);
            break;

        case Screen::MENU:
            if (_cur < _itemCount)
                _dispatch(_items[_cur].action);
            break;

        case Screen::INFO:
        case Screen::DONE:
            _goMenu(_backMenu);
            break;

        case Screen::CONFIRM:
            if (!_executeConfirmed())
                _showDone(_confirmRows[1], _confirmRows[2]);
            break;

        case Screen::DUR_PICK:
            _commitDurPick();
            break;

        case Screen::DATE_EDIT:
            _commitDateEdit();
            break;

        case Screen::TIME_EDIT:
            _commitTimeEdit();
            break;
    }
}

// ─────────────────────────────────────────────────────────
// Navigation helpers
// ─────────────────────────────────────────────────────────
void UI::_goMenu(MenuID mid) {
    _screen = Screen::MENU;
    _mid    = mid;
    _cur    = 0;
    _off    = 0;
    _buildMenu(mid);
    _renderMenu();
}

void UI::_goIdle() {
    if (_configChanged) {
        storage.save();
        _configChanged = false;
    }
    _screen = Screen::IDLE;
    _renderIdle();
}

void UI::_showInfo(const char* l0, const char* l1,
                   const char* l2, const char* l3, MenuID back) {
    char buf[LCD_COLS + 1];
    safe_copy(_infoRows[0], _d.cx(buf, l0), LCD_COLS + 1);
    safe_copy(_infoRows[1], _d.fx(buf, l1), LCD_COLS + 1);
    safe_copy(_infoRows[2], _d.fx(buf, l2), LCD_COLS + 1);
    safe_copy(_infoRows[3], _d.fx(buf, l3), LCD_COLS + 1);
    _backMenu = back;
    _screen   = Screen::INFO;
    _renderInfo();
}

void UI::_showConfirm(const char* l1, const char* l2,
                      MenuID back, const char* tag) {
    char buf[LCD_COLS + 1];
    safe_copy(_confirmRows[0], _d.cx(buf, "-- CONFIRMAR? --"),   LCD_COLS + 1);
    safe_copy(_confirmRows[1], _d.fx(buf, l1),                   LCD_COLS + 1);
    safe_copy(_confirmRows[2], _d.fx(buf, l2),                   LCD_COLS + 1);
    safe_copy(_confirmRows[3], _d.cx(buf, "[OK]      [Voltar]"), LCD_COLS + 1);
    safe_copy(_pendingConfirmTag, tag, sizeof(_pendingConfirmTag));
    _backMenu = back;
    _screen   = Screen::CONFIRM;
    _renderConfirm();
}

void UI::_showDone(const char* l1, const char* l2) {
    char buf[LCD_COLS + 1];
    safe_copy(_infoRows[0], _d.cx(buf, "-- EXECUTADO --"),  LCD_COLS + 1);
    safe_copy(_infoRows[1], _d.fx(buf, l1),                 LCD_COLS + 1);
    safe_copy(_infoRows[2], _d.fx(buf, l2),                 LCD_COLS + 1);
    safe_copy(_infoRows[3], _d.cx(buf, "Click p/ voltar"),  LCD_COLS + 1);
    _screen = Screen::DONE;
    _renderDone();
}

void UI::_showDurPick(uint8_t initial, DurContext ctx, uint8_t zoneIdx) {
    // Limits based on context
    int vmin = 1, vmax = 20;

    if (ctx == DurContext::CFG_ZONE) {
        vmin = 0; vmax = 20;
    } else if (ctx == DurContext::SUSPEND) {
        vmin = 1; vmax = 15;
    } else if (ctx == DurContext::FREQ_DAYS) {
        vmin = 1; vmax = 14;
    } else if (ctx == DurContext::NUM_CYCLES) {
        vmin = 1; vmax = 4;
    }

    _durValue = (initial >= vmin && initial <= vmax) ? initial : (uint8_t)vmin;

    // ── Duration picker ───────────────────────────────────
    _durContext  = ctx;
    _durZoneIdx  = zoneIdx;
    _screen      = Screen::DUR_PICK;
    _renderDurPick();
}

void UI::_commitDurPick() {
    if (_durContext == DurContext::CFG_ZONE) {
        uint8_t old_dur = gState.zones[_durZoneIdx].duration_min;
        bool    old_en  = gState.zones[_durZoneIdx].enabled;
        
        gState.zones[_durZoneIdx].enabled      = (_durValue > 0);
        gState.zones[_durZoneIdx].duration_min = _durValue;

        // Overlap check for Personalizado mode if it has multiple cycles
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        if (cs.slot_count > 1) {
            uint16_t total_dur = _totalZoneDuration();
            // Incluir tempo de espera entre zonas (~8s por gap, arredondado para 1 min)
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
                    if (diff <= total_dur) { overlap = true; break; }
                }
                if (overlap) break;
            }
            if (overlap) {
                gState.zones[_durZoneIdx].enabled      = old_en;
                gState.zones[_durZoneIdx].duration_min = old_dur;
                _showInfo("! SOBREPOSICAO !", "Duracao excessiva",
                          "para ciclos pers.", "Reduza zonas/ciclos",
                          _inSetup ? MenuID::SETUP_ZONES : MenuID::CFG_ZONAS);
                return;
            }
        }

        _configChanged = true;
        _goMenu(_inSetup ? MenuID::SETUP_ZONES : MenuID::CFG_ZONAS);
        return;
    }

    if (_durContext == DurContext::FREQ_DAYS) {
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        cs.interval_days = (_durValue > 0) ? _durValue : 1;
        DateTime localDate(gState.now.year, gState.now.month, gState.now.day, 0, 0, 0);
        gState.custom_ref_day = localDate.unixtime() / 86400UL;
        _configChanged = true;
        scheduler.onModeChanged();
        _goMenu(_inSetup ? MenuID::SETUP_CUSTOM : MenuID::CFG_CUSTOM);
        return;
    }

    if (_durContext == DurContext::NUM_CYCLES) {
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        uint16_t total_dur = _totalZoneDuration();
        if ((uint32_t)_durValue * total_dur > 1440) {
            _showInfo("! ERRO !", "Duracao total",
                      "excede 24h.", "", _inSetup ? MenuID::SETUP_CUSTOM : MenuID::CFG_CUSTOM);
            return;
        }
        // Obter a hora do primeiro ciclo como âncora para manter a preferência do utilizador
        uint8_t anchor_hour = cs.slots[0].hour;
        uint8_t anchor_min = cs.slots[0].minute;

        cs.slot_count = (_durValue > 0) ? _durValue : 1;
        
        // Distribuição circular perfeita (equidistante no círculo de 24 horas)
        uint16_t interval = 1440 / cs.slot_count;
        for (int i = 0; i < cs.slot_count; i++) {
            uint16_t mins = (anchor_hour * 60 + anchor_min + i * interval) % 1440;
            cs.slots[i].hour   = mins / 60;
            cs.slots[i].minute = mins % 60;
        }
        // Sort slots chronologically (Bubble sort for small array)
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
        _configChanged = true;
        scheduler.onModeChanged();
        _goMenu(_inSetup ? MenuID::SETUP_CUSTOM : MenuID::CFG_CUSTOM);
        return;
    }

    if (_durContext == DurContext::SUSPEND) {
        if (!gState.rtc_valid) {
            _showInfo("! SEM RTC !", "Sem hora valida -", "nao e possivel", "suspender rega.", MenuID::PROG);
            return;
        }
        LOG_I("UI", "Acao: Suspender rega %d dias", _durValue);
        gState.suspended = true;
        // _durValue is days. 1 day = 86400 seconds.
        gState.suspended_until = gState.now.unix + ((uint32_t)_durValue * 86400UL);
        _configChanged = true;
        char msg[21];
        snprintf(msg, sizeof(msg), "Pausa: %d dias", _durValue);
        _showDone("REGA SUSPENSA", msg);
        return;
    }

    // CUSTOM_RUN
    gState.custom_dur_min = _durValue;
    char zstr[LCD_COLS + 1] = "Zonas: ";
    bool any = false;
    for (int i = 0; i < NUM_ZONES; i++) {
        if (gState.custom_sel[i]) {
            char zid[4]; snprintf(zid, sizeof(zid), "Z%d ", i + 1);
            strncat(zstr, zid, LCD_COLS - strlen(zstr));
            any = true;
        }
    }
    if (!any) {
        _showInfo("! ATENCAO !", "Seleciona pelo",
                  "menos 1 zona.", "", MenuID::CUSTOM_ZONAS);
        return;
    }
    char dstr[LCD_COLS + 1];
    snprintf(dstr, sizeof(dstr), "Duracao: %d min", _durValue);
    _showConfirm(zstr, dstr, MenuID::MANUAL, "custom");
}

void UI::_showDateEdit() {
    _deDay   = gState.rtc_valid ? gState.now.day : 1;
    _deMonth = gState.rtc_valid ? gState.now.month : 1;
    _deYear  = gState.rtc_valid ? gState.now.year : 2026;
    _deField = 0; // começar no dia
    _backMenu = MenuID::DEF;
    _screen  = Screen::DATE_EDIT;
    _renderDateEdit();
}

void UI::_commitDateEdit() {
    if (_deField < 2) {
        _deField++;
        _renderDateEdit();
    } else {
        // Transitar para edição de tempo (preservando o contexto RTC)
        _showTimeEdit(TimeEditContext::RTC);
    }
}

void UI::_showTimeEdit(TimeEditContext ctx, uint8_t cycleIdx) {
    _teContext  = ctx;
    _teCycleIdx = cycleIdx;

    if (ctx == TimeEditContext::RTC) {
        _teHour   = gState.rtc_valid ? gState.now.hour : 0;
        _teMin    = gState.rtc_valid ? gState.now.min  : 0;
        _backMenu = MenuID::DEF;
    } else {
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        _teHour   = cs.slots[cycleIdx].hour;
        _teMin    = cs.slots[cycleIdx].minute;
        _backMenu = _inSetup ? MenuID::SETUP_CUSTOM : MenuID::CFG_CUSTOM;
    }

    _teField = 0;  // start on hour
    _screen  = Screen::TIME_EDIT;
    _renderTimeEdit();
}

// Click cycles: hour → minute → commit & save
void UI::_commitTimeEdit() {
    if (_teField == 0) {
        _teField = 1;  // move to minute
        _renderTimeEdit();
        return;
    }

    if (_teContext == TimeEditContext::RTC) {
        if (!rtclock.isValid()) {
            _showInfo("! SEM RTC !", "Modulo nao",
                      "encontrado.", "", MenuID::DEF);
            return;
        }
        // Save using the full date from _deDay/_deMonth/_deYear and new time
        rtclock.set(_deYear, _deMonth, _deDay, _teHour, _teMin, 0);

        if (_inSetup) {
            _advanceSetup();  // DATE_TIME → MODE_SELECT
            return;           // ← previne fallthrough para _showDone
        }

        char saved[LCD_COLS + 1];
        snprintf(saved, sizeof(saved), "Hora: %02d:%02d", _teHour, _teMin);
        _showDone(saved, "RTC actualizado!");
        _backMenu = MenuID::DEF;

    } else {
        // CUSTOM_CYCLE
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        uint16_t new_start = _teHour * 60 + _teMin;
        uint16_t total_dur = _totalZoneDuration();
        // Incluir tempo de espera entre zonas (~8s por gap, arredondado para 1 min)
        uint8_t enCount = 0;
        for (int z = 0; z < NUM_ZONES; z++) if (gState.zones[z].enabled) enCount++;
        if (enCount > 1) total_dur += (enCount - 1);

        for (int i = 0; i < cs.slot_count; i++) {
            if (i == _teCycleIdx) continue;
            uint16_t other_start = cs.slots[i].hour * 60 + cs.slots[i].minute;
            int16_t diff = (int16_t)new_start - (int16_t)other_start;
            if (diff < 0) diff = -diff;
            if (diff > 720) diff = 1440 - diff; // shortest distance on 24h circle

            if (diff <= total_dur) {
                _showInfo("! SOBREPOSICAO !", "Ciclos muito",
                          "proximos.", "Ajuste tempo/zona", _inSetup ? MenuID::SETUP_CUSTOM : MenuID::CFG_CUSTOM);
                return;
            }
        }
        cs.slots[_teCycleIdx].hour   = _teHour;
        cs.slots[_teCycleIdx].minute = _teMin;

        // Sort slots chronologically (Bubble sort for small array)
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

        _configChanged = true;
        scheduler.onModeChanged();
        _goMenu(_inSetup ? MenuID::SETUP_CUSTOM : MenuID::CFG_CUSTOM);
    }
}

// ─────────────────────────────────────────────────────────
// _executeConfirmed - real-world side-effects on OK
// ─────────────────────────────────────────────────────────
bool UI::_executeConfirmed() {
    const char* tag = _pendingConfirmTag;

    if (strcmp(tag, "general") == 0) {
        LOG_I("UI", "Acao: Rega manual geral");
        wateringCtrl.startGeneral();

    } else if (strcmp(tag, "custom") == 0) {
        LOG_I("UI", "Acao: Rega personalizada");
        wateringCtrl.startCustom(gState.custom_sel, gState.custom_dur_min);

    } else if (strcmp(tag, "reset") == 0) {
        LOG_I("UI", "Acao: Reset de fabrica");
        storage.clear();
        history.clear();
        initAppState();
        scheduler.onModeChanged();
        wateringCtrl.stop();
        _startSetup();
        return true;

    } else if (strcmp(tag, "test_all") == 0) {
        LOG_I("UI", "Acao: Teste todas as zonas");
        wateringCtrl.startTest(-1);

    } else if (strncmp(tag, "test_", 5) == 0) {
        int8_t z = (int8_t)atoi(tag + 5);
        LOG_I("UI", "Acao: Teste zona %d", z + 1);
        wateringCtrl.startTest(z);
    }
    // tag == "" → no side-effect needed
    return false;
}

// ─────────────────────────────────────────────────────────
// Action dispatcher
// ─────────────────────────────────────────────────────────
void UI::_dispatch(const char* action) {

    // go:<menu|idle>
    if (strncmp(action, "go:", 3) == 0) {
        const char* t = action + 3;
        if (strcmp(t, "idle") == 0) _goIdle();
        else                        _goMenu(_parseMenuID(t));
        return;
    }

    // sel:<idx> - change AppMode
    if (strncmp(action, "sel:", 4) == 0) {
        uint8_t idx = (uint8_t)atoi(action + 4);
        if (idx < (uint8_t)AppMode::_COUNT)
            gState.mode = (AppMode)idx;
        
        LOG_I("UI", "Modo selecionado: %s", _modeName(gState.mode));
        scheduler.onModeChanged();
        _configChanged = true;

        if (_inSetup) {
            if (gState.mode == AppMode::PERSONALIZADO) {
                _setupStep = SetupStep::CUSTOM_CONFIG;
                _goMenu(MenuID::SETUP_CUSTOM);
            } else {
                _setupStep = SetupStep::ZONE_CONFIG;
                _goMenu(MenuID::SETUP_ZONES);
            }
            return;
        }

        if (gState.mode == AppMode::PERSONALIZADO) {
            _goMenu(MenuID::CFG_CUSTOM);
        } else {
            _showInfo("MODO SELECIONADO", _modeName(gState.mode),
                      "", "Guardado com sucesso", MenuID::MODOS);
        }
        return;
    }

    // setup_advance - avançar (usado por "Saltar >" e "Terminar >")
    if (strcmp(action, "setup_advance") == 0) {
        _advanceSetup();
        return;
    }

    // setup_back - voltar ao passo anterior
    if (strcmp(action, "setup_back") == 0) {
        switch (_setupStep) {
            case SetupStep::MODE_SELECT:
                _setupStep = SetupStep::DATE_TIME;
                _showDateEdit();
                break;
            case SetupStep::CUSTOM_CONFIG:
                _setupStep = SetupStep::MODE_SELECT;
                _goMenu(MenuID::SETUP_MODE);
                break;
            case SetupStep::ZONE_CONFIG:
                if (gState.mode == AppMode::PERSONALIZADO) {
                    _setupStep = SetupStep::CUSTOM_CONFIG;
                    _goMenu(MenuID::SETUP_CUSTOM);
                } else {
                    _setupStep = SetupStep::MODE_SELECT;
                    _goMenu(MenuID::SETUP_MODE);
                }
                break;
            default:
                break;
        }
        return;
    }

    // cfgz:<idx> - open DUR_PICK for zone configuration
    if (strncmp(action, "cfgz:", 5) == 0) {
        uint8_t i = (uint8_t)atoi(action + 5);
        if (i >= NUM_ZONES) return;
        Zone& z = gState.zones[i];
        if (!z.enabled) {
            z.enabled      = true;
            z.duration_min = 5;
        }
        _showDurPick(z.duration_min, DurContext::CFG_ZONE, i);
        return;
    }

    // cz:<idx> - toggle custom zone selection
    if (strncmp(action, "cz:", 3) == 0) {
        uint8_t i = (uint8_t)atoi(action + 3);
        if (i >= NUM_ZONES) return;
        gState.custom_sel[i] = !gState.custom_sel[i];
        _buildMenu(MenuID::CUSTOM_ZONAS);
        _renderMenu();
        return;
    }

    // dur_pick:custom - open DUR_PICK for a custom manual run
    if (strcmp(action, "dur_pick:custom") == 0) {
        bool any = false;
        for (int i = 0; i < NUM_ZONES; i++) any |= gState.custom_sel[i];
        if (!any) {
            _showInfo("! ATENCAO !", "Seleciona pelo",
                      "menos 1 zona.", "", MenuID::CUSTOM_ZONAS);
            return;
        }
        _showDurPick(gState.custom_dur_min, DurContext::CUSTOM_RUN);
        return;
    }

    if (strcmp(action, "dur_pick:freq") == 0) {
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        _showDurPick(cs.interval_days, DurContext::FREQ_DAYS);
        return;
    }

    if (strcmp(action, "dur_pick:cycles") == 0) {
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        _showDurPick(cs.slot_count, DurContext::NUM_CYCLES);
        return;
    }

    // date_edit:rtc
    if (strcmp(action, "date_edit:rtc") == 0) {
        _showDateEdit();
        return;
    }

    // toggle_dst
    if (strcmp(action, "toggle_dst") == 0) {
        gState.auto_dst = !gState.auto_dst;
        LOG_I("UI", "DST alterado: %s", gState.auto_dst ? "Auto" : "Fixo");
        _configChanged = true;
        rtclock.update(); // read offset immediately
        _buildMenu(MenuID::DEF_AVANCADO);
        _renderMenu();
        return;
    }

    // time_edit:rtc or time_edit:c<idx>
    if (strncmp(action, "time_edit:", 10) == 0) {
        const char* t = action + 10;
        if (strcmp(t, "rtc") == 0) {
            _showTimeEdit(TimeEditContext::RTC);
        } else if (t[0] == 'c') {
            uint8_t idx = (uint8_t)atoi(t + 1);
            if (idx < MAX_SLOTS_PER_MODE)
                _showTimeEdit(TimeEditContext::CUSTOM_CYCLE, idx);
        }
        return;
    }

    // info:<l0>|<l1>|<l2>|<l3>|<back>
    if (strncmp(action, "info:", 5) == 0) {
        char buf[128]; safe_copy(buf, action + 5, sizeof(buf));
        char* p[5] = {};
        uint8_t n = 0;
        char* tok = strtok(buf, "|");
        while (tok && n < 5) { p[n++] = tok; tok = strtok(nullptr, "|"); }
        MenuID back = (n >= 5) ? _parseMenuID(p[4]) : _mid;
        _showInfo(p[0]?p[0]:"", p[1]?p[1]:"",
                  p[2]?p[2]:"", p[3]?p[3]:"", back);
        return;
    }

    // confirm:<l1>|<l2>|<back>|<tag>
    if (strncmp(action, "confirm:", 8) == 0) {
        char buf[128]; safe_copy(buf, action + 8, sizeof(buf));
        char* p[4] = {};
        uint8_t n = 0;
        char* tok = strtok(buf, "|");
        while (tok && n < 4) { p[n++] = tok; tok = strtok(nullptr, "|"); }
        MenuID back     = (n >= 3) ? _parseMenuID(p[2]) : _mid;
        const char* tag = (n >= 4) ? p[3] : "";
        _showConfirm(p[0]?p[0]:"", p[1]?p[1]:"", back, tag);
        return;
    }

    // bl:<ms> - set backlight timeout
    if (strncmp(action, "bl:", 3) == 0) {
        uint32_t ms = (uint32_t)strtoul(action + 3, nullptr, 10);
        gState.backlight_timeout_ms = ms;
        LOG_I("UI", "Backlight timeout: %lu ms", (unsigned long)ms);
        _configChanged = true;
        _d.backlightOn();
        _lastActivity = millis();
        _buildMenu(MenuID::BLSEL);
        _renderMenu();
        return;
    }

    // horarios - computed from live state
    if (strcmp(action, "horarios") == 0) {
        char zstr[LCD_COLS + 1] = "Zonas: ";
        for (int i = 0; i < NUM_ZONES; i++) {
            if (gState.zones[i].enabled) {
                char zid[4]; snprintf(zid, sizeof(zid), "Z%d ", i + 1);
                strncat(zstr, zid, LCD_COLS - strlen(zstr));
            }
        }
        char mstr[LCD_COLS + 1];
        snprintf(mstr, sizeof(mstr), "Modo: %s", _modeName(gState.mode));
        char hstr[LCD_COLS + 1];
        snprintf(hstr, sizeof(hstr), "Hora: %s", _modeHours(gState.mode));
        _showInfo("HORARIOS ATIVOS", mstr, hstr, zstr, MenuID::PROG);
        return;
    }

    // dur_pick:suspend - open picker for suspension days
    if (strcmp(action, "dur_pick:suspend") == 0) {
        _showDurPick(SUSPEND_DEFAULT_DAYS, DurContext::SUSPEND);
        return;
    }

    // cancel_susp - reactivate watering immediately
    if (strcmp(action, "cancel_susp") == 0) {
        gState.suspended = false;
        gState.suspended_until = 0;
        _configChanged = true;
        _showDone("REGA REATIVADA", "Suspensao cancelada");
        return;
    }
}

// ─────────────────────────────────────────────────────────
// Menu builder
// ─────────────────────────────────────────────────────────
void UI::_buildMenu(MenuID mid) {
    _mid       = mid;
    _itemCount = 0;
    MenuItem* it = _items;

    auto mc = [&](uint8_t i) -> const char* {
        return ((uint8_t)gState.mode == i) ? "[*]" : "[ ]";
    };

    char lbuf[21];

    switch (mid) {

    case MenuID::MAIN:
        makeItem(it++, "Rega Manual",       "go:manual");
        makeItem(it++, "Programacao",       "go:prog");
        makeItem(it++, "Definicoes",        "go:def");
        makeItem(it++, "<- Voltar",         "go:idle");
        break;

    case MenuID::MANUAL:
        makeItem(it++, "Rega Geral",    "confirm:Iniciar rega com|params. atuais?|main|general");
        makeItem(it++, "Personalizado", "go:czonas");
        makeItem(it++, "<- Voltar",     "go:main");
        break;

    case MenuID::PROG:
        makeItem(it++, "Ver Horarios",     "horarios");
        makeItem(it++, "Alterar Modo",     "go:modos");
        if (gState.mode == AppMode::PERSONALIZADO) {
            makeItem(it++, "Personalizar", "go:ccustom");
        }
        makeItem(it++, "Configurar Zonas", "go:cfgz");
        if (gState.suspended)
            makeItem(it++, "Retomar Rega", "cancel_susp");
        else
            makeItem(it++, "Suspender Rega", "dur_pick:suspend");
        makeItem(it++, "<- Voltar",        "go:main");
        break;

    case MenuID::CFG_CUSTOM: {
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        snprintf(lbuf, sizeof(lbuf), "Freq: %d dias", cs.interval_days);
        makeItem(it++, lbuf, "dur_pick:freq");
        snprintf(lbuf, sizeof(lbuf), "Ciclos: %d", cs.slot_count);
        makeItem(it++, lbuf, "dur_pick:cycles");
        for (int i = 0; i < cs.slot_count; i++) {
            snprintf(lbuf, sizeof(lbuf), "Ciclo %d: %02d:%02d", i + 1, cs.slots[i].hour, cs.slots[i].minute);
            char act[16];
            snprintf(act, sizeof(act), "time_edit:c%d", i);
            makeItem(it++, lbuf, act);
        }
        makeItem(it++, "<- Terminar", "go:prog");
        break;
    }

    case MenuID::SETUP_CUSTOM: {
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        snprintf(lbuf, sizeof(lbuf), "Freq: %d dias", cs.interval_days);
        makeItem(it++, lbuf, "dur_pick:freq");
        snprintf(lbuf, sizeof(lbuf), "Ciclos: %d", cs.slot_count);
        makeItem(it++, lbuf, "dur_pick:cycles");
        for (int i = 0; i < cs.slot_count; i++) {
            snprintf(lbuf, sizeof(lbuf), "Ciclo %d: %02d:%02d", i + 1, cs.slots[i].hour, cs.slots[i].minute);
            char act[16];
            snprintf(act, sizeof(act), "time_edit:c%d", i);
            makeItem(it++, lbuf, act);
        }
        makeItem(it++, "Avancar >", "setup_advance");
        makeItem(it++, "<- Voltar", "setup_back");
        break;
    }

    case MenuID::MODOS:
        snprintf(lbuf, sizeof(lbuf), "%s Intenso",      mc(0)); makeItem(it++, lbuf, "sel:0");
        snprintf(lbuf, sizeof(lbuf), "%s Medio",         mc(1)); makeItem(it++, lbuf, "sel:1");
        snprintf(lbuf, sizeof(lbuf), "%s Fraco",         mc(2)); makeItem(it++, lbuf, "sel:2");
        snprintf(lbuf, sizeof(lbuf), "%s Desativado",    mc(3)); makeItem(it++, lbuf, "sel:3");
        snprintf(lbuf, sizeof(lbuf), "%s Personalizado", mc(4)); makeItem(it++, lbuf, "sel:4");
        makeItem(it++, "<- Voltar", "go:prog");
        break;

    case MenuID::CFG_ZONAS:
        for (int i = 0; i < NUM_ZONES; i++) {
            Zone& z = gState.zones[i];
            if (z.enabled)
                snprintf(lbuf, sizeof(lbuf), "[ON]  Z%d %-6s %2dmin",
                         i+1, z.name, z.duration_min);
            else
                snprintf(lbuf, sizeof(lbuf), "[OFF] Z%d %-10s", i+1, z.name);
            char act[12]; snprintf(act, sizeof(act), "cfgz:%d", i);
            makeItem(it++, lbuf, act);
        }
        makeItem(it++, "<- Voltar", "go:prog");
        break;

    case MenuID::CUSTOM_ZONAS:
        for (int i = 0; i < NUM_ZONES; i++) {
            snprintf(lbuf, sizeof(lbuf), "%s Z%d %s",
                     gState.custom_sel[i] ? "[X]" : "[ ]",
                     i+1, gState.zones[i].name);
            char act[8]; snprintf(act, sizeof(act), "cz:%d", i);
            makeItem(it++, lbuf, act);
        }
        makeItem(it++, "-> Definir duracao", "dur_pick:custom");
        makeItem(it++, "<- Voltar",          "go:manual");
        break;

    case MenuID::HISTORICO: {
        HistoryEntry entries[HISTORY_DISPLAY];
        uint8_t n = history.readLast(HISTORY_DISPLAY, entries);

        if (n == 0) {
            makeItem(it++, "Sem registos", "");
        } else {
            // Build one menu item per entry (most recent last → show in reverse)
            for (int8_t i = (int8_t)n - 1; i >= 0; i--) {
                const HistoryEntry& e = entries[i];

                // Label: "26Abr 18:02 GENERAL"
                static const char* MON[] = {
                    "","Jan","Fev","Mar","Abr","Mai","Jun",
                    "Jul","Ago","Set","Out","Nov","Dez"
                };
                uint8_t m = (e.month <= 12) ? e.month : 0;
                snprintf(lbuf, sizeof(lbuf), "%02d%s %02d:%02d %s",
                         e.day, MON[m], e.hour, e.min,
                         e.trigger == WaterTrigger::CUSTOM ? "CUSTOM" : "GERAL");

                // Detail info lines - two zones per line
                char l1[LCD_COLS+1], l2[LCD_COLS+1];
                snprintf(l1, sizeof(l1), "Z1:%dmin  Z2:%dmin",
                         e.zone_dur[0], e.zone_dur[1]);
                snprintf(l2, sizeof(l2), "Z3:%dmin  Z4:%dmin",
                         e.zone_dur[2], e.zone_dur[3]);

                char act[64];
                snprintf(act, sizeof(act),
                         "info:%02d/%02d %02d:%02d|%s|%s| |hist",
                         e.day, m, e.hour, e.min, l1, l2);
                makeItem(it++, lbuf, act);
            }
        }
        makeItem(it++, "<- Voltar", "go:def");
        break;
    }

    case MenuID::DEF: {
        makeItem(it++, "Testar Zonas",    "go:testes");
        makeItem(it++, "Historico",       "go:hist");
        makeItem(it++, "Data/Hora",       "date_edit:rtc");
        makeItem(it++, "Def. Avancadas",  "go:def_avancado");
        makeItem(it++, "<- Voltar",       "go:main");
        break;
    }

    case MenuID::DEF_AVANCADO: {
        makeItem(it++, gState.auto_dst ? "Fuso Horario: Auto" : "Fuso Horario: Fixo", "toggle_dst");
        // Backlight timeout - show current setting in label
        const char* blLabel;
        switch (gState.backlight_timeout_ms) {
            case 30000UL:              blLabel = "Ecra: 30 seg"; break;
            case 60000UL:              blLabel = "Ecra:  1 min"; break;
            case 120000UL:             blLabel = "Ecra:  2 min"; break;
            case 300000UL:             blLabel = "Ecra:  5 min"; break;
            case BACKLIGHT_TIMEOUT_NEVER: blLabel = "Ecra: sempre"; break;
            default:                   blLabel = "Ecra: 2 min";  break;
        }
        makeItem(it++, blLabel,           "go:blsel");
        makeItem(it++, "Versao Firmware", "info:FIRMWARE|" FW_VERSION "|" FW_BUILD_DATE "|ESP32 rev1.0|def_avancado");
        makeItem(it++, "Reset Fabrica",   "confirm:Apagar TODAS as|definicoes?|def_avancado|reset");
        makeItem(it++, "<- Voltar",       "go:def");
        break;
    }

    case MenuID::TESTES:
        makeItem(it++, "Testar Todas (5s)",
                 "confirm:Testar todas as|zonas, 5s cada?|testes|test_all");
        for (int i = 0; i < NUM_ZONES; i++) {
            snprintf(lbuf, sizeof(lbuf), "Z%d %-8s   5s", i+1, gState.zones[i].name);
            char act[64];
            snprintf(act, sizeof(act),
                     "confirm:Ativar Z%d %s|por 5 segundos?|testes|test_%d",
                     i+1, gState.zones[i].name, i);
            makeItem(it++, lbuf, act);
        }
        makeItem(it++, "<- Voltar", "go:def");
        break;

    case MenuID::SETUP_MODE:
        makeItem(it++, "Intenso",       "sel:0");
        makeItem(it++, "Medio",         "sel:1");
        makeItem(it++, "Fraco",         "sel:2");
        makeItem(it++, "Personalizado", "sel:4");
        makeItem(it++, "Saltar >",      "setup_advance");
        makeItem(it++, "<- Voltar",     "setup_back");
        break;

    case MenuID::SETUP_ZONES:
        for (int i = 0; i < NUM_ZONES; i++) {
            Zone& z = gState.zones[i];
            if (z.enabled)
                snprintf(lbuf, sizeof(lbuf), "[ON]  Z%d %-6s %2dmin",
                         i+1, z.name, z.duration_min);
            else
                snprintf(lbuf, sizeof(lbuf), "[OFF] Z%d %-10s", i+1, z.name);
            char act[12]; snprintf(act, sizeof(act), "cfgz:%d", i);
            makeItem(it++, lbuf, act);
        }
        makeItem(it++, "Terminar >",  "setup_advance");
        makeItem(it++, "<- Voltar",   "setup_back");
        break;

    case MenuID::BLSEL: {
        // Each item sets the timeout via "bl:<ms>" action.
        // Active option is marked with [*].
        auto blmc = [&](uint32_t ms) -> const char* {
            return (gState.backlight_timeout_ms == ms) ? "[*]" : "[ ]";
        };
        snprintf(lbuf, sizeof(lbuf), "%s 30 segundos", blmc(30000UL));
        makeItem(it++, lbuf, "bl:30000");
        snprintf(lbuf, sizeof(lbuf), "%s  1 minuto",   blmc(60000UL));
        makeItem(it++, lbuf, "bl:60000");
        snprintf(lbuf, sizeof(lbuf), "%s  2 minutos",  blmc(120000UL));
        makeItem(it++, lbuf, "bl:120000");
        snprintf(lbuf, sizeof(lbuf), "%s  5 minutos",  blmc(300000UL));
        makeItem(it++, lbuf, "bl:300000");
        snprintf(lbuf, sizeof(lbuf), "%s  Sempre",     blmc(BACKLIGHT_TIMEOUT_NEVER));
        makeItem(it++, lbuf, "bl:4294967295");
        makeItem(it++, "<- Voltar", "go:def_avancado");
        break;
    }

    default:
        makeItem(it++, "<- Voltar", "go:main");
        break;
    }

    _itemCount = (uint8_t)(it - _items);
}

// ─────────────────────────────────────────────────────────
// Renderers
// ─────────────────────────────────────────────────────────
void UI::_renderIdle() {
    char b0[LCD_COLS+1], b3[LCD_COLS+1];

    // Row 0: time from RTC (or warning if RTC not ready)
    if (gState.rtc_valid) {
        char tstr[6];
        snprintf(tstr, sizeof(tstr), "%02d:%02d",
                 gState.now.hour, gState.now.min);
        _d.cx(b0, tstr);
    } else {
        _d.cx(b0, "--:--");
    }

    if (gState.watering.active) {
        if (gState.watering.is_waiting) {
            char p0[LCD_COLS+1], p1[LCD_COLS+1];
            _d.setRows(b0, _d.cx(p0, "Pausa entre zonas"), _d.cx(p1, "Aguarde..."), "");
        } else {
            uint8_t zi = gState.watering.zone_idx;
            if (zi >= NUM_ZONES) zi = 0;  // bounds guard
            char zrow[LCD_COLS+1], pb[LCD_COLS+1];
            snprintf(zrow, sizeof(zrow), "Z%d %s", zi + 1, gState.zones[zi].name);
            _d.pbar(pb, gState.watering.progress_pct);
            _d.setRows(b0, "A regar agora...", zrow, pb);
        }
    } else {
        // Row 3: next scheduled watering or suspension status
        if (!gState.rtc_valid) {
            _d.setRows(b0, "", "", _d.cx(b3, "! Acertar hora !"));
        } else if (gState.suspended) {
            _d.setRows(b0, "", _d.cx(b3, "Rega Suspensa"), "");
        } else if (gState.mode == AppMode::DESATIVADO) {
            _d.setRows(b0, "", _d.cx(b3, "Rega Desativada"), "");
        } else {
            char nxstr[LCD_COLS+1];
            snprintf(nxstr, sizeof(nxstr), "Proxima: %02d:%02d",
                     gState.next_hour, gState.next_min);
            _d.setRows(b0, "", "", _d.cx(b3, nxstr));
        }
    }
}

void UI::_renderMenu() {
    if (_cur < _off)                 _off = _cur;
    if (_cur >= _off + MENU_VISIBLE) _off = _cur - MENU_VISIBLE + 1;

    auto menuTitle = [](MenuID m) -> const char* {
        switch (m) {
            case MenuID::MAIN:         return "Menu Principal";
            case MenuID::MANUAL:       return "Rega Manual";
            case MenuID::PROG:         return "Programacao";
            case MenuID::MODOS:        return "Alterar Modo";
            case MenuID::CFG_ZONAS:    return "Configurar Zonas";
            case MenuID::CUSTOM_ZONAS: return "Escolher Zonas";
            case MenuID::CFG_CUSTOM:   return "Personalizar";
            case MenuID::HISTORICO:    return "Historico";
            case MenuID::DEF:          return "Definicoes";
            case MenuID::DEF_AVANCADO: return "Def. Avancadas";
            case MenuID::TESTES:       return "Testar Zonas";
            case MenuID::BLSEL:        return "Tempo Ecra";
            case MenuID::SETUP_MODE:   return "Modo de Rega";
            case MenuID::SETUP_ZONES:  return "Config. Zonas";
            case MenuID::SETUP_CUSTOM: return "Modo Pers.";
            default:                   return "Menu";
        }
    };

    char hbuf[LCD_COLS+1];
    _d.hdr(hbuf, menuTitle(_mid));

    char rows[MENU_VISIBLE][LCD_COLS+1];
    for (uint8_t i = 0; i < MENU_VISIBLE; i++) {
        uint8_t idx = _off + i;
        if (idx < _itemCount) {
            char line[LCD_COLS+2];
            snprintf(line, sizeof(line), "%c%s",
                     idx == _cur ? '>' : ' ', _items[idx].label);
            _d.fx(rows[i], line);
        } else {
            _d.fx(rows[i], "");
        }
    }
    _d.setRows(hbuf, rows[0], rows[1], rows[2]);
}

void UI::_renderInfo()    { _d.setRows(_infoRows[0],    _infoRows[1],    _infoRows[2],    _infoRows[3]);    }
void UI::_renderConfirm() { _d.setRows(_confirmRows[0], _confirmRows[1], _confirmRows[2], _confirmRows[3]); }
void UI::_renderDone()    { _d.setRows(_infoRows[0],    _infoRows[1],    _infoRows[2],    _infoRows[3]);    }

void UI::_renderDurPick() {
    char hbuf[LCD_COLS + 1], vbuf[LCD_COLS + 1], h1[LCD_COLS + 1], h2[LCD_COLS + 1];
    const char* title = "Definir Valor";
    const char* unit  = "minutos";

    switch (_durContext) {
        case DurContext::CFG_ZONE:
            title = "Duracao Zona";
            break;
        case DurContext::SUSPEND:
            title = "Dias de Pausa";
            unit = "dias";
            break;
        case DurContext::FREQ_DAYS:
            title = "Intervalo";
            unit = "dias";
            break;
        case DurContext::NUM_CYCLES:
            title = "Ciclos Diarios";
            unit = "ciclos";
            break;
        default:
            title = "Rega Manual";
            break;
    }

    _d.hdr(hbuf, title);

    char vstr[20];
    if (_durContext == DurContext::CFG_ZONE && _durValue == 0)
        snprintf(vstr, sizeof(vstr), "Desativada");
    else
        snprintf(vstr, sizeof(vstr), "%d %s", _durValue, unit);

    _d.cx(vbuf, vstr);
    _d.cx(h1, "Rode para alterar");
    _d.cx(h2, "Clique para guardar");
    _d.setRows(hbuf, vbuf, h1, h2);
}

void UI::_renderDateEdit() {
    char hbuf[LCD_COLS + 1], vbuf[LCD_COLS + 1], fbuf[LCD_COLS + 1], hintbuf[LCD_COLS + 1];
    _d.hdr(hbuf, "Data/Hora");

    char vstr[21];
    if (_deField == 0) snprintf(vstr, sizeof(vstr), "[%02d] / %02d / %04d", _deDay, _deMonth, _deYear);
    else if (_deField == 1) snprintf(vstr, sizeof(vstr), " %02d /[%02d]/ %04d", _deDay, _deMonth, _deYear);
    else snprintf(vstr, sizeof(vstr), " %02d / %02d /[%04d]", _deDay, _deMonth, _deYear);
    _d.cx(vbuf, vstr);

    if (_deField == 0) _d.cx(fbuf, "Escolher dia");
    else if (_deField == 1) _d.cx(fbuf, "Escolher mes");
    else _d.cx(fbuf, "Escolher ano");

    if (_deField == 0) _d.cx(hintbuf, "Clique p/ mes");
    else if (_deField == 1) _d.cx(hintbuf, "Clique p/ ano");
    else _d.cx(hintbuf, "Clique p/ hora");

    _d.setRows(hbuf, vbuf, fbuf, hintbuf);
}

void UI::_renderTimeEdit() {
    char hbuf[LCD_COLS + 1], vbuf[LCD_COLS + 1], fbuf[LCD_COLS + 1], hintbuf[LCD_COLS + 1];
    const char* title = "Data/Hora";

    if (_teContext == TimeEditContext::CUSTOM_CYCLE) {
        static char cbuf[21];
        snprintf(cbuf, sizeof(cbuf), "Horario Ciclo %d", _teCycleIdx + 1);
        title = cbuf;
    }

    _d.hdr(hbuf, title);

    // Show HH:MM with brackets around the active field
    char vstr[12];
    if (_teField == 0)
        snprintf(vstr, sizeof(vstr), "[%02d] : %02d", _teHour, _teMin);
    else
        snprintf(vstr, sizeof(vstr), " %02d  :[%02d]", _teHour, _teMin);
    _d.cx(vbuf, vstr);

    // Field indicator
    _d.cx(fbuf, _teField == 0 ? "Escolher hora" : "Escolher minutos");

    // Context-sensitive hint
    if (_teField == 0)
        _d.cx(hintbuf, "Clique p/ minutos");
    else
        _d.cx(hintbuf, "Clique p/ guardar");

    _d.setRows(hbuf, vbuf, fbuf, hintbuf);
}

void UI::_renderSetupWelcome() {
    char b0[LCD_COLS+1], b1[LCD_COLS+1], b2[LCD_COLS+1], b3[LCD_COLS+1];
    _d.cx(b0, "Bem-vindo!");
    _d.cx(b1, "");
    _d.cx(b2, "Clique p/ iniciar");
    _d.cx(b3, "a config. inicial");
    _d.setRows(b0, b1, b2, b3);
}

void UI::_renderSetupComplete() {
    char b0[LCD_COLS+1], b1[LCD_COLS+1], b2[LCD_COLS+1], b3[LCD_COLS+1];
    _d.cx(b0, "Config. concluida!");
    _d.cx(b1, "");
    _d.cx(b2, "Sistema pronto.");
    _d.cx(b3, "Clique p/ iniciar");
    _d.setRows(b0, b1, b2, b3);
}

// ─────────────────────────────────────────────────────────
// Utilities
// ─────────────────────────────────────────────────────────
uint8_t UI::_totalZoneDuration() {
    uint8_t total = 0;
    for (int i = 0; i < NUM_ZONES; i++) {
        if (gState.zones[i].enabled)
            total += gState.zones[i].duration_min;  // max 4×20=80, fits uint8_t
    }
    return total;
}

// ─────────────────────────────────────────────────────────
MenuID UI::_parseMenuID(const char* s) {
    if (!strcmp(s,"main"))   return MenuID::MAIN;
    if (!strcmp(s,"manual")) return MenuID::MANUAL;
    if (!strcmp(s,"prog"))   return MenuID::PROG;
    if (!strcmp(s,"modos"))  return MenuID::MODOS;
    if (!strcmp(s,"cfgz"))   return MenuID::CFG_ZONAS;
    if (!strcmp(s,"czonas")) return MenuID::CUSTOM_ZONAS;
    if (!strcmp(s,"ccustom")) return MenuID::CFG_CUSTOM;
    if (!strcmp(s,"hist"))   return MenuID::HISTORICO;
    if (!strcmp(s,"def"))    return MenuID::DEF;
    if (!strcmp(s,"def_avancado")) return MenuID::DEF_AVANCADO;
    if (!strcmp(s,"testes")) return MenuID::TESTES;
    if (!strcmp(s,"blsel"))  return MenuID::BLSEL;
    if (!strcmp(s,"smode"))  return MenuID::SETUP_MODE;
    if (!strcmp(s,"szones")) return MenuID::SETUP_ZONES;
    if (!strcmp(s,"scustom")) return MenuID::SETUP_CUSTOM;
    return MenuID::MAIN;
}

const char* UI::_modeName(AppMode m) {
    switch (m) {
        case AppMode::INTENSO:       return "Intenso";
        case AppMode::MEDIO:         return "Medio";
        case AppMode::FRACO:         return "Fraco";
        case AppMode::DESATIVADO:    return "Desativado";
        case AppMode::PERSONALIZADO: return "Personalizado";
        default:                     return "?";
    }
}

const char* UI::_modeHours(AppMode m) {
    // Build display string from the live schedule table so it always
    // reflects the defines in config.h, even if they are changed.
    static char buf[32];
    const ModeSchedule& sched = MODE_SCHEDULES[(uint8_t)m];

    if (sched.slot_count == 0) return "---";

    buf[0] = '\0';
    for (uint8_t i = 0; i < sched.slot_count; i++) {
        char tbuf[8];
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d", 
                 sched.slots[i].hour, sched.slots[i].minute);
        
        if (i > 0) strncat(buf, "+", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, tbuf, sizeof(buf) - strlen(buf) - 1);
    }

    return buf;
}
