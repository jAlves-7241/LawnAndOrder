# Native Unit Test Suite

Technical documentation for the Lawn & Order native unit test suite, implemented via PlatformIO's `native` environment and the Unity test framework.

---

## 1. Technical Architecture

### Execution Model
* **Platform**: PlatformIO `native` environment running on host OS (Windows, Linux, macOS) compiled with GCC/Clang in C++17 (`-std=gnu++17`).
* **Test Framework**: Unity (`test_framework = unity`).
* **Compilation Model**: Header shadowing and selective translation unit inclusion. Hardware/framework headers are shadowed by `-I test/mocks` placed ahead of `-I src` in `platformio.ini`.
* **Zero Source Modification**: Production source code in `src/` is compiled as-is without `#ifdef` test pollution. Test runners directly `#include` required implementation files (`.cpp`) to link isolated subsystems without pulling in `main.cpp`, FreeRTOS tasks, or physical hardware drivers.

```
test/
├── mocks/                  # Hardware abstraction and shim layer
│   ├── Arduino.h           # Core types, GPIO registers, time stubs, MockSerial stream
│   ├── RTClib.h            # Pure algorithmic DateTime/TimeSpan implementation
│   ├── Preferences.h       # In-memory NVS flash emulation (std::map backed)
│   ├── FS.h / LittleFS.h   # In-memory virtual filesystem and stream File mock
│   ├── Wire.h              # TwoWire I2C bus mock
│   ├── LiquidCrystal_I2C.h # LCD controller stub
│   ├── esp_task_wdt.h      # ESP-IDF task watchdog stub
│   ├── TestCommon.h        # Common test harness definitions
│   └── TestCommon.cpp      # Global mock instances and resetAllTestMocks()
├── test_appstate/          # Memory layout, alignment, and static schedules
├── test_history/           # CSV serialization, deserialization, rotation, and cache
├── test_rtclock/           # European DST transitions and UTC-to-local conversion
├── test_scheduler/         # Next cycle computation, intervals, and blackout expiration
├── test_storage/           # Hex configuration codec, validation, and NVS persistence
├── test_terminal/          # Serial CLI tokenizer, ANSI filter, buffer protection, commands
└── test_watering/          # Valve queue sequencing, relay dead-time, stop, and recovery
```

---

## 2. Mock Layer Implementation (`test/mocks/`)

| Mock File | Implemented Functionality | State Reset Mechanism |
|---|---|---|
| `Arduino.h` | <ul><li>Time tracking: `millis()`, `micros()`, `delay()`, `setMockMillis()`, `advanceMockMillis()`</li><li>GPIO: Pin array `_mock_gpio[64]`, `pinMode()`, `digitalWrite()`, `digitalRead()`</li><li>Stream I/O: `MockSerial Serial` with input/output string buffers, formatted `printf()`, chunked `read()`</li><li>Utilities: `strlcpy()`, `min()`, `max()`, `constrain()`, `abs()`, `String` adapter</li><li>Hardware stubs: `IRAM_ATTR`, `EspClass ESP` with `restart()` tracking</li></ul> | `_mock_millis = 1000`<br>`memset(_mock_gpio, HIGH, 64)`<br>`Serial.reset()` |
| `RTClib.h` | <ul><li>Pure algorithmic `DateTime` and `TimeSpan` matching Adafruit RTClib interface</li><li>Leap year determination, days-in-month calculations, day-of-week calculation</li><li>Arithmetic operators (`+`, `-`, `<`, `==`)</li><li>Stubbed `RTC_DS3231` and `RTC_DS1307` classes</li></ul> | Stateless (value types) |
| `Preferences.h` | <ul><li>In-memory NVS key-value store: `std::map<std::string, std::map<std::string, std::vector<uint8_t>>>`</li><li>Binary blob access: `getBytes()`, `putBytes()`</li><li>Type-specific access: `getUShort()`, `putUShort()`, `getUInt()`, `putUInt()`, `getBool()`, `putBool()`</li><li>Namespace management: `begin()`, `end()`, `clear()`</li></ul> | `Preferences::resetStorage()` |
| `FS.h` / `LittleFS.h` | <ul><li>In-memory file table: `std::map<std::string, std::string>`</li><li>Stream operations: `seek()` (`SeekSet`, `SeekCur`, `SeekEnd`), `read()`, `println()`, `available()`, `size()`</li><li>Filesystem operations: `open()`, `remove()`, `rename()`, `exists()`, `begin()`</li></ul> | `LittleFS.reset()` |
| `Wire.h` | <ul><li>I2C address detection simulation via `detectedAddresses` vector</li><li>`beginTransmission()`, `endTransmission()` return codes</li></ul> | `Wire.detectedAddresses.clear()` |
| `LiquidCrystal_I2C.h` | <ul><li>No-op stubs: `init()`, `clear()`, `setCursor()`, `print()`, `backlight()`, `noBacklight()`</li></ul> | Stateless |
| `esp_task_wdt.h` | <ul><li>No-op stubs: `esp_task_wdt_reset()`, `esp_task_wdt_add()`, `esp_task_wdt_delete()`</li></ul> | Stateless |

