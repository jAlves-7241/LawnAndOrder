# Lawn & Order — Controlador de Rega Autónomo (ESP32)

Sistema de rega automática para jardim, baseado em ESP32, com ecrã LCD 2004 (I2C), encoder rotativo, relógio de tempo real (RTC) e controlo de até 4 zonas de rega através de relés. O firmware é totalmente não bloqueante, regista histórico de regas em LittleFS, persiste configurações em NVS Flash e expõe um terminal de comandos via Serial.

Este projeto foi desenvolvido com [PlatformIO](https://platformio.org/) e é compatível com simulação no [Wokwi](https://wokwi.com/).

---

## Funcionalidades Principais

- **4 modos de rega automática**: Intenso, Médio, Fraco e Desativado, cada um com horários predefinidos (configuráveis em `config.h`).
- **Modo Personalizado**: o utilizador define o número de ciclos diários (1–4), a frequência em dias (a cada N dias) e o horário exato de cada ciclo.
- **Rega manual**: rega geral (todas as zonas ativas) ou rega personalizada (seleção de zonas + duração).
- **Gestão de 4 zonas independentes**: nome, ativação e duração (0–20 min) configuráveis por zona.
- **Suspensão temporária**: pausa a rega automática por um número de dias definido pelo utilizador.
- **Assistente de configuração inicial (Setup Wizard)**: guia o utilizador no primeiro arranque (data/hora, modo, zonas).
- **Histórico de regas**: gravado em CSV no LittleFS, com cache em RAM/NVS para acesso instantâneo no LCD e exportação completa via Serial.
- **Recuperação de falha de energia**: ciclos de rega interrompidos por um blackout são retomados de forma segura ao reiniciar (ou descartados se já não fizerem sentido).
- **Hora automática com DST (horário de verão)**: suporte às regras europeias de transição de horário.
- **Terminal de comandos Serial**: consulta de estado, acerto de hora, exportação/importação de configurações e histórico, reset remoto.
- **Tolerância a falhas do RTC**: se a pilha do DS3231 estiver descarregada ou o módulo não for detetado, o sistema cai automaticamente para um relógio por software, mantém a interface a funcionar (assinalando a hora como inválida no LCD) e tenta redetetar o RTC a cada ~10 s; uma falha a meio da operação (queda de tensão/EMI) é detetada por leitura contínua do oscilador e despoleta recuperação automática do barramento I2C.
- **Suporte multi-idioma**: textos do LCD e do terminal Serial separados e configuráveis independentemente (`PT`/`EN`) em `i18n.h`.

---

## Hardware Necessário

| Componente | Notas |
|---|---|
| ESP32 DevKit C (v4) | Microcontrolador principal |
| LCD 2004 com módulo I2C | 20 colunas × 4 linhas, endereço `0x27` |
| Módulo RTC DS3231 | Relógio de tempo real com bateria (DS1307 usado apenas em simulação Wokwi) |
| Encoder rotativo KY-040 | Navegação nos menus (rotação + clique) |
| Módulo de relés (até 4 canais) | Controlo das eletroválvulas, ativo em LOW por defeito |

### Mapa de Pinagem (ver `src/config.h`)

| Função | Pino |
|---|---|
| I2C SDA / SCL (LCD + RTC) | GPIO 21 / GPIO 22 |
| Encoder CLK / DT / SW | GPIO 32 / GPIO 33 / GPIO 25 |
| Relé Zona 1 / 2 / 3 / 4 | GPIO 26 / 27 / 14 / 13 |

> O ficheiro `diagram.json` contém o esquema de ligações pronto a usar no simulador Wokwi.

---

## Estrutura do Projeto

```
.
├── platformio.ini       # Configuração do ambiente PlatformIO (board, libs, build flags)
├── wokwi.toml            # Configuração da simulação Wokwi
├── diagram.json           # Esquema de ligações para o simulador Wokwi
├── src/
│   ├── main.cpp            # Ponto de entrada: setup() e loop() principais
│   ├── config.h            # Pinagem, constantes temporais e valores por defeito
│   ├── i18n.h               # Strings de interface (LCD) e de sistema (Serial), PT/EN
│   ├── log.h                # Sistema de logs com 4 níveis, filtrado em compile-time
│   ├── AppState.h / .cpp     # Estado central da aplicação (gState)
│   ├── Display.h / .cpp       # Driver LCD com shadow buffering (sem flicker)
│   ├── Encoder.h / .cpp        # Driver do encoder rotativo (interrupções + debounce)
│   ├── RTClock.h / .cpp         # Interface com o RTC, fuso horário e DST automático
│   ├── Scheduler.h / .cpp        # Motor de agendamento automático O(1)
│   ├── WateringController.h / .cpp # Controlo dos relés e da fila de rega
│   ├── History.h / .cpp           # Histórico de regas em CSV (LittleFS)
│   ├── Storage.h / .cpp            # Persistência de configurações em NVS Flash
│   ├── Terminal.h / .cpp            # Terminal de comandos interativo via Serial
│   ├── UI.h / .cpp                   # Gestor de interface e navegação entre ecrãs
│   ├── README.md                      # Documentação detalhada do código fonte e da UI
│   └── ui/                             # Ecrãs da interface (padrão polimórfico)
│       ├── UIScreen.h                   # Classe base abstrata de ecrã
│       ├── UITypes.h                     # MenuID, DurContext, TimeEditContext, SetupStep
│       ├── MenuBuilder.h / .cpp           # Construção dinâmica de itens de menu
│       ├── ScreenIdle.h / .cpp             # Ecrã principal (idle)
│       ├── ScreenMenu.h / .cpp              # Menus scrolláveis
│       ├── ScreenCommon.h / .cpp             # Ecrãs Info / Confirm / Done
│       ├── ScreenEditors.h / .cpp             # Seletores de duração, data e hora
│       └── ScreenSetup.h / .cpp                # Assistente de configuração inicial
```

Para uma descrição detalhada de cada módulo, do fluxo de inicialização e do ciclo de execução principal, consulte **[src/README.md](src/README.md)**.

---

## Como Compilar e Carregar

### Pré-requisitos

- [PlatformIO Core](https://platformio.org/install) (CLI) ou a extensão PlatformIO no VS Code.

### Compilar

```bash
pio run
```

### Carregar para o ESP32

```bash
pio run -t upload
```

### Monitor Série

```bash
pio device monitor
```

(baud rate: `115200`, configurado em `platformio.ini`)


## Terminal de Comandos (Serial, 115200 baud)

Disponível através de `pio device monitor` ou qualquer terminal série:

| Comando | Descrição |
|---|---|
| `help` / `?` | Mostra a lista de comandos disponíveis |
| `status` | Relatório em tempo real do sistema |
| `set_time AAAA-MM-DD HH:MM:SS` | Define a data e hora local |
| `export_config` | Exporta a configuração atual como string hexadecimal |
| `import_config <hex>` | Importa configuração a partir de uma string hexadecimal |
| `export_history` | Exporta todo o histórico de regas em CSV via Serial |
| `clear_history` | Apaga permanentemente o histórico de regas (pede confirmação) |
| `reboot` | Reinicia o controlador |

---

## Configuração e Personalização

A maioria dos parâmetros do sistema está centralizada em **`src/config.h`** e pode ser ajustada antes de compilar, sem necessidade de alterar lógica de código. Os mais relevantes:

| Define | Descrição | Valor por defeito |
|---|---|---|
| `LCD_ADDR` | Endereço I2C do LCD | `0x27` |
| `RELAY_ON` / `RELAY_OFF` | Polaridade do módulo de relés (alterar para `HIGH`/`LOW` se a placa for ativa-HIGH) | `LOW` / `HIGH` |
| `TIMEZONE_OFFSET` | Fuso horário base, em horas | `0` |
| `AUTO_DST_DEFAULT` | Ativa por defeito a compensação automática de horário de verão (regras EU) | `true` |
| `SCHED_INTENSO_*` / `SCHED_MEDIO_*` / `SCHED_FRACO_*` | Horários (hora/minuto) de cada slot dos modos predefinidos | ver `config.h` |
| `SCHED_CUSTOM_*` | Valores iniciais do modo Personalizado (hora do 1º ciclo, intervalo em dias, nº de ciclos) | 06:00 / 1 dia / 1 ciclo |
| `ZONE1_NAME..ZONE4_NAME` / `ZONE1_DUR..ZONE4_DUR` | Nome e duração (min) de fábrica de cada zona (nomes devem ter ≤7 carateres para caber no LCD) | ver `config.h` |
| `NUM_ZONES` | Número de zonas de rega suportadas | `4` |
| `MAX_SLOTS_PER_MODE` | Número máximo de ciclos diários no modo Personalizado | `4` |
| `IDLE_TIMEOUT_MS` | Tempo de inatividade até regressar ao ecrã inicial | `30000` (30 s) |
| `BACKLIGHT_TIMEOUT_NEVER` | Valor sentinela para "luz de fundo sempre ligada" (selecionável no menu Definições Avançadas) | `0xFFFFFFFF` |
| `DISPLAY_OFF_DELAY_MS` | Atraso adicional após apagar a luz de fundo até desligar os pixels do LCD | `20000` (20 s) |
| `ZONE_TEST_DURATION_S` | Duração de cada zona no modo de teste manual | `5` s |
| `ZONE_WAIT_DELAY_MS` | Tempo de espera entre o fecho de uma válvula e a abertura da seguinte | `5000` ms |
| `RELAY_DEADTIME_MS` | Dead-time de segurança aplicado antes de ativar um relé | `20` ms |
| `WDT_TIMEOUT_S` | Timeout do watchdog de hardware | `8` s |
| `SUSPEND_DEFAULT_DAYS` | Valor inicial sugerido ao suspender a rega | `3` dias |
| `SAFETY_GAP_SEC` | Intervalo mínimo de segurança (s) entre o fim de um ciclo recuperado e o próximo ciclo agendado, para evitar regas duplas | `7200` (2 h) |
| `DATE_YEAR_MIN` / `DATE_YEAR_MAX` | Intervalo de anos aceite no editor de data e no `set_time` | `2020` – `2099` |
| `HISTORY_MAX_ENTRIES` | Número máximo de linhas guardadas no ficheiro de histórico antes de rotação | `1500` |
| `HISTORY_DISPLAY` | Número de entradas de histórico mostradas no menu do LCD | `3` |
| `MENU_WRAP_AROUND` | Se `1`, a navegação nos menus dá a volta ao chegar ao fim da lista | `1` |
| `LOG_LEVEL` | Nível de verbosidade dos logs Serial (`LVL_NONE`..`LVL_DEBUG`) | `LVL_INFO` |
| `NVS_VERSION` (em `Storage.h`) | Versão do esquema de dados persistidos; incrementar sempre que a estrutura `AppConfigBlob`/`RecoveryState` mudar, para invalidar dados antigos incompatíveis | `4` |

O idioma da interface LCD e dos logs/terminal Serial é definido de forma independente em `platformio.ini`:

```ini
build_flags =
    -D LANG_UI_PT       ; LANG_UI_PT ou LANG_UI_EN
    -D LANG_SERIAL_EN   ; LANG_SERIAL_PT ou LANG_SERIAL_EN
```