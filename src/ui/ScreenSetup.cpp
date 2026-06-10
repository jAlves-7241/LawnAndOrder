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
        LOG_I("UI", "Configuracao inicial concluida");
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
        ui.getDisplay().cx(b0, "Config. concluida!");
        ui.getDisplay().cx(b1, "");
        ui.getDisplay().cx(b2, "Sistema pronto.");
        ui.getDisplay().cx(b3, "Clique p/ iniciar");
    } else {
        ui.getDisplay().cx(b0, "Bem-vindo!");
        ui.getDisplay().cx(b1, "");
        ui.getDisplay().cx(b2, "Clique p/ iniciar");
        ui.getDisplay().cx(b3, "a config. inicial");
    }
    ui.getDisplay().setRows(b0, b1, b2, b3);
}
