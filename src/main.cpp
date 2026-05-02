#include <Arduino.h>

#include "config.h"
#include "AppState.h"
#include "Display.h"
#include "Encoder.h"
#include "RTClock.h"
#include "Scheduler.h"
#include "Storage.h"
#include "UI.h"
#include "WateringController.h"

static Display display;
static Encoder encoder(PIN_CLK, PIN_DT, PIN_SW);
static UI      ui(display, encoder);

// ─────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("[REGA] Arrancar...");

    initAppState();       // load firmware defaults into RAM

    storage.begin();      // open NVS namespace
    storage.load();       // overwrite defaults with persisted values

    display.begin();
    encoder.begin();

    if (!rtclock.begin()) {
        Serial.println("[REGA] AVISO: RTC nao disponivel");
    }

    wateringCtrl.begin();
    scheduler.begin();    // computes next_hour/min from live RTC + current mode
    ui.begin();

    Serial.println("[REGA] Pronto.");
}

// ─────────────────────────────────────────────────────────
void loop() {
    rtclock.update();       // reads DS3231 into gState.now once/sec
    scheduler.update();     // checks trigger, advances next_*, fires watering
    ui.update();            // handles encoder, redraws LCD
    wateringCtrl.update();  // advances zone timer, drives relays
}
