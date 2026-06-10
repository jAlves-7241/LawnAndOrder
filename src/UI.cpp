#include "i18n.h"
#include "UI.h"
#include "WateringController.h"
#include "RTClock.h"
#include "Scheduler.h"
#include "Storage.h"
#include "History.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "log.h"

// Nota: extern wateringCtrl, rtclock, scheduler, storage, history
// já declarados nos respectivos headers (.h) incluídos acima.

// ─────────────────────────────────────────────────────────
// Constructor / begin
// ─────────────────────────────────────────────────────────
UI::UI(Display& disp, Encoder& enc)
    : _d(disp), _enc(enc),
      _currentScreen(nullptr),
      _inSetup(false), _setupStep(SetupStep::WELCOME),
      _configChanged(false), _lastActivity(0),
      _dateYear(2026), _dateMonth(1), _dateDay(1)
{
}

void UI::begin() {
    _lastActivity = millis();
    if (!gState.setup_done) {
        _startSetup();
    } else {
        goIdle();
    }
}

// ─────────────────────────────────────────────────────────
// State Context
// ─────────────────────────────────────────────────────────
void UI::changeScreen(UIScreen* screen) {
    if (_currentScreen) {
        _currentScreen->onExit(*this);
    }
    _currentScreen = screen;
    if (_currentScreen) {
        _currentScreen->onEnter(*this);
    }
}

void UI::goIdle() {
    if (_configChanged) {
        storage.save();
        _configChanged = false;
    }
    changeScreen(&_screenIdle);
}

void UI::goMenu(MenuID mid) {
    _screenMenu.setup(mid);
    changeScreen(&_screenMenu);
}

// ─────────────────────────────────────────────────────────
// update()
// ─────────────────────────────────────────────────────────
void UI::update() {
    if (_currentScreen == &_screenIdle && _configChanged) {
        storage.save();
        _configChanged = false;
    }

    int8_t rot   = _enc.getRotation();
    bool   click = _enc.getClick();

    bool hadInput = (rot != 0 || click);

    if (hadInput && (!_d.isBacklightOn() || !_d.isDisplayOn())) {
        _d.displayOn();
        _d.backlightOn();
        _lastActivity = millis();
        if (_currentScreen) _currentScreen->render(*this);
        return;
    }

    if (hadInput) {
        _lastActivity = millis();
        if (_currentScreen) {
            UIScreen* oldScreen = _currentScreen;
            if (rot != 0) _currentScreen->handleRotation(*this, rot);
            if (click && _currentScreen == oldScreen) _currentScreen->handleClick(*this);
        }
    }

    if (_currentScreen != &_screenIdle && !_inSetup && (millis() - _lastActivity) >= IDLE_TIMEOUT_MS) {
        goIdle();
    }

    if (_currentScreen == &_screenIdle && !_inSetup && gState.backlight_timeout_ms != BACKLIGHT_TIMEOUT_NEVER) {
        uint32_t elapsed = millis() - _lastActivity;
        if (_d.isBacklightOn() && elapsed >= gState.backlight_timeout_ms) {
            _d.backlightOff();
        }
        if (_d.isDisplayOn() && elapsed > DISPLAY_OFF_DELAY_MS && (elapsed - DISPLAY_OFF_DELAY_MS) >= gState.backlight_timeout_ms) {
            _d.displayOff();
        }
    }

    if (_currentScreen) {
        _currentScreen->update(*this);
    }
}

// ─────────────────────────────────────────────────────────
// Setup Logic
// ─────────────────────────────────────────────────────────
void UI::_startSetup() {
    _inSetup   = true;
    _setupStep = SetupStep::WELCOME;
    _screenSetupWelcome.setup(false);
    changeScreen(&_screenSetupWelcome);
}

