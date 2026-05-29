# Sistema de Rega - ESP32

Controlador autónomo de rega para jardim, com interface física por ecrã LCD e encoder rotativo. Sem aplicação, sem Wi-Fi, sem dependências de rede - funciona de forma completamente independente.

---

## O que faz

O sistema controla até **4 zonas de rega** de forma independente, com horários automáticos configuráveis e a possibilidade de iniciar regas manuais a qualquer momento. Toda a interação é feita através de um único encoder rotativo: rodar para navegar, clicar para selecionar.

O ecrã principal mostra a hora atual e o horário da próxima rega. Quando uma rega está em curso, o ecrã passa a mostrar a zona ativa e uma barra de progresso em tempo real.

---

## Interface - guia de utilização

### Ecrã de idle (ecrã principal)

```
     14:32


    Prox: 18:00
```

Com rega em curso:

```
     14:32

  Z2 Horta:
[######-------]  47%
```

Sem RTC configurado:

```
     --:--


  ! Acertar hora !
```

Qualquer interação com o encoder abre o menu principal.

---

### Menu principal

```
~~~~~ MENU ~~~~~~~~~~
 Rega Manual
 Programacao
 Definicoes
```

Navega rodando o encoder. Clica para entrar. `<- Voltar` regressa sempre ao nível anterior. Sem ação durante 30 segundos, o sistema volta automaticamente ao ecrã de idle.

---

### Rega Manual

| Opção | Comportamento |
|---|---|
| **Rega Geral** | Executa um ciclo completo seguindo o modo automático atual - zonas ativas pela duração configurada para cada uma |
| **Personalizado** | Seleciona as zonas a incluir e define uma duração uniforme (1–20 min); pede confirmação antes de iniciar |

---

### Programação

| Opção | Comportamento |
|---|---|
| **Ver Horários** | Mostra o modo ativo, hora de rega e zonas incluídas |
| **Alterar Modo** | Escolhe entre Intenso, Médio, Fraco, Desativado ou Personalizado |
| **Configurar Zonas** | Clica numa zona para abrir o selector de duração (0 = desativar, 1–20 min); roda para ajustar |
| **Suspender Rega** | Pausa a rega automática por 3 dias sem alterar os horários; expira automaticamente |

#### Modos automáticos

| Modo | Horário | Indicado para |
|---|---|---|
| **Intenso** | 3x/dia, todos os dias | Verão, calor intenso |
| **Médio** | 2x/dia, todos os dias | Primavera / Outono |
| **Fraco** | 1x/dia, todos os dias | Inverno suave |
| **Desativado** | - | Inverno / ausência prolongada |
| **Personalizado** | Número de ciclos diários e frequência variáveis | Qualquer |

Os horários padrão são definidos em `config.h` com `SCHED_*` e podem ser alterados sem tocar no resto do código.

---

### Definições

| Opção | Comportamento |
|---|---|
| **Testar Zonas** | Ativa cada zona por 5 segundos para verificar as electroválvulas (não registado no histórico) |
| **Histórico** | Últimos 3 ciclos de rega com data, hora, tipo e duração por zona; lidos do ficheiro CSV em LittleFS |
| **Data/Hora** | Editor dividido em 2 ecrãs: ajusta primeiro a data (dia/mês/ano) e depois a hora (horas/minutos), guardando no RTC DS3231 |
| **Fuso Horário** | Liga/Desliga o ajuste automático para horário de verão (regras EU) com compensação de perdas na suspensão |
| **Tempo Ecrã** | Define quando o ecrã adormece após inatividade: 30s / 1min / **2min** / 5min / Sempre |
| **Versão Firmware** | Versão, data de build e modelo do microcontrolador |
| **Reset de Fábrica** | Repõe configurações para os valores padrão e apaga o histórico (pede confirmação) |

#### Tempo de ecrã