---

## 3. Test Suites Technical Reference

### `test_appstate`
* `test_struct_sizes_and_alignment`: Validates compile-time size invariants (`sizeof(HistoryEntry) == 12`, `sizeof(RecoveryState)`, `sizeof(AppConfigBlob)`).
* `test_init_app_state_defaults`: Verifies `initAppState()` establishes default zone mappings, durations, disabled modes, and initial sentinel values.
* `test_mode_schedules_table`: Validates static `MODE_SCHEDULES` matrix against compile-time configuration defines (`INTENSO`, `MEDIO`, `FRACO`, `DESATIVADO`, `PERSONALIZADO`).

### `test_rtclock`
* `test_eu_dst_winter_and_summer_months`: Tests DST state classification across non-boundary months.
* `test_eu_dst_march_transition_boundary`: Tests boundary conditions on the last Sunday of March (00:59:59 UTC = standard time; 01:00:00 UTC = daylight saving time).
* `test_eu_dst_october_transition_boundary`: Tests boundary conditions on the last Sunday of October (00:59:59 UTC = daylight saving time; 01:00:00 UTC = standard time).
* `test_utc_to_local_conversion`: Verifies offset computation (`TIMEZONE_OFFSET` and optional DST hour) and behavior when `auto_dst` is disabled.

### `test_scheduler`
* `test_scheduler_medio_next_slot`: Slot selection for 2-slot daily schedule across morning, midday, and night rollovers.
* `test_scheduler_intenso_next_slot`: Slot selection for 3-slot daily schedule.
* `test_scheduler_desativado_returns_false`: Asserts `computeNext` returns `false` and zeroes output when automatic watering is disabled.
* `test_scheduler_custom_every_x_days`: Multi-day interval calculations (`EVERY_X_DAYS`) against arbitrary reference day epochs.
* `test_scheduler_cycle_expired_check`: Hard maximum duration validation (8h/12h limits), backwards clock detection, and next-cycle overlap safety gaps.
* `test_scheduler_suspension_auto_wake`: Verifies automatic clearance of `gState.suspended` when `now.unix >= suspended_until`.
* `test_scheduler_month_end_and_leap_year_rollover`: Calendar arithmetic across Jan 31 → Feb 1, Feb 28 → Mar 1 (non-leap year), and Feb 28 → Feb 29 (leap year 2028).
* `test_scheduler_on_mode_changed_immediate_update`: Verifies instant recomputation of `next_hour`/`next_min` when the operating mode changes.

### `test_storage`
* `test_storage_export_hex_length_and_format`: Verifies exported string length equals `sizeof(AppConfigBlob) * 2` (68 bytes) with valid uppercase hexadecimal representation.
* `test_storage_export_and_import_roundtrip`: End-to-end serialization and deserialization integrity across state, schedules, and zone durations.
* `test_storage_import_rejects_invalid_inputs`: Defensive rejection of null pointers, incorrect lengths, non-hex characters, and out-of-bounds mode enumerations.
* `test_storage_recovery_state_roundtrip`: Verifies persistence and recovery of packed `RecoveryState` binary struct.
* `test_storage_save_and_load_persistence`: Verifies NVS flash persistence cycle via the simulated Preferences backend.
* `test_storage_rejects_corrupted_boolean_bytes`: Validates `_blobHasValidBooleans` rejection when boolean bytes exceed value 1.
* `test_storage_duration_clamping_on_import`: Verifies durations exceeding 20 minutes are clamped to default value 8.

