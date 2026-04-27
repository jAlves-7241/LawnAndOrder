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

// ── Application ───────────────────────────────────────────
#define NUM_ZONES        4
#define MENU_VISIBLE     3        // rows visible in a list at once
#define DEBOUNCE_MS      200UL
#define IDLE_TIMEOUT_MS  30000UL  // return to idle after 30 s of inactivity