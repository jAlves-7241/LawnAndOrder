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
│   ├── esp_timer.h         # High-resolution microsecond timer stub
│   ├── driver/gpio.h       # ESP-IDF GPIO level query shim
│   ├── TestCommon.h        # Common test harness definitions
│   └── TestCommon.cpp      # Global mock instances and resetAllTestMocks()
├── test_appstate/          # Memory layout, alignment, and static schedules
├── test_encoder/           # Rotary encoder ISR debounce, click detection, delta clamping
├── test_history/           # CSV serialization, rotation, cache FIFO, deferred queuing, export
├── test_rtclock/           # European DST transitions, leap years, and UTC-to-local conversion
├── test_scheduler/         # Slot computation, patterns (DAILY, DOW_MASK, EVERY_X_DAYS), DST guards
├── test_storage/           # Hex config codec, validation, NVS persistence, cache round-trip
├── test_terminal/          # Serial CLI tokenizer, ANSI filter, buffer protection, commands
└── test_watering/          # Valve sequencing, relay dead-time, stop, recovery, multi-zone runs
```

---

## 2. Mock Layer Implementation (`test/mocks/`)

| Mock File | Implemented Functionality | State Reset Mechanism |
|---|---|---|
| `Arduino.h` | <ul><li>Time tracking: `millis()`, `micros()`, `delay()`, `setMockMillis()`, `advanceMockMillis()`, `setMockMicros()`, `advanceMockMicros()`</li><li>GPIO: Pin array `_mock_gpio[64]`, `pinMode()`, `digitalWrite()`, `digitalRead()`</li><li>Stream I/O: `MockSerial Serial` with input/output string buffers, formatted `printf()`, chunked `read()`, buffer bounds checking</li><li>Interrupts: `digitalPinToInterrupt()`, `attachInterrupt()`, `detachInterrupt()`, `FALLING`, `RISING`, `CHANGE`</li><li>Utilities: `strlcpy()`, `min()`, `max()`, `constrain()`, `abs()`, `String` adapter with indexing and slicing</li><li>Hardware stubs: `IRAM_ATTR`, `EspClass ESP` with `restart()` tracking and `reset()`</li><li>Critical sections: `portMUX_TYPE`, `portENTER_CRITICAL()`, `portEXIT_CRITICAL()`</li></ul> | `_mock_millis = 1000`<br>`_mock_micros = 1000000`<br>`memset(_mock_gpio, HIGH, 64)`<br>`Serial.reset()`<br>`ESP.reset()` |
| `RTClib.h` | <ul><li>Pure algorithmic `DateTime` and `TimeSpan` matching Adafruit RTClib interface</li><li>Leap year determination, days-in-month calculations, day-of-week calculation</li><li>Arithmetic operators (`+`, `-`, `<`, `==`)</li><li>Stubbed `RTC_DS3231` and `RTC_DS1307` classes</li></ul> | Stateless (value types) |
| `Preferences.h` | <ul><li>In-memory NVS key-value store: `std::map<std::string, std::map<std::string, std::vector<uint8_t>>>`</li><li>Binary blob access: `getBytes()`, `putBytes()`</li><li>Type-specific access: `getUShort()`, `putUShort()`, `getUInt()`, `putUInt()`, `getBool()`, `putBool()`</li><li>Namespace management: `begin()`, `end()`, `clear()`</li></ul> | `Preferences::resetStorage()` |
| `FS.h` / `LittleFS.h` | <ul><li>In-memory file table: `std::map<std::string, std::string>`</li><li>Stream operations: `seek()` (`SeekSet`, `SeekCur`, `SeekEnd`), `read()`, `println()`, `available()`, `size()`</li><li>Filesystem operations: `open()`, `remove()`, `rename()`, `exists()`, `begin()`</li></ul> | `LittleFS.reset()` |
| `Wire.h` | <ul><li>I2C address detection simulation via `detectedAddresses` vector</li><li>`beginTransmission()`, `endTransmission()` return codes</li><li>Bus address reset: `reset()`</li></ul> | `Wire.reset()` (via `resetAllTestMocks()`) |
| `LiquidCrystal_I2C.h` | <ul><li>No-op stubs: `init()`, `clear()`, `setCursor()`, `print()`, `backlight()`, `noBacklight()`</li></ul> | Stateless |
| `esp_task_wdt.h` | <ul><li>No-op stubs: `esp_task_wdt_reset()`, `esp_task_wdt_add()`, `esp_task_wdt_delete()`</li></ul> | Stateless |
| `esp_timer.h` | <ul><li>High-resolution microsecond timer stub: `esp_timer_get_time()` backed by mock `micros()`</li></ul> | Tied to `_mock_micros` |
| `driver/gpio.h` | <ul><li>ESP-IDF GPIO query stub: `gpio_get_level()` backed by mock `digitalRead()`</li></ul> | Tied to `_mock_gpio` |

---

## 3. Test Suites Technical Reference

### `test_appstate`
* `test_struct_sizes_and_alignment`: Validates compile-time size and packed layout invariants (`sizeof(HistoryEntry) == 12`, `sizeof(RecoveryState) == 33`, `sizeof(AppConfigBlob) == 34`).
* `test_init_app_state_defaults`: Verifies `initAppState()` establishes default zone mappings, durations, disabled modes, and initial sentinel values.
* `test_mode_schedules_table`: Validates static `MODE_SCHEDULES` matrix against compile-time configuration defines (`INTENSO`, `MEDIO`, `FRACO`, `DESATIVADO`, `PERSONALIZADO`).

### `test_encoder`
* `test_encoder_click_debounce_and_single_trigger`: Verifies 50ms pushbutton debounce, single click emission per physical press, immunity to held buttons, and release transition handling.
* `test_encoder_rotation_delta_and_clamping`: Tests positive/negative rotation tracking via ISR, delta consumption on read, and clamp-and-preserve arithmetic at ±127 bounds.
* `test_encoder_isr_rate_limiting`: Verifies 2000µs ISR debounce filter drops pulses arriving faster than 2ms.

### `test_rtclock`
* `test_eu_dst_winter_and_summer_months`: Tests DST state classification across non-boundary months.
* `test_eu_dst_march_transition_boundary`: Tests boundary conditions on the last Sunday of March (00:59:59 UTC = standard time; 01:00:00 UTC = daylight saving time).
* `test_eu_dst_october_transition_boundary`: Tests boundary conditions on the last Sunday of October (00:59:59 UTC = daylight saving time; 01:00:00 UTC = standard time).
* `test_utc_to_local_conversion`: Verifies offset computation (`TIMEZONE_OFFSET` and optional DST hour) and behavior when `auto_dst` is disabled.
* `test_rtclock_leap_year_dst_calculation`: Verifies algorithmic DST calculation and Sunday transition boundaries across leap years (e.g. 2028).
* `test_rtclock_year_end_rollover`: Tests calendar year-end boundary transitions (Dec 31 23:59:59 -> Jan 1 00:00:00) during UTC-to-local conversion.

### `test_scheduler`
* `test_scheduler_medio_next_slot`: Slot selection for 2-slot daily schedule across morning, midday, and night rollovers.
* `test_scheduler_intenso_next_slot`: Slot selection for 3-slot daily schedule.
* `test_scheduler_desativado_returns_false`: Asserts `computeNext` returns `false` and zeroes output when automatic watering is disabled.
* `test_scheduler_custom_every_x_days`: Multi-day interval calculations (`EVERY_X_DAYS`) against arbitrary reference day epochs.
* `test_scheduler_cycle_expired_check`: Hard maximum duration validation (8h/12h limits), backwards clock detection, and next-cycle overlap safety gaps.
* `test_scheduler_suspension_auto_wake`: Verifies automatic clearance of `gState.suspended` when `now.unix >= suspended_until`.
* `test_scheduler_month_end_and_leap_year_rollover`: Calendar arithmetic across Jan 31 → Feb 1, Feb 28 → Mar 1 (non-leap year), and Feb 28 → Feb 29 (leap year 2028).
* `test_scheduler_on_mode_changed_immediate_update`: Verifies instant recomputation of `next_hour`/`next_min` when the operating mode changes.
* `test_scheduler_fraco_next_slot`: Verifies single-slot daily schedule (08:00) slot selection before and after the trigger point.
* `test_scheduler_dow_mask`: Verifies custom day-of-week mask scheduling (e.g. Mon/Wed/Fri) with rollover to subsequent scheduled days.
* `test_scheduler_dst_fallback_duplicate_block`: Verifies 2-hour duplicate suppression guard prevents duplicate auto-cycles during autumn DST fallback.
* `test_scheduler_on_watering_done_advances_slot`: Verifies that cycle completion notification immediately recomputes and advances `next_hour`/`next_min` to the subsequent slot.
* `test_scheduler_exact_minute_boundary`: Asserts that `computeNext` evaluates boundary minutes (e.g. 08:00:00) as already triggered and selects the subsequent slot.

### `test_storage`
* `test_storage_export_hex_length_and_format`: Verifies exported string length equals `sizeof(AppConfigBlob) * 2` (68 bytes) with valid uppercase hexadecimal representation.
* `test_storage_export_and_import_roundtrip`: End-to-end serialization and deserialization integrity across state, schedules, and zone durations.
* `test_storage_import_rejects_invalid_inputs`: Defensive rejection of null pointers, incorrect lengths, non-hex characters, and out-of-bounds mode enumerations.
* `test_storage_recovery_state_roundtrip`: Verifies persistence and recovery of packed `RecoveryState` binary struct.
* `test_storage_save_and_load_persistence`: Verifies NVS flash persistence cycle via the simulated Preferences backend.
* `test_storage_rejects_corrupted_boolean_bytes`: Validates `_blobHasValidBooleans` rejection when boolean bytes exceed value 1.
* `test_storage_duration_clamping_on_import`: Verifies durations exceeding 20 minutes are clamped to default value 8.
* `test_storage_import_rejects_version_mismatch`: Verifies defensive rejection of hex imports containing unrecognised binary schema versions.
* `test_storage_clear_wipes_all`: Verifies that `storage.clear()` purges the NVS namespace and causes subsequent loads to fail cleanly.
* `test_storage_history_cache_persistence`: Validates direct NVS persistence and retrieval of `HistoryEntry` RAM cache buffers and line counts.

### `test_history`
* `test_history_entry_to_line_format`: Validates ISO 8601 formatting (`YYYY-MM-DDTHH:MM,<trigger>,d0,d1,d2,d3`) across `AUTO` ('A'), `MANUAL` ('M'), and `CUSTOM` ('C') triggers.
* `test_history_line_to_entry_valid`: Validates parsing of standard CSV lines into `HistoryEntry` records.
* `test_history_line_to_entry_rejects_corrupted`: Validates rejection of invalid dates, out-of-bounds months (>12), hours (>23), minutes (>59), and non-standard delimiters.
* `test_history_record_and_read_last`: Verifies RAM cache FIFO ordering, line count tracking, and exclusion of `TEST` ('T') triggers.
* `test_history_rotation_triggered_at_capacity`: Verifies that exceeding `HISTORY_MAX_ENTRIES` (1500) initiates asynchronous rotation, discarding `HISTORY_MAX_ENTRIES / 10` oldest records while retaining latest.
* `test_history_rejects_missing_columns`: Validates parser resilience on partial CSV lines (zero-padding missing zones) and rejection of malformed lines missing required timestamps or triggers.
* `test_history_cache_fifo_eviction`: Verifies that recording beyond `HISTORY_DISPLAY` (3) entries evicts the oldest entry in the RAM cache while maintaining correct temporal ordering.
* `test_history_deferred_queue_when_busy`: Verifies that history records triggered while disk rotation or export is active are queued and not dropped.
* `test_history_export_stream`: Verifies streaming export state machine, serial line emission, and return to idle.

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
* `test_terminal_export_and_import_config_commands`: Verifies CLI configuration export to hex, chunked input parsing of `import_config`, UI synchronization, and validation error messages.
* `test_terminal_export_history_command`: Verifies CLI history export command behavior on empty and populated logs.
* `test_terminal_unknown_and_case_insensitive_commands`: Verifies unknown command dispatch, case insensitivity (`HeLp`), and whitespace tolerance.

### `test_watering`
* `test_watering_general_builds_queue_and_skips_disabled`: Queue compilation validating that disabled zones and zones with `duration_min == 0` are excluded.
* `test_watering_custom_selection`: Selective zone execution with runtime duration overrides.
* `test_watering_dead_time_and_relay_activation`: Verifies relay dead-time enforcement (50ms where all pins remain `HIGH`) before activating zone relay (`LOW`).
* `test_watering_emergency_stop`: Immediate hardware deactivation (all pins forced `HIGH`), queue purging, and state synchronization.
* `test_watering_test_run_does_not_log_to_history`: Verifies 30-second zone test runs are excluded from persistent history logging.
* `test_watering_blackout_recovery_resumes_interrupted_cycle`: Verifies reboot recovery loads persisted `RecoveryState`, validates expiration window, and resumes from interrupted queue position.
* `test_watering_multi_zone_sequence_and_history_logging`: Verifies full end-to-end multi-zone execution: dead-time, valve activation, inter-zone delay (`ZONE_WAIT_DELAY_MS` = 5000ms), subsequent zone execution, cycle completion, history record logging, and recovery state clearance.
* `test_watering_emergency_stop_partial_duration_recorded`: Verifies that aborting a cycle after >= 30 seconds records rounded duration to history, while aborting under 30 seconds records 0 duration.
* `test_watering_empty_selection_does_not_activate`: Verifies controller refuses activation when all zones are disabled or zero-duration.

---

## 4. Usage & Execution Commands

```bash
# Execute entire test suite (all 65 test cases)
pio test -e native