void UI::advanceSetup() {
    switch (_setupStep) {
        case SetupStep::WELCOME:
            _setupStep = SetupStep::DATE_TIME;
            _screenDateEdit.setup(MenuID::MAIN); // backMenu is ignored in setup
            changeScreen(&_screenDateEdit);
            break;

        case SetupStep::DATE_TIME:
            _setupStep = SetupStep::MODE_SELECT;
            goMenu(MenuID::SETUP_MODE);
            break;

        case SetupStep::MODE_SELECT:
            _setupStep = SetupStep::COMPLETE;
            _screenSetupWelcome.setup(true);
            changeScreen(&_screenSetupWelcome);
            break;

        case SetupStep::CUSTOM_CONFIG:
            _setupStep = SetupStep::ZONE_CONFIG;
            goMenu(MenuID::SETUP_ZONES);
            break;

        case SetupStep::ZONE_CONFIG:
            _setupStep = SetupStep::COMPLETE;
            _screenSetupWelcome.setup(true);
            changeScreen(&_screenSetupWelcome);
            break;

        case SetupStep::COMPLETE:
            break;
    }
}

// ─────────────────────────────────────────────────────────
// Dispatch / Actions
// ─────────────────────────────────────────────────────────
bool UI::executeConfirmed(const char* tag) {
    LOG_I("UI", TXT_LOG_UI_CONFIRM_TAG, tag);

    if (!strcmp(tag, "reset")) {
        history.clear();
        storage.clear();
        Serial.flush();   // Garantir que logs pendentes no buffer UART são transmitidos
        delay(50);
        ESP.restart();
        return true;
    }

    if (!strcmp(tag, "general")) {
        bool any = false;
        for (int i=0; i<NUM_ZONES; i++) {
            if (gState.zones[i].enabled) any = true;
        }
        if (!any) {
            _screenInfo.setup(TXT_ERR_ACTIVE_ZONES, TXT_NO_ACTIVE_ZONES, TXT_ERR_ACTIVATE_ZONES, TXT_ERR_SCHEDULE, MenuID::MANUAL);
            changeScreen(&_screenInfo);
            return true;
        }
        wateringCtrl.startGeneral();
        return false;
    }

    if (!strcmp(tag, "custom")) {
        wateringCtrl.startCustom(gState.custom_sel, gState.custom_dur_min);
        return false;
    }

    if (!strcmp(tag, "test_all")) {
        wateringCtrl.startTest(-1);
        return false;
    }

    if (strncmp(tag, "test_", 5) == 0) {
        int z = atoi(tag + 5);
        if (z >= 0 && z < NUM_ZONES) {
            wateringCtrl.startTest((int8_t)z);
            return false;
        }
    }

    return false;
}

