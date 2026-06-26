#include "i18n.h"
#include "Terminal.h"
#include "AppState.h"
#include "History.h"
#include "RTClock.h"
#include "Scheduler.h"
#include "Storage.h"
#include "UI.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Terminal terminal;
extern UI ui;

void terminalAwareLog(const char* msg) {
    if (terminal._bufLen > 0 || terminal._pendingClearHistory) {
        // Apagar a linha parcial do utilizador (CR + clear-to-EOL)
        Serial.print("\r\033[K");
        // Imprimir o log na sua própria linha
        Serial.println(msg);
        
        // Redesenhar o prompt se aplicável
        if (terminal._pendingClearHistory) {
            Serial.print(TXT_TERM_WIPE_CONFIRM);
            Serial.print(" ");
        }

        // Re-ecoar o input parcial para o utilizador continuar a escrever
        for (uint16_t i = 0; i < terminal._bufLen; i++) {
            Serial.write(terminal._buffer[i]);
        }
    } else {
        Serial.println(msg);
    }
}

Terminal::Terminal() : _bufLen(0), _pendingClearHistory(false), _in_ansi(false), _ansi_bytes(0), _ignore_rest(false), _last_c(0) { memset(_buffer, 0, sizeof(_buffer)); }

void Terminal::begin() {
  _bufLen = 0;
  _in_ansi = false;
  _ansi_bytes = 0;
  _ignore_rest = false;
  _last_c = 0;
  LOG_I("TERM", TXT_LOG_TERM_READY);
}

void Terminal::update() {
  // 1. Salvaguarda crítica: se a exportação de histórico estiver ativa,
  // silenciar o terminal
  if (history.isExporting()) {
    uint8_t max_bytes_drop = 64;
    while (Serial.available() > 0 && max_bytes_drop-- > 0) {
      Serial.read(); // Descartar inputs
    }
    _bufLen = 0;
    return;
  }

  // 2. Leitura não bloqueante de caracteres com limite
  uint8_t max_bytes_proc = 64;
  while (Serial.available() > 0 && max_bytes_proc-- > 0) {
    char c = Serial.read();

    if (c == 27) {
      _in_ansi = true;
      _ansi_bytes = 0;
      _last_c = c;
      continue;
    }
    if (_in_ansi) {
      if (_ansi_bytes > 10) {
        _in_ansi = false;
        _last_c = c;
        continue;
      } else {
        _ansi_bytes++;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '~') {
          _in_ansi = false;
        }
        _last_c = c;
        continue;
      }
    }

    // Ignorar \n se vier imediatamente após \r (CRLF)
    if (c == '\n' && _last_c == '\r') {
      _last_c = c;
      continue;
    }

    // Se for fim de linha (LF ou CR)
    if (c == '\n' || c == '\r') {
      _ignore_rest = false;
      if (_bufLen > 0 || _pendingClearHistory) {
        _buffer[_bufLen] = '\0';
        _bufLen = 0;
        _processCommand(_buffer);
      }
    } else if (_ignore_rest) {
      _last_c = c;
      continue;
    }
    // Lidar com backspace/delete
    else if (c == '\b' || c == 127) {
      if (_bufLen > 0) {
        _bufLen--;
        // Apaga o caracter visualmente do terminal remoto
        Serial.write('\b');
        Serial.write(' ');
        Serial.write('\b');
      }
    }
    // Acumular caracteres normais e imprimíveis
    else if (c >= 32 && c <= 126) {
      if (_bufLen < sizeof(_buffer) - 1) {
        _buffer[_bufLen++] = c;
        // Eco do caracter digitado
        Serial.write(c);
      } else {
        Serial.print("\r\033[K"); // Limpar a linha visualmente
        Serial.println(TXT_TERM_ERR_OVERFLOW);
        _bufLen = 0;
        _ignore_rest = true;
      }
    }
    _last_c = c;
  }
}

static char *trimWhitespace(char *str) {
  if (!str)
    return nullptr;
  // Trim esquerdo
  while (*str == ' ' || *str == '\t')
    str++;

  size_t len = strlen(str);
  if (len == 0)
    return str;

  // Trim direito
  char *end = str + len - 1;
  while (end > str && (*end == ' ' || *end == '\t')) {
    *end = '\0';
    end--;
  }
  return str;
}

