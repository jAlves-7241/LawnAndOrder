#include <Arduino.h>

#include "config.h"
#include "AppState.h"
#include "Display.h"
#include "Encoder.h"
#include "UI.h"

// ── Singletons ────────────────────────────────────────────
static Display display;
static Encoder encoder(PIN_CLK, PIN_DT, PIN_SW);
static UI      ui(display, encoder);

// ─────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("[REGA] Arrancar...");

    initAppState();
    display.begin();
    encoder.begin();
    ui.begin();

    Serial.println("[REGA] Pronto.");
}

// ─────────────────────────────────────────────────────────
void loop() {
    ui.update();

    // TODO: scheduler.update()  — check RTC, fire zones
    // TODO: wateringCtrl.update() — drive relays, update gState.watering
}
