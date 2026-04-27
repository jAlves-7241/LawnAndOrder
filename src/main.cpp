#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ── Hardware ──────────────────────────────────────────────
LiquidCrystal_I2C lcd(0x27, 20, 4);

const uint8_t PIN_CLK = 32;
const uint8_t PIN_DT  = 33;
const uint8_t PIN_SW  = 25;

// ── Estado do encoder ─────────────────────────────────────
const int ZONA_MIN = 1;
const int ZONA_MAX = 8;

volatile int  contador    = ZONA_MIN;
volatile bool encoderMoveu = false;

// ── Estado do botão ───────────────────────────────────────
bool          botaoPressionado = false;
unsigned long ultimoClique     = 0;
const unsigned long DEBOUNCE_MS = 200;

// ── Cache do LCD (evita flickering) ───────────────────────
int  ultimoContador  = -1;
int  ultimaZonaSel   = -1;

// ─────────────────────────────────────────────────────────
// ISR – corre na IRAM, variáveis partilhadas são volatile
// ─────────────────────────────────────────────────────────
void IRAM_ATTR lerEncoder() {
  contador += (digitalRead(PIN_DT) != digitalRead(PIN_CLK)) ? 1 : -1;
  if (contador < ZONA_MIN) contador = ZONA_MAX;
  if (contador > ZONA_MAX) contador = ZONA_MIN;
  encoderMoveu = true;
}

// ─────────────────────────────────────────────────────────
// Atualiza só a linha 2 – sem lcd.clear(), sem flicke