# Sistema de Rega — arquitetura e funcionamento

Este diretório contém o firmware do controlador ESP32. A execução é cooperativa: o `loop()` chama os serviços regularmente e a rega é temporizada sem esperar bloqueado pela duração da válvula. Algumas operações curtas de I²C, LittleFS, NVS e Serial são síncronas; exportação de histórico e rotação do ficheiro são processadas incrementalmente.

## Arquitetura

- `main.cpp`: inicialização e ciclo principal; estabelece os níveis OFF dos relés no início do arranque.
- `AppState.h / .cpp`: estruturas da aplicação e estado central `gState`.
- `WateringController.h / .cpp`: fila de zonas, temporização, comutação dos relés e estado de recuperação persistente.
- `Scheduler.h / .cpp`: cálculo limitado do próximo horário, tratamento de frequência personalizada e disparo de ciclos automáticos.
- `RTClock.h / .cpp`: DS3231 no hardware, conversão local/UTC, DST europeu e avanço por software entre leituras do RTC.
- `Storage.h / .cpp`: configuração e estado de recuperação em NVS; exportação/importação hexadecimal validada.
- `History.h / .cpp`: CSV em LittleFS, cache de entradas recentes em NVS e exportação série.
- `Display.h / .cpp`, `Encoder.h / .cpp`, `UI.h / .cpp`: LCD com shadow buffer, encoder via ISR/debounce e encaminhamento dos ecrãs.
- `Terminal.h / .cpp`: CLI Serial a 115200 baud, com leitura incremental de comandos.
- `log.h`, `i18n.h`, `config.h`: logs, idiomas e parâmetros/hardware.
- `ui/`: ecrãs individuais e construção de menus. O `MenuBuilder` gera ações textuais que o gestor UI encaminha.

A separação por módulos é acompanhada por singletons e estado global partilhado (`gState`), uma escolha simples para este firmware de uma só aplicação, mas que torna testes unitários isolados mais difíceis. O encoder é o único serviço com entrada assíncrona por interrupção; o delta é protegido por secção crítica.

## Inicialização (`setup()`)

1. Comanda os relés para OFF e configura os GPIOs.
2. Inicializa o watchdog com `WDT_TIMEOUT_S` (8 s por defeito) e o estado em RAM.
3. Abre NVS e carrega configurações validadas.
4. Monta LittleFS e carrega a cache do histórico.
5. Recupera o barramento I²C, inicializa-o, faz um scan, inicia o LCD e o encoder.
6. Inicializa o RTC, instala a recuperação I²C, verifica suspensão e tenta recuperar um ciclo interrompido.
7. Calcula o horário seguinte, inicia UI e terminal.

`History::begin()` usa `LittleFS.begin(true)`: se a montagem falhar, a biblioteca pode formatar o filesystem. Isto permite recuperar a disponibilidade do controlador, mas o histórico local pode perder-se. A configuração NVS é separada.

## Ciclo principal (`loop()`)

A ordem efetiva em `main.cpp` é:

1. Alimentar o watchdog.
2. `rtclock.update()`.
3. `scheduler.update()`.
4. `ui.update()`.
5. `wateringCtrl.update()`.
6. `history.update()`.
7. `terminal.update()` e pausa de 1 ms.

O RTC de hardware é consultado com intervalo nominal de 30 s; entre leituras, o relógio por software avança com base em `millis()`. O scheduler é avaliado com o tempo atualizado, e o controlador de rega implementa dead-time e espera entre zonas sem `delay()` durante a rega.

## Decisões e limites operacionais

- **Blackout durante uma zona:** a NVS não é atualizada a cada segundo para limitar desgaste. A posição e a duração configurada ficam guardadas; se a energia falhar durante uma zona, essa zona pode ser repetida pela duração completa após reinício. A posição é persistida quando uma zona termina, por isso uma falha durante essa gravação também pode repetir a zona acabada de concluir. O reinício valida hora, duração máxima e distância para o próximo ciclo antes de retomar. É um compromisso deliberado entre desgaste da flash e precisão da retoma.

- **DST e reboot:** a proteção contra duplicação da hora repetida é mantida em RAM para evitar escrita persistente adicional. Um reboot na janela entre as duas ocorrências pode permitir uma segunda ativação. A janela é rara; se o custo dessa duplicação aumentar, a alternativa é persistir um marcador de último disparo.
- **RTC inválido/ausente:** o relógio por software mantém a interface utilizável, mas sem uma hora válida não autoriza agendamentos automáticos nem retoma validada após blackout. No assistente de primeiro arranque, o utilizador pode confirmar que pretende continuar sem RTC. `set_time` por Serial pode acertar o relógio por software e permitir automatismos até ao próximo reboot; para operação automática persistente, é necessário um RTC válido.

- **Importação de configuração:** o blob é verificado antes da aplicação. Uma importação inválida não deve deixar alterações parciais no estado em RAM.
- **Hardware:** o estado OFF por software não deteta relés ou válvulas mecanicamente presos. A camada de hardware da instalação continua responsável por isolamento, corte de água e proteção contra falhas físicas.

## Interface

A UI do LCD 20×4 usa classes de ecrã com ciclo de vida (`onEnter`, `render`, `update`, `handleRotation`, `handleClick`). `ScreenCommon` fornece informação, confirmação e conclusão; `ScreenEditors` contém editores numéricos/data/hora; `ScreenSetup` gere o assistente inicial. A configuração sem RTC exige confirmação explícita antes de avançar para seleção de modo e zonas.

Para instruções de utilização, comandos Serial e opções de configuração, consulte o [README principal](../README.md).
