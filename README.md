# Sistema de Rega - ESP32

Controlador autónomo de rega para jardim, com interface física por ecrã LCD e encoder rotativo. Sem aplicação, sem Wi-Fi, sem dependências de rede — funciona de forma completamente independente.

---

## O que faz

O sistema controla até **4 zonas de rega** de forma independente, com horários automáticos configuráveis e a possibilidade de disparar regas manuais a qualquer momento. Toda a interação é feita através de um único botão rotativo: rodar para navegar, clicar para selecionar.

O ecrã principal mostra a hora atual e o horário da próxima rega. Quando uma rega está em curso, o ecrã passa a mostrar a zona ativa e uma barra de progresso em tempo real.

---

## Interface — guia de utilização

### Ecrã de idle (ecrã principal)

```
     14:32
                        ← linha vazia
   4 zonas ativas
    Prox: 18:00
```

Com rega em curso:

```
     14:32

  Z2 Horta:
[######-------]  47%
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

Navega com o encoder. Clica para entrar. `<- Voltar` regressa sempre ao nível anterior; sem ação durante 30 segundos o sistema volta automaticamente ao ecrã de idle.

---

### Rega Manual

| Opção | Comportamento |
|---|---|
| **Rega Geral** | Executa um ciclo completo seguindo o modo automático atual — zonas ativas, pela duração configurada para cada uma |
| **Personalizado** | Seleciona frequência (cada X dias), número de ciclos (1-4) e horários livres; inclui proteção contra sobreposição de ciclos |

---

### Programação

| Opção | Comportamento |
|---|---|
| **Ver Horários** | Mostra o modo ativo, hora de disparo e zonas incluídas |
| **Alterar Modo** | Escolhe entre Intenso, Médio, Fraco, Desativado ou Personalizado |
| **Config. Personaliz.** | (Novo) Define frequência (1-14 dias) e até 4 horários de rega diários |
| **Configurar Zonas** | Ativa/desativa cada zona e define a duração individual (0 = desativar) |
| **Suspender Rega** | Pausa a rega automática por N dias sem alterar os horários |

#### Modos automáticos

| Modo | Horário | Indicado para |
|---|---|---|
| **Intenso** | 07:00 + 13:00 + 19:00, todos os dias | Verão, calor intenso (3 ciclos/dia) |
| **Médio** | 08:00 + 20:00, todos os dias | Primavera / Outono (2 ciclos/dia) |
| **Fraco** | 08:00, todos os dias | Manutenção ligeira (1 ciclo/dia) |
| **Desativado** | — | Inverno / ausência prolongada |
| **Personalizado** | Definido pelo utilizador | Total flexibilidade (1-14 dias, 1-4 ciclos/dia) |

---

### Definições

...

1. **Modo Personalizado** — Horários e frequência totalmente configuráveis pelo utilizador ✅
2. **Proteção contra sobreposição** — Sistema valida se os ciclos configurados não se atropelam com base na duração das zonas ✅
3. **Persistência NVS** — Todas as configurações personalizadas são guardadas na memória flash ✅

| **Histórico** | Registo dos últimos ciclos de rega com data/hora e duração |
| **Acertar Hora** | Editor de hora via encoder: primeiro as horas, depois os minutos, clica para guardar no RTC (seleção circular) |
| **Tempo Ecrã** | Define quando o ecrã adormece após inatividade: 30s / 1min / **2min** / 5min / Sempre |
| **Versão Firmware** | Número de versão, data de build, modelo do microcontrolador |
| **Reset de Fábrica** | Repõe todas as configurações para os valores padrão (pede confirmação) |

#### Gestão de Ecrã e Longevidade

Após o período de inatividade configurado, o sistema executa um desligamento faseado do display para maximizar a sua vida útil:
1. **Backlight**: A luz de fundo apaga-se primeiro.
2. **Cristais Líquidos**: 60 segundos depois, os caracteres (píxeis) são também desligados, colocando o display em repouso total.

Qualquer interação com o encoder volta a ligar instantaneamente ambos os componentes — esse primeiro toque é ignorado para segurança.

---

### Navegação e Edição

Todos os seletores do sistema (horas, minutos, durações, intervalos e menus) possuem **navegação circular**. Ao atingir o valor máximo, o seletor volta automaticamente ao início, facilitando ajustes rápidos.

---

### Acertar hora

```
~~~ ACERTAR HORA ~~~~
    [14] :  32
      ^ horas
  click -> minutos
