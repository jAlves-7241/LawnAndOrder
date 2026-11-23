#pragma once

// ── Firmware ──────────────────────────────────────────────
#define FW_VERSION "v1.4.0"
#define FW_BUILD_DATE "16Nov2026"

// ── Logs (ver log.h para macros e documentação) ──────────
// LVL_NONE=-1  LVL_ERROR=0  LVL_WARN=1  LVL_INFO=2  LVL_DEBUG=3
#ifndef LVL_NONE
#define LVL_NONE   -1
#define LVL_ERROR   0
#define LVL_WARN    1
#define LVL_INFO    2
#define LVL_DEBUG   3
#endif

#ifndef LOG_LEVEL
#define LOG_LEVEL LVL_INFO
#endif

// ── LCD ───────────────────────────────────────────────────
#ifndef PIN_SDA
#define PIN_SDA 21
#endif
#ifndef PIN_SCL
#define PIN_SCL 22
#endif
#ifndef LCD_ADDR
#define LCD_ADDR 0x27
#endif
#define LCD_COLS 20
#define LCD_ROWS 4

// ── Encoder pins ──────────────────────────────────────────
#ifndef PIN_CLK
#define PIN_CLK 32
#endif
#ifndef PIN_DT
#define PIN_DT 33
#endif
#ifndef PIN_SW
#define PIN_SW 25
#endif

// ── Relay pins (active-LOW - relay board pulls IN to GND to close) ───────────
// Adjust to your wiring. GPIO 26-27-18-13 are free on a standard DevKit
// (GPIO 18 avoids GPIO 14's ROM-boot PWM burst and stays safe on PSRAM/WROVER boards;
// external 10k pull-ups on active-LOW relay inputs are still recommended).
#ifndef PIN_RELAY_1
#define PIN_RELAY_1 26
#endif
#ifndef PIN_RELAY_2
#define PIN_RELAY_2 27
#endif
#ifndef PIN_RELAY_3
#define PIN_RELAY_3 18
#endif
#ifndef PIN_RELAY_4
#define PIN_RELAY_4 13
#endif
#ifndef RELAY_ON
#define RELAY_ON LOW // change to HIGH if your board is active-HIGH
#endif
#ifndef RELAY_OFF
#define RELAY_OFF HIGH
#endif

// ── Schedule defaults - alter these to change the built-in mode timetables ───
//
// INTENSO: three daily slots
#define SCHED_INTENSO_SLOT0_H 7
#define SCHED_INTENSO_SLOT0_M 0
#define SCHED_INTENSO_SLOT1_H 13
#define SCHED_INTENSO_SLOT1_M 0
#define SCHED_INTENSO_SLOT2_H 19
#define SCHED_INTENSO_SLOT2_M 0

// MEDIO: two daily slots
#define SCHED_MEDIO_SLOT0_H 8
#define SCHED_MEDIO_SLOT0_M 0
#define SCHED_MEDIO_SLOT1_H 20
#define SCHED_MEDIO_SLOT1_M 0

// FRACO: one daily slot
#define SCHED_FRACO_SLOT0_H 8
#define SCHED_FRACO_SLOT0_M 0

// PERSONALIZADO: default initial settings
#define SCHED_CUSTOM_SLOT0_H 6
#define SCHED_CUSTOM_SLOT0_M 0
#define SCHED_CUSTOM_INTERVAL 1
#define SCHED_CUSTOM_SLOTS 1

// ── Zone Defaults ─────────────────────────────────────────
// Note: Zone names should ideally be 7 characters or less to perfectly fit the 20x4 LCD.
#define ZONE1_NAME "Entrada"
#define ZONE1_DUR 8
#define ZONE2_NAME "Portao"
#define ZONE2_DUR 6
#define ZONE3_NAME "Garagem"
#define ZONE3_DUR 7
#define ZONE4_NAME "Relva"
#define ZONE4_DUR 0