Após o período de inatividade configurado, o backlight do LCD apaga-se automaticamente. O primeiro toque no encoder volta a ligar o ecrã - esse toque é descartado para não disparar ações acidentais.

---

### Data / Hora

```
~~~~~ DATA/HORA ~~~~~
  [14] / 05 / 2026
    ^ dia
click -> proximo campo
```

O fluxo guia-te pelo dia, mês, ano, hora e minutos. Apenas no final do processo a hora é escrita no módulo RTC DS3231. O chip armazena a hora de forma linear (UTC) e a interface apresenta-a com o fuso apropriado caso a opção **Fuso Horario: Auto** esteja ativa. Isto previne saltos ou perdas de dias de suspensão durante as mudanças de hora!

---

### Assistente de Configuração (Setup Wizard)

No primeiro arranque (ou após um reset de fábrica), o sistema entra automaticamente no **Assistente de Configuração**. Este guia interativo orienta o utilizador passo a passo nas seguintes etapas cruciais:
1. **Ecrã de Boas-Vindas**: Apresentação inicial do assistente.
2. **Acerto de Data e Hora**: Definição inicial do relógio local em tempo real.
3. **Seleção de Modo**: Escolha do modo automático de rega (Intenso, Médio, Fraco ou Personalizado).
4. **Configuração Avançada (Modo Personalizado)**: Se selecionado, define a frequência em dias, o número de ciclos diários e o horário preciso de cada ciclo.
5. **Configuração das Zonas**: Ativação/desativação de cada zona e respetivos tempos individuais de rega.
6. **Ecrã de Conclusão**: Guarda as definições de forma persistente na NVS e inicia o sistema no estado de repouso (`IDLE`).

Qualquer progresso ou recuo no assistente é suportado de forma intuitiva rodando e clicando no encoder.

---

### Terminal de Comando Serial CLI

O Lawn & Order dispõe de um terminal de comandos interativo acessível através da porta de comunicação Serial a **115200 baud** (com terminação de linha `LF` ou `CR`). O terminal funciona de forma totalmente não bloqueante no loop de execução do ESP32.

#### Comandos Disponíveis:

| Comando | Descrição | Exemplo / Formato |
|---|---|---|
| **`help`** ou **`?`** | Mostra o menu de ajuda com a lista de comandos. | `help` |
| **`status`** | Relatório detalhado em tempo real de telemetria (hora local e UTC, estado do RTC, modo ativo, suspensão, configuração das 4 zonas de rega e estado da rega em curso com percentagem de progresso). | `status` |
| **`set_time`** | Define a data e hora do hardware RTC, com validação inteligente contra anos fora de gama (`2020-2099`), dias inválidos por mês e anos bissextos. Atualiza o agendamento imediatamente. | `set_time 2026-05-29 15:30:00` |
| **`export_config`** | Gera e exporta um **Hex Blob** seguro contendo todas as configurações atuais do sistema para cópia de segurança. | `export_config` |
| **`import_config`** | Importa as definições a partir do Hex Blob fornecido, valida a integridade, grava-as na NVS Flash e atualiza a interface LCD. | `import_config <68_caracteres_hex>` |
| **`reboot`** | Efetua o reinício físico de segurança do ESP32. | `reboot` |

> **Salvaguarda de Transmissão:** O terminal descarta automaticamente qualquer entrada serial e permanece silencioso sempre que a funcionalidade de **Exportação de Histórico** via LittleFS estiver ativa, evitando corrupção ou colisão na porta série.

---

## Hardware necessário

| Componente | Especificação |
|---|---|
| Microcontrolador | ESP32 DevKit v1 |
| Ecrã | LCD 2004 (20×4) com backpack I2C (PCF8574, endereço 0x27) |
| Interface | Encoder rotativo com botão integrado |
| Relógio | Módulo RTC DS3231 com bateria CR2032 |
| Relés | Módulo de 4 relés, ativo em LOW (standard) |

### Ligações

