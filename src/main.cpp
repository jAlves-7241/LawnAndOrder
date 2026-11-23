#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_task_wdt.h>
#include <esp_idf_version.h>
#if !defined(ESP_IDF_VERSION) || !defined(ESP_IDF_VERSION_VAL) || \
    (ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0))
  #error "Lawn & Order requires ESP-IDF >= 5.0.0 (Arduino-ESP32 Core 3.x)"
#endif
#if __has_include(<esp_system.h>)
  #include <esp_system.h>
#endif
#include <Wire.h>

#include "config.h"
#include "AppState.h"
#include "Display.h"
#include "Encoder.h"
#include "RTClock.h"
#include "History.h"
#include "Scheduler.h"
#include "Storage.h"
#include "SystemBoot.h"
#include "UI.h"
#include "WateringController.h"
#include "log.h"
#include "Terminal.h"
#include "i18n.h"

Display display;
static Encoder encoder(PIN_CLK, PIN_DT, PIN_SW);
UI             ui(display, encoder);

#if LOG_LEVEL >= LVL_INFO
static const char* resetReasonStr() {
#if __has_include(<esp_system.h>)
    static char otherBuf[40];
    esp_reset_reason_t r = esp_reset_reason();
    switch (r) {
        case ESP_RST_POWERON:   return TXT_RST_POWERON;
        case ESP_RST_EXT:       return TXT_RST_EXT;
        case ESP_RST_SW:        return TXT_RST_SW;
        case ESP_RST_PANIC:     return TXT_RST_PANIC;
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:       return TXT_RST_WDT;
        case ESP_RST_DEEPSLEEP: return TXT_RST_DEEPSLEEP;
        case ESP_RST_BROWNOUT:  return TXT_RST_BROWNOUT;
        default:
            snprintf(otherBuf, sizeof(otherBuf), TXT_RST_OTHER, (int)r);
            return otherBuf;
    }
#else
    return TXT_RST_POWERON;
#endif
}
#endif

#if LOG_LEVEL >= LVL_ERROR
static void espErrDesc(esp_err_t err, char* buf, size_t len) {
    switch (err) {
        case ESP_ERR_INVALID_ARG:   snprintf(buf, len, "%s", TXT_ERR_INV_ARG);     break;
        case ESP_ERR_INVALID_STATE: snprintf(buf, len, "%s", TXT_ERR_INV_STATE);   break;
        case ESP_ERR_NO_MEM:        snprintf(buf, len, "%s", TXT_ERR_NO_MEM);      break;
        case ESP_ERR_NOT_FOUND:     snprintf(buf, len, "%s", TXT_ERR_NOT_FOUND);   break;
        default:                    snprintf(buf, len, TXT_ERR_CODE_FMT, (int)err); break;
    }
}
#endif

static I2CRecoveryState gI2cRecState;

void recoverI2C() {
    recoverI2CWith(display, rtclock, gI2cRecState);
}

void setup() {
    // 1. HARDWARE SAFETY: Force relays OFF immediately before anything else.
    initRelayPinsSafe();

    Serial.begin(115200);
    Serial.println();
    LOG_I(TXT_LOG_TAG_SYS, TXT_LOG_SYS_BOOT,
          FW_VERSION, FW_BUILD_DATE, resetReasonStr());

    // Inicializar o Watchdog com timeout parametrizado e reset automatico (ESP-IDF 5 / Arduino Core 3.x)
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT_S * 1000,
        .idle_core_mask = (1 << 0), // Watch Core 0 idle (Arduino-ESP32 default); Core 1 loopTask is watched via esp_task_wdt_add(NULL)
        .trigger_panic = true
    };
#if LOG_LEVEL >= LVL_ERROR
    esp_err_t wdt_err = esp_task_wdt_init(&wdt_config);
    if (wdt_err == ESP_ERR_INVALID_STATE) {
        // No Arduino Core 3.x (ESP-IDF 5), o TWDT ja vem pre-inicializado pelo core.
        // Reconfigurar para aplicar o timeout customizado de 8s.
        wdt_err = esp_task_wdt_reconfigure(&wdt_config);
    }
    if (wdt_err != ESP_OK) {
        char errBuf[40];
        espErrDesc(wdt_err, errBuf, sizeof(errBuf));
        LOG_E(TXT_LOG_TAG_SYS, TXT_LOG_WDT_INIT_FAIL, errBuf);
    }

    wdt_err = esp_task_wdt_add(NULL); // Adiciona a thread principal do loop()
    // No Arduino Core 3.x, a loopTask pode ja vir subscrita ao TWDT (retorna ESP_ERR_INVALID_STATE)
    if (wdt_err != ESP_OK && wdt_err != ESP_ERR_INVALID_STATE) {
        char errBuf[40];
        espErrDesc(wdt_err, errBuf, sizeof(errBuf));
        LOG_E(TXT_LOG_TAG_SYS, TXT_LOG_WDT_ADD_FAIL, errBuf);
    }
