# Sistema de Rega — arquitetura e funcionamento

Este diretório contém o firmware do controlador ESP32. A execução é cooperativa: o `loop()` chama os serviços regularmente e a rega é temporizada sem esperar bloqueado pela duração da válvula. Algumas operações curtas de I²C, LittleFS, NVS e Serial são síncronas; exportação de histórico e rotação do ficheiro são processadas incrementalmente.

## Arquitetura

- `main.cpp`, `SystemBoot.h / .cpp`: ponto de entrada ESP32 (exige ESP-IDF 5 / Core 3.x, configura WDT e buffer UART RX de 1024 B) e orquestração partilhada `bootSystem()` / `tickSystem()`, arranque seguro dos relés (`initRelayPinsSafe`), recuperação do barramento I²C (`clearI2CBus`, `recoverI2CWith`) e avaliação de suspensão no boot (`evaluateBootSuspension`).
- `AppState.h / .cpp`: estruturas da aplicação e estado central `gState`.
- `WateringController.h / .cpp`: fila de zonas, temporização, comutação dos relés, recusa de preempção sobre ciclos ativos e estado de recuperação persistente.
- `Scheduler.h / .cpp`: cálculo limitado do próximo horário, tratamento de frequência personalizada, disparo de ciclos automáticos, recuperação adiada após colisões com `TEST`/`CUSTOM` e indicador de horário omitido (`hasSkippedSlot()`).
- `RTClock.h / .cpp`: DS3231 no hardware, conversão local/UTC, DST europeu e avanço por software entre leituras do RTC.
- `Storage.h / .cpp`: configuração NVS v5 redundante em dois slots alternados (`cfg` / `cfg_b`) com contador de sequência, integridade por CRC-16/CCITT em `AppConfigBlob` (72 carateres hex, retrocompatível com `cfg` v5 legado e com hook de migração `N-1`), diagnóstico de falha (`ConfigLoadResult` / `configLoadFailed()`), `save()` puro sem mutar `gState`, cache atómica de histórico (`hblob`), estado de recuperação (`recv`) e marcadores `sched_tr`.
- `History.h / .cpp`: CSV em LittleFS, cache de entradas recentes e dimensão esperada do ficheiro na NVS (`hblob`), persistência de adiamentos durante rotação em `/hist_pend.txt` e exportação série.
- `Display.h / .cpp`, `Encoder.h / .cpp`, `UI.h / .cpp`: LCD com shadow buffer, encoder via ISR/debounce e encaminhamento dos ecrãs.
- `Terminal.h / .cpp`: CLI Serial a 115200 baud, com leitura incremental de comandos (incluindo `stop`, `reboot`, `i2c_scan`, gestão de configurações e histórico).
- `log.h`, `i18n.h`, `config.h`: logs, idiomas e parâmetros/hardware.
- `ui/`: ecrãs individuais e construção de menus. O `MenuBuilder` gera ações textuais que o gestor UI encaminha (incluindo a injeção do botão "Parar Rega" quando há rega em curso).

A separação por módulos é acompanhada por singletons e estado global partilhado (`gState`), uma escolha simples para este firmware de uma só aplicação, suportada para testes unitários isolados através da suite nativa com mocks em `test/` (ver [test/README.md](../test/README.md)). O encoder é o único serviço com entrada assíncrona por interrupção; o delta é protegido por secção crítica.

## Inicialização (`setup()` → `bootSystem()`)

A sequência crítica de arranque está centralizada em `bootSystem()` (`SystemBoot.cpp`) e é reutilizada diretamente por `main.cpp`, `test_system` e `test_soak`:

1. `main.cpp` pré-carrega os níveis OFF nos registos GPIO (`initRelayPinsSafe`) antes de configurar os pinos dos relés como `OUTPUT` (evitando pulsos espúrios sob ESP32 Arduino Core 3.x), expande o buffer UART RX para 1024 B (`Serial.setRxBufferSize(1024)`), abre Serial e inicializa o watchdog ESP-IDF 5 com `WDT_TIMEOUT_S` (8 s por defeito).
2. `bootSystem()` inicializa o estado em RAM (`initAppState`), abre NVS e carrega configurações dos slots redundantes `cfg`/`cfg_b` (se ambos falharem, regista `LOG_E` e mantém `storage.configLoadFailed()` ativo).
3. Monta LittleFS e carrega a cache do histórico (`history.begin()`).
4. Limpa e inicializa o barramento I²C (`clearI2CBus`, `Wire.begin`, `configureI2CTimeout`, com scan opcional em `LVL_DEBUG`), inicia o LCD e o encoder.
5. Inicializa o RTC, instala a recuperação I²C, avalia suspensão no arranque (`evaluateBootSuspension`), carrega os marcadores DST persistidos (`scheduler.loadTriggers()`) antes de tentar recuperar um ciclo interrompido (`wateringCtrl.begin()`).
6. Inicia o scheduler (`scheduler.begin()`, após a recuperação para detetar `wateringCtrl.isActive()`), a UI (apresentando banner persistente de erro de configuração antes do assistente inicial se `configLoadFailed()`) e o terminal.

