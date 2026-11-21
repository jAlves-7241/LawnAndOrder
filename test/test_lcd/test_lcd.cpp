#include <unity.h>
#include <vector>
#include <utility>

#define ARDUINO 10819
#include "TestCommon.h"
#include "../../test/mocks/TestCommon.cpp"
#include "../../src/AppState.cpp"
#include "../../lib/LiquidCrystal_I2C/LiquidCrystal_I2C.h"
#include "../../lib/LiquidCrystal_I2C/LiquidCrystal_I2C.cpp"

namespace {

static const uint8_t TEST_LCD_ADDR = 0x27;

struct PcfCapture {
    std::vector<uint8_t> rawBytes;
    uint32_t txCount = 0;
    uint32_t failAfterTx = 0;   // 0 = never fail; N = fail on Nth tx and all subsequent
    uint8_t  failCode = 4;

    void reset() {
        rawBytes.clear();
        txCount = 0;
        failAfterTx = 0;
        failCode = 4;
    }
};

static PcfCapture gPcf;

static uint8_t mockPcfTx(uint8_t addr, const uint8_t* txBuf, size_t txLen) {
    if (addr != TEST_LCD_ADDR) return 2;
    gPcf.txCount++;
    if (gPcf.failAfterTx > 0 && gPcf.txCount >= gPcf.failAfterTx) {
        return gPcf.failCode;
    }
    for (size_t i = 0; i < txLen; i++) {
        gPcf.rawBytes.push_back(txBuf[i]);
    }
    return 0;
}

// Reconstruct 4-bit HD44780 nibbles latched on En rising->falling transitions.
// Each pair is {rs_flag (0=cmd, 1=data), 4-bit high-aligned nibble}.
static std::vector<std::pair<uint8_t, uint8_t>> decodeLatchedNibbles(const std::vector<uint8_t>& raw) {
    std::vector<std::pair<uint8_t, uint8_t>> nibbles;
    bool prevEn = false;
    uint8_t latchedByte = 0;
    for (uint8_t b : raw) {
        bool en = (b & En) != 0;
        if (!prevEn && en) {
            latchedByte = b;
        } else if (prevEn && !en) {
            nibbles.push_back({ (uint8_t)(latchedByte & Rs), (uint8_t)(latchedByte & 0xF0U) });
        }
        prevEn = en;
    }
    return nibbles;
}

// Combine consecutive nibbles starting at startIdx into full 8-bit HD44780 transfers.
static std::vector<std::pair<uint8_t, uint8_t>> decodeBytesFrom(
        const std::vector<std::pair<uint8_t, uint8_t>>& nibbles, size_t startIdx) {
    std::vector<std::pair<uint8_t, uint8_t>> out;
    for (size_t i = startIdx; i + 1 < nibbles.size(); i += 2) {
        uint8_t rs = nibbles[i].first;
        uint8_t val = (uint8_t)(nibbles[i].second | (nibbles[i + 1].second >> 4));
        out.push_back({ rs, val });
    }
    return out;
}

} // namespace

void setUp(void) {
    resetAllTestMocks();
    gPcf.reset();
    Wire.setSlaveHandlers(TEST_LCD_ADDR, mockPcfTx, nullptr);
}

void tearDown(void) {
    Wire.setSlaveHandlers(0, nullptr, nullptr);
}

void test_lcd_init_fast_abort_on_nack_and_wire_begin_control(void) {
    // 1. Disconnected PCF8574 (NACK on probe) fast-aborts without 50ms/10ms stalls or Wire.begin()
    LiquidCrystal_I2C lcd(TEST_LCD_ADDR, 20, 4);
    uint32_t msBefore = millis();
    uint32_t usBefore = micros();

    lcd.init();
    TEST_ASSERT_EQUAL_UINT8(2, lcd.lastError());
    TEST_ASSERT_EQUAL_UINT32(0, Wire.beginCalls);
    TEST_ASSERT_EQUAL_UINT32(0, gPcf.txCount);
    TEST_ASSERT_EQUAL_UINT32(msBefore, millis());
    TEST_ASSERT_EQUAL_UINT32(usBefore, micros());

    // Explicit init(false) and oled_init() also leave Wire untouched
    lcd.init(false);
    TEST_ASSERT_EQUAL_UINT8(2, lcd.lastError());
    TEST_ASSERT_EQUAL_UINT32(0, Wire.beginCalls);

    lcd.oled_init();
    TEST_ASSERT_EQUAL_UINT8(2, lcd.lastError());
    TEST_ASSERT_EQUAL_UINT32(0, Wire.beginCalls);

    // 2. Explicit init(true) calls Wire.begin() once (for standalone sketches)
    lcd.init(true);
    TEST_ASSERT_EQUAL_UINT8(2, lcd.lastError());
    TEST_ASSERT_EQUAL_UINT32(1, Wire.beginCalls);
    TEST_ASSERT_EQUAL_UINT32(msBefore, millis());
}