#else
    if (esp_task_wdt_init(&wdt_config) == ESP_ERR_INVALID_STATE) {
        esp_task_wdt_reconfigure(&wdt_config);
    }
    esp_task_wdt_add(NULL); // Adiciona a thread principal do loop()
#endif

    initAppState();       // load firmware defaults into RAM

    storage.begin();      // open NVS namespace
    storage.load();       // overwrite defaults with persisted values

    esp_task_wdt_reset();
    history.begin();      // mount LittleFS, open/create history file
    esp_task_wdt_reset();

    clearI2CBus();
    Wire.begin(PIN_SDA, PIN_SCL);
    configureI2CTimeout();
    delay(200); // Allow LCD to boot fully

#if LOG_LEVEL >= LVL_DEBUG
    // Full I2C bus diagnostic scan (debug builds only; LCD/RTC probe their own addresses below)
    byte i2cCount = 0;
    char i2cList[192] = "";
    size_t i2cPos = 0;
    bool i2cTruncated = false;
    for (byte i = 8; i < 120; i++) {
        esp_task_wdt_reset();
        Wire.beginTransmission(i);
        if (Wire.endTransmission() == 0) {
            if (!i2cTruncated) {
                int n = snprintf(i2cList + i2cPos, sizeof(i2cList) - i2cPos,
                                 "%s0x%02X (%s)",
                                 (i2cCount > 0 ? ", " : ""),
                                 i, i2cDeviceName(i));
                if (n > 0 && i2cPos + (size_t)n < sizeof(i2cList)) {
                    i2cPos += (size_t)n;
                } else {
                    if (sizeof(i2cList) >= 4U) {
                        strcpy(i2cList + sizeof(i2cList) - 4U, "...");
                    }
                    i2cTruncated = true;
                }
            }
            i2cCount++;
            delay(1);
        }
    }
    if (i2cCount == 0) {
        LOG_E(TXT_LOG_TAG_I2C, TXT_LOG_I2C_SCAN_NONE);
    } else {
        LOG_D(TXT_LOG_TAG_I2C, TXT_LOG_I2C_SCAN_SUMMARY,
              (unsigned)i2cCount, i2cList);
    }
#endif
    esp_task_wdt_reset();
    display.begin();
    esp_task_wdt_reset();
    display.setErrorCallback(recoverI2C);
    encoder.begin();

    rtclock.setErrorCallback(recoverI2C);
    rtclock.begin();

    // Evaluate active suspension against the real RTC time before resuming interrupted cycles
    evaluateBootSuspension();

    scheduler.loadTriggers(); // Load persisted DST fall-back guards before recovery checks next slot
    wateringCtrl.begin();

    scheduler.begin();    // computes next_hour/min from live RTC + current mode
    ui.begin();
    terminal.begin();

    LOG_I(TXT_LOG_TAG_SYS, TXT_LOG_SYS_READY);
}

// ─────────────────────────────────────────────────────────
void loop() {
#if LOG_LEVEL >= LVL_WARN
    uint32_t loopStartMs = millis();
    static uint32_t lastSlowLogMs = 0;
    static bool hasSlowLogged = false;
#endif
    esp_task_wdt_reset();   // Alimentar o Watchdog em cada ciclo do loop
    rtclock.update();       // advances software clock once/sec and polls DS3231 every 30 s
    scheduler.update();     // checks trigger, advances next_*, fires watering
    ui.update();            // handles encoder, redraws LCD
    wateringCtrl.update();  // advances zone timer, drives relays
    history.update();       // processes background file rotations
    terminal.update();
    if (!ui.inSetup()) {
        storage.update();   // retries transient NVS config write failures
    }
#if LOG_LEVEL >= LVL_WARN
    uint32_t loopEndMs = millis();
    uint32_t loopDurMs = loopEndMs - loopStartMs;
    if (loopDurMs >= 2000UL &&
        (!hasSlowLogged || (loopEndMs - lastSlowLogMs) >= 30000UL)) {
        hasSlowLogged = true;
        lastSlowLogMs = loopEndMs;
        LOG_W(TXT_LOG_TAG_SYS, TXT_LOG_SLOW_LOOP, (unsigned long)(loopDurMs / 1000UL));
    }
#endif
    delay(1);
}
