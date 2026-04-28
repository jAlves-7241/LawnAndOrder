# Sistema de Rega — ESP32

## Estrutura do projeto

```
rega-esp32/
├── .vscode/
│   └── extensions.json       # extensões recomendadas
├── src/
│   ├── config.h              # pinos, constantes, versão firmware
│   ├── AppState.h / .cpp     # estado global único da aplicação
│   ├── Display.h / .cpp      # wrapper LCD sem flickering
│   ├── Encoder.h / .cpp      # driver ISR do encoder rotativo
│   ├── UI.h / .cpp           # máquina de estados da interface
│   └── main.cpp              # setup() e loop()
├── diagram.json              # circuito Wokwi
├── platformio.ini            # configuração PlatformIO
└── wokwi.toml                # apontador para firmware compilado
```

### Dependências entre módulos

```
main.cpp
  ├── AppState   (estado partilhado por todos)
  ├── Display    (LCD I2C)
  ├── Encoder    (ISR + debounce)
  └── UI         (consome Display + Encoder, lê/escreve AppState)
```

> Próximas adições encaixam no `loop()`:
> `scheduler.update()` → consulta RTC, dispara zonas  
> `wateringCtrl.update()` → controla relés, atualiza `gState.watering`

---

## Pré-requisitos

| Ferramenta | Versão mínima |
|---|---|
| VSCode | qualquer |
| Extensão PlatformIO IDE | qualquer |
| Extensão Wokwi for VSCode | qualquer |
| Conta Wokwi (gratuita) | — |

---

## Como compilar

### Via VSCode (recomendado)

1. Abre a pasta `rega-esp32/` no VSCode  
   `File → Open Folder…`

2. O PlatformIO deteta o `platformio.ini` automaticamente e descarrega a toolchain ESP32 + a biblioteca LCD na primeira abertura (pode demorar 1–2 min).

3. Compila:  
   - Atalho: **`Ctrl + Alt + B`**  
   - Ou clica no ícone **✓** na barra inferior do VSCode

4. O firmware fica em:  
   `.pio/build/esp32dev/firmware.elf` e `firmware.bin`

### Via terminal (dentro da pasta do projeto)

```bash
pio run
```

---

## Como simular no Wokwi

> O Wokwi precisa que o firmware esteja **compilado** antes de simular.

1. Compila primeiro (passo acima).

2. Abre o ficheiro `diagram.json` no VSCode.

3. Inicia o simulador:  
   **`F1`** → `Wokwi: Start Simulator`  
   — ou clica no botão **▶ Start Simulation** que aparece no canto do `diagram.json`.

4. Na simulação:
   - O encoder rotativo tem uma seta para rodar e um botão de clique central.
   - O monitor série aparece no terminal do VSCode (115200 baud).

### Licença Wokwi

Na primeira utilização o Wokwi pede que actives a licença gratuita:  
**`F1`** → `Wokwi: Request License`  
(requer conta em [wokwi.com](https://wokwi.com))

---

## Como fazer upload para hardware real

1. Liga o ESP32 via USB.

2. Upload:  
   - Atalho: **`Ctrl + Alt + U`**  
   - Ou clica no ícone **→** na barra inferior

3. Monitor série:  
   - Atalho: **`Ctrl + Alt + S`**  
   - Ou clica no ícone de tomada na barra inferior

---

## Notas de desenvolvimento

- **Sem `lcd.clear()`** após init — o `Display` mantém um shadow buffer e só reescreve linhas que mudaram (elimina flickering).
- **ISR no encoder** — a rotação é capturada por interrupção (`IRAM_ATTR`), o `loop()` apenas lê o delta acumulado.
- **Timeout de idle** — após `IDLE_TIMEOUT_MS` (30 s) sem interação, a UI regressa ao ecrã de IDLE automaticamente.
- **`gState`** é o único estado persistente em RAM; está preparado para ser serializado para NVS (flash) quando o módulo de agendamento for adicionado.