```

Roda para ajustar as horas → clica → roda para ajustar os minutos → clica para guardar. A hora é escrita no módulo RTC DS3231 e mantém-se mesmo sem energia no ESP32, desde que a bateria do módulo esteja carregada.

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
| RTC SDA | 21 (mesmo barramento I2C) |
| RTC SCL | 22 (mesmo barramento I2C) |
| Relé Zona 1 | 26 |
| Relé Zona 2 | 27 |
| Relé Zona 3 | 14 |
| Relé Zona 4 | 12 |

> Os relés são ativados em LOW (padrão dos módulos de relé com optoacoplador). Se o teu módulo for ativo em HIGH, altera `RELAY_ON` / `RELAY_OFF` em `config.h`.

---

## Estado atual do projeto

| Funcionalidade | Estado |
|---|---|
| Interface LCD + encoder | ✅ Completo |
| Modos automáticos (Intenso / Médio / Fraco / Desativado) | ✅ Completo |
| Rega manual geral e personalizada | ✅ Completo |
| Testes de zona | ✅ Completo |
| Configuração de zonas (duração, ativar/desativar) | ✅ Completo |
| Controlo de relés GPIO | ✅ Completo |
| Relógio RTC DS3231 com acerto de hora | ✅ Completo |
| Suspensão de rega | ✅ Completo |
| Gestão de ecrã (sleep/wake) | ✅ Completo |
| Persistência de configurações (NVS) | ✅ Completo |
| Agendamento automático por hora | ✅ Completo |
| Histórico em ficheiro CSV (LittleFS) | ✅ Completo |
| Modo Personalizado (horário livre) | ✅ Completo |

---

## Implementação

### Compilar e simular

**Pré-requisitos:**

| Ferramenta | Notas |
|---|---|
| VSCode | Qualquer versão |
| Extensão PlatformIO IDE | `platformio.ide` |
| Extensão Wokwi for VSCode | `wokwi.wokwi-vscode` + conta gratuita em wokwi.com |

**Compilar:**
```
Ctrl + Alt + B   (ou botão ✓ na barra inferior do VSCode)
```
Via terminal:
```bash
pio run
```

**Simular no Wokwi** (requer firmware compilado):
```
Abre diagram.json → F1 → "Wokwi: Start Simulator"
```
Na primeira utilização: `F1 → Wokwi: Request License`

**Upload para hardware real:**
```
Ctrl + Alt + U   (ou botão → na barra inferior)
```

**Monitor série** (115200 baud):
```
Ctrl + Alt + S   (ou botão de tomada na barra inferior)
```

---

### Estrutura de ficheiros

```
rega-esp32/
├── src/
│   ├── config.h                 # pinos, constantes, versão de firmware
│   ├── AppState.h / .cpp        # estado global único (gState)
│   ├── Display.h / .cpp         # wrapper LCD com shadow buffer
│   ├── Encoder.h / .cpp         # driver ISR do encoder rotativo
│   ├── History.h / .cpp         # gestão de ficheiro CSV em LittleFS
│   ├── RTClock.h / .cpp         # módulo DS3231
│   ├── Scheduler.h / .cpp       # agendamento automático
│   ├── Storage.h / .cpp         # persistência NVS (Preferences)
│   ├── WateringController.h/.cpp# fila de zonas + controlo de relés
│   ├── UI.h / .cpp              # máquina de estados da interface
│   └── main.cpp                 # setup() e loop()
├── diagram.json                 # circuito para o simulador Wokwi
├── platformio.ini               # configuração PlatformIO + dependências
└── wokwi.toml                   # apontador para o firmware compilado
```

### Dependências entre módulos

```
main.cpp
  ├── initAppState()          inicializa gState
  ├── Storage                 lê/escreve NVS → gState
  ├── History                 lê/escreve LittleFS (CSV)
  ├── Display                 LCD I2C
  ├── Encoder                 ISR + debounce
  ├── RTClock                 DS3231 → escreve gState.now
  ├── WateringController      fila de zonas → controla relés → escreve gState.watering
  ├── Scheduler               compara gState.now com horários → dispara WateringController
  └── UI                      lê Encoder, escreve Display, lê/escreve gState
