#include <Arduino.h>
#include <esp_task_wdt.h>
#include <Wire.h>

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
#include "Terminal.h"

Display display;
static Encoder encoder(PIN_CLK, PIN_DT, PIN_SW);
UI             ui(display, encoder);

void recoverI2C() {
    esp_task_wdt_reset();
    LOG_W("SYS", "A iniciar recuperacao do barramento I2C preso...");
    Wire.end();
    
    pinMode(21, INPUT_PULLUP); // SDA como entrada com pull-up
    pinMode(22, OUTPUT);       // SCL como saída manual
    
    // Toggle SCL até 9 vezes enquanto SDA estiver preso em LOW
    for (int i = 0; i < 9; i++) {
        esp_task_wdt_reset();
        if (digitalRead(21) == HIGH) break; // Escravo libertou SDA
        digitalWrite(22, LOW);
        delayMicroseconds(5);
        digitalWrite(22, HIGH);
        delayMicroseconds(5);
    }
    
    // Gerar uma condição de STOP manual (SCL HIGH, depois SDA HIGH)
    pinMode(21, OUTPUT);
    digitalWrite(21, LOW);
    digitalWrite(22, HIGH);
    delayMicroseconds(5);
    digitalWrite(21, HIGH); // Transição LOW -> HIGH com SCL HIGH = STOP
    delay(10);
    
    Wire.begin();
    rtclock.begin();
    esp_task_wdt_reset();
    display.begin();
    LOG_I("SYS", "Recuperacao I2C concluida.");
}

void setup() {
    // 1. HARDWARE SAFETY: Force relays OFF immediately before anything else
    pinMode(PIN_RELAY_1, OUTPUT); digitalWrite(PIN_RELAY_1, RELAY_OFF);
    pinMode(PIN_RELAY_2, OUTPUT); digitalWrite(PIN_RELAY_2, RELAY_OFF);
    pinMode(PIN_RELAY_3, OUTPUT); digitalWrite(PIN_RELAY_3, RELAY_OFF);
    pinMode(PIN_RELAY_4, OUTPUT); digitalWrite(PIN_RELAY_4, RELAY_OFF);

    Serial.begin(115200);
    LOG_I("SYS", "A iniciar sistema...");

    // Inicializar o Watchdog com timeout parametrizado e reset automático (compatível com ESP-IDF v4 e v5)
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT_S * 1000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_config);
#else
    esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
    esp_task_wdt_add(NULL); // Adiciona a thread principal do loop()

    initAppState();       // load firmware defaults into RAM

    storage.begin();      // open NVS namespace
    storage.load();       // overwrite defaults with persisted values

    history.begin();      // mount LittleFS, open/create history file

    display.begin();
    encoder.begin();

    rtclock.setErrorCallback(recoverI2C);
    if (!rtclock.begin()) {
        LOG_E("SYS", "RTC nao disponivel");
    }

    wateringCtrl.begin();

    // BUG-5 fix: clear stale suspension from NVS if already expired.
    // Between storage.load() and the first scheduler.update(), suspended
    // could be true with a past timestamp, causing a brief "Rega Suspensa".
    if (gState.suspended && gState.rtc_valid && gState.now.unix >= gState.suspended_until) {
        gState.suspended = false;
        gState.suspended_until = 0;
        storage.save();
        LOG_I("SYS", "Suspensao expirada durante reboot - limpa");
    }

    scheduler.begin();    // computes next_hour/min from live RTC + current mode
    ui.begin();
    terminal.begin();

    LOG_I("SYS", "Sistema pronto.");
}

// ─────────────────────────────────────────────────────────
void loop() {
    esp_task_wdt_reset();   // Alimentar o Watchdog em cada ciclo do loop
    rtclock.update();       // reads DS3231 into gState.now once/sec
    scheduler.update();     // checks trigger, advances next_*, fires watering
    ui.update();            // handles encoder, redraws LCD
    wateringCtrl.update();  // advances zone timer, drives relays
    history.update();       // processes background file rotations
    terminal.update();
}
