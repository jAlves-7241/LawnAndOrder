# Sistema de Rega - ESP32 (Código Fonte)

Este diretório contém a implementação do controlador de rega autónomo para o ESP32.

---

## Estrutura de Ficheiros de Código

- `config.h`: Definições globais de pinagem (relés, LCD, RTC, encoder), constantes temporais, limites físicos e de versão do firmware.
- `log.h`: Sistema otimizado de logs com 4 níveis (`ERRO`, `AVISO`, `INFO`, `DEBUG`), resolvido e filtrado em tempo de compilação (zero overhead de código quando desligado).
- `AppState.h / .cpp`: Estado central global da aplicação (`gState`), dados das zonas, configurações de temporização e cache do histórico.
- `Display.h / .cpp`: Driver LCD otimizado com *shadow buffering* (só escreve caracteres alterados física e visualmente no ecrã para eliminar qualquer flicker).
- `Encoder.h / .cpp`: Driver para encoder rotativo baseado em interrupções de hardware com debounce inteligente.
- `History.h / .cpp`: Sistema de logs de rega em formato CSV gravado no LittleFS com rotação de ficheiro não bloqueante e exportação série assíncrona.
- `RTClock.h / .cpp`: Interface com o relógio de hardware DS3231 com fuso horário e compensação automática de horário de verão (regras EU).
- `Scheduler.h / .cpp`: Motor de agendamento automático $O(1)$ baseado em aritmética modular.
- `Storage.h / .cpp`: Persistência de definições de utilizador e cache binária de histórico na NVS Flash do ESP32.
- `Terminal.h / .cpp`: Terminal de comando interativo por comunicação Serial CLI (115200 baud) totalmente não bloqueante.
- `WateringController.h / .cpp`: Controlo de baixo nível dos relés GPIO das eletroválvulas, gestão da fila e temporização ativa de rega.
- `UI.h / .cpp`: Gestor de interface centralizado, responsável pelo tratamento de inatividade/backlight e encaminhamento de ecrãs.
- `main.cpp`: Ponto de entrada do firmware, executando a inicialização segura e o ciclo de execução principal.

---

## Módulo de Interface do Utilizador (`src/ui/`)

A interface física no LCD 2004 é implementada através de um padrão polimórfico orientado a objetos, separando cada ecrã numa classe especializada:

- `ui/UIScreen.h`: Classe abstrata base. Define o ciclo de vida de um ecrã (`onEnter`, `onExit`, `render`, `update`, `handleRotation` e `handleClick`).
- `ui/UITypes.h`: Define as identidades de cada ecrã (`MenuID`), os contextos das caixas de entrada de dados (`DurContext` e `TimeEditContext`) e os passos do assistente de configuração (`SetupStep`).
- `ui/MenuBuilder.h / .cpp`: Constrói dinamicamente os itens de menu baseados nas variáveis de estado de RAM e nos buffers estáticos.
- `ui/ScreenCommon.h / .cpp`:
  - `ScreenInfo`: Apresenta ecrãs informativos genéricos com botão de regresso automático.
  - `ScreenConfirm`: Pede confirmação explícita de Sim/Voltar para ações perigosas ou arranques manuais.
  - `ScreenDone`: Confirma a execução imediata de uma tarefa com mensagem de sucesso.
- `ui/ScreenEditors.h / .cpp`:
  - `ScreenDurPick`: Seletor interativo para escolher valores numéricos (duração de zona 0–20 min, dias de suspensão, frequência).
  - `ScreenDateEdit`: Introdução sequencial do dia/mês/ano com validação de limites temporais.
  - `ScreenTimeEdit`: Seletor interativo para horas/minutos dos ciclos de rega automática.

- `ui/ScreenIdle.h / .cpp`: Ecrã principal passivo (idle). Mostra a hora atual e as informações de agendamento automático, progresso ativo da rega por zona ou estado de suspensão.
- `ui/ScreenMenu.h / .cpp`: Renders e controla menus scrolláveis gerados a partir do `MenuBuilder`.
- `ui/ScreenSetup.h / .cpp`: Controla o assistente interativo de primeiro arranque (**Setup Wizard**).

---

## Fluxo Detalhado do Sistema

### 1. Inicialização (`setup()`)
```
[Arranque do ESP32]
  │
  ├── 1. SAFETY FIRST: Força todos os relés OFF antes de iniciar periféricos.
  ├── 2. Watchdog: Configura o Task Watchdog do ESP32 a 5 segundos no loop principal.
  ├── 3. AppState: Carrega definições de fábrica para a RAM.
  ├── 4. NVS (Storage): Inicializa e carrega definições persistentes na RAM.
  ├── 5. LittleFS (History): Monta o sistema de ficheiros e popula a cache binária.
  ├── 6. UI Hardware: Inicializa o Display (LCD) e Encoder (GPIO ISRs).
  ├── 7. RTClock: Liga o barramento I2C, configura fuso automático e recoverI2C como callback.
  ├── 8. Safety Clean: Remove suspensão expirada e desliga relés de segurança no WateringController.
  ├── 9. Scheduler: Calcula e agenda o ciclo seguinte cronológico.
  ├── 10. UI & Terminal: Lança o terminal de comandos serial e o Setup Wizard (se aplicável).
  ▼
[Pronto para o Loop]
```

### 2. Ciclo de Execução Principal (`loop()`)
O ciclo principal corre continuamente sem bloqueios. Cada milissegundo de inatividade é repartido entre os vários controladores:
1. **`esp_task_wdt_reset()`**: Reseta e alimenta o watchdog físico.
2. **`rtclock.update()`**: Lê o RTC DS3231 uma vez por segundo para sincronizar o relógio do sistema.
3. **`scheduler.update()`**: Compara a hora e o dia com a tabela de agendamento modular para disparar ciclos automáticos.
4. **`ui.update()`**: Monitoriza delta de rotação do encoder, cliques e gere o backlight/screen off automático.
5. **`wateringCtrl.update()`**: Trata da comutação e do tempo ativo de rega física de cada zona.
6. **`history.update()`**: Efetua operações de ficheiro assíncronas em LittleFS.
7. **`terminal.update()`**: Lê e processa comandos remotos inseridos na consola CLI.

---

Para documentação completa sobre o funcionamento e utilização, consulte o [README.md principal](../README.md).
