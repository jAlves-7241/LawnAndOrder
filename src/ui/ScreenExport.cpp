#include "ScreenExport.h"
#include "../UI.h"
#include "../History.h"
#include <stdio.h>

extern History history;

void ScreenExport::setup(MenuID backMenu) {
    _backMenu = backMenu;
    _lastPct = 255; // force first render
}

void ScreenExport::onEnter(UI& ui) {
    render(ui);
}

void ScreenExport::handleRotation(UI& ui, int8_t dir) {
    // Ignorado durante a exportação
}

void ScreenExport::handleClick(UI& ui) {
    // Ignorado durante a exportação
}

void ScreenExport::update(UI& ui) {
    if (!history.isExporting()) {
        // Exportação concluída — transicionar para ScreenDone
        char msg[LCD_COLS + 1];
        snprintf(msg, sizeof(msg), "%d enviados", history.exportSent());
        ui.getScreenDone().setup("Exportacao concluida", msg);
        ui.getScreenDone().setBackMenu(_backMenu);
        ui.changeScreen(&ui.getScreenDone());
        return;
    }

    uint8_t pct = history.exportProgress();
    if (pct != _lastPct) {
        render(ui);
    }
}

void ScreenExport::render(UI& ui) {
    _lastPct = history.exportProgress();

    char l2[LCD_COLS + 1];
    ui.getDisplay().pbar(l2, _lastPct);

    char l3[LCD_COLS + 1];
    snprintf(l3, sizeof(l3), "   %d registos", history.exportSent());

    ui.getDisplay().setRows("  A enviar dados...", "", l2, l3);
}