void test_lcd_init_4bit_sequence_commands_and_backlight_flag(void) {
    Wire.detectedAddresses.push_back(TEST_LCD_ADDR);
    LiquidCrystal_I2C lcd(TEST_LCD_ADDR, 20, 4);

    uint32_t msBefore = millis();
    lcd.init(false);
    TEST_ASSERT_EQUAL_UINT8(0, lcd.lastError());
    TEST_ASSERT_EQUAL_UINT32(0, Wire.beginCalls);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(60, millis() - msBefore); // 50ms + 10ms init delays

    // First 4 latched nibbles must be the HD44780 4-bit reset sequence: 0x30, 0x30, 0x30, 0x20
    auto initNibbles = decodeLatchedNibbles(gPcf.rawBytes);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(14, (uint32_t)initNibbles.size());
    TEST_ASSERT_EQUAL_HEX8(0x30, initNibbles[0].second);
    TEST_ASSERT_EQUAL_HEX8(0x30, initNibbles[1].second);
    TEST_ASSERT_EQUAL_HEX8(0x30, initNibbles[2].second);
    TEST_ASSERT_EQUAL_HEX8(0x20, initNibbles[3].second);

    // Subsequent 8-bit init commands: FunctionSet(0x28), DisplayOn(0x0C), Clear(0x01), EntryMode(0x06), Home(0x02)
    auto initCmds = decodeBytesFrom(initNibbles, 4);
    TEST_ASSERT_EQUAL_UINT32(5, (uint32_t)initCmds.size());
    TEST_ASSERT_EQUAL_UINT8(0, initCmds[0].first);
    TEST_ASSERT_EQUAL_HEX8(LCD_FUNCTIONSET | LCD_4BITMODE | LCD_2LINE | LCD_5x8DOTS, initCmds[0].second);
    TEST_ASSERT_EQUAL_HEX8(LCD_DISPLAYCONTROL | LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF, initCmds[1].second);
    TEST_ASSERT_EQUAL_HEX8(LCD_CLEARDISPLAY, initCmds[2].second);
    TEST_ASSERT_EQUAL_HEX8(LCD_ENTRYMODESET | LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT, initCmds[3].second);
    TEST_ASSERT_EQUAL_HEX8(LCD_RETURNHOME, initCmds[4].second);

    // Enable backlight and print text; verify LCD_BACKLIGHT bit is set on every single expander write
    gPcf.reset();
    lcd.backlight();
    lcd.setCursor(5, 1);
    size_t written = lcd.print("Hi!");
    TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)written);
    TEST_ASSERT_EQUAL_UINT8(0, lcd.lastError());
    TEST_ASSERT_FALSE(gPcf.rawBytes.empty());
    for (uint8_t b : gPcf.rawBytes) {
        TEST_ASSERT_TRUE((b & LCD_BACKLIGHT) != 0);
        TEST_ASSERT_TRUE((b & Rw) == 0);
    }

    auto textTransfers = decodeBytesFrom(decodeLatchedNibbles(gPcf.rawBytes), 0);
    TEST_ASSERT_EQUAL_UINT32(4, (uint32_t)textTransfers.size());
    TEST_ASSERT_EQUAL_UINT8(0, textTransfers[0].first);
    TEST_ASSERT_EQUAL_HEX8(LCD_SETDDRAMADDR | (5 + 0x40), textTransfers[0].second);
    TEST_ASSERT_EQUAL_UINT8(Rs, textTransfers[1].first);
    TEST_ASSERT_EQUAL_CHAR('H', (char)textTransfers[1].second);
    TEST_ASSERT_EQUAL_UINT8(Rs, textTransfers[2].first);
    TEST_ASSERT_EQUAL_CHAR('i', (char)textTransfers[2].second);
    TEST_ASSERT_EQUAL_UINT8(Rs, textTransfers[3].first);
    TEST_ASSERT_EQUAL_CHAR('!', (char)textTransfers[3].second);

    // noBacklight() clears LCD_BACKLIGHT bit and keeps it clear on subsequent commands
    gPcf.reset();
    lcd.noBacklight();
    lcd.noDisplay();
    lcd.display();
    TEST_ASSERT_EQUAL_UINT8(0, lcd.lastError());
    TEST_ASSERT_FALSE(gPcf.rawBytes.empty());
    for (uint8_t b : gPcf.rawBytes) {
        TEST_ASSERT_TRUE((b & LCD_BACKLIGHT) == 0);
    }
}