### `test_history`
* `test_history_entry_to_line_format`: Validates ISO 8601 formatting (`YYYY-MM-DDTHH:MM,<trigger>,d0,d1,d2,d3`) across `AUTO` ('A'), `MANUAL` ('M'), and `CUSTOM` ('C') triggers.
* `test_history_line_to_entry_valid`: Validates parsing of standard CSV lines into `HistoryEntry` records.
* `test_history_line_to_entry_rejects_corrupted`: Validates rejection of invalid dates, out-of-bounds months (>12), hours (>23), minutes (>59), and non-standard delimiters.
* `test_history_record_and_read_last`: Verifies RAM cache FIFO ordering, line count tracking, and exclusion of `TEST` ('T') triggers.
* `test_history_rotation_triggered_at_capacity`: Verifies that exceeding `HISTORY_MAX_ENTRIES` (1500) initiates asynchronous rotation, discarding `HISTORY_MAX_ENTRIES / 10` oldest records while retaining latest.
* `test_history_rejects_missing_columns`: Validates parser resilience against truncated CSV records.

### `test_terminal`
* `test_terminal_help_command`: Help menu formatting and command presence.
* `test_terminal_status_command`: Real-time system report formatting via Serial stream.
* `test_terminal_set_time_command`: Time parsing, clock update, and RTC validity flag assignment.
* `test_terminal_clear_history_confirmation_flow`: Interactive confirmation state machine (`y` proceeds to wipe).
* `test_terminal_clear_history_cancellation`: Interactive confirmation abort (`n` leaves history intact).
* `test_terminal_ansi_arrow_keys_filtered`: Verifies terminal line editor processes ANSI cursor sequences without leaking raw escape bytes into command buffer.
* `test_terminal_buffer_overflow_protection`: Verifies lines exceeding 256 bytes trigger buffer overflow protection and reset safely across chunked 64-byte update cycles.
* `test_terminal_reboot_command`: Verifies `reboot` command triggers `ESP.restart()`.
* `test_terminal_i2c_scan_command`: Verifies address reporting across simulated I2C devices.

### `test_watering`
* `test_watering_general_builds_queue_and_skips_disabled`: Queue compilation validating that disabled zones and zones with `duration_min == 0` are excluded.
* `test_watering_custom_selection`: Selective zone execution with runtime duration overrides.
* `test_watering_dead_time_and_relay_activation`: Verifies relay dead-time enforcement (50ms where all pins remain `HIGH`) before activating zone relay (`LOW`).
* `test_watering_emergency_stop`: Immediate hardware deactivation (all pins forced `HIGH`), queue purging, and state synchronization.
* `test_watering_test_run_does_not_log_to_history`: Verifies 30-second zone test runs are excluded from persistent history logging.
* `test_watering_blackout_recovery_resumes_interrupted_cycle`: Verifies reboot recovery loads persisted `RecoveryState`, validates expiration window, and resumes from interrupted queue position.

---

## 4. Usage & Execution Commands

```bash
# Execute entire test suite (all 43 test cases)
pio test -e native

# Execute specific test suite
pio test -e native -f test_scheduler
pio test -e native -f test_watering
pio test -e native -f test_terminal
pio test -e native -f test_storage
pio test -e native -f test_history
pio test -e native -f test_rtclock
pio test -e native -f test_appstate

# Verbose output (detailed assertion logs)
pio test -e native -vv
```

---

## 5. Adding New Tests

### Pattern for Adding a Test Case to Existing Suite
```cpp
void test_specific_condition(void) {
    // 1. Arrange: configure global or local state
    gState.mode = AppMode::MEDIO;
    gState.now = { 2026, 6, 15, 8, 0, 0, 1, 1781500000UL };

    // 2. Act: invoke module method
    uint8_t nextH = 0, nextM = 0;
    bool ok = scheduler.computeNext(gState.mode, gState.now, nextH, nextM);

    // 3. Assert: verify results via Unity macros
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(20, nextH);
}

// In main():
RUN_TEST(test_specific_condition);
```

### Pattern for Creating a New Test Suite
1. Create directory `test/test_<subsystem>/`.
2. Create test runner `test/test_<subsystem>/test_<subsystem>.cpp`.
3. Include test infrastructure:
   ```cpp
   #include <unity.h>
   #include "TestCommon.h"
   #include "../../test/mocks/TestCommon.cpp"
   #include "../../src/<RequiredSubsystem>.cpp"

   void setUp(void) {
       resetAllTestMocks();
   }

   void tearDown(void) {}

   // Test cases...

   int main(int argc, char **argv) {
       UNITY_BEGIN();
       RUN_TEST(test_case_name);
       return UNITY_END();
   }
   ```
4. Run `pio test -e native -f test_<subsystem>`.
