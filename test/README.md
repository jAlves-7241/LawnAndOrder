# Suite de Testes Unitários — Lawn & Order

Este diretório contém a suite de testes unitários automatizados do firmware **Lawn & Order**, executada nativamente no computador de desenvolvimento (Windows, Linux, macOS) através do ambiente `[env:native]` do **PlatformIO** e do framework **Unity**.

A abordagem de testes é **100% não-invasiva**: o código-fonte de produção em `src/` não foi alterado nem poluído com diretivas de teste ou frameworks pesados. Todos os drivers de hardware e dependências do Arduino/ESP32 são substituídos em tempo de compilação por shims ultraleves em `test/mocks/`.

---

## 🚀 Como Executar os Testes

### Executar Toda a Suite de Testes (43 Testes)

```bash
pio test -e native
```

O tempo de execução de toda a suite é de aproximadamente **13 segundos** (~1.9s por módulo).

### Executar Apenas um Módulo Específico

Utilize o argumento `-f` (filtro) para selecionar uma suite:

```bash
# Testar apenas o Scheduler (agendamento e horários)
pio test -e native -f test_scheduler

# Testar apenas o Terminal de Comandos (CLI Serial)
pio test -e native -f test_terminal

# Testar apenas o Controlador de Rega (relés, dead-time, filas)
pio test -e native -f test_watering

# Testar apenas o RTClock (regras de DST europeu e fusos)
pio test -e native -f test_rtclock

# Testar apenas a Persistência NVS (Storage e exportação hexadecimal)
pio test -e native -f test_storage

# Testar apenas o Histórico CSV e LittleFS
pio test -e native -f test_history

# Testar apenas as estruturas e invariantes do AppState
pio test -e native -f test_appstate
```

### Modo Detalhado (Verbose)

Para ver a saída completa linha a linha:

```bash
pio test -e native -v
```

---

## 📁 Estrutura dos Testes

```
test/
├── README.md               # Este guia de documentação
├── mocks/                  # Camada de abstração e mocks de hardware
│   ├── Arduino.h           # Tipos base, millis(), micros(), GPIO, MockSerial, String
│   ├── RTClib.h            # DateTime e TimeSpan matematicamente exatos (sem I2C)
│   ├── Preferences.h       # Mock em RAM para a NVS Flash da ESP32
│   ├── FS.h / LittleFS.h   # Mock em RAM para o sistema de ficheiros LittleFS
│   ├── Wire.h              # Mock para o barramento I2C
│   ├── LiquidCrystal_I2C.h # Mock para o ecrã LCD 2004
│   ├── esp_task_wdt.h      # Mock do watchdog do ESP-IDF
│   ├── TestCommon.h        # Declaração do harness comum
│   └── TestCommon.cpp      # Implementação do reset de mocks (resetAllTestMocks)
├── test_appstate/          # Invariantes de memória e tabelas estáticas de modos
├── test_history/           # Formatação e parsing CSV, rotação e sanitização
├── test_rtclock/           # Regras do horário de verão europeu (DST) e conversão UTC/Local
├── test_scheduler/         # Cálculo do próximo ciclo, intervalos, anos bissextos e proteção DST
├── test_storage/           # Exportação/importação hexadecimal e tolerância a dados corruptos
├── test_terminal/          # Tokenizer da CLI Serial, filtros ANSI, overflow e comandos
└── test_watering/          # Filas de rega, dead-time de relés, paragem e recuperação
```

---

## 🧪 Suítes de Teste e Cobertura

### 1. `test_scheduler` (Motor de Agendamento)
* `test_scheduler_medio_next_slot`: Validação de transições entre horários no modo Médio (08:00 e 20:00).
* `test_scheduler_intenso_next_slot`: Validação dos 3 horários diários no modo Intenso (06:00, 14:00, 22:00).
* `test_scheduler_desativado_returns_false`: Garante que o modo Desativado inibe qualquer agendamento.
* `test_scheduler_custom_every_x_days`: Validação da aritmética de dias no modo Personalizado (`DayPattern::EVERY_X_DAYS`).
* `test_scheduler_cycle_expired_check`: Limites rígidos de segurança para evitar retoma de regas obsoletas.
* `test_scheduler_suspension_auto_wake`: Despertar automático quando a data limite de suspensão é atingida.
* `test_scheduler_month_end_and_leap_year_rollover`: Transições em fim de mês (31 Jan → 1 Fev, 28 Fev → 1 Mar em anos normais, 28 Fev → 29 Fev em anos bissextos como 2028).
* `test_scheduler_on_mode_changed_immediate_update`: Recálculo instantâneo de horários ao alterar o modo de rega.

### 2. `test_watering` (Controlador de Válvulas e Filas)
* `test_watering_general_builds_queue_and_skips_disabled`: Construção correta da fila de zonas (zonas desativadas ou com duração zero são omitidas).
* `test_watering_custom_selection`: Execução personalizada apenas para zonas selecionadas com duração override.
* `test_watering_dead_time_and_relay_activation`: Garantia de intervalo morto (*dead-time* de 50ms) antes de acionar o GPIO do relé (ativo em LOW), impedindo sobretensão.
* `test_watering_emergency_stop`: Desativação imediata de todos os relés e limpeza de estado após paragem de emergência.
* `test_watering_test_run_does_not_log_to_history`: Ciclos de teste de 30s não poluem o ficheiro de histórico permanente.
* `test_watering_blackout_recovery_resumes_interrupted_cycle`: Validação da retoma automática de um ciclo interrompido por corte de energia a partir da zona interrompida.

