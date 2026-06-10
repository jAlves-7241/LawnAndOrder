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
    
    pinMode(PIN_SDA, INPUT_PULLUP); // SDA como entrada com pull-up
    pinMode(PIN_SCL, OUTPUT_OPEN_DRAIN);
    digitalWrite(PIN_SCL, HIGH);
    
    // Toggle SCL até 9 vezes enquanto SDA estiver preso em LOW
    for (int i = 0; i < 9; i++) {
        esp_task_wdt_reset();
        if (digitalRead(PIN_SDA) == HIGH) break; // Escravo libertou SDA
        digitalWrite(PIN_SCL, LOW);
        delayMicroseconds(5);
        digitalWrite(PIN_SCL, HIGH);
        delayMicroseconds(5);
    }
    
    // Gerar uma condição de STOP manual (SCL HIGH, depois SDA HIGH)
    pinMode(PIN_SDA, OUTPUT_OPEN_DRAIN);
    digitalWrite(PIN_SDA, LOW);
    digitalWrite(PIN_SCL, HIGH);
    delayMicroseconds(5);
    digitalWrite(PIN_SDA, HIGH); // Transição LOW -> HIGH com SCL HIGH = STOP
    delay(10);
    
    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setTimeOut(150);
    rtclock.begin();
    esp_task_wdt_reset();
    display.begin();
    LOG_I("SYS", "Recuperacao I2C concluida.");
}

void setup() {
    // 1. HARDWARE SAFETY: Force relays OFF immediately before anything else
    digitalWrite(PIN_RELAY_1, RELAY_OFF); pinMode(PIN_RELAY_1, OUTPUT);
    digitalWrite(PIN_RELAY_2, RELAY_OFF); pinMode(PIN_RELAY_2, OUTPUT);
    digitalWrite(PIN_RELAY_3, RELAY_OFF); pinMode(PIN_RELAY_3, OUTPUT);
    digitalWrite(PIN_RELAY_4, RELAY_OFF); pinMode(PIN_RELAY_4, OUTPUT);

    Serial.begin(115200);
    LOG_I("SYS", "A iniciar sistema...");

    // Inicializar o Watchdog com timeout parametrizado e reset automático (compatível com ESP-IDF v4 e v5)
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT_S * 1000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true
    };
    esp_err_t wdt_err = esp_task_wdt_init(&wdt_config);
#else
    esp_err_t wdt_err = esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
    if (wdt_err != ESP_OK && wdt_err != ESP_ERR_INVALID_STATE) {
        LOG_E("SYS", "WDT init falhou: %d", wdt_err);
    }

    wdt_err = esp_task_wdt_add(NULL); // Adiciona a thread principal do loop()
    if (wdt_err != ESP_OK) {
        LOG_E("SYS", "WDT add falhou: %d", wdt_err);
    }

    initAppState();       // load firmware defaults into RAM

    storage.begin();      // open NVS namespace
    storage.load();       // overwrite defaults with persisted values

    history.begin();      // mount LittleFS, open/create history file

    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setTimeOut(150);
    display.begin();
    encoder.begin();

    rtclock.setErrorCallback(recoverI2C);
    if (!rtclock.begin()) {
        LOG_E("SYS", "RTC nao disponivel");
    }

    wateringCtrl.begin();

    // Evaluate active suspension against the real RTC time
    if (gState.suspended_until > 0) {
        if (gState.rtc_valid && gState.now.unix >= gState.suspended_until) {
            gState.suspended = false;
            gState.suspended_until = 0;
            storage.save();
            LOG_I("SYS", "Suspensao expirada durante reboot - limpa");
        } else {
            gState.suspended = true;
        }
    } else {
        gState.suspended = false;
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
