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

Terminal::Terminal() : _bufLen(0), _pendingClearHistory(false) { memset(_buffer, 0, sizeof(_buffer)); }

void Terminal::begin() {
  _bufLen = 0;
  LOG_I("TERM", "Terminal de comando serial ativado a 115200 baud.");
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
  static bool in_ansi = false;
  static uint8_t ansi_bytes = 0;
  static bool ignore_rest = false;
  while (Serial.available() > 0 && max_bytes_proc-- > 0) {
    char c = Serial.read();

    if (c == 27) {
      in_ansi = true;
      ansi_bytes = 0;
      continue;
    }
    if (in_ansi) {
      ansi_bytes++;
      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '~' || ansi_bytes > 10) {
        in_ansi = false;
      }
      continue;
    }

    // Se for fim de linha (LF ou CR)
    if (c == '\n' || c == '\r') {
      ignore_rest = false;
      if (_bufLen > 0) {
        _buffer[_bufLen] = '\0';
        _processCommand(_buffer);
        _bufLen = 0;
      }
    } else if (ignore_rest) {
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
        Serial.println("\n[ERRO: Comando excedeu limite de caracteres. Buffer limpo.]");
        _bufLen = 0;
        ignore_rest = true;
      }
    }
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
  if (!trimmed || *trimmed == '\0') {
    return;
  }

  if (_pendingClearHistory) {
    if (strcmp(trimmed, "yes") == 0 || strcmp(trimmed, "y") == 0 || strcmp(trimmed, "sim") == 0 || strcmp(trimmed, "s") == 0) {
      _cmdClearHistory();
    } else {
      Serial.println("Operacao cancelada.");
    }
    _pendingClearHistory = false;
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
    Serial.printf("Comando desconhecido: '%s'. Digite 'help' ou '?' para ver a "
                  "lista de comandos.\n",
                  trimmed);
  }
}

void Terminal::_cmdHelp() {
  Serial.println(
      "======================================================================");
  Serial.println(
      "                    LAWN & ORDER - SERVICO TERMINAL                  ");
  Serial.println(
      "======================================================================");
  Serial.println("Comandos Disponiveis:");
  Serial.println("  help, ?                      - Mostra este menu de ajuda");
  Serial.println(
      "  status                       - Relatorio em tempo real do sistema");
  Serial.println(
      "  set_time AAAA-MM-DD HH:MM:SS - Define a data e hora local do relogio");
  Serial.println(
      "                               Ex: set_time 2026-05-29 15:30:00");
  Serial.println("  export_config                - Exporta a configuracao "
                 "atual (Hex Blob)");
  Serial.println("  import_config <68_hex>       - Importa configuracoes a "
                 "partir do Hex Blob");
  Serial.println("  export_history               - Exporta o historico de regas por Serial");
  Serial.println("  clear_history                - Apaga completamente o "
                 "historico de regas do sistema");
  Serial.println(
      "  reboot                       - Reinicia o controlador ESP32");
  Serial.println(
      "======================================================================");
}

static const char *getModeStr(AppMode mode) {
  switch (mode) {
  case AppMode::INTENSO:
    return "INTENSO";
  case AppMode::MEDIO:
    return "MEDIO";
  case AppMode::FRACO:
    return "FRACO";
  case AppMode::DESATIVADO:
    return "DESATIVADO";
  case AppMode::PERSONALIZADO:
    return "PERSONALIZADO";
  default:
    return "DESCONHECIDO";
  }
}

void Terminal::_cmdStatus() {
  Serial.println(TXT_SYS_STATUS_HDR);

  // 1. Relógio / RTC
  if (gState.rtc_valid) {
    Serial.printf("  Hora Local:  %04d-%02d-%02d %02d:%02d:%02d\n",
                  gState.now.year, gState.now.month, gState.now.day,
                  gState.now.hour, gState.now.min, gState.now.sec);
    Serial.printf("  Epoch UTC:   %lu (segundos desde 1970)\n", (unsigned long)gState.now.unix);
    Serial.println("  Hardware RTC: Ligado e Valido [OK]");
  } else {
    Serial.printf(
        "  Hora Local:  %04d-%02d-%02d %02d:%02d:%02d (SOFTWARE ONLY)\n",
        gState.now.year, gState.now.month, gState.now.day, gState.now.hour,
        gState.now.min, gState.now.sec);
    Serial.println("  Hardware RTC: Desconectado/Invalido [FALHA]");
  }

  // 2. Estado Geral
  Serial.printf("  Modo Ativo:  %s\n", getModeStr(gState.mode));

  if (gState.suspended) {
    uint32_t remaining = (gState.suspended_until > gState.now.unix)
                             ? (gState.suspended_until - gState.now.unix)
                             : 0;
    uint32_t hours = remaining / 3600;
    uint32_t mins = (remaining % 3600) / 60;
    Serial.printf("  Estado Rega: SUSPENSA (Resta %luh %lum)\n", hours, mins);
  } else if (gState.mode == AppMode::DESATIVADO) {
    Serial.println("  Estado Rega: INATIVA (Desativada no painel)");
  } else {
    Serial.printf(
        "  Estado Rega: ATIVA (Proxima rega agendada para %02d:%02d)\n",
        gState.next_hour, gState.next_min);
  }

  // 3. Zonas
  Serial.println("  Zonas de Rega:");
  for (int i = 0; i < NUM_ZONES; i++) {
    Serial.printf("    Z%d: [%s] Duracao: %d min - Nome: %s\n", i + 1,
                  gState.zones[i].enabled ? "ON" : "OFF",
                  gState.zones[i].duration_min, gState.zones[i].name);
  }

  // 4. Rega em execução
  Serial.print("  Rega Ativa:  ");
  if (gState.watering.active) {
    if (gState.watering.is_waiting) {
      Serial.printf(TXT_LOG_WAIT_ZONE_TRANS,
                    gState.watering.zone_idx + 1);
    } else {
      Serial.printf(TXT_LOG_WATERING_PROG,
                    gState.watering.zone_idx + 1, gState.watering.progress_pct);
    }
  } else {
    Serial.println("Nao");
  }

  Serial.println("=======================================================");
}