```

`gState` é o único estado partilhado em RAM. Todos os módulos leem e escrevem nele diretamente — não há passagem de mensagens. Este modelo funciona bem num sistema single-threaded (loop Arduino) e simplifica a adição de novos módulos.

---

### Decisões de design

**Shadow buffer no Display**
`lcd.clear()` causa um flash visível. O `Display` mantém uma cópia em memória do conteúdo atual do LCD e só envia para o hardware as linhas que efetivamente mudaram. O resultado é atualização sem flickering.

**ISR no encoder**
A rotação é capturada por interrupção hardware (`IRAM_ATTR`). O driver processa todos os passos acumulados de forma sequencial, garantindo que mesmo rotações muito rápidas sejam registadas sem perda de passos, resultando numa interface fluida.

**Aritmética de calendário no Scheduler**
O agendador utiliza aritmética de tempo Unix (através da `RTClib`) para calcular as próximas regas. Isto garante que transições de mês, anos bissextos e modos de dias alternados funcionem com precisão absoluta, sem erros de lógica de calendário manual.

**Otimização de Memória Flash (NVS)**
O sistema monitoriza o estado atual das configurações e apenas realiza escritas físicas na memória flash quando deteta uma alteração real nos valores. Esta técnica de "escrita diferencial" aumenta drasticamente a vida útil do chip ESP32.

**Máquina de estados da UI**
A UI é uma FSM com seis estados (`IDLE`, `MENU`, `INFO`, `CONFIRM`, `DONE`, `DUR_PICK`, `TIME_EDIT`). Transições são acionadas por rotação ou clique. O sistema de ações é codificado em strings compactas nos itens de menu (ex: `"confirm:Iniciar rega|...|main|general"`) — permite declarar menus como dados estáticos sem callbacks.

**Sem Wi-Fi por design**
O RTC DS3231 fornece tempo real com precisão de ±2 ppm (≈1 minuto por ano) sem qualquer dependência de rede. Não há superfície de ataque TCP/IP, não há credenciais em flash, não há falhas por falta de internet.

**Relés em active-LOW**
Os módulos de relé com optoacoplador mais comuns ativam com LOW. O código usa `RELAY_ON`/`RELAY_OFF` definidos em `config.h` — para módulos active-HIGH basta inverter os dois defines sem tocar no resto do código.

---

### Bibliotecas

| Biblioteca | Uso |
|---|---|
| `marcoschwartz/LiquidCrystal_I2C` | Driver LCD I2C |
| `adafruit/RTClib` | Driver DS3231 |

Instaladas automaticamente pelo PlatformIO na primeira compilação.

---

## Logs e Diagnóstico

O sistema envia informações de estado via Serial (115200 baud) utilizando prefixos padronizados para facilitar a monitorização:

| Categoria | Descrição |
|---|---|
| **[SYS]** | Inicialização e estado global do sistema. |
| **[RTC]** | Estado do relógio em tempo real (DS3231). |
| **[NVS]** | Persistência de dados na memória Flash. |
| **[FS]** | Operações de ficheiros no LittleFS (Histórico). |
| **[SCHED]** | Lógica de agendamento e suspensão. |
| **[WATER]** | Controlo das zonas de rega e relés. |
| **[UI]** | Interações do utilizador e navegação de menus. |

---

### Próximas iterações

1. **Modo Personalizado** — implementar configuração de horários livres pelo utilizador
2. **Sensores de Humidade** — bloquear rega se o solo estiver húmido
3. **Deteção de Corrente** — monitorizar se a bomba/válvula está efetivamente a consumir energia
4. **Exportação de Dados** — interface simples para descarregar o histórico CSV via Serial
