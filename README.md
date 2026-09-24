# Lawn & Order — Controlador de Rega Autónomo (ESP32)

Sistema de rega automática para jardim, baseado em ESP32, com ecrã LCD 2004 (I2C), encoder rotativo, relógio de tempo real (RTC) e controlo de até 4 zonas de rega através de relés. A execução da rega e da interface é orientada por ciclo e não espera pelo fim de uma rega; algumas operações curtas de periféricos, LittleFS e NVS são síncronas. O histórico é guardado em LittleFS, as configurações em NVS Flash, e existe um terminal de comandos via Serial.

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
- **Recuperação de falha de energia**: após reiniciar, um ciclo interrompido é validado contra o RTC, limites de duração e próximo horário. Para reduzir escritas na flash, o estado persistido não acompanha o tempo decorrido na zona atual: essa zona pode voltar a regar durante toda a duração configurada. Ver os limites de segurança abaixo.
- **Hora automática com DST (horário de verão)**: suporte às regras europeias de transição de horário.
- **Terminal de comandos Serial**: consulta de estado, acerto de hora, exportação/importação de configurações e histórico, reset remoto.
- **Tolerância a falhas do RTC**: a interface continua a funcionar com relógio por software, mas a hora é marcada como inválida e a rega automática fica inibida até existir uma hora válida. O RTC é consultado periodicamente (intervalo nominal de 30 s); falhas persistentes despoletam tentativas de deteção e, após leituras corrompidas consecutivas, recuperação do barramento I2C.
- **Suporte multi-idioma**: textos do LCD e do terminal Serial separados e configuráveis independentemente (`PT`/`EN`) em `i18n.h`.

## Decisões de projeto e limites operacionais

- **Retoma após blackout e desgaste da flash**: a NVS guarda o início do ciclo, a fila e a posição da zona, mas não acompanha continuamente o tempo decorrido. Isto reduz escritas na flash. Se faltar energia durante uma zona, ao reiniciar essa zona pode ser executada novamente pela duração completa configurada. A posição da fila é gravada ao concluir cada zona; uma falha antes de essa gravação terminar também pode repetir a zona acabada de concluir. A retoma só ocorre se houver hora válida e as verificações de duração e intervalo de segurança permitirem. Este compromisso é intencional; para instalações em que a repetição máxima não seja aceitável, usar um corte de água independente ou rever a política para exigir confirmação manual após um blackout.
- **Hora de verão**: a proteção contra a hora repetida no fim do DST impede duplicados durante a execução normal, mas o marcador fica apenas em RAM. Um reboot exatamente entre as duas ocorrências da mesma hora local pode permitir uma segunda ativação. O risco é limitado a essa janela rara e não justifica uma escrita adicional persistente por defeito.
- **RTC indisponível**: no assistente inicial, é possível confirmar que se pretende continuar sem RTC. A interface e a rega manual continuam disponíveis. Sem uma hora válida, rega agendada, suspensões temporizadas e validação da retoma após blackout ficam inibidas. `set_time` por Serial pode acertar o relógio por software e permitir automatismos até ao próximo reboot; para operação automática persistente, defina/recupere o RTC.
- **Histórico**: o arranque tenta montar LittleFS com formatação automática em caso de falha. Se essa recuperação for acionada, o histórico local pode ser apagado; as configurações NVS são armazenadas separadamente. Exporte o histórico se precisar de uma cópia duradoura.
- **Saída de relé**: o firmware comanda os GPIOs para o estado OFF no arranque, mas não consegue detetar um relé ou válvula mecanicamente preso. A instalação deve usar alimentação, isolamento e válvulas adequados; para instalações onde uma fuga tenha consequências relevantes, considere um corte de água independente.

A simulação Wokwi usa o modelo RTC disponível no simulador e não reproduz todas as características elétricas e de falha do DS3231 físico. A validação em hardware com o DS3231 é a referência para o comportamento real.

---

## Hardware Necessário

| Componente | Notas |
|---|---|
| ESP32 DevKit C (v4) | Microcontrolador principal |
| LCD 2004 com módulo I2C | 20 colunas × 4 linhas, endereço `0x27` |
| Módulo RTC DS3231 | Relógio de tempo real com bateria (o Wokwi usa o modelo RTC disponível no simulador) |
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
| `SAFETY_GAP_SEC` | Intervalo mínimo de segurança (s) entre o fim estimado de um ciclo recuperado e o próximo ciclo agendado, para evitar sobreposição | `7200` (2 h) |
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