# Execute specific test suite
pio test -e native -f test_encoder
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

---

## 6. IDE & CI/CD Automation

### VS Code Integration
* **Status Bar Architecture**: The PlatformIO VS Code extension's bottom toolbar actions (Build, Upload, Test) target the active environment selected in the Environment Switcher (`$(chip)` in the status bar). `default_envs = esp32dev` keeps Build/Upload pointing to the microcontroller by default.
* **Embedded Test Safety**: `test_ignore = *` is configured on `[env:esp32dev]`. If tests are triggered while `esp32dev` is active, PlatformIO skips test discovery without attempting to flash or connect via serial.
* **Execution Options in VS Code**:
  1. **VS Code Test Task**: Run `Tasks: Run Test Task` (or custom keybinding). Configured in `.vscode/tasks.json` as the default test task targeting `pio test -e native`.
  2. **Activity Bar Testing Panel**: Open the beaker icon on the left Activity Bar to view and run individual or all test suites under `native`.
  3. **PlatformIO Project Tasks Panel**: Open the PlatformIO alien icon -> `native` -> `Advanced` / `General` -> `Test`.
  4. **Status Bar Switcher**: Click the environment name in the bottom status bar, select `env:native` to run tests via the status bar beaker icon, then switch back to `Default` or `esp32dev` for firmware compilation and flashing.

### CI/CD Pipeline (GitHub Actions)
Continuous integration is defined in `.github/workflows/ci.yml` and executes on every push to active branches (`main`, `master`, `test-suite`, `migration/*`) and pull requests:
* **Job 1 (`test-native`)**: Sets up Python, installs PlatformIO, restores package cache, and executes all native unit tests via `pio test -e native` on an `ubuntu-latest` runner.
* **Job 2 (`build-esp32`)**: Restores toolchain/platform cache and verifies full embedded firmware compilation via `pio run -e esp32dev` on an `ubuntu-latest` runner.

---

## 7. Test Case Necessity & Redundancy Audit

A comprehensive evaluation across all 65 test cases was conducted to verify test uniqueness, boundary coverage, and eliminate dead assertions:

| Suite | Tests | Analysis & Redundancy Assessment | Verdict |
|---|---|---|---|
| `test_appstate` | 3 | Checks compile-time struct alignment/padding (`sizeof`), uninitialized state defaults, and static mode schedule definitions. Does not overlap with dynamic scheduling. | **100% Essential** |
| `test_encoder` | 3 | Covers 50ms pushbutton debounce, hold/release transitions, delta reset-on-read, ±127 clamping with remainder, and 2000µs ISR contact bounce rate limiting. Each test exercises a distinct state machine branch. | **100% Essential** |
| `test_rtclock` | 6 | Covers non-boundary seasonal months, March spring-forward (01:00 UTC boundary), October fall-back (01:00 UTC boundary), UTC-to-local offset arithmetic with/without DST, leap-year Sunday calculation (2028), and year-end rollover (Dec 31 → Jan 1). | **100% Essential** |
| `test_scheduler` | 13 | Covers 1-slot, 2-slot, and 3-slot daily schedules, disabled mode guard, custom multi-day interval modular math, blackout duration expiration, auto-unsuspend, calendar month/leap-year rollovers, instant mode recomputation, day-of-week bitmasks, autumn DST fallback duplicate protection, cycle completion notification, and the exact minute `<=` boundary guard. Minor apparent overlap between `medio`, `intenso`, and `fraco` serves specifically to protect loop boundary logic against 1-element, 2-element, and 3-element off-by-one errors. | **100% Essential** |
| `test_storage` | 10 | Covers hex string formatting, end-to-end round-trip, invalid hex/null rejection, recovery state binary persistence, NVS simulated cycles, corrupted boolean byte rejection, duration clamping, schema version guard, NVS namespace purge, and history cache buffer persistence. | **100% Essential** |
| `test_history` | 9 | Covers CSV line serialization for all trigger types, CSV deserialization, 7 corruption rejection modes, RAM FIFO queue eviction, capacity rotation state machine (1500 lines), missing column padding, non-blocking deferred queueing during I/O, and streaming serial export. | **100% Essential** |
| `test_terminal` | 12 | Covers CLI commands (`help`, `status`, `set_time`, `reboot`, `i2c_scan`, `export_config`, `import_config`, `export_history`), 2-step confirmation flows (`y`/`n`), ANSI cursor sequence stripping, 256-byte chunked buffer overflow protection, and case/whitespace tolerance. | **100% Essential** |
| `test_watering` | 9 | Covers general queue compilation (skipping disabled/0-min zones), custom duration selection, relay dead-time enforcement, emergency stop hardware off, test run history exclusion, blackout recovery queue resume, multi-zone sequencing with inter-zone delays, partial duration rounding (>=30s vs <30s), and empty selection rejection. | **100% Essential** |