void UI::dispatchAction(const char* action) {
    if (!action || !*action) return;

    if (strncmp(action, "go:", 3) == 0) {
        if (!strcmp(action+3, "idle")) goIdle();
        else goMenu(MenuBuilder::parseMenuID(action+3));
        return;
    }

    if (strncmp(action, "sel:", 4) == 0) {
        uint8_t m = atoi(action+4);
        if (m >= (uint8_t)AppMode::_COUNT) return;
        gState.mode = (AppMode)m;
        _configChanged = true;
        scheduler.onModeChanged();
        
        if (_inSetup) {
            if (gState.mode == AppMode::PERSONALIZADO) {
                _setupStep = SetupStep::CUSTOM_CONFIG;
                goMenu(MenuID::SETUP_CUSTOM);
            } else {
                _setupStep = SetupStep::ZONE_CONFIG;
                goMenu(MenuID::SETUP_ZONES);
            }
        } else {
            goMenu(MenuID::PROG);
        }
        return;
    }

    if (strncmp(action, "cfgz:", 5) == 0) {
        int zi = atoi(action+5);
        if (zi >= 0 && zi < NUM_ZONES) {
            _screenDurPick.setup(gState.zones[zi].duration_min, DurContext::CFG_ZONE, zi, _inSetup ? MenuID::SETUP_ZONES : MenuID::CFG_ZONAS);
            changeScreen(&_screenDurPick);
        }
        return;
    }

    if (strncmp(action, "cz:", 3) == 0) {
        int zi = atoi(action+3);
        if (zi >= 0 && zi < NUM_ZONES) {
            gState.custom_sel[zi] = !gState.custom_sel[zi];
            goMenu(MenuID::CUSTOM_ZONAS);
        }
        return;
    }

    if (strncmp(action, "dur_pick:", 9) == 0) {
        const char* type = action + 9;
        if (!strcmp(type, "custom")) {
            _screenDurPick.setup(gState.custom_dur_min > 0 ? gState.custom_dur_min : 5, DurContext::CUSTOM_RUN, 0, MenuID::CUSTOM_ZONAS);
        } else if (!strcmp(type, "suspend")) {
            _screenDurPick.setup(1, DurContext::SUSPEND, 0, MenuID::PROG);
        } else if (!strcmp(type, "freq")) {
            ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
            _screenDurPick.setup(cs.interval_days, DurContext::FREQ_DAYS, 0, _inSetup ? MenuID::SETUP_CUSTOM : MenuID::CFG_CUSTOM);
        } else if (!strcmp(type, "cycles")) {
            ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
            _screenDurPick.setup(cs.slot_count, DurContext::NUM_CYCLES, 0, _inSetup ? MenuID::SETUP_CUSTOM : MenuID::CFG_CUSTOM);
        } else {
            return; // unknown type — don't enter uninitialized screen
        }
        changeScreen(&_screenDurPick);
        return;
    }

    if (strncmp(action, "date_edit:", 10) == 0) {
        _screenDateEdit.setup(MenuID::DEF);
        changeScreen(&_screenDateEdit);
        return;
    }

    if (strncmp(action, "time_edit:", 10) == 0) {
        const char* type = action + 10;
        if (type[0] == 'c') {
            int ci = atoi(type+1);
            _screenTimeEdit.setup(TimeEditContext::CUSTOM_CYCLE, ci, _inSetup ? MenuID::SETUP_CUSTOM : MenuID::CFG_CUSTOM);
            changeScreen(&_screenTimeEdit);
        }
        return;
    }

    if (!strcmp(action, "toggle_dst")) {
        gState.auto_dst = !gState.auto_dst;
        _configChanged = true;
        goMenu(MenuID::DEF_AVANCADO);
        return;
    }

    if (strncmp(action, "bl:", 3) == 0) {
        uint32_t ms = strtoul(action+3, nullptr, 10);
        gState.backlight_timeout_ms = ms;
        _configChanged = true;
        _d.displayOn();
        _d.backlightOn();
        _lastActivity = millis();
        goMenu(MenuID::DEF_AVANCADO);
        return;
    }

    if (!strcmp(action, "cancel_susp")) {
        gState.suspended = false;
        gState.suspended_until = 0;
        _configChanged = true;
        _screenDone.setup(TXT_PAUSE_CANCELED, "");
        _screenDone.setBackMenu(MenuID::MAIN);
        changeScreen(&_screenDone);
        return;
    }

    if (!strcmp(action, "horarios")) {
        char l1[LCD_COLS+1], l2[LCD_COLS+1], l3[LCD_COLS+1];

        snprintf(l1, sizeof(l1), TXT_PROG_MODE, MenuBuilder::modeName((uint8_t)gState.mode));

        if (gState.mode == AppMode::DESATIVADO) {
            snprintf(l2, sizeof(l2), "%s", TXT_PROG_OFF);
            l3[0] = '\0';

        } else if (gState.mode == AppMode::PERSONALIZADO) {
            ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];

            if (cs.slot_count <= 2) {
                // Cabe numa linha: "H: 06:00+18:00"
                char hbuf[32];
                MenuBuilder::modeHours((uint8_t)gState.mode, hbuf, sizeof(hbuf));
                snprintf(l2, sizeof(l2), "H: %s", hbuf);
                snprintf(l3, sizeof(l3), TXT_PROG_EVERY_DAYS, cs.interval_days);
            } else {
                // 3 ou 4 ciclos: dividir em duas linhas de horas
                // Linha horas A: ciclos 0 e 1
                snprintf(l2, sizeof(l2), "H: %02d:%02d + %02d:%02d",
                         cs.slots[0].hour, cs.slots[0].minute,
                         cs.slots[1].hour, cs.slots[1].minute);
                // Linha horas B: ciclos 2 (e 3 se existir)
                if (cs.slot_count == 4) {
                    snprintf(l3, sizeof(l3), "   %02d:%02d + %02d:%02d",
                             cs.slots[2].hour, cs.slots[2].minute,
                             cs.slots[3].hour, cs.slots[3].minute);
                } else {
                    snprintf(l3, sizeof(l3), "   %02d:%02d",
                             cs.slots[2].hour, cs.slots[2].minute);
                }
                // Frequência sacrificada, mas apenas cabem 4 linhas (0 a 3)
            }

        } else {
            // Modos fixos (Intenso, Médio, Fraco): no máximo 3 slots, sempre cabem
            char hbuf[32];
            MenuBuilder::modeHours((uint8_t)gState.mode, hbuf, sizeof(hbuf));
            snprintf(l2, sizeof(l2), "H: %s", hbuf);
            snprintf(l3, sizeof(l3), "%s", TXT_PROG_DAILY);
        }

        _screenInfo.setup(TXT_CURRENT_SCHED, l1, l2, l3, MenuID::PROG);
        // Para o caso de 4 ciclos, a linha l4 seria a 4ª linha do ScreenInfo
        // mas setup() só aceita 4 parâmetros de linha (l0..l3).
        // Com a estrutura atual, l3 fica como frequência ou último ciclo.
        changeScreen(&_screenInfo);
        return;
    }

    if (strncmp(action, "info:", 5) == 0) {
        char buf[80];
        strncpy(buf, action + 5, sizeof(buf) - 1);
        buf[sizeof(buf)-1] = '\0';
        char* saveptr = buf;
        char* t0 = strsep(&saveptr, "|");
        char* t1 = strsep(&saveptr, "|");
        char* t2 = strsep(&saveptr, "|");
        char* t3 = strsep(&saveptr, "|");
        char* back = strsep(&saveptr, "|");
        if (t0 && t1 && t2 && t3 && back) {
            _screenInfo.setup(t0, t1, t2, t3, MenuBuilder::parseMenuID(back));
            changeScreen(&_screenInfo);
        }
        return;
    }

    if (strncmp(action, "confirm:", 8) == 0) {
        char buf[80];
        strncpy(buf, action + 8, sizeof(buf) - 1);
        buf[sizeof(buf)-1] = '\0';
        char* saveptr = buf;
        char* t1 = strsep(&saveptr, "|");
        char* t2 = strsep(&saveptr, "|");
        char* back = strsep(&saveptr, "|");
        char* tag = strsep(&saveptr, "|");
        if (t1 && t2 && back && tag) {
            _screenConfirm.setup(t1, t2, MenuBuilder::parseMenuID(back), tag);
            changeScreen(&_screenConfirm);
        }
        return;
    }

    if (!strcmp(action, "setup_advance")) {
        advanceSetup();
        return;
    }

    if (!strcmp(action, "setup_back")) {
        if (_setupStep == SetupStep::MODE_SELECT) {
            _setupStep = SetupStep::DATE_TIME;
            _screenDateEdit.setup(MenuID::MAIN);
            changeScreen(&_screenDateEdit);
        } else if (_setupStep == SetupStep::ZONE_CONFIG) {
            if (gState.mode == AppMode::PERSONALIZADO) {
                _setupStep = SetupStep::CUSTOM_CONFIG;
                goMenu(MenuID::SETUP_CUSTOM);
            } else {
                _setupStep = SetupStep::MODE_SELECT;
                goMenu(MenuID::SETUP_MODE);
            }
        } else if (_setupStep == SetupStep::CUSTOM_CONFIG) {
            _setupStep = SetupStep::MODE_SELECT;
            goMenu(MenuID::SETUP_MODE);
        }
        return;
    }
}

uint16_t UI::getTotalZoneDuration() {
    uint16_t total = 0;
    for (int i = 0; i < NUM_ZONES; i++) {
        if (gState.zones[i].enabled)
            total += gState.zones[i].duration_min;
    }
    return total;
}
