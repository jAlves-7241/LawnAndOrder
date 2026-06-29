#include "../i18n.h"
#include "ScreenSetup.h"
#include "../UI.h"
#include "../AppState.h"
#include "../Storage.h"
#include "../Scheduler.h"
#include "../log.h"

extern Storage storage;
extern Scheduler scheduler;

void ScreenSetupWelcome::setup(bool isCompleteScreen) {
    _isComplete = isCompleteScreen;
}

void ScreenSetupWelcome::onEnter(UI& ui) {
    render(ui);
}

void ScreenSetupWelcome::handleRotation(UI& ui, int8_t dir) {
    // Ignorar rotacao
}

void ScreenSetupWelcome::handleClick(UI& ui) {
    if (_isComplete) {
        LOG_I("UI", TXT_SETUP_DONE_LOG);
        ui.setInSetup(false);
        gState.setup_done = true;
        storage.save();
        scheduler.onModeChanged();
        ui.goIdle();
    } else {
        ui.advanceSetup();
    }
}

void ScreenSetupWelcome::render(UI& ui) {
    char b0[LCD_COLS+1], b1[LCD_COLS+1], b2[LCD_COLS+1], b3[LCD_COLS+1];
    if (_isComplete) {
        ui.getDisplay().cx(b0, TXT_CONFIG_DONE);
        b1[0] = '\0';
        ui.getDisplay().cx(b2, TXT_SYSTEM_READY);
        ui.getDisplay().cx(b3, TXT_CLICK_TO_START);
    } else {
        ui.getDisplay().cx(b0, TXT_WELCOME);
        b1[0] = '\0';
        ui.getDisplay().cx(b2, TXT_CLICK_TO_START);
        ui.getDisplay().cx(b3, TXT_SETUP_INITIAL);
    }
    ui.getDisplay().setRows(b0, b1, b2, b3);
}
