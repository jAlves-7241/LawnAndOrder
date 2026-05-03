# Sistema de Rega — ESP32 (Código Fonte)

Este diretório contém a implementação do controlador de rega.

## Estrutura de Ficheiros

- `config.h`: Definições de pinagem e constantes globais.
- `AppState.h / .cpp`: Estado centralizado da aplicação (`gState`).
- `Display.h / .cpp`: Driver LCD otimizado com *shadow buffering*.
- `Encoder.h / .cpp`: Gestão de interrupções para o encoder rotativo.
- `History.h / .cpp`: Registo de histórico em LittleFS (CSV).
- `RTClock.h / .cpp`: Interface com o módulo RTC DS3231.
- `Scheduler.h / .cpp`: Lógica de agendamento e disparo automático.
- `Storage.h / .cpp`: Persistência de dados em NVS Flash.
- `WateringController.h / .cpp`: Controlo de hardware (relés) e fila de rega.
- `UI.h / .cpp`: Interface de utilizador e máquina de estados dos menus.
- `main.cpp`: Ponto de entrada e ciclo principal.

Para documentação completa sobre o funcionamento e utilização, consulte o [README.md principal](../README.md).
