#pragma once

// ── Firmware ──────────────────────────────────────────────
#define FW_VERSION     "v0.1.2-beta"
#define FW_BUILD_DATE  "20Mai2026"

// ── Logs (ver log.h para macros e documentação) ──────────
// LVL_NONE=-1  LVL_ERROR=0  LVL_WARN=1  LVL_INFO=2  LVL_DEBUG=3
#define LOG_LEVEL  LVL_INFO

// ── LCD ───────────────────────────────────────────────────
#define LCD_ADDR   0x27
#define LCD_COLS   20
#define LCD_ROWS   4

// ── Encoder pins ──────────────────────────────────────────
#define PIN_CLK    32
#define PIN_DT     33
#define PIN_SW     25

// ── Relay pins (active-LOW - relay board pulls IN to GND to close) ───────────
// Adjust to your wiring. GPIO 26-27-14-12 are free on a standard DevKit.
#define PIN_RELAY_1   26
#define PIN_RELAY_2   27
#define PIN_RELAY_3   14
#define PIN_RELAY_4   12
#define RELAY_ON      LOW    // change to HIGH if your board is active-HIGH
#define RELAY_OFF     HIGH

// ── Schedule defaults - alter these to change the built-in mode timetables ───
//
// INTENSO: three daily slots
#define SCHED_INTENSO_SLOT0_H   7
#define SCHED_INTENSO_SLOT0_M   0
#define SCHED_INTENSO_SLOT1_H   13
#define SCHED_INTENSO_SLOT1_M   0
#define SCHED_INTENSO_SLOT2_H   19
#define SCHED_INTENSO_SLOT2_M   0

// MEDIO: two daily slots
#define SCHED_MEDIO_SLOT0_H     8
#define SCHED_MEDIO_SLOT0_M     0
#define SCHED_MEDIO_SLOT1_H     20
#define SCHED_MEDIO_SLOT1_M     0

// FRACO: one daily slot
#define SCHED_FRACO_SLOT0_H     8
#define SCHED_FRACO_SLOT0_M     0

// PERSONALIZADO: default initial settings
#define SCHED_CUSTOM_SLOT0_H    6
#define SCHED_CUSTOM_SLOT0_M    0
#define SCHED_CUSTOM_INTERVAL   1
#define SCHED_CUSTOM_SLOTS      1

// ── Zone Defaults ─────────────────────────────────────────
#define ZONE1_NAME    "Jardim"
#define ZONE1_DUR     15
#define ZONE2_NAME    "Horta"
#define ZONE2_DUR     15
#define ZONE3_NAME    "Relvado"
#define ZONE3_DUR     10
#define ZONE4_NAME    "Sebe"
#define ZONE4_DUR     10

// ── RTC / Tempo ───────────────────────────────────────────
#define AUTO_DST_DEFAULT      true  // Ajuste automático de horário de verão (EU)

// ── History (LittleFS CSV) ────────────────────────────────
#define HISTORY_FILE          "/history.csv"
#define HISTORY_MAX_ENTRIES   1500  // max lines in file (~2 years on 2x/day)
#define HISTORY_DISPLAY       3     // entries shown in the UI menu

// ── Application ───────────────────────────────────────────
#define NUM_ZONES               4
#define MAX_SLOTS_PER_MODE      4         // Personalizado can have up to 4 cycles
#define MENU_VISIBLE            3         // rows visible in a list at once
#define DEBOUNCE_MS             200UL
#define IDLE_TIMEOUT_MS         30000UL   // return to idle after 30 s of inactivity
#define ZONE_TEST_DURATION_S    5         // seconds per zone in test mode
#define ZONE_WAIT_DELAY_MS      5000UL    // ms to wait between zones closing/opening
#define BACKLIGHT_TIMEOUT_NEVER 0xFFFFFFFFUL  // sentinel - never turn off
#define DISPLAY_OFF_DELAY_MS    60000UL   // turn off pixels 60s after backlight