### 3. `test_terminal` (Linha de Comandos Série)
* `test_terminal_help_command`: Listagem formatada de ajuda.
* `test_terminal_status_command`: Relatório detalhado do estado do sistema via Serial.
* `test_terminal_set_time_command`: Acerto de hora por comando e marcação de RTC válido.
* `test_terminal_clear_history_confirmation_flow`: Máquina de estados de confirmação interativa (`y` para apagar).
* `test_terminal_clear_history_cancellation`: Cancelamento seguro de limpeza de histórico (`n`).
* `test_terminal_ansi_arrow_keys_filtered`: Filtragem de sequências de escape ANSI (setas do teclado) sem corromper comandos.
* `test_terminal_buffer_overflow_protection`: Rejeição graciosa de inputs excessivos (>256 caracteres).
* `test_terminal_reboot_command`: Acionamento do reset de software (`ESP.restart()`).
* `test_terminal_i2c_scan_command`: Varredura de endereços do barramento I2C.

### 4. `test_storage` (Persistência NVS e Configuração Hexadecimal)
* `test_storage_export_hex_length_and_format`: Validação do formato de 68 caracteres hexadecimais em maiúsculas.
* `test_storage_export_and_import_roundtrip`: Integridade total do ciclo exportar → limpar RAM → importar.
* `test_storage_import_rejects_invalid_inputs`: Rejeição defensiva de strings nulas, incompletas ou com carateres inválidos.
* `test_storage_recovery_state_roundtrip`: Persistência binária compactada do estado de recuperação pós-blackout.
* `test_storage_save_and_load_persistence`: Carregamento e gravação de preferências NVS.
* `test_storage_rejects_corrupted_boolean_bytes`: Rejeição de valores booleanos corrompidos (ex: `0x02` ou `0xFF`).
* `test_storage_duration_clamping_on_import`: Clamping de segurança para durações superiores a 20 minutos.

### 5. `test_history` (Registo CSV e Rotação LittleFS)
* `test_history_entry_to_line_format`: Formatação padronizada ISO 8601 (`YYYY-MM-DDTHH:MM,A,d0,d1,d2,d3`).
* `test_history_line_to_entry_valid`: Leitura e conversão de linha CSV em struct `HistoryEntry`.
* `test_history_line_to_entry_rejects_corrupted`: Rejeição de anos inválidos (<2020 ou >2099), meses > 12 e horas > 23.
* `test_history_record_and_read_last`: Gestão de cache em RAM para consulta rápida no ecrã LCD.
* `test_history_rotation_triggered_at_capacity`: Rotação assíncrona automática ao atingir 1500 linhas (`HISTORY_MAX_ENTRIES`).
* `test_history_rejects_missing_columns`: Resiliência contra ficheiros incompletos ou linhas truncadas.

### 6. `test_rtclock` (Relógio de Tempo Real e Horário de Verão)
* `test_eu_dst_winter_and_summer_months`: Classificação de meses de inverno vs verão.
* `test_eu_dst_march_transition_boundary`: Transição exata de entrada em DST no último domingo de março às 01:00 UTC.
* `test_eu_dst_october_transition_boundary`: Transição exata de saída de DST no último domingo de outubro às 01:00 UTC.
* `test_utc_to_local_conversion`: Conversão UTC para Hora Local considerando `TIMEZONE_OFFSET` e DST automático.

### 7. `test_appstate` (Estruturas e Invariantes)
* `test_struct_sizes_and_alignment`: `static_assert` de 12 bytes para `HistoryEntry` e integridade de structs empacotadas.
* `test_init_app_state_defaults`: Inicialização limpa e valores de fábrica do sistema.
* `test_mode_schedules_table`: Verificação das tabelas de agendamento pré-configuradas em compile-time.

---

## 🛠️ Como Adicionar um Novo Teste

Para adicionar novos casos de teste a uma suite existente (ex: `test_scheduler`):

1. Abra o ficheiro correspondente: `test/test_scheduler/test_scheduler.cpp`.
2. Adicione a função do teste:
   ```cpp
   void test_meu_novo_caso(void) {
       // Configurar estado
       gState.mode = AppMode::FRACO;
       
       // Executar função a testar
       uint8_t h, m;
       bool ok = scheduler.computeNext(gState.mode, gState.now, h, m);
       
       // Asserções Unity
       TEST_ASSERT_TRUE(ok);
       TEST_ASSERT_EQUAL_UINT8(8, h);
   }
   ```
3. Registe o teste na função `main()`:
   ```cpp
   int main(int argc, char **argv) {
       UNITY_BEGIN();
       // ... outros testes ...
       RUN_TEST(test_meu_novo_caso);
       return UNITY_END();
   }
   ```
4. Execute `pio test -e native -f test_scheduler` para validar.

---

## 💡 Boas Práticas e Convenções

* **Isolamento**: Cada teste deve ser independente. A função `setUp()` invoca `resetAllTestMocks()` antes de cada caso de teste.
* **Tempo Simulado**: Nunca use chamadas que bloqueiem o CPU (`sleep`, `delay` real). Use `advanceMockMillis(ms)` para avançar o relógio de simulação.
* **Logs Silenciosos**: Durante os testes unitários, `terminalAwareLog()` é silenciado por defeito para manter o terminal limpo para o relatório do Unity.
