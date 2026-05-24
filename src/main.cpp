#include <Arduino.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "AppState.h"
#include "Display.h"
#include "Encoder.h"
#include "RTClock.h"
#include "History.h"
#include "Scheduler.h"
#include "Storage.h"
#include "UI.h"
#include "WateringController.h"
#include "log.h"

Display display;
static Encoder encoder(PIN_CLK, PIN_DT, PIN_SW);
static UI      ui(display, encoder);

#include <Wire.h>

void recoverI2C() {
    LOG_W("SYS", "A iniciar recuperacao do barramento I2C preso...");
    Wire.end();
    
    // Forçar linhas a HIGH manualmente (SDA=21, SCL=22)
    pinMode(21, OUTPUT);
    pinMode(22, OUTPUT);
    digitalWrite(21, HIGH);
    digitalWrite(22, HIGH);
    delay(10);
    
    Wire.begin();
    rtclock.begin();
    display.begin();
    LOG_I("SYS", "Recuperacao I2C concluida.");
}

// ─────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    LOG_I("SYS", "A iniciar sistema...");

    // Inicializar o Watchdog com timeout parametrizado e reset automático
    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    esp_task_wdt_add(NULL); // Adiciona a thread principal do loop()

    initAppState();       // load firmware defaults into RAM

    storage.begin();      // open NVS namespace
    storage.load();       // overwrite defaults with persisted values

    history.begin();      // mount LittleFS, open/create history file

    display.begin();
    encoder.begin();

    if (!rtclock.begin()) {
        LOG_E("SYS", "RTC nao disponivel");
    }

    wateringCtrl.begin();
    scheduler.begin();    // computes next_hour/min from live RTC + current mode
    ui.begin();

    LOG_I("SYS", "Sistema pronto.");
}

// ─────────────────────────────────────────────────────────
void loop() {
    esp_task_wdt_reset();   // Alimentar o Watchdog em cada ciclo do loop
    rtclock.update();       // reads DS3231 into gState.now once/sec
    scheduler.update();     // checks trigger, advances next_*, fires watering
    ui.update();            // handles encoder, redraws LCD
    wateringCtrl.update();  // advances zone timer, drives relays
}