void Terminal::_cmdSetTime(char *args) {
  if (!args || *args == '\0') {
    Serial.println("Erro: Comando set_time requer argumentos: set_time "
                   "AAAA-MM-DD HH:MM:SS");
    return;
  }

  int yr, mo, dy, hh, mm, ss;
  if (sscanf(args, "%d-%d-%d %d:%d:%d", &yr, &mo, &dy, &hh, &mm, &ss) != 6) {
    Serial.println("Erro: Formato de data/hora incorreto. Use: set_time "
                   "AAAA-MM-DD HH:MM:SS");
    return;
  }

  // Validações básicas de limites cronológicos
  if (yr < DATE_YEAR_MIN || yr > DATE_YEAR_MAX) {
    Serial.printf("Erro: Ano fora do intervalo suportado [%d - %d].\n",
                  DATE_YEAR_MIN, DATE_YEAR_MAX);
    return;
  }
  if (mo < 1 || mo > 12) {
    Serial.println("Erro: Mes deve estar entre 1 e 12.");
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
    Serial.printf("Erro: Dia invalido para o mes e ano especificados "
                  "(%d/%d/%04d). O limite e %d dias.\n",
                  dy, mo, yr, maxDays);
    return;
  }
  if (hh < 0 || hh > 23) {
    Serial.println("Erro: Hora deve estar entre 0 e 23.");
    return;
  }
  if (mm < 0 || mm > 59) {
    Serial.println("Erro: Minuto deve estar entre 0 e 59.");
    return;
  }
  if (ss < 0 || ss > 59) {
    Serial.println("Erro: Segundo deve estar entre 0 e 59.");
    return;
  }

  // Acertar a hora local
  rtclock.set((uint16_t)yr, (uint8_t)mo, (uint8_t)dy, (uint8_t)hh, (uint8_t)mm,
              (uint8_t)ss);

  // Atualizar o scheduler imediatamente baseado no novo relógio
  scheduler.onModeChanged();

  Serial.printf(
      "Hora definida com sucesso para: %04d-%02d-%02d %02d:%02d:%02d\n", yr, mo,
      dy, hh, mm, ss);
}

void Terminal::_cmdExportConfig() {
  char hexStr[sizeof(AppConfigBlob) * 2 + 1];
  storage.exportConfigHex(hexStr, sizeof(hexStr));

  Serial.println("=== EXPORTACAO DE CONFIGURACOES LAWN&ORDER ===");
  Serial.println("Copie a linha abaixo para salvaguarda:");
  Serial.println(hexStr);
  Serial.println("===============================================");
}

void Terminal::_cmdImportConfig(char *hexStr) {
  if (!hexStr || *hexStr == '\0') {
    Serial.println("Erro: Comando import_config requer a string hexadecimal "
                   "correspondente.");
    return;
  }

  if (storage.importConfigHex(hexStr)) {
    // Sucesso na importação - recalcular agendamento do Scheduler
    scheduler.onModeChanged();
    // Reposicionar ecrã físico para Idle para refletir as novas configurações
    // imediatamente
    ui.goIdle();
    Serial.println("Configuracoes importadas e gravadas em NVS com sucesso!");
  } else {
    Serial.println("Erro: Falha na importacao do Hex Blob. Verifique a "
                   "integridade da string.");
  }
}

void Terminal::_cmdClearHistory() {
  if (!_pendingClearHistory) {
    Serial.println("AVISO: Tem a certeza que deseja apagar permanentemente o historico?");
    Serial.println("Escreva 'yes' para confirmar ou qualquer outra coisa para cancelar.");
    _pendingClearHistory = true;
    return;
  }
  Serial.println("A limpar o historico de regas...");
  history.clear();
  // Reposicionar ecra fisico para Idle para refletir o historico limpo
  // imediatamente
  ui.goIdle();
  Serial.println("Historico de regas limpo com sucesso!");
}

void Terminal::_cmdReboot() {
  Serial.println("A reiniciar o controlador Lawn & Order...");
  Serial.flush();
  delay(100);
  ESP.restart();
}

void Terminal::_cmdExportHistory() {
  Serial.println(TXT_LOG_START_EXPORT);
  history.startExport();
}
