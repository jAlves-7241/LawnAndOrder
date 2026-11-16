# Sistema de Rega — arquitetura e funcionamento

Este diretório contém o firmware do controlador ESP32. A execução é cooperativa: o `loop()` chama os serviços regularmente e a rega é temporizada sem esperar bloqueado pela duração da válvula. Algumas operações curtas de I²C, LittleFS, NVS e Serial são síncronas; exportação de histórico e rotação do ficheiro são processadas incrementalmente.

## Arquitetura

- `main.cpp`: inicialização e ciclo principal; estabelece os níveis OFF dos relés no início do arranque.
- `AppState.h / .cpp`: estruturas da aplicação e estado central `gState`.
- `WateringController.h / .cpp`: fila de zonas, temporização, comutação dos relés e estado de recuperação persistente.
- `Scheduler.h / .cpp`: cálculo limitado do próximo horário, tratamento de frequência personalizada e disparo de ciclos automáticos.
- `RTClock.h / .cpp`: DS3231 no hardware, conversão local/UTC, DST europeu e avanço por software entre leituras do RTC.
- `Storage.h / .cpp`: configuração NVS v5 com integridade por CRC-16/CCITT em `AppConfigBlob` (72 carateres hex, sem retrocompatibilidade v4), cache atómica de histórico (`hblob`) e marcadores `sched_tr`.
- `History.h / .cpp`: CSV em LittleFS, cache de entradas recentes e dimensão esperada do ficheiro na NVS (`hblob`), persistência de adiamentos durante rotação em `/hist_pend.txt` e exportação série.
- `Display.h / .cpp`, `Encoder.h / .cpp`, `UI.h / .cpp`: LCD com shadow buffer, encoder via ISR/debounce e encaminhamento dos ecrãs.
- `Terminal.h / .cpp`: CLI Serial a 115200 baud, com leitura incremental de comandos (incluindo `stop`, `reboot`, `i2c_scan`, gestão de configurações e histórico).
- `log.h`, `i18n.h`, `config.h`: logs, idiomas e parâmetros/hardware.
- `ui/`: ecrãs individuais e construção de menus. O `MenuBuilder` gera ações textuais que o gestor UI encaminha (incluindo a injeção do botão "Parar Rega" quando há rega em curso).

A separação por módulos é acompanhada por singletons e estado global partilhado (`gState`), uma escolha simples para este firmware de uma só aplicação, suportada para testes unitários isolados através da suite nativa com mocks em `test/` (ver [test/README.md](../test/README.md)). O encoder é o único serviço com entrada assíncrona por interrupção; o delta é protegido por secção crítica.

## Inicialização (`setup()`)

1. Comanda os relés para OFF e configura os GPIOs.
2. Inicializa o watchdog com `WDT_TIMEOUT_S` (8 s por defeito) e o estado em RAM.
3. Abre NVS e carrega configurações validadas.
4. Monta LittleFS e carrega a cache do histórico.
5. Recupera o barramento I²C, inicializa-o, faz um scan, inicia o LCD e o encoder.
6. Inicializa o RTC, instala a recuperação I²C, verifica suspensão e tenta recuperar um ciclo interrompido.
7. Calcula o horário seguinte, inicia UI e terminal.

`History::begin()` tenta montar o sistema de ficheiros com `LittleFS.begin(false)` (com retentativa antes de desistir). Se a montagem falhar, consulta `storage.hasHistoryData()` (`hblob` na NVS); se já existirem registos conhecidos, a formatação destrutiva é prevenida para salvaguardar o histórico. A formatação (`begin(true)`) só é executada quando não há dados históricos conhecidos em NVS. No arranque seguinte, valida também o tamanho do ficheiro contra o registado em `hblob` e reprocessa eventuais entradas pendentes de `/hist_pend.txt`. A configuração NVS permanece separada.

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

- **Blackout durante uma zona:** a NVS não é atualizada a cada segundo para limitar desgaste; em vez disso, o progresso da zona ativa é guardado a cada `RECOVERY_CHECKPOINT_MIN` (2 min), persistindo os minutos restantes e os minutos já executados. O reinício valida hora, idade máxima (`RECOVERY_MAX_AGE_S` = 2 h), distância para o próximo ciclo e o contador persistente `RecoveryState::retry_count` (aborta após `MAX_RECOVERY_RETRIES` = 3 tentativas falhadas na mesma zona; repõe a `0` apenas ao concluir a zona) para impedir loops de brownout periódicos. É um compromisso deliberado entre desgaste da flash e precisão da retoma.

- **DST e reboot:** a proteção contra duplicação da hora repetida (janela 2 h no recuo DST) persiste os últimos timestamps de disparo na NVS (`sched_tr`) protegidos por CRC-16 para manter a proteção após reinício; alterações manuais de modo ou acertos explícitos do relógio (`rtclock.revision()`) limpam esses marcadores.
- **RTC inválido/ausente:** o relógio por software mantém a interface utilizável, mas sem uma hora válida não autoriza agendamentos automáticos nem retoma validada após blackout. No assistente de primeiro arranque, o utilizador pode confirmar que pretende continuar sem RTC. `set_time` por Serial pode acertar o relógio por software e permitir automatismos até ao próximo reboot; para operação automática persistente, é necessário um RTC válido.

- **Importação de configuração:** apenas formato NVS v5 (72 carateres hex) protegido por CRC-16/CCITT e validação estrita de limites em memória antes de alterar `gState`. Uma importação inválida, v4 antiga ou durante rega ativa é integralmente rejeitada sem efeitos parciais.
- **Hardware:** o estado OFF por software não deteta relés ou válvulas mecanicamente presos. A camada de hardware da instalação continua responsável por isolamento, corte de água e proteção contra falhas físicas.

## Interface

A UI do LCD 20×4 usa classes de ecrã com ciclo de vida (`onEnter`, `render`, `update`, `handleRotation`, `handleClick`). `ScreenCommon` fornece informação, confirmação e conclusão; `ScreenEditors` contém editores numéricos/data/hora; `ScreenSetup` gere o assistente inicial. A configuração sem RTC exige confirmação explícita antes de avançar para seleção de modo e zonas.

Para instruções de utilização, comandos Serial e opções de configuração, consulte o [README principal](../README.md).
