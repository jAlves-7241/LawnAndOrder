#pragma once
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "AppState.h"
#include "i18n.h"

// ── Niveis de log ─────────────────────────────────────────
// Definir LOG_LEVEL em config.h (ou build flags) para filtrar em compile-time.
// Quanto mais alto o nivel, mais detalhado o output.
//
//   LVL_NONE  - desliga todos os logs
//   LVL_ERROR - erros criticos (falhas de hardware, limites ultrapassados)
//   LVL_WARN  - avisos (dados incompativeis, bateria fraca, quedas recuperadas)
//   LVL_INFO  - eventos relevantes (arranque, acoes do utilizador, ciclos de rega)
//   LVL_DEBUG - detalhes internos de diagnostico e manutencao
//
// Formato padrao (precisao ao segundo):
//   [YYYY-MM-DD HH:MM:SS] [LEVEL] [Component] Mensagem
//   [Uptime HH:MM:SS] [LEVEL] [Component] Mensagem (quando hora nao validada)

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

// ── Suspensao temporaria de logs (exportacao CSV) ────────
inline bool _log_suspended = false;
inline uint16_t _log_suppressed_total = 0;
inline uint16_t _log_suppressed_warn_err = 0;

// ── Terminal-aware logging ────────────────────────────────
#define LOG_FMT_BUF_SIZE 256

// Imprime msg numa linha propria, preservando qualquer input
// parcial do utilizador no terminal serie (usa ANSI escapes).
void terminalAwareLog(const char* msg);

// Formata e emite uma linha de log padronizada; contabiliza linhas ocultadas
// durante a exportacao CSV em _log_suppressed_*.
// Nao chamar dentro de ISR (IRAM_ATTR).
inline void _logWrite(bool isWarnOrErr, const char* level, const char* tag,
                      const char* fmt, ...) __attribute__((format(printf, 4, 5)));

inline void _logWrite(bool isWarnOrErr, const char* level, const char* tag,
                      const char* fmt, ...) {
  if (_log_suspended) {
    if (_log_suppressed_total < 0xFFFFU) _log_suppressed_total++;
    if (isWarnOrErr && _log_suppressed_warn_err < 0xFFFFU) {
      _log_suppressed_warn_err++;
    }
    return;
  }

  char ts[32];
  if (gState.rtc_valid && gState.now.year >= 2000 &&
      gState.now.month >= 1 && gState.now.day >= 1) {
    snprintf(ts, sizeof(ts), "%04u-%02u-%02u %02u:%02u:%02u",
             (unsigned)gState.now.year, (unsigned)gState.now.month,
             (unsigned)gState.now.day, (unsigned)gState.now.hour,
             (unsigned)gState.now.min, (unsigned)gState.now.sec);
  } else {
    unsigned long upSec = millis() / 1000UL;
    unsigned long h = upSec / 3600UL;
    unsigned long m = (upSec % 3600UL) / 60UL;
    unsigned long s = upSec % 60UL;
    snprintf(ts, sizeof(ts), "%s %02lu:%02lu:%02lu",
             TXT_LOG_UPTIME_PREFIX, h, m, s);
  }

  const char* safeLevel = level ? level : "";
  const char* safeTag   = tag   ? tag   : "";
  const char* safeFmt   = fmt   ? fmt   : "";

  char line[LOG_FMT_BUF_SIZE];
  int prefixLen = snprintf(line, sizeof(line), "[%s] [%s] [%s] ", ts, safeLevel, safeTag);
  if (prefixLen < 0) prefixLen = 0;

  if ((size_t)prefixLen < sizeof(line) - 1U) {
    va_list args;
    va_start(args, fmt);
    int msgLen = vsnprintf(line + prefixLen, sizeof(line) - (size_t)prefixLen,
                           safeFmt, args);
    va_end(args);
    if (msgLen >= (int)(sizeof(line) - (size_t)prefixLen)) {
      if (sizeof(line) >= 4U) {
        strcpy(line + sizeof(line) - 4U, "...");
      }
    }
  } else if (sizeof(line) >= 4U) {
    strcpy(line + sizeof(line) - 4U, "...");
  }

  terminalAwareLog(line);
}

// ── Macros ────────────────────────────────────────────────
// Nao incluir '\n' no fmt; o terminalAwareLog adiciona terminacao de linha.
// Nao utilizar dentro de ISR (IRAM_ATTR).

#if LOG_LEVEL >= LVL_ERROR
  #define LOG_E(tag, fmt, ...) \
    _logWrite(true, TXT_LOG_LVL_ERROR, tag, fmt, ##__VA_ARGS__)
#else
  #define LOG_E(tag, fmt, ...) ((void)0)
#endif

#if LOG_LEVEL >= LVL_WARN
  #define LOG_W(tag, fmt, ...) \
    _logWrite(true, TXT_LOG_LVL_WARN, tag, fmt, ##__VA_ARGS__)
#else
  #define LOG_W(tag, fmt, ...) ((void)0)
#endif

#if LOG_LEVEL >= LVL_INFO
  #define LOG_I(tag, fmt, ...) \
    _logWrite(false, TXT_LOG_LVL_INFO, tag, fmt, ##__VA_ARGS__)
#else
  #define LOG_I(tag, fmt, ...) ((void)0)
#endif

#if LOG_LEVEL >= LVL_DEBUG
  #define LOG_D(tag, fmt, ...) \
    _logWrite(false, TXT_LOG_LVL_DEBUG, tag, fmt, ##__VA_ARGS__)
#else
  #define LOG_D(tag, fmt, ...) ((void)0)
#endif
