# Sistema de Rega - ESP32

Controlador autónomo de rega para jardim, com interface física por ecrã LCD e encoder rotativo. Sem aplicação, sem Wi-Fi, sem dependências de rede - funciona de forma completamente independente.

---

## O que faz

O sistema controla até **4 zonas de rega** de forma independente, com horários automáticos configuráveis e a possibilidade de disparar regas manuais a qualquer momento. Toda a interação é feita através de um único encoder rotativo: rodar para navegar, clicar para selecionar.

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
| **Ver Horários** | Mostra o modo ativo, hora de disparo e zonas incluídas |
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
| Interface LCD + encoder (FSM com 7 estados) | ✅ Completo |
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
| Assistente de setup inicial | ✅ Completo |


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
│   ├── config.h                   pinos, constantes, horários padrão, versão
│   ├── AppState.h / .cpp          estado global (gState), tabela de horários
│   ├── Display.h / .cpp           wrapper LCD com shadow buffer anti-flickering
│   ├── Encoder.h / .cpp           driver ISR do encoder rotativo
│   ├── RTClock.h / .cpp           módulo DS3231 (DS1307 em Wokwi)
│   ├── Scheduler.h / .cpp         agendamento automático, cálculo de próxima rega
│   ├── WateringController.h/.cpp  fila de zonas, controlo de relés, registo de histórico
│   ├── History.h / .cpp           leitura/escrita do histórico CSV em LittleFS
│   ├── Storage.h / .cpp           persistência de configurações em NVS
│   ├── UI.h / .cpp                máquina de estados da interface (7 estados, 10 menus)
│   └── main.cpp                   setup() e loop()
├── diagram.json                   circuito para o simulador Wokwi
├── platformio.ini                 configuração PlatformIO
└── wokwi.toml                     apontador para o firmware compilado
```

### Fluxo de inicialização

```
setup()
  initAppState()          defaults em RAM
  storage.begin/load()    sobrescreve RAM com valores guardados em NVS
  history.begin()         monta LittleFS
  display/encoder.begin() hardware UI
  rtclock.begin()         DS3231 → gState.now, gState.rtc_valid
  wateringCtrl.begin()    configura pinos de relé
  scheduler.begin()       calcula primeiro next_hour/min
  ui.begin()              desenha ecrã de idle
```

### Fluxo do loop

```
loop() - executado continuamente
  rtclock.update()       lê DS3231 → gState.now (1×/seg)
  scheduler.update()     verifica trigger, dispara rega, expira suspensão
  ui.update()            trata encoder, redesenha LCD
  wateringCtrl.update()  avança temporizador de zona, controla relés
```

---

### Decisões de design

**Shadow buffer no Display** - `lcd.clear()` causa um flash visível. O `Display` mantém uma cópia do conteúdo atual e só escreve para o hardware as linhas que mudaram, eliminando o flickering.

**ISR no encoder** - A rotação é capturada por interrupção hardware (`IRAM_ATTR`). O `loop()` lê apenas o delta acumulado, evitando perder passos quando o bus I2C está ocupado.

**Máquina de estados da UI** - 7 estados (`IDLE`, `MENU`, `INFO`, `CONFIRM`, `DONE`, `DUR_PICK`, `TIME_EDIT`). Ações codificadas em strings compactas nos itens de menu (ex: `"confirm:...|main|general"`) - menus declarados como dados estáticos, sem callbacks.

**`WaterTrigger` no histórico** - Cada ciclo é marcado com `AUTO`/`MANUAL`/`CUSTOM`/`TEST`. O caractere ASCII do enum é escrito diretamente no CSV (`A`, `M`, `C`, `T`), eliminando conversões.

**Suspensão com expiração por unix timestamp** - `suspended_until` é um timestamp UNIX calculado no momento da suspensão (`now.unix + 3 * 86400`). O Scheduler verifica a expiração a cada segundo. Sobrevive a reboots via NVS.

**Sem Wi-Fi por design** - O DS3231 fornece tempo real com precisão de ±2 ppm (≈1 min/ano). Não há superfície de ataque TCP/IP, credenciais em flash, nem dependência de rede.

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

// Timeout de backlight padrão (alterável no menu):
// (definido em AppState.cpp como 120000UL = 2 min)
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


