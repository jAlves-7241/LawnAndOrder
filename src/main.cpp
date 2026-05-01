#include <Arduino.h>

#include "config.h"
#include "AppState.h"
#include "Display.h"
#include "Encoder.h"
#include "RTClock.h"
#include "UI.h"
#include "WateringController.h"

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

    if (!rtclock.begin()) {
        Serial.println("[REGA] AVISO: RTC nao disponivel");
    }

    wateringCtrl.begin();
    ui.begin();

    Serial.println("[REGA] Pronto.");
}

// ─────────────────────────────────────────────────────────
void loop() {
    rtclock.update();       // reads DS3231 into gState.now once/sec
    ui.update();            // handles encoder, redraws LCD
    wateringCtrl.update();  // advances zone timer, drives relays

    // TODO: scheduler.update() — check gState.now vs schedule, auto-trigger
}