**Conclusion**: All 65 test cases are strictly necessary. None are redundant duplicates. Every test exercises a distinct contract, boundary condition, failure mode, or hardware safety rule.

---

## 8. Performance & Speed Optimization

### Benchmark Profile
* **Runtime Execution Time**: ~15–30 ms across all 65 test cases combined.
* **Compilation Time**: ~5–8 s when compiling 8 distinct native binaries sequentially.

### Applied Optimizations
1. **Bulk Buffer Mock File Population (`test_history`)**:
   - In `test_history_rotation_triggered_at_capacity`, 1500 repeated `File::println()` calls with incremental dynamic allocations have been replaced by a single reserved `std::string` buffer moved directly into `LittleFS.files[HISTORY_FILE]`.
   - Setup time for the capacity rotation test drops from ~100 ms to <1 ms (>99% reduction).
2. **Compiler Inter-Process Pipelining (`platformio.ini`)**:
   - Added `-pipe` flag to `[env:native]`. Avoids writing intermediate temporary assembly files to disk between compiler stages, accelerating compilation on Windows file systems.
3. **Comprehensive Mock Reset (`resetAllTestMocks`)**:
   - Centralized `Wire.reset()`, `ESP.reset()`, and `_log_suspended = false` inside `resetAllTestMocks()`. Removes ad-hoc manual resets in individual test suites and ensures cross-test isolation.
4. **Buffer Safe Serial Printf (`MockSerial::printf`)**:
   - Added size bounds clamping to prevent heap/stack over-read when formatting strings approaching 512 bytes.