| Sinal | ESP32 GPIO |
|---|---|
| LCD SDA | 21 |
| LCD SCL | 22 |
| Encoder CLK | 32 |
| Encoder DT | 33 |
| Encoder SW | 25 |
| RTC SDA | 21 (mesmo barramento I2C que o LCD) |
| RTC SCL | 22 (mesmo barramento I2C que o LCD) |
| Relé Zona 1 - Jardim | 26 |
| Relé Zona 2 - Horta | 27 |
| Relé Zona 3 - Relvado | 14 |
| Relé Zona 4 - Sebe | 12 |

> **I2C partilhado:** LCD (0x27) e DS3231 (0x68) coexistem nos mesmos dois fios SDA/SCL sem conflito - cada dispositivo responde apenas ao seu endereço.

> **Relés active-LOW:** padrão nos módulos com optoacoplador. Se o teu módulo for active-HIGH, altera `RELAY_ON` / `RELAY_OFF` em `config.h`.

---

## Estado atual do projeto

| Funcionalidade | Estado |
|---|---|
| Interface LCD + encoder (Ecrãs modulares OOP e MenuBuilder) | ✅ Completo |
| Modos automáticos com horários configuráveis | ✅ Completo |
| Rega manual geral e personalizada | ✅ Completo |
| Testes de zona (5s por zona) | ✅ Completo |
| Configuração de zonas (duração 0–20 min, ativar/desativar) | ✅ Completo |
| Controlo de relés GPIO | ✅ Completo |
| Relógio RTC DS3231 com acerto de hora via encoder | ✅ Completo |
| Agendamento automático por hora (Scheduler) | ✅ Completo |
| Suspensão de rega com compensação de transição de fusos | ✅ Completo |
| Gestão de ecrã (sleep/wake, timeout configurável) | ✅ Completo |
| Persistência de configurações em NVS flash | ✅ Completo |
| Histórico de ciclos em CSV (LittleFS) | ✅ Completo |
| Simulação Wokwi | ✅ Completo |
| Modo Personalizado (horário livre) | ✅ Completo |
| Acertar data completa (dia/mês/ano) via encoder | ✅ Completo |
| Horário de Verão Automático | ✅ Completo |
| Assistente de setup inicial (Setup Wizard) | ✅ Completo |
| Sistema de logs com níveis de severidade | ✅ Completo |
| Terminal Serial CLI não bloqueante | ✅ Completo |
| Recuperação automática de barramento I2C preso | ✅ Completo |
| Watchdog de hardware (ESP-IDF) integrado | ✅ Completo |
| Cache binária NVS de histórico para arranque rápido | ✅ Completo |
| NVS flash batching (Diferimento de escrita) | ✅ Completo |
| Exportação assíncrona de CSV via Serial | ✅ Completo |


---

## Implementação

### Compilar e simular

**Pré-requisitos:**

| Ferramenta | Notas |
|---|---|
| VSCode | Qualquer versão |
| Extensão PlatformIO IDE | `platformio.ide` |
| Extensão Wokwi for VSCode | `wokwi.wokwi-vscode` + conta gratuita em wokwi.com |

**Compilar para hardware real:**
```
Ctrl + Alt + B
```
Ou via terminal:
```bash
pio run -e esp32dev
```

**Simular no Wokwi** (requer firmware compilado):
```
Abre diagram.json → F1 → "Wokwi: Start Simulator"
```
Na primeira utilização: `F1 → Wokwi: Request License`

**Upload para hardware real:**
```
Ctrl + Alt + U
```

**Monitor série** (115200 baud):
```
Ctrl + Alt + S
```

---

### Estrutura de ficheiros

