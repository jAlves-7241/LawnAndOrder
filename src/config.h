#pragma once

// ── Firmware ──────────────────────────────────────────────
#define FW_VERSION     "v0.1.0-alpha"
#define FW_BUILD_DATE  "27Abr2026"

// ── LCD ───────────────────────────────────────────────────
#define LCD_ADDR   0x27
#define LCD_COLS   20
#define LCD_ROWS   4

// ── Encoder pins ──────────────────────────────────────────
#define PIN_CLK    32
#define PIN_DT     33
#define PIN_SW     25

// ── Relay pins (active-LOW — relay board pulls IN to GND to close) ───────────
// Adjust to your wiring. GPIO 26-27-14-12 are free on a standard DevKit.
#define PIN_RELAY_1   26
#define PIN_RELAY_2   27
#define PIN_RELAY_3   14
#define PIN_RELAY_4   12
#define RELAY_ON      LOW    // change to HIGH if your board is active-HIGH
#define RELAY_OFF     HIGH

// ── Application ───────────────────────────────────────────
#define NUM_ZONES            4
#define MENU_VISIBLE         3        // rows visible in a list at once
#define DEBOUNCE_MS          200UL
#define IDLE_TIMEOUT_MS      30000UL  // return to idle after 30 s of inactivity
#define ZONE_TEST_DURATION_S 5        // seconds per zone in test mode