void Terminal::_processCommand(char *cmd) {
  // Nova linha no terminal após submissão de comando
  Serial.println();

  char *trimmed = trimWhitespace(cmd);

  if (_pendingClearHistory) {
    _pendingClearHistory = false;
    if (trimmed && (strcasecmp(trimmed, "y") == 0 || strcasecmp(trimmed, "yes") == 0 || 
        strcasecmp(trimmed, "s") == 0 || strcasecmp(trimmed, "sim") == 0)) {
      Serial.println(TXT_TERM_WIPING);
      history.clear();
      // Reposicionar ecra fisico para Idle para refletir o historico limpo
      // imediatamente
      ui.goIdle();
      Serial.println(TXT_TERM_WIPE_OK);
    } else {
      Serial.println(TXT_TERM_CANCELLED);
    }
    return;
  }

  if (!trimmed || *trimmed == '\0') {
    return;
  }

  // Separar o comando dos argumentos (suporta espaço ou tabulação)
  char *args = strpbrk(trimmed, " \t");
  if (args) {
    *args = '\0';
    args++;
    args = trimWhitespace(args);
  }

  if (strcmp(trimmed, "help") == 0 || strcmp(trimmed, "?") == 0) {
    _cmdHelp();
  } else if (strcmp(trimmed, "status") == 0) {
    _cmdStatus();
  } else if (strcmp(trimmed, "set_time") == 0) {
    _cmdSetTime(args);
  } else if (strcmp(trimmed, "export_config") == 0) {
    _cmdExportConfig();
  } else if (strcmp(trimmed, "import_config") == 0) {
    _cmdImportConfig(args);
  } else if (strcmp(trimmed, "export_history") == 0) {
    _cmdExportHistory();
  } else if (strcmp(trimmed, "clear_history") == 0) {
    _cmdClearHistory();
  } else if (strcmp(trimmed, "reboot") == 0) {
    _cmdReboot();
  } else {
    Serial.printf(TXT_TERM_UNKNOWN_CMD, trimmed);
  }
}

void Terminal::_cmdHelp() {
  Serial.println(
      "======================================================================");
  Serial.println(TXT_TERM_WELCOME_HDR);
  Serial.println(
      "======================================================================");
  Serial.println(TXT_TERM_HELP_TITLE);
  Serial.println(TXT_TERM_HELP_1);
  Serial.println(
      TXT_TERM_HELP_2);
  Serial.println(
      TXT_TERM_HELP_3);
  Serial.println(
      TXT_TERM_HELP_3_EX);
  Serial.println(TXT_TERM_HELP_4);
  Serial.println(TXT_TERM_HELP_5);
  Serial.println(TXT_TERM_HELP_6);
  Serial.println(TXT_TERM_HELP_7);
  Serial.println(
      TXT_TERM_HELP_8);
  Serial.println(
      "======================================================================");
}

static const char *getModeStr(AppMode mode) {
  switch (mode) {
  case AppMode::INTENSO:
    return TXT_INTENSE;
  case AppMode::MEDIO:
    return TXT_MEDIUM;
  case AppMode::FRACO:
    return TXT_WEAK;
  case AppMode::DESATIVADO:
    return TXT_DISABLED;
  case AppMode::PERSONALIZADO:
    return TXT_CUSTOM;
  default:
    return TXT_UNKNOWN;
  }
}

void Terminal::_cmdStatus() {
  Serial.println(TXT_SYS_STATUS_HDR);

  // 1. Relógio / RTC
  if (gState.rtc_valid) {
    Serial.printf(TXT_TERM_STAT_LOCAL,
                  gState.now.year, gState.now.month, gState.now.day,
                  gState.now.hour, gState.now.min, gState.now.sec);
    Serial.printf(TXT_TERM_STAT_UTC, (unsigned long)gState.now.unix);
    Serial.println(TXT_TERM_STAT_RTC_OK);
  } else {
    Serial.printf(
        TXT_TERM_STAT_LOCAL_SW,
        gState.now.year, gState.now.month, gState.now.day, gState.now.hour,
        gState.now.min, gState.now.sec);
    Serial.println(TXT_TERM_STAT_RTC_ERR);
  }

  // 2. Estado Geral
  Serial.printf(TXT_TERM_STAT_MODE, getModeStr(gState.mode));

  if (gState.suspended) {
    uint32_t remaining = (gState.suspended_until > gState.now.unix)
                             ? (gState.suspended_until - gState.now.unix)
                             : 0;
    uint32_t hours = remaining / 3600;
    uint32_t mins = (remaining % 3600) / 60;
    Serial.printf(TXT_TERM_STAT_SUSP, hours, mins);
  } else if (gState.mode == AppMode::DESATIVADO) {
    Serial.println(TXT_TERM_STAT_INACTIVE);
  } else {
    Serial.printf(
        TXT_TERM_STAT_ACTIVE,
        gState.next_hour, gState.next_min);
  }

  // 3. Zonas
  Serial.println(TXT_TERM_STAT_ZONES);
  for (int i = 0; i < NUM_ZONES; i++) {
    Serial.printf(TXT_TERM_STAT_Z_FMT, i + 1,
                  gState.zones[i].enabled ? TXT_ON : TXT_OFF,
                  gState.zones[i].duration_min, gState.zones[i].name);
  }

  // 4. Rega em execução
  Serial.print(TXT_TERM_STAT_ACT);
  if (gState.watering.active) {
    if (gState.watering.is_waiting) {
      Serial.printf(TXT_LOG_WAIT_ZONE_TRANS,
                    gState.watering.zone_idx + 1);
    } else {
      Serial.printf(TXT_LOG_WATERING_PROG,
                    gState.watering.zone_idx + 1, gState.watering.progress_pct);
    }
  } else {
    Serial.println(TXT_TERM_STAT_ACT_NO);
  }

  Serial.println(TXT_TERM_STAT_SEP);
}