```
rega-esp32/
├── src/
│   ├── ui/                        ecrãs modulares e construtor de menus (UIScreen, MenuBuilder, etc.)
│   │   ├── UIScreen.h             classe base para todos os ecrãs polimórficos
│   │   ├── UITypes.h              definições de enums, contextos e passos de configuração
│   │   ├── MenuBuilder.h / .cpp   construção dinâmica de menus e items
│   │   ├── ScreenCommon.h / .cpp  ecrãs comuns (ScreenInfo, ScreenConfirm, ScreenDone)
│   │   ├── ScreenEditors.h / .cpp ecrãs de edição (ScreenDurPick, ScreenDateEdit, ScreenTimeEdit)
│   │   ├── ScreenIdle.h / .cpp    ecrã principal com telemetria e barra de progresso
│   │   ├── ScreenMenu.h / .cpp    renderização e navegação de menus dinâmicos
│   │   └── ScreenSetup.h / .cpp   ecrãs do assistente de configuração inicial (Setup Wizard)
│   ├── config.h                   pinos, constantes, horários padrão, versão
│   ├── log.h                      macros de log com níveis (ERRO/AVISO/INFO/DEBUG)
│   ├── AppState.h / .cpp          estado global (gState), tabela de horários
│   ├── Display.h / .cpp           wrapper LCD com shadow buffer anti-flickering
│   ├── Encoder.h / .cpp           driver ISR do encoder rotativo
│   ├── RTClock.h / .cpp           módulo DS3231 (DS1307 em Wokwi)
│   ├── Scheduler.h / .cpp         agendamento automático, cálculo de próxima rega
│   ├── WateringController.h/.cpp  fila de zonas, controlo de relés, registo de histórico
│   ├── History.h / .cpp           leitura/escrita de CSV e exportação assíncrona
│   ├── Storage.h / .cpp           persistência de configurações em NVS
│   ├── Terminal.h / .cpp          terminal serial de comandos CLI não bloqueante
│   ├── UI.h / .cpp                gestor central de ecrãs e navegação
│   └── main.cpp                   setup() e loop()
├── diagram.json                   circuito para o simulador Wokwi
├── platformio.ini                 configuração PlatformIO
└── wokwi.toml                     apontador para o firmware compilado
```

### Fluxo de inicialização

```
setup()
  Pinos de relé forced OFF   Segurança física imediata dos relés
  esp_task_wdt_init()        Inicialização do Watchdog de hardware (5 segundos)
  initAppState()             Carrega definições por omissão em RAM
  storage.begin/load()       Sobrescreve RAM com valores guardados em NVS
  history.begin()            Monta LittleFS
  display/encoder.begin()    Inicializa hardware da UI
  rtclock.begin()            DS3231 → gState.now, gState.rtc_valid (com callback recoverI2C)
  wateringCtrl.begin()       Configura pinos de relé
  Limpa suspensão expirada   BUG-5 fix: limpa suspensão stale da NVS caso expirada
  scheduler.begin()          Calcula próximo ciclo com base no RTC e no modo
  ui.begin()                 Configura primeiro ecrã (Setup Wizard ou IDLE)
  terminal.begin()           Inicializa o Terminal Serial de comandos
```

### Fluxo do loop

```
loop() - executado continuamente
  esp_task_wdt_reset()   Alimenta o Watchdog de hardware (WDT) em cada iteração
  rtclock.update()       Lê DS3231 → gState.now (1×/seg)
  scheduler.update()     Verifica triggers automáticos e expiração de suspensão
  ui.update()            Trata inputs do encoder, processa timeouts de ecrã/backlight
  wateringCtrl.update()  Avança temporizadores de zona e ativa/desativa relés
  history.update()       Executa a rotação e cópia de ficheiro CSV em background
  terminal.update()      Processa a entrada serial não bloqueante e comandos CLI
```

---

### Decisões de design

**Shadow buffer no Display** - `lcd.clear()` causa um flash visível. O `Display` mantém uma cópia do conteúdo atual e só escreve para o hardware as linhas que mudaram, eliminando o flickering.

