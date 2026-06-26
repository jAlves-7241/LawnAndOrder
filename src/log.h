#pragma once
#include <Arduino.h>
#include <stdio.h>

// ── Níveis de log ─────────────────────────────────────────
// Definir LOG_LEVEL em config.h para filtrar mensagens em compile-time.
// Quanto mais alto o nível, mais verboso o output.
//
//   LVL_NONE  - desliga todos os logs
//   LVL_ERROR - apenas erros críticos (falhas de hardware, limites ultrapassados)
//   LVL_WARN  - avisos (dados incompatíveis, bateria fraca)
//   LVL_INFO  - operações normais (boot, acções do utilizador, rega)
//   LVL_DEBUG - detalhes internos (transições de estado, valores intermédios)
//
// Uso:
//   LOG_I("NVS", "Dados carregados (Modo: %d)", mode);
//   LOG_E("RTC", "Modulo nao encontrado");
//   LOG_D("SCHED", "custom_ref_day definido: %lu", day);

#define LVL_NONE   -1
#define LVL_ERROR   0
#define LVL_WARN    1
#define LVL_INFO    2
#define LVL_DEBUG   3

#ifndef LOG_LEVEL
#define LOG_LEVEL LVL_INFO
#endif

// ── Suspensão temporária de logs ─────────────────────────
// Quando true, todas as macros LOG_* são silenciadas em runtime.
// Usado pelo módulo History para evitar intercalar logs com dados
// CSV durante a exportação via Serial.
extern bool _log_suspended;

// ── Terminal-aware logging ────────────────────────────────
// Tamanho do buffer de formatação para cada invocação de LOG_*.
// Alocado na stack apenas quando o log realmente dispara.
#define LOG_FMT_BUF_SIZE 192

// Imprime msg numa linha própria, preservando qualquer input
// parcial do utilizador no terminal série (usa ANSI escapes).
void terminalAwareLog(const char* msg);

// ── Macros ────────────────────────────────────────────────
// A tag é uma string literal (ex: "NVS", "RTC", "REGA").
// O \n é adicionado automaticamente - não incluir no fmt.
//
// AVISO: Estas macros usam Serial.printf() e NÃO são seguras
// para uso dentro de ISR (IRAM_ATTR). Utilizar apenas no loop()
// ou em funções chamadas a partir do loop().

#if LOG_LEVEL >= LVL_ERROR
  #define LOG_E(tag, fmt, ...) do { \
    if (!_log_suspended) { \
      char _lb[LOG_FMT_BUF_SIZE]; \
      snprintf(_lb, sizeof(_lb), "[" tag "] ERRO: " fmt, ##__VA_ARGS__); \
      terminalAwareLog(_lb); \
    } \
  } while(0)
#else
  #define LOG_E(tag, fmt, ...) ((void)0)
#endif

#if LOG_LEVEL >= LVL_WARN
  #define LOG_W(tag, fmt, ...) do { \
    if (!_log_suspended) { \
      char _lb[LOG_FMT_BUF_SIZE]; \
      snprintf(_lb, sizeof(_lb), "[" tag "] AVISO: " fmt, ##__VA_ARGS__); \
      terminalAwareLog(_lb); \
    } \
  } while(0)
#else
  #define LOG_W(tag, fmt, ...) ((void)0)
#endif

#if LOG_LEVEL >= LVL_INFO
  #define LOG_I(tag, fmt, ...) do { \
    if (!_log_suspended) { \
      char _lb[LOG_FMT_BUF_SIZE]; \
      snprintf(_lb, sizeof(_lb), "[" tag "] " fmt, ##__VA_ARGS__); \
      terminalAwareLog(_lb); \
    } \
  } while(0)
#else
  #define LOG_I(tag, fmt, ...) ((void)0)
#endif

#if LOG_LEVEL >= LVL_DEBUG
  #define LOG_D(tag, fmt, ...) do { \
    if (!_log_suspended) { \
      char _lb[LOG_FMT_BUF_SIZE]; \
      snprintf(_lb, sizeof(_lb), "[" tag "] " fmt, ##__VA_ARGS__); \
      terminalAwareLog(_lb); \
    } \
  } while(0)
#else
  #define LOG_D(tag, fmt, ...) ((void)0)
#endif

