#include "SystemBoot.h"
#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_task_wdt.h>
#include <Wire.h>
#include <RTClib.h>

#include "config.h"
#include "AppState.h"
#include "Display.h"
#include "RTClock.h"
#include "Storage.h"
#include "log.h"
#include "i18n.h"

void initRelayPinsSafe() {
    // On Arduino-ESP32 Core 3.x (periman), digitalWrite() before pinMode() is ignored,
    // leaving GPIO.out at reset value 0 (LOW == RELAY_ON). Pre-load the hardware latch
    // via gpio_set_level() before enabling OUTPUT, then re-assert with digitalWrite().
    const uint8_t relayPins[NUM_ZONES] = {
        PIN_RELAY_1, PIN_RELAY_2, PIN_RELAY_3, PIN_RELAY_4
    };
    for (uint8_t i = 0; i < NUM_ZONES; i++) {
        gpio_set_level((gpio_num_t)relayPins[i], RELAY_OFF);
        pinMode(relayPins[i], OUTPUT);
        digitalWrite(relayPins[i], RELAY_OFF);
    }
}

bool clearI2CBus() {
    pinMode(PIN_SDA, INPUT_PULLUP); // SDA como entrada com pull-up
    // Pre-load SCL HIGH before enabling open-drain output so GPIO.out=0 cannot emit a LOW glitch
    gpio_set_level((gpio_num_t)PIN_SCL, 1);
    pinMode(PIN_SCL, OUTPUT_OPEN_DRAIN | PULLUP); // Open-drain com pull-up evita curto e linha flutuante
    digitalWrite(PIN_SCL, HIGH);

    delayMicroseconds(25);
    // Toggle SCL up to 9 times while SDA is LOW
    for (int i = 0; i < 9; i++) {
        if (digitalRead(PIN_SDA) == HIGH) break;
        digitalWrite(PIN_SCL, LOW);
        delayMicroseconds(25);
        digitalWrite(PIN_SCL, HIGH);
        delayMicroseconds(25);
    }

    // Generate STOP condition
    digitalWrite(PIN_SCL, LOW);
    delayMicroseconds(25);
    pinMode(PIN_SDA, OUTPUT_OPEN_DRAIN | PULLUP);
    digitalWrite(PIN_SDA, LOW);
    delayMicroseconds(25);
    digitalWrite(PIN_SCL, HIGH);
    delayMicroseconds(25);
    digitalWrite(PIN_SDA, HIGH);
    pinMode(PIN_SDA, INPUT_PULLUP);
    delayMicroseconds(2000);
    bool sdaHigh = (digitalRead(PIN_SDA) == HIGH);
    if (!sdaHigh) {
        LOG_W(TXT_LOG_TAG_I2C, TXT_LOG_I2C_SDA_STUCK);
    }
    return sdaHigh;
}

void configureI2CTimeout() {
    Wire.setTimeOut(50);
}

void recoverI2CWith(Display& disp, RTClock& clk, I2CRecoveryState& state) {
    uint32_t now = millis();
    if (state.hasRecovered && (now - state.lastRecoverMs) < 10000UL) {
        disp.onRecoveryThrottled();
        return;
    }
    state.hasRecovered = true;
    state.lastRecoverMs = now;
    esp_task_wdt_reset();
    LOG_W(TXT_LOG_TAG_I2C, TXT_LOG_I2C_REC_START);
    Wire.end();

    bool busOk = clearI2CBus();

    Wire.begin(PIN_SDA, PIN_SCL);
    configureI2CTimeout();
    clk.begin();
    esp_task_wdt_reset();
    disp.recover();
    // Timeout ja configurado antes de clk.begin() e preservado por Display::_lcd.init(false)
    if (busOk) {
        LOG_I(TXT_LOG_TAG_I2C, TXT_LOG_I2C_REC_DONE);
    } else {
        LOG_E(TXT_LOG_TAG_I2C, TXT_LOG_I2C_REC_FAIL);
    }
}

void evaluateBootSuspension() {
    if (gState.suspended_until > 0) {
        if (!gState.rtc_valid) {
            // Transient RTC error: retain suspended_until so subsequent NVS saves do not wipe it.
            // Scheduler will evaluate expiration once RTC time is available.
            gState.suspended = true;
            LOG_W(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SUSP_KEPT_NO_CLK);
        } else if (gState.now.unix >= gState.suspended_until) {
            gState.suspended = false;
            gState.suspended_until = 0;
            storage.save();
            LOG_I(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SUSP_EXP_CLR);
        } else {
            gState.suspended = true;
#if LOG_LEVEL >= LVL_INFO
            DateTime suspLocal = RTClock::utcToLocal(DateTime(gState.suspended_until));
            LOG_I(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SUSP_BOOT_ACTIVE,
                  (int)suspLocal.year(), (int)suspLocal.month(),
                  (int)suspLocal.day(), (int)suspLocal.hour(),
                  (int)suspLocal.minute());
#endif
        }
    } else {
        gState.suspended = false;
    }
}