**ISR no encoder** - A rotação é capturada por interrupção hardware (`IRAM_ATTR`). O `loop()` lê apenas o delta acumulado, evitando perder passos quando o bus I2C está ocupado.

**Arquitetura polimórfica de UI (UIScreen)** - Para garantir máxima escalabilidade e legibilidade do código, a antiga máquina de estados monolítica de 7 estados foi completamente reestruturada numa arquitetura modular orientada a objetos. Cada ecrã da interface física é uma classe especializada que herda de `UIScreen` (como `ScreenIdle`, `ScreenMenu`, `ScreenSetup`, etc.). Cada ecrã gere individualmente o seu desenho (`render`), interações do encoder (`handleRotation` / `handleClick`) e atualização de tempo (`update`).

**MenuBuilder e dispatch de ações** - O `MenuBuilder` constrói dinamicamente as estruturas de dados dos menus e submenus, incluindo itens com ações codificadas em formato string e tags de controlo compactas (ex: `"confirm:Iniciar rega...|main|general"`). O gestor central `UI` despacha as ações para o ecrã correspondente de forma desacoplada e limpa.

**`WaterTrigger` no histórico** - Cada ciclo é marcado com `AUTO`/`MANUAL`/`CUSTOM`/`TEST`. O caractere ASCII do enum é escrito diretamente no CSV (`A`, `M`, `C`, `T`), eliminando conversões.

**Suspensão com expiração por unix timestamp** - `suspended_until` é um timestamp UNIX calculado no momento da suspensão (`now.unix + 3 * 86400`). O Scheduler verifica a expiração a cada segundo. Sobrevive a reboots via NVS.

**Sem Wi-Fi por design** - O DS3231 fornece tempo real com precisão de ±2 ppm (≈1 min/ano). Não há superfície de ataque TCP/IP, credenciais em flash, nem dependência de rede.

**Watchdog de Hardware (WDT)** - Integrado o Task Watchdog do ESP32 (`esp_task_wdt`) com timeout configurável no loop principal. Se a thread de controlo principal bloquear por qualquer motivo por mais de `WDT_TIMEOUT_S` segundos, o chip sofre um reboot físico de segurança e desativa imediatamente os relés.

**Cache Binário na NVS (Boot instantâneo)** - Em vez de ler e efetuar o parse sequencial de até 1500 linhas do arquivo CSV no LittleFS a cada boot (o que demoraria centenas de milissegundos), o array de cache do histórico e a contagem de linhas são guardados em formato binário compacto na NVS do ESP32. O arranque do sistema lê esta cache em **0ms**, protegendo também o LittleFS contra acessos excessivos de leitura.

**NVS Batching (Diferimento de Flash)** - Para preservar a integridade da memória flash NVS contra o desgaste das rotações de alta frequência do encoder, as escritas em flash (`storage.save()`) são agrupadas em batch e efetuadas apenas na transição do ecrã de menu de regresso ao ecrã principal `IDLE`.

**Scheduler O(1) com Salto Modular** - Para o modo `EVERY_X_DAYS`, eliminámos a pesquisa sequencial temporal linear diária. O Scheduler calcula a correspondência do dia utilizando aritmética modular em tempo constante $O(1)$, com salvaguardas contra o recuo abrupto no relógio do RTC (ex: falha de bateria).

**Recuperação automática de barramento I2C preso (`recoverI2C`)** - Em caso de ruído ou falha transitória que prenda a linha SDA em LOW (um problema clássico em barramentos I2C com cabos longos), a rotina de erro do RTC liberta o barramento gerando manualmente até 9 pulsos de relógio no pino SCL, finalizando com uma condição de STOP manual. Isto permite ao sistema recuperar a comunicação com o ecrã LCD e o RTC sem necessidade de reiniciar o microcontrolador.