`History::begin()` tenta montar o sistema de ficheiros com `LittleFS.begin(false)` (com retentativa antes de desistir). Se a montagem falhar, consulta `storage.hasHistoryData()` (`hblob` na NVS); se já existirem registos conhecidos, a formatação destrutiva é prevenida para salvaguardar o histórico. A formatação (`begin(true)`) só é executada quando não há dados históricos conhecidos em NVS. No arranque seguinte, valida também o tamanho do ficheiro contra o registado em `hblob` e reprocessa eventuais entradas pendentes de `/hist_pend.txt`. A configuração NVS permanece separada.

## Ciclo principal (`loop()` → `tickSystem()`)

A ordem efetiva executada por `tickSystem(ui)` (chamado de `main.cpp::loop()` e dos testes):

1. Alimentar o watchdog.
2. `rtclock.update()`.
3. `scheduler.update()`.
4. `ui.update()`.
5. `wateringCtrl.update()`.
6. `history.update()`.
7. `terminal.update()`, `storage.update()` (fora do assistente inicial, para retentativa de escritas NVS) e pausa de 1 ms.

O RTC de hardware é consultado com intervalo nominal de 30 s; entre leituras, o relógio por software avança com base em `millis()`. O scheduler é avaliado com o tempo atualizado, e o controlador de rega implementa dead-time e espera entre zonas sem `delay()` durante a rega.

## Decisões e limites operacionais

- **Blackout durante uma zona:** a NVS não é atualizada a cada segundo para limitar desgaste; em vez disso, o progresso da zona ativa é guardado a cada `RECOVERY_CHECKPOINT_MIN` (2 min), persistindo os minutos restantes e os minutos já executados. O reinício valida hora, idade máxima (`RECOVERY_MAX_AGE_S` = 2 h), distância para o próximo ciclo e o contador persistente `RecoveryState::retry_count` (aborta após `MAX_RECOVERY_RETRIES` = 3 tentativas falhadas na mesma zona; repõe a `0` apenas ao concluir a zona) para impedir loops de brownout periódicos. Quando uma retoma válida é abortada (por limite de tentativas, expiração ou suspensão), os minutos já acumulados em `zone_dur_min` são preservados no histórico antes de limpar a NVS. É um compromisso deliberado entre desgaste da flash e precisão da retoma.

- **DST, catch-up e reboot:** a proteção contra duplicação da hora repetida (janela 2 h no recuo DST) persiste os últimos timestamps de disparo na NVS (`sched_tr`) protegidos por CRC-16 para manter a proteção após reinício; apenas alterações de modo ou acertos explícitos do relógio (`rtclock.manualRevision()`) limpam esses marcadores — sincronizações automáticas por hotplug ou drift do RTC não apagam `sched_tr`. No arranque ou após salto de avanço DST, o scheduler aplica ainda uma janela de recuperação (`SCHED_CATCHUP_WINDOW_S` = 30 min) para executar um horário perdido, sendo inibida em acertos manuais (`rtclock.manualRevision()`).
- **RTC inválido/ausente:** o relógio por software mantém a interface utilizável, mas sem uma hora válida não autoriza agendamentos automáticos nem retoma validada após blackout. No assistente de primeiro arranque, o utilizador pode confirmar que pretende continuar sem RTC; falhas de escrita no DS3231 a partir do LCD (`allowSwFallback = false`) não ativam o relógio em RAM. Apenas `set_time` por Serial pode acertar o relógio por software e permitir automatismos até ao próximo reboot; para operação automática persistente, é necessário um RTC válido.

- **Importação de configuração e diagnósticos:** apenas formato NVS v5 (72 carateres hex) protegido por CRC-16/CCITT e validação estrita de limites em memória antes de alterar `gState`. Uma importação inválida, v4 antiga ou durante rega ativa é integralmente rejeitada sem efeitos parciais; `i2c_scan` também é bloqueado durante rega ativa para evitar bloqueios do barramento durante controlo de válvulas.
- **Hardware:** o estado OFF por software não deteta relés ou válvulas mecanicamente presos. A camada de hardware da instalação continua responsável por isolamento, corte de água e proteção contra falhas físicas.

## Interface

A UI do LCD 20×4 usa classes de ecrã com ciclo de vida (`onEnter`, `render`, `update`, `handleRotation`, `handleClick`). `ScreenCommon` fornece informação, confirmação e conclusão; `ScreenEditors` contém editores numéricos/data/hora; `ScreenSetup` gere o assistente inicial. A configuração sem RTC exige confirmação explícita antes de avançar para seleção de modo e zonas.

Para instruções de utilização, comandos Serial e opções de configuração, consulte o [README principal](../README.md).