void test_lcd_mid_print_i2c_error_short_circuits_and_clear_error(void) {
    Wire.detectedAddresses.push_back(TEST_LCD_ADDR);
    LiquidCrystal_I2C lcd(TEST_LCD_ADDR, 20, 4);
    lcd.init(false);
    lcd.backlight();
    TEST_ASSERT_EQUAL_UINT8(0, lcd.lastError());

    // Each character sends 2 nibbles * 3 expanderWrites = 6 I2C transactions.
    // Fail on the 8th transaction (during the 2nd char 'e' of "Hello World"):
    // char 0 ('H') succeeds (tx 1..6), char 1 ('e') fails at tx 8 and aborts immediately!
    gPcf.reset();
    gPcf.failAfterTx = 8;
    gPcf.failCode = 4;

    size_t written = lcd.print("Hello World");
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)written);
    TEST_ASSERT_EQUAL_UINT8(4, lcd.lastError());
    TEST_ASSERT_EQUAL_UINT32(8, gPcf.txCount); // ZERO extra I2C transactions for remaining 9 chars!

    // Subsequent commands/writes remain short-circuited while _i2cErr != 0
    lcd.setCursor(0, 2);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)lcd.print("Skipped"));
    TEST_ASSERT_EQUAL_UINT32(8, gPcf.txCount);

    // clearError() + bus recovery resumes normal printing
    gPcf.reset();
    lcd.clearError();
    TEST_ASSERT_EQUAL_UINT8(0, lcd.lastError());
    lcd.setCursor(2, 2);
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)lcd.print("OK"));
    TEST_ASSERT_EQUAL_UINT8(0, lcd.lastError());

    auto transfers = decodeBytesFrom(decodeLatchedNibbles(gPcf.rawBytes), 0);
    TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)transfers.size());
    TEST_ASSERT_EQUAL_HEX8(LCD_SETDDRAMADDR | (2 + 0x14), transfers[0].second);
    TEST_ASSERT_EQUAL_CHAR('O', (char)transfers[1].second);
    TEST_ASSERT_EQUAL_CHAR('K', (char)transfers[2].second);
}

void test_lcd_set_cursor_bounds_uninitialized_guard_and_create_char(void) {
    Wire.detectedAddresses.push_back(TEST_LCD_ADDR);
    LiquidCrystal_I2C lcd(TEST_LCD_ADDR, 20, 4);

    // 1. Before init() (_numlines == 0), setCursor() must be a safe no-op (no underflow / no I2C writes)
    lcd.setCursor(0, 0);
    TEST_ASSERT_EQUAL_UINT32(0, gPcf.txCount);

    // 2. After init(), out-of-range row clamps cleanly to row 3 (DDRAM offset 0x54)
    lcd.init(false);
    gPcf.reset();
    lcd.setCursor(1, 99);
    auto cursorCmd = decodeBytesFrom(decodeLatchedNibbles(gPcf.rawBytes), 0);
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)cursorCmd.size());
    TEST_ASSERT_EQUAL_UINT8(0, cursorCmd[0].first);
    TEST_ASSERT_EQUAL_HEX8(LCD_SETDDRAMADDR | (1 + 0x54), cursorCmd[0].second);

    // 3. createChar() (both RAM and PROGMEM overloads) masks slot to 0..7 and writes 8 rows
    uint8_t glyph[8] = { 0x04, 0x0E, 0x0E, 0x0E, 0x1F, 0x00, 0x04, 0x00 };
    gPcf.reset();
    lcd.createChar(9, glyph); // 9 & 0x7 == 1 -> CGRAM 0x48
    auto cgram = decodeBytesFrom(decodeLatchedNibbles(gPcf.rawBytes), 0);
    TEST_ASSERT_EQUAL_UINT32(9, (uint32_t)cgram.size());
    TEST_ASSERT_EQUAL_UINT8(0, cgram[0].first);
    TEST_ASSERT_EQUAL_HEX8(LCD_SETCGRAMADDR | (1 << 3), cgram[0].second);
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL_UINT8(Rs, cgram[i + 1].first);
        TEST_ASSERT_EQUAL_HEX8(glyph[i], cgram[i + 1].second);
    }

    const char progGlyph[8] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 };
    gPcf.reset();
    lcd.createChar(2, progGlyph);
    auto progCgram = decodeBytesFrom(decodeLatchedNibbles(gPcf.rawBytes), 0);
    TEST_ASSERT_EQUAL_UINT32(9, (uint32_t)progCgram.size());
    TEST_ASSERT_EQUAL_HEX8(LCD_SETCGRAMADDR | (2 << 3), progCgram[0].second);
    TEST_ASSERT_EQUAL_HEX8(0x01, progCgram[1].second);
    TEST_ASSERT_EQUAL_HEX8(0x02, progCgram[8].second);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_lcd_init_fast_abort_on_nack_and_wire_begin_control);
    RUN_TEST(test_lcd_init_4bit_sequence_commands_and_backlight_flag);
    RUN_TEST(test_lcd_mid_print_i2c_error_short_circuits_and_clear_error);
    RUN_TEST(test_lcd_set_cursor_bounds_uninitialized_guard_and_create_char);
    return UNITY_END();
}
