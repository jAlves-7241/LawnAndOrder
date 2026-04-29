#include <Arduino.h>

#include "config.h"
#include "AppState.h"
#include "Display.h"
#include "Encoder.h"
#include "UI.h"
#include "WateringController.h"

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
    wateringCtrl.begin();   // configure relay GPIO pins
    ui.begin();

    Serial.println("[REGA] Pronto.");
}

// ─────────────────────────────────────────────────────────
void loop() {
    ui.update();
    wateringCtrl.update();  // advances zone timer, drives relays

    // TODO: scheduler.update()  — check RTC, auto-trigger watering
}