**Segurança física de hardware em boot** - Ao arrancar o ESP32, os pinos de GPIO podem flutuar eletricamente e causar ativações breves e indesejadas nos relés de rega. Para mitigar isto, o Lawn & Order força de forma imediata o estado de todos os relés para desligados (`RELAY_OFF`) como primeira ação absoluta da função `setup()`, antes de qualquer outra inicialização de periféricos ou sistema de ficheiros.

---

### Configuração rápida (`config.h`)

```cpp
// Alterar horários padrão por modo:
// INTENSO: 3 ciclos (07:00, 13:00, 19:00)
#define SCHED_INTENSO_SLOT0_H   7
#define SCHED_INTENSO_SLOT1_H   13
#define SCHED_INTENSO_SLOT2_H   19

// MEDIO: 2 ciclos (08:00, 20:00)
#define SCHED_MEDIO_SLOT0_H     8
#define SCHED_MEDIO_SLOT1_H     20

// FRACO: 1 ciclo diário
#define SCHED_FRACO_SLOT0_H     8

// Duração dos testes de zona:
#define ZONE_TEST_DURATION_S    5

// Timeout de idle antes de regressar ao ecrã principal:
#define IDLE_TIMEOUT_MS         30000UL

// Timeout de watchdog de segurança (em segundos):
#define WDT_TIMEOUT_S           5

// Dias padrão para suspensão de rega:
#define SUSPEND_DEFAULT_DAYS    3

// Limites de anos no acerto de data:
#define DATE_YEAR_MIN           2020
#define DATE_YEAR_MAX           2099

// Nível de log (ver log.h):
#define LOG_LEVEL  LVL_INFO   // LVL_NONE / LVL_ERROR / LVL_WARN / LVL_INFO / LVL_DEBUG
```

---

### Sistema de logs

O projecto usa um sistema de logging com 4 níveis de severidade, definido em `log.h`. Cada chamada é filtrada em compile-time - quando desligada, não gera código.

```cpp
LOG_E("TAG", "mensagem", args...)   // ERRO:  falhas de hardware, limites
LOG_W("TAG", "mensagem", args...)   // AVISO: dados incompatíveis, bateria
LOG_I("TAG", "mensagem", args...)   // info:  operações normais, acções
LOG_D("TAG", "mensagem", args...)   // debug: transições internas
```

**Tags por módulo:**

| Tag | Módulo | Ficheiro |
|---|---|---|
| `SYS` | Sistema (boot) | `main.cpp` |
| `APP` | Estado da aplicação | `AppState.cpp` |
| `NVS` | Persistência NVS | `Storage.cpp` |
| `HIST` | Histórico LittleFS | `History.cpp` |
| `LCD` | Display I2C | `Display.cpp` |
| `RTC` | Relógio DS3231 | `RTClock.cpp` |
| `SCHED` | Agendamento | `Scheduler.cpp` |
| `REGA` | Controlo de rega | `WateringController.cpp` |
| `UI` | Interface / acções | `UI.cpp` |

Para alterar o nível de verbosidade, edita `LOG_LEVEL` em `config.h`:

```cpp
#define LOG_LEVEL  LVL_INFO    // default - erros, avisos e operações normais
#define LOG_LEVEL  LVL_DEBUG   // desenvolvimento - inclui transições internas
#define LOG_LEVEL  LVL_ERROR   // produção - apenas erros críticos
#define LOG_LEVEL  LVL_NONE    // desliga todos os logs
```

---

### Bibliotecas

| Biblioteca | Uso |
|---|---|
| `marcoschwartz/LiquidCrystal_I2C` | Driver LCD I2C |
| `adafruit/RTClib` | Driver DS3231 / DS1307 |
| `adafruit/Adafruit BusIO` | Dependência do RTClib |
| `Preferences.h` | NVS (incluída no framework Arduino ESP32) |
| `LittleFS.h` | Sistema de ficheiros (incluído no framework Arduino ESP32) |

As bibliotecas externas são instaladas automaticamente pelo PlatformIO na primeira compilação.