// ── RTC / Tempo ───────────────────────────────────────────
#ifndef AUTO_DST_DEFAULT
#define AUTO_DST_DEFAULT true // Ajuste automático de horário de verão (EU)
#endif
#ifndef TIMEZONE_OFFSET
#define TIMEZONE_OFFSET 0     // Fuso horário base em horas (0 para Portugal/UK, 1 para Europa Central)
#endif

// ── History (LittleFS CSV) ────────────────────────────────
#define HISTORY_FILE "/history.csv"
#define HISTORY_MAX_ENTRIES 1500 // max lines in file (~2 years on 2x/day)
#define HISTORY_DISPLAY 3        // entries shown in the UI menu

// ── Application ───────────────────────────────────────────
// Se 1, os menus dão a volta quando chegam ao fim. Se 0, param nos limites.
#ifndef MENU_WRAP_AROUND
#define MENU_WRAP_AROUND 1
#endif

#define NUM_ZONES 4
#define MAX_SLOTS_PER_MODE 4 // Personalizado can have up to 4 cycles
#define MENU_VISIBLE 3       // rows visible in a list at once
#define DEBOUNCE_MS 50UL
#define IDLE_TIMEOUT_MS 30000UL   // return to idle after 30 s of inactivity
#define ZONE_TEST_DURATION_S 5    // seconds per zone in test mode
#define ZONE_WAIT_DELAY_MS 5000UL // ms to wait between zones closing/opening
#define BACKLIGHT_TIMEOUT_NEVER 0xFFFFFFFFUL // sentinel - never turn off
#define DISPLAY_OFF_DELAY_MS 20000UL // turn off pixels 20s after backlight

// ── Otimizações e Segurança ───────────────────────────────
#define RELAY_DEADTIME_MS 20UL // Tempo de espera (dead time) de relés em ms
#define WDT_TIMEOUT_S 8        // Watchdog timeout em segundos
#define SUSPEND_DEFAULT_DAYS 3 // Dias padrão para suspensão de rega
#define SAFETY_GAP_SEC 7200UL  // Tempo de segurança (em segundos) para evitar regas duplas. Valor seguro: 7200 (2h)
#define DST_DUP_WINDOW_S 7200UL // Janela (2 h) para bloquear duplicacao do mesmo slot no recuo DST
#define SCHED_WALL_JUMP_MAX_S 7200UL // Salto maximo de hora local (2 h) que aciona recuperacao de slot perdido
#define SCHED_CATCHUP_WINDOW_S 1800UL // Janela (30 min) para recuperar rega perdida apos falha de energia ou salto DST
#define DATE_YEAR_MIN 2020     // Ano mínimo para o editor de data
#define DATE_YEAR_MAX 2099     // Ano máximo para o editor de data
#define UNIX_MIN_VALID 1577836800UL // 2020-01-01 00:00:00 UTC
#define UNIX_MAX_VALID 4102444799UL // 2099-12-31 23:59:59 UTC
#define UNIX_DEFAULT_FALLBACK 1767225600UL // 2026-01-01 00:00:00 UTC (placeholder em RAM sem RTC)
// 2099-12-31 UTC = dia 47481; +1 dia de margem para fusos positivos (data local 2100-01-01)
#define CUSTOM_REF_DAY_MAX ((UNIX_MAX_VALID / 86400UL) + 1UL)
#define MAX_RECOVERY_RETRIES 3    // Máximo de retentativas por zona após reboot
#define RECOVERY_CHECKPOINT_MIN 2 // Intervalo (min) para guardar progresso da zona em NVS
#define RECOVERY_MAX_AGE_S 7200UL // Idade máxima (2 h) para retomar ciclo interrompido
#define DATE_YEAR_SPAN                                                         \
  (DATE_YEAR_MAX - DATE_YEAR_MIN +                                             \
   1) // Faixa de anos (DATE_YEAR_MAX - DATE_YEAR_MIN + 1)

// Standards-compliant white-box hook for native unit tests (zero overhead on ESP32)
#ifdef UNIT_TEST
#define UNIT_TEST_FRIEND friend struct UnitTestAccess;
#else
#define UNIT_TEST_FRIEND
#endif