void Terminal::_cmdSetTime(char *args) {
  if (!args || *args == '\0') {
    Serial.println(TXT_TERM_ERR_SET_TIME_ARGS);
    return;
  }

  int yr, mo, dy, hh, mm, ss;
  if (sscanf(args, "%d-%d-%d %d:%d:%d", &yr, &mo, &dy, &hh, &mm, &ss) != 6) {
    Serial.println(TXT_TERM_ERR_FORMAT);
    return;
  }

  // Validações básicas de limites cronológicos
  if (yr < DATE_YEAR_MIN || yr > DATE_YEAR_MAX) {
    Serial.printf(TXT_TERM_ERR_YEAR,
                  DATE_YEAR_MIN, DATE_YEAR_MAX);
    return;
  }
  if (mo < 1 || mo > 12) {
    Serial.println(TXT_TERM_ERR_MONTH);
    return;
  }
  // Validação avançada de dias por mês (incluindo ano bissexto)
  int maxDays = 31;
  if (mo == 2) {
    bool isLeap = ((yr % 4 == 0 && yr % 100 != 0) || (yr % 400 == 0));
    maxDays = isLeap ? 29 : 28;
  } else if (mo == 4 || mo == 6 || mo == 9 || mo == 11) {
    maxDays = 30;
  }

  if (dy < 1 || dy > maxDays) {
    Serial.printf(TXT_TERM_ERR_DAY,
                  dy, mo, yr, maxDays);
    return;
  }
  if (hh < 0 || hh > 23) {
    Serial.println(TXT_TERM_ERR_HOUR);
    return;
  }
  if (mm < 0 || mm > 59) {
    Serial.println(TXT_TERM_ERR_MIN);
    return;
  }
  if (ss < 0 || ss > 59) {
    Serial.println(TXT_TERM_ERR_SEC);
    return;
  }

  // Acertar a hora local
  rtclock.set((uint16_t)yr, (uint8_t)mo, (uint8_t)dy, (uint8_t)hh, (uint8_t)mm,
              (uint8_t)ss);

  // Atualizar o scheduler imediatamente baseado no novo relógio
  scheduler.onModeChanged();

  Serial.printf(
      TXT_TERM_TIME_SET, yr, mo,
      dy, hh, mm, ss);
}

void Terminal::_cmdExportConfig() {
  char hexStr[sizeof(AppConfigBlob) * 2 + 1];
  storage.exportConfigHex(hexStr, sizeof(hexStr));

  Serial.println(TXT_TERM_EXPORT_HDR);
  Serial.println(TXT_TERM_EXPORT_COPY);
  Serial.println(hexStr);
  Serial.println(TXT_TERM_EXPORT_SEP);
}

void Terminal::_cmdImportConfig(char *hexStr) {
  if (!hexStr || *hexStr == '\0') {
    Serial.println(TXT_TERM_ERR_IMP_FMT);
    return;
  }

  if (storage.importConfigHex(hexStr)) {
    // Sucesso na importação - recalcular agendamento do Scheduler
    scheduler.onModeChanged();
    // Reposicionar ecrã físico para Idle para refletir as novas configurações
    // imediatamente
    ui.goIdle();
    Serial.println(TXT_TERM_IMP_OK);
  } else {
    Serial.println(TXT_TERM_ERR_IMP_BLOB);
  }
}

void Terminal::_cmdClearHistory() {
  Serial.println(TXT_TERM_WIPE_WARN);
  Serial.print(TXT_TERM_WIPE_CONFIRM);
  Serial.print(" ");
  _pendingClearHistory = true;
}

void Terminal::_cmdReboot() {
  Serial.println(TXT_TERM_REBOOT);
  Serial.flush();
  delay(100);
  ESP.restart();
}

void Terminal::_cmdExportHistory() {
  Serial.println(TXT_LOG_START_EXPORT);
  history.startExport();
}
