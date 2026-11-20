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
│   ├── hal/gpio_ll.h       # ESP-IDF IRAM-safe low-level GPIO shim
│   ├── soc/gpio_struct.h   # ESP-IDF GPIO register struct stub
│   ├── TestCommon.h        # Common test harness definitions
│   └── TestCommon.cpp      # Global mock instances and resetAllTestMocks()
├── test_appstate/          # Memory layout, alignment, and static schedules
├── test_encoder/           # Rotary encoder ISR debounce, click detection, delta clamping
├── test_history/           # CSV serialization, rotation, cache FIFO, deferred queuing, export
├── test_rtclock/           # European DST transitions, leap years, and UTC-to-local conversion
├── test_scheduler/         # Slot computation, patterns (DAILY, DOW_MASK, EVERY_X_DAYS), DST guards
├── test_soak/              # 32-bit millis() wraparound, 520-day soak, double-watering matrix, fault injection
├── test_storage/           # Hex config codec, validation, NVS persistence, cache round-trip
├── test_terminal/          # Serial CLI tokenizer, ANSI filter, buffer protection, commands
├── test_ui/                # Display shadow buffer, MenuBuilder, Setup Wizard, UI screens & actions
└── test_watering/          # Valve sequencing, relay dead-time, stop, recovery, multi-zone runs
```

---

## 2. Mock Layer Implementation (`test/mocks/`)

| Mock File | Implemented Functionality | State Reset Mechanism |
|---|---|---|
| `Arduino.h` | <ul><li>Time tracking: `millis()`, `micros()`, `delay()`, `setMockMillis()`, `advanceMockMillis()`, `setMockMicros()`, `advanceMockMicros()`</li><li>GPIO: Pin array `_mock_gpio[64]`, `pinMode()` (`OUTPUT`, `INPUT`, `INPUT_PULLUP`, `OUTPUT_OPEN_DRAIN`), `digitalWrite()`, `digitalRead()`</li><li>Stream I/O: `MockSerial Serial` with input/output string buffers, formatted `printf()`, chunked `read()`, buffer bounds checking</li><li>Interrupts: `digitalPinToInterrupt()`, `attachInterrupt()`, `detachInterrupt()`, `FALLING`, `RISING`, `CHANGE`</li><li>Utilities: `strlcpy()`, `min()`, `max()`, `constrain()`, `abs()`, `String` adapter with indexing and slicing</li><li>Hardware stubs: `IRAM_ATTR`, `EspClass ESP` with `restart()` tracking and `reset()`</li><li>Critical sections: `portMUX_TYPE`, `portENTER_CRITICAL()`, `portEXIT_CRITICAL()`</li></ul> | `_mock_millis = 1000`<br>`_mock_micros = 1000000`<br>`memset(_mock_gpio, HIGH, 64)`<br>`Serial.reset()`<br>`ESP.reset()` |
| `RTClib.h` | <ul><li>Pure algorithmic `DateTime` and `TimeSpan` matching Adafruit RTClib interface</li><li>Leap year determination, days-in-month calculations, day-of-week calculation</li><li>Arithmetic operators (`+`, `-`, `<`, `==`)</li><li>Configurable `RTC_DS3231` (`mockPresent`, `mockLostPower`, `mockI2cReadFail`, `mockAdjustFail`, `mockRawOverride`/`mockRawStatus`/`mockRawTime`, `adjust()`, `now()`, `resetMock()`) wired to DS3231 I2C register emulation on `Wire`</li></ul> | `resetMock()` / `RTClock()` |
| `Preferences.h` | <ul><li>In-memory NVS key-value store: `std::map<std::string, std::map<std::string, std::vector<uint8_t>>>`</li><li>Binary blob access: `getBytes()`, `putBytes()`</li><li>Type-specific access: `getUShort()`, `putUShort()`, `getUInt()`, `putUInt()`, `getBool()`, `putBool()`</li><li>Namespace management: `begin()`, `end()`, `clear()`</li></ul> | `Preferences::resetStorage()` |
| `FS.h` / `LittleFS.h` | <ul><li>In-memory file table: `std::map<std::string, std::string>`</li><li>Stream operations: `seek()` (`SeekSet`, `SeekCur`, `SeekEnd`), `read()`, `println()`, `flush()`, `available()`, `size()`</li><li>Filesystem operations: `open()`, `remove()`, `rename()`, `exists()`, `begin(formatOnFail)`, `format()`, `end()`, plus `mountFail`, `writeFail`, `partialWriteFail`, `readFail`, `formatFail`, `removeFail`, and `bufferedSize` (pre-flush `VFSFileImpl::size()` emulation)</li></ul> | `LittleFS.reset()` |
| `Wire.h` | <ul><li>I2C address detection simulation via `detectedAddresses` vector plus slave register-transaction callbacks (`setSlaveHandlers`, `write()`, `requestFrom()`, `read()`) powering real DS3231 raw BCD tests without `#ifdef UNIT_TEST` in `src/`</li><li>`begin()`, `begin(int, int)`, `end()`, `setTimeOut()`, `beginTransmission()`, `endTransmission()` return codes</li><li>Bus address reset: `reset()`</li></ul> | `Wire.reset()` (via `resetAllTestMocks()`) |
| `LiquidCrystal_I2C.h` | <ul><li>20×4 character grid buffer (`screen[4][21]`, `getRow()`) with cursor tracking</li><li>Fault injection (`mockI2cError`, `lastError()`, `clearError()`) and wire-init tracking (`init(bool)`, `initWireCalls`)</li><li>Call counters (`printCalls`, `clearCalls`) and state flags (`backlightState`, `displayState`) for `init()`, `clear()`, `setCursor()`, `print()`, `backlight()`, `noBacklight()`, `display()`, `noDisplay()`</li></ul> | `Display()` / `clear()` |
| `esp_task_wdt.h` | <ul><li>No-op stubs: `esp_task_wdt_reset()`, `esp_task_wdt_add()`, `esp_task_wdt_delete()`</li></ul> | Stateless |
| `esp_timer.h` | <ul><li>High-resolution microsecond timer stub: `esp_timer_get_time()` backed by mock `micros()`</li></ul> | Tied to `_mock_micros` |
| `driver/gpio.h`, `hal/gpio_ll.h`, `soc/gpio_struct.h` | <ul><li>ESP-IDF GPIO query shims: `gpio_get_level()` and IRAM-safe `gpio_ll_get_level(&GPIO, pin)` backed by mock `digitalRead()`</li></ul> | Tied to `_mock_gpio` |

---

## 3. Test Suites Technical Reference

### `test_appstate`
* `test_struct_sizes_and_alignment`: Validates compile-time size and packed layout invariants (`sizeof(HistoryEntry) == 12`, `sizeof(RecoveryState) == 36` with `offsetof(RecoveryState, crc16) == 34`, `sizeof(AppConfigBlob) == 36`).
* `test_init_app_state_defaults`: Verifies `initAppState()` establishes default zone mappings, durations, disabled modes, and initial sentinel values.
* `test_mode_schedules_table`: Validates static `MODE_SCHEDULES` matrix against compile-time configuration defines (`INTENSO`, `MEDIO`, `FRACO`, `DESATIVADO`, `PERSONALIZADO`) and human-readable serial name helpers (`i2cDeviceName`, `serialTriggerName`, `serialModeName`).
* `test_calendar_and_custom_schedule_helpers`: Verifies shared `daysInMonth()`, `computeMinCustomSpacing()`, `sortCustomSlots()`, `hasCustomSlotOverlap()` (including midnight wrap), and `respaceCustomSlots()` (including trailing slot zeroing).

### `test_encoder`
* `test_encoder_click_debounce_and_single_trigger`: Verifies 50ms pushbutton debounce, single click emission per physical press, immunity to held buttons, and release transition handling.
* `test_encoder_rotation_delta_and_clamping`: Tests positive/negative rotation tracking via ISR, delta consumption on read, and clamp-and-preserve arithmetic at ±127 bounds.
* `test_encoder_isr_rate_limiting`: Verifies 2000µs ISR debounce filter drops pulses arriving faster than 2ms.
* `test_encoder_begin_resets_isr_and_accepts_early_boot_pulse`: Verifies `begin()` clears stale deltas, accepts immediate boot-time pulses (`micros() < 2000`) without unsigned underflow, and logs a warning if the pushbutton is held LOW at startup.

### `test_rtclock`
* `test_eu_dst_winter_and_summer_months`: Tests DST state classification across non-boundary months.
* `test_eu_dst_march_transition_boundary`: Tests boundary conditions on the last Sunday of March (00:59:59 UTC = standard time; 01:00:00 UTC = daylight saving time).
* `test_eu_dst_october_transition_boundary`: Tests boundary conditions on the last Sunday of October (00:59:59 UTC = daylight saving time; 01:00:00 UTC = standard time).
* `test_utc_to_local_conversion`: Verifies offset computation (`TIMEZONE_OFFSET` and optional DST hour) and behavior when `auto_dst` is disabled.
* `test_rtclock_leap_year_dst_calculation`: Verifies algorithmic DST calculation and Sunday transition boundaries across leap years (e.g. 2028).
* `test_rtclock_year_end_rollover`: Tests calendar year-end boundary transitions (Dec 31 23:59:59 -> Jan 1 00:00:00) during UTC-to-local conversion.
* `test_rtclock_throttle_after_internal_set`: Verifies internal clock setting/recovery maintains the 30-second read throttle interval and prevents RTC bus saturation.
* `test_rtclock_local_to_utc_roundtrip_and_ambiguous_hour`: Verifies `localToUtc()` winter/summer round-trips and October fall-back repeated-hour disambiguation via `afterUtc`.
* `test_rtclock_midrun_begin_preserves_ram_and_restores_lost_power`: Verifies mid-run `begin()` (during I2C recovery) preserves valid RAM time and restores RTC hardware after power loss.
* `test_rtclock_refresh_local_time_on_dst_toggle`: Verifies immediate local hour recalculation when `auto_dst` is toggled.
* `test_rtclock_initial_lost_power_preserves_osf_and_rejects_invalid_set`: Verifies initial boot preserves DS3231 OSF when RAM is invalid and `set()` validates calendar bounds & hardware presence.
* `test_rtclock_hotplug_validates_reading_and_tracks_revision`: Verifies hotplug keeps `rtc_valid` false on invalid hardware dates and `set()` increments `revision()`.
* `test_rtclock_battery_warning_dedup_export_deferral_and_hotplug_logs`: Verifies `_batRecoveryWarned` deduplication across repeated 30s polls with persistent `lostPower()`, hardware poll deferral while CSV export is active (`_log_suspended`), hotplug `revision()` increment and `HOT_RESTORED`/`BAT_LOW` logs, and deduplicated `set()` logs.
* `test_rtclock_manual_revision_only_tracks_valid_sets`: Verifies `manualRevision()` increments only on valid `set()` calls (hardware and RAM fallback) and stays invariant on invalid dates, `begin()`, hotplug sync/OSF restore, and >300s clock drift.
* `test_rtclock_i2c_read_fail_blocks_spurious_adjust_and_hotplug_corrupt_guard`: Verifies failed I2C status/time reads do not trigger spurious `adjust()` writes or corrupt hotplug overwrites.
* `test_rtclock_battery_low_latch_periodic_recheck_and_clear`: Verifies `isBatteryLow()` latches on OSF, re-checks periodically, and clears cleanly once time is saved to hardware.
* `test_rtclock_adjust_verify_bcd_12h_0xff_and_no_sw_fallback`: Exercises real DS3231 raw Wire/BCD decoding (12-hour AM/PM conversion, `0xFF` floating-bus fault rejection vs genuine OSF), `_adjustAndVerify()` write-value verification (`<= 2 s`), `allowSwFallback = false` non-mutation on hardware failure, immediate post-`set()` poll suppression, and `suspended_until` `4102444799UL` clamping.

### `test_scheduler`
* `test_scheduler_medio_next_slot`: Slot selection for 2-slot daily schedule across morning, midday, and night rollovers.
* `test_scheduler_intenso_next_slot`: Slot selection for 3-slot daily schedule.
* `test_scheduler_desativado_returns_false`: Asserts `computeNext` returns `false` and zeroes output when automatic watering is disabled.
* `test_scheduler_custom_every_x_days`: Multi-day interval calculations (`EVERY_X_DAYS`) against arbitrary reference day epochs.
* `test_scheduler_cycle_expired_check`: Hard maximum duration validation (`RECOVERY_MAX_AGE_S` 2h limit), backwards clock detection, and next-cycle overlap safety gaps.
* `test_scheduler_suspension_auto_wake`: Verifies automatic clearance of `gState.suspended` when `now.unix >= suspended_until`.
* `test_scheduler_month_end_and_leap_year_rollover`: Calendar arithmetic across Jan 31 → Feb 1, Feb 28 → Mar 1 (non-leap year), and Feb 28 → Feb 29 (leap year 2028).
* `test_scheduler_on_mode_changed_immediate_update`: Verifies instant recomputation of `next_hour`/`next_min` when the operating mode changes.
* `test_scheduler_fraco_next_slot`: Verifies single-slot daily schedule (08:00) slot selection before and after the trigger point.
* `test_scheduler_dow_mask`: Verifies custom day-of-week mask scheduling (e.g. Mon/Wed/Fri) with rollover to subsequent scheduled days.
* `test_scheduler_dst_fallback_duplicate_block`: Verifies 2-hour duplicate suppression guard prevents duplicate auto-cycles during autumn DST fallback.
* `test_scheduler_on_watering_done_advances_slot`: Verifies that cycle completion notification immediately recomputes and advances `next_hour`/`next_min` to the subsequent slot.
* `test_scheduler_exact_minute_boundary`: Asserts that `computeNext` evaluates boundary minutes (e.g. 08:00:00) as already triggered and selects the subsequent slot.
* `test_scheduler_on_watering_done_does_not_skip_adjacent_minute_slot`: Verifies completing a cycle at `07:59:15` does not skip an upcoming `08:00` slot.
* `test_scheduler_cycle_expired_bounds_and_overflow_safety`: Verifies `isCycleExpired()` rejects zero or out-of-range `remaining_duration_sec` without `uint32_t` wraparound.
* `test_scheduler_last_triggers_persist_across_reboot`: Verifies `sched_tr` NVS persistence blocks duplicate DST fallback triggers across reboots.
* `test_scheduler_clock_set_clears_last_triggers`: Verifies `rtclock.manualRevision()` changes clear cached/persisted trigger locks when time is set manually.
* `test_scheduler_recovery_max_age_2h`: Verifies absolute `RECOVERY_MAX_AGE_S` (7200s / 2h) expiration ceiling (`+1s` boundary), minute-based `TXT_LOG_SCHED_REC_DUR` logging, and quiet `onModeChanged(false)` refresh.
* `test_scheduler_catchup_grace_window_and_guards`: Verifies 30-min missed-slot catch-up, duplicate suppression, manual `set()` guard, midnight wrap, and DST spring-forward skipped-hour catch-up.
* `test_scheduler_catchup_exact_boundaries_and_dst_fallback`: Verifies exact 1800s/1801s window boundary, 1-min wall jump (`120s`), continuous manual `set()`/`onModeChanged()`, `!rtc_valid`/active-recovery/`MAX_RECOVERY_RETRIES`/suspension guards, first-hour DST fall-back catch-up without double watering, and spring-forward bounds.
* `test_scheduler_catchup_patterns_and_dense_slots`: Verifies midnight-wrap catch-up across `DOW_MASK` and `EVERY_X_DAYS` (preserving `custom_ref_day`), newest-slot selection among multiple missed slots, and exact-second/dead-time collision prevention on dense slots.
* `test_scheduler_non_manual_rtc_revisions_preserve_triggers`: Verifies forward `>300 s` RTC drift, transient I2C glitch + hotplug OSF restore during October DST fall-back, and cold-boot delayed RTC hotplug all preserve `sched_tr` without double-watering.
* `test_scheduler_spring_forward_next_unix_no_zones_and_same_min_jump`: Verifies `getNextCycleUnix()` matches `_tryCatchUp()` spring-forward transition instant (`01:00 UTC`), disabled-zones exact-minute slots do not persist `sched_tr`, and exact 60-min mid-minute jumps evaluate immediately.

### `test_storage`
* `test_storage_export_hex_length_and_format`: Verifies exported string length equals `sizeof(AppConfigBlob) * 2` (72 hex chars for NVS v5 + CRC16) with valid uppercase hexadecimal representation.
* `test_storage_export_and_import_roundtrip`: End-to-end serialization and deserialization integrity across state, schedules, and zone durations.
* `test_storage_import_rejects_invalid_inputs`: Defensive rejection of null pointers, incorrect lengths, non-hex characters, and out-of-bounds mode enumerations.
* `test_storage_recovery_state_roundtrip`: Verifies persistence and recovery of packed `RecoveryState` binary struct.
* `test_storage_recovery_state_rejects_legacy_size_ver_bool_and_crc_bitflip`: Verifies `loadRecoveryState()` rejects legacy 34-byte pre-CRC blobs without mutating caller state, wrong schema versions, invalid `active` boolean bytes, and 1-bit CRC-16 flips.
* `test_storage_save_and_load_persistence`: Verifies NVS flash persistence cycle via the simulated Preferences backend.
* `test_storage_rejects_corrupted_boolean_bytes`: Validates `_blobHasValidBooleans` rejection when boolean bytes exceed value 1.
* `test_storage_duration_clamping_on_import`: Verifies `importConfigHex()` rejects bad CRC16, strictly rejects durations > 20 minutes even with valid CRC, and rejects imports while watering is active.
* `test_storage_import_rejects_version_mismatch`: Verifies defensive rejection of hex imports containing unrecognised binary schema versions.
* `test_storage_clear_wipes_all`: Verifies that `storage.clear()` purges the NVS namespace and causes subsequent loads to fail cleanly.
* `test_storage_history_cache_persistence`: Validates direct NVS persistence and retrieval of `HistoryEntry` RAM cache buffers and line counts.
* `test_storage_dormant_custom_overlap_auto_respaced_vs_active_rejected`: Verifies dormant `PERSONALIZADO` slot overlaps are auto-respaced on load when another mode is active, while active `PERSONALIZADO` overlaps are rejected.
* `test_storage_zero_duration_normalizes_enabled_false`: Verifies zones with `duration_min == 0` normalize `enabled` to `false` across save/load.
* `test_storage_nvs_load_crc_rejection_and_clamping`: Verifies `load()` rejects CRC mismatches while still defensively clamping valid-CRC out-of-range stored durations.
* `test_storage_history_blob_atomic_and_filesize`: Verifies atomic CRC16 `hblob` persistence (`lineCount`, `fileSize`), `hasHistoryData()`, and corruption rejection.
* `test_storage_last_triggers_roundtrip`: Verifies `sched_tr` timestamp array persistence and erasure on `clear()`.
* `test_storage_clear_guard_invalid_hist_cache_and_respace_logs`: Verifies `storage.clear()` failure guard when NVS is uninitialized, `loadHistoryCache()` invalid-header warning (`TXT_LOG_NVS_HIST_INV`), and dormant custom slot auto-respace warning (`TXT_LOG_NVS_CUSTOM_RESPACED`).
* `test_storage_backlight_upper_bound_import_and_load`: Verifies `importConfigHex()` rejects `backlight_timeout_ms > 3600000UL` (unless `BACKLIGHT_TIMEOUT_NEVER`) and `load()` defensively falls back to `120000UL`.

### `test_history`
* `test_history_entry_to_line_format`: Validates ISO 8601 formatting (`YYYY-MM-DDTHH:MM,<trigger>,d0,d1,d2,d3`) across `AUTO` ('A'), `MANUAL` ('M'), and `CUSTOM` ('C') triggers.
* `test_history_line_to_entry_valid`: Validates parsing of standard CSV lines into `HistoryEntry` records.
* `test_history_line_to_entry_rejects_corrupted`: Validates rejection of invalid dates, out-of-bounds months (>12), hours (>23), minutes (>59), and non-standard delimiters.
* `test_history_record_and_read_last`: Verifies RAM cache FIFO ordering, line count tracking, and exclusion of `TEST` ('T') triggers.
* `test_history_rotation_triggered_at_capacity`: Verifies that exceeding `HISTORY_MAX_ENTRIES` (1500) initiates asynchronous rotation, discarding `HISTORY_MAX_ENTRIES / 10` oldest records while retaining latest.
* `test_history_rejects_missing_columns`: Validates parser resilience on partial CSV lines (zero-padding missing zones) and rejection of malformed lines missing required timestamps or triggers.
* `test_history_cache_fifo_eviction`: Verifies that recording beyond `HISTORY_DISPLAY` (3) entries evicts the oldest entry in the RAM cache while maintaining correct temporal ordering.
* `test_history_deferred_queue_when_busy`: Verifies multi-entry (3-item) deferred records queued while disk rotation or export is active drain completely in FIFO order via `history.update()` without stranding entries #2/#3.
* `test_history_export_stream`: Verifies streaming export state machine, serial line emission, and return to idle.
* `test_history_guarded_format_on_mount_failure`: Verifies that LittleFS mount failure refuses destructive reformatting (`formatOnFail`) when cached historical entries exist in NVS storage.
* `test_history_line_to_entry_negative_and_overflow_bounds`: Verifies negative hour/minute rejection, `[0, 255]` clamping on zone durations, and null buffer safety on `readLast()`.
* `test_history_rotation_write_failure_preserves_original_file`: Verifies LittleFS write failures during rotation abort `rename()` and preserve the existing `/history.csv`.
* `test_history_corrupted_nvs_cache_rebuilds_from_fs`: Verifies corrupted NVS cache entries fall back to rebuilding the cache from LittleFS.
* `test_history_begin_detects_truncated_file_via_filesize`: Verifies `hblob` `fileSize` mismatch forces LittleFS cache rebuild.
* `test_history_pending_record_persisted_and_replayed_across_rotation`: Verifies deferred records persist to `/hist_pend.txt` during rotation, dynamic over-capacity trimming, and startup replay.
* `test_history_rejects_test_trigger_and_long_corrupt_line`: Verifies `'T'` trigger exclusion in `_lineToEntry()` and overlong line discarding in `_populateCache()`.
* `test_history_rotation_double_fail_export_suppression_and_guards`: Verifies `TXT_LOG_HIST_SKIP_NO_RTC` when `!rtc_valid`, `clear()`/`startExport()` boolean guards when `!_ready`, rotation fallback double-failure logging `TXT_LOG_HIST_FILE_ERR` without false `ROT_OPEN_FAIL`, and deferred `record()` during CSV export emitting info-only suppression summary (`TXT_LOG_EXPORT_SUPPRESSED_INFO`).
* `test_history_rotation_read_failure_preserves_original_file`: Verifies mid-rotation LittleFS read failures abort `rename()` and preserve the original `/history.csv`.
* `test_history_periodic_remount_and_force_repair`: Verifies 24h non-destructive LittleFS remount retry in `History::update()` while preserving NVS history data and explicit repair via `clear(true)`.
* `test_history_remount_watering_guard_and_clear_edge_cases`: Verifies 24h remount deferral while `gState.watering.active`, unconditional cleanup of orphaned `/hist_tmp.csv` on `clear()`, `clear(true)` `LittleFS.end()+format()` fallback when `remove()` fails while mounted, and `TXT_LOG_HIST_FORMAT_FAIL` logging on permanent format failure.
* `test_history_buffered_filesize_partial_writes_rotation_tail_and_strict_csv`: Verifies post-close `fileSize` accuracy under ESP32 buffered-stdio emulation (`LittleFS.bufferedSize`), partial-write rejection (`partialWriteFail`), rotation tail preservation when file exceeds `_lineCount`, and strict calendar/delimiter CSV parsing.

### `test_terminal`
* `test_terminal_help_command`: Help menu formatting and command presence.
* `test_terminal_status_command`: Real-time system report formatting via Serial stream.
* `test_terminal_stop_command`: Verifies `stop` command immediately aborts active watering cycles via `WateringController` and reports idle status when inactive.
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
* `test_terminal_set_time_rejects_trailing_garbage_and_plus_sign`: Verifies `set_time` rejects trailing characters, `+` prefixes, and >4-digit years.
* `test_terminal_status_truthful_when_rtc_invalid`: Verifies `status` reports suspended/awaiting-RTC states truthfully when `!gState.rtc_valid`.
* `test_terminal_reboot_deenergizes_active_relays_preserving_recovery`: Verifies `reboot` forces active relay pins `HIGH` (OFF) while preserving NVS `RecoveryState`.
* `test_terminal_pending_clear_expires_and_ctrl_c_clears_overflow`: Verifies 30s expiration of pending `clear_history` prompts and Ctrl+C recovery from overflow ignore mode.
* `test_terminal_import_active_guard_setup_sync_and_set_time_strict`: Verifies active-watering `import_config` rejection, `syncSetupState()` dispatch, strict `set_time` syntax parsing, and `refreshClockEditors()`.
* `test_terminal_midline_batch_coalesces_tail_redraw`: Verifies contiguous mid-line inserts coalesce into a single `\033[K` tail redraw.
* `test_logging_standardized_format_and_seconds_precision`: Verifies standardized `[timestamp] [LEVEL] [Component] message` formatting across uptime/RTC clocks, second-precision timestamps, `LOG_LEVEL` filtering, and exact 255-char `...` truncation cap.
* `test_logging_export_suppression_summary_and_human_modes`: Verifies CSV export log suppression counters, warning/error tracking, human-readable mode names, and the post-export suppression summary log.
* `test_terminal_sw_clock_status_pipelined_export_and_wipe_failure`: Verifies software-clock `status` (`TXT_TERM_STAT_RTC_SW`, no raw Unix timestamp), blocked `export_history` not printing `TXT_LOG_START_EXPORT`, pipelined `export_history\nstatus\n` input draining without corrupting CSV output, info-only export suppression summary, and `clear_history` failure / `clear_history force` LittleFS repair.
* `test_terminal_status_reports_and_clears_rtc_battery_warning`: Verifies `status` displays `TXT_TERM_STAT_RTC_BAT` when RTC battery failure is latched and clears it once hardware time is saved.
* `test_terminal_clear_history_force_args_timeout_and_empty_repair`: Verifies invalid `clear_history` argument rejection (`TXT_TERM_ERR_CLEAR_HIST_ARGS`), `--force` alias, `n` cancellation & 30s expiration resetting `_pendingClearForce`, and `clear_history --force` repair on an already-empty mounted FS.
* `test_terminal_phase4_export_unmounted_i2c_guard_reboot_setup_and_status_date`: Verifies `export_history` unavailability reporting on unmounted FS, `i2c_scan` active-watering rejection, `reboot` preserving Setup Wizard NVS atomicity, full `YYYY-MM-DD HH:MM` next-watering date in `status`, and expired wipe blank-line spacing.

### `test_ui`
* `test_display_delta_rows_and_nullptr_preservation_across_refresh`: Verifies shadow-buffer delta writes, `nullptr` row preservation across `recover()` and 60s full refreshes, and absence of `0xFF` corruption bytes.
* `test_display_formatting_helpers_and_null_safety`: Verifies 20-column formatting (`fx`, `cx`, `hdr`, `pbar`), percentage clamping, and `nullptr` input safety.
* `test_menubuilder_dynamic_stop_watering_and_live_menu_refresh`: Verifies dynamic `"stop_now"` injection in `MenuID::MAIN` and live removal when watering finishes while viewing the menu.
* `test_menubuilder_history_localization_and_dynamic_test_duration`: Verifies localized `TXT_HIST_TRIG_MANUAL` label, dynamic `ZONE_TEST_DURATION_S` formatting, and `|` delimiter sanitization in zone names.
* `test_ui_setup_wizard_skip_mode_advances_to_zones_not_complete`: Verifies full first-boot Setup Wizard flow and ensures skipping mode selection advances to `SETUP_ZONES` rather than skipping zone configuration.
* `test_ui_setup_missing_rtc_skip_clock_back_and_ok_flow`: Verifies missing-RTC `"skip_clock"` confirmation returns to `ScreenDateEdit` on `>Back` and advances to `SETUP_MODE` on `>OK`.
* `test_ui_info_and_done_post_enter_rotation_bounce_guard`: Verifies 350ms post-enter rotation bounce guard on `ScreenInfo`/`ScreenDone`.
* `test_ui_actions_dst_refresh_custom_respace_zero_dur_and_reset_safety`: Verifies immediate DST refresh, custom slot auto-respacing on `"sel:4"`, 0-min zone guard on `"general"`, and relay shutdown on `"reset"`.
* `test_ui_idle_screen_waiting_between_zones_and_backlight_never`: Verifies `TXT_IDLE_WAITING` rendering during inter-zone pauses and display wake-up under `BACKLIGHT_TIMEOUT_NEVER`.
* `test_display_i2c_fault_abort_cooldown_and_recovery_callback`: Verifies `Display` frame abort on I2C error, 3-error recovery threshold, frozen `_consecErrors` during the 10s recovery cooldown, `_commErrWarned` log deduplication, and wake/sleep `_i2cErr` clearance.
* `test_ui_four_cycle_horarios_and_zero_dur_overlap_guard`: Verifies 4-cycle schedule display formatting (with interval) within 20 LCD columns and `min_spacing = 1` overlap protection.
* `test_ui_clock_editor_refresh_and_freq_days_without_rtc`: Verifies `refreshClockEditors()` updates active date/time editors and `FREQ_DAYS` keeps `0xFFFFFFFFUL` when `!rtc_valid`.
* `test_ui_settings_logs_unknown_confirm_and_missing_rtc_warning`: Verifies `bl:` timeout and editor save logs (`CFG_ZONE`, `FREQ_DAYS`, `NUM_CYCLES`, `CUSTOM_CYCLE`), `"general"` no-zones warning, unknown confirm tag error screen guard, and `ScreenTimeEdit` missing-RTC warning.
* `test_display_recover_flushes_shadow_and_sync_catches_always_on_lcd_fault`: Verifies `Display::recover()` flushes valid `_shadow` rows and 1s `Display::sync()` catches LCD-only I2C faults under `BACKLIGHT_TIMEOUT_NEVER`.
* `test_ui_idle_displays_battery_low_warning_immediately_and_clears_on_set`: Verifies `ScreenIdle` renders `TXT_IDLE_RTC_BAT` immediately when battery-low latches and clears it after saving valid RTC time.
* `test_ui_immediate_save_quiet_wake_recover_backoff_and_idle_info`: Verifies immediate non-setup NVS persistence (`dispatchAction`), quiet `Display::wake()`/`recover(true)` on wake from `displayOff()`, 6h periodic LCD recovery, exponential backoff (`10s -> 60s -> 300s`), and `ScreenIdle` last-watering / history-unavailable / missing-RTC indicators.
* `test_ui_setup_atomicity_wake_shadow_backoff_throttle_and_cold_rtc_idle`: Verifies Setup Wizard NVS atomicity across `"sel:4"`, clean wake from `displayOff()` without flushing stale `_shadow`, 6h periodic recovery guards (`watering.active`, `_inSetup`, `_busFault`), `recoverI2C()` throttle coordination (`onRecoveryThrottled()`) + 4th-attempt 300s firing, and simultaneous cold-boot Row 1 (`TXT_IDLE_RTC_MISSING` / `batLow` priority) + Row 3 (`TXT_IDLE_SET_TIME`) display.
* `test_ui_phase4_rtc_fail_cycles_preserve_idle_menu_and_strict_actions`: Verifies failed LCD `rtclock.set()` no-fallback behavior, unchanged `NUM_CYCLES` slot preservation & trailing-slot zeroing, post-sort `CUSTOM_CYCLE` log index, `SUSPEND` `4102444799UL` clamp, `cancel_susp` immediate `next_*` refresh, `ScreenIdle` same-minute hour/date refresh, `ScreenMenu` shrink `_off` clamping & `PROG` live suspension refresh, `goIdle()` setup-atomicity guard, strict action index parsing, and factory reset `history.clear(true)` repair.

### `test_watering`
* `test_watering_general_builds_queue_and_skips_disabled`: Queue compilation validating that disabled zones and zones with `duration_min == 0` are excluded.
* `test_watering_custom_selection`: Selective zone execution with runtime duration overrides.
* `test_watering_dead_time_and_relay_activation`: Verifies relay dead-time enforcement (20ms where all pins remain `HIGH`) before activating zone relay (`LOW`).
* `test_watering_emergency_stop`: Immediate hardware deactivation (all pins forced `HIGH`), queue purging, and state synchronization.
* `test_watering_test_run_does_not_log_to_history`: Verifies zone test runs are excluded from persistent history logging.
* `test_watering_blackout_recovery_resumes_interrupted_cycle`: Verifies reboot recovery loads persisted `RecoveryState`, validates expiration window, and resumes from interrupted queue position.
* `test_watering_multi_zone_sequence_and_history_logging`: Verifies full end-to-end multi-zone execution: dead-time, valve activation, inter-zone delay (`ZONE_WAIT_DELAY_MS` = 5000ms), subsequent zone execution, cycle completion, history record logging, and recovery state clearance.
* `test_watering_emergency_stop_partial_duration_recorded`: Verifies that aborting a cycle after >= 30 seconds records rounded duration to history, while aborting under 30 seconds records 0 duration.
* `test_watering_empty_selection_does_not_activate`: Verifies controller refuses activation when all zones are disabled or zero-duration.
* `test_watering_recovery_rejects_corrupted_and_suspended_states`: Verifies `begin()` rejects out-of-range durations, duplicate zone indices, and `AUTO` recovery while `gState.suspended` is active.
* `test_watering_inter_zone_wait_updates_upcoming_zone_index`: Verifies `gState.watering.zone_idx` and `progress_pct` update to the upcoming zone immediately upon entering inter-zone wait.
* `test_watering_deenergize_relays_preserves_recovery_state`: Verifies `deenergizeRelays()` turns all relays OFF while leaving NVS `RecoveryState` intact.
* `test_watering_recovery_retry_cap_and_stable_reset`: Verifies `MAX_RECOVERY_RETRIES` (3) boot-loop cap aborts recovery, 2-min mid-zone NVS checkpoints persist remaining duration & accumulated history, and completing the zone resets `retry_count` to `0`.
* `test_watering_invalid_custom_duration_preserves_active_cycle`: Verifies invalid `startCustom()` arguments (`dur == 0`, `dur > 20`, `nullptr`) do not abort an active watering cycle.
* `test_watering_logs_seconds_for_test_and_clean_stop_during_deadtime`: Verifies second-precision completion/early-stop logs for sub-minute runs and ensures `stop()` during initial pre-Zone-1 relay dead-time does not log a spurious inter-zone transition.
* `test_watering_oob_deadtime_guard_cycle_done_order_and_arg_warnings`: Verifies `startCustom`/`startTest` invalid-argument warnings, out-of-bounds relay dead-time abort avoiding bogus duration/transition logs, and `TXT_LOG_CYCLE_DONE` logging before `_finishCycle()`.
* `test_watering_max_zone_cap_and_periodic_pin_refresh`: Verifies 1s periodic GPIO relay re-assertion during idle and active watering, plus hard zone runtime caps (`20 min` / `ZONE_TEST_DURATION_S`) against corrupted `_zoneDurationMs`.
* `test_watering_inter_zone_wait_pin_refresh_and_clamped_start_log`: Verifies 1s periodic `RELAY_OFF` re-assertion during the 5s `_isWaiting` inter-zone pause and clamped zone-duration logging at dead-time completion.
* `test_watering_mid_cycle_rtc_set_and_aborted_recovery_history`: Verifies cycles started without valid RTC never log bogus `2026-01-01` entries after a mid-cycle clock set, aborted valid blackout recoveries preserve already-watered partial minutes in history while corrupt blobs do not, single-zone recovery logs `TXT_LOG_REC_STABLE`, and early stop on a resumed zone logs cumulative minutes.

### `test_soak`
* `test_wraparound_49_day_millis_all_subsystems`: Sets `millis()`/`micros()` near `0xFFFFFFFF` (`49.7` days) and verifies overflow-safe subtraction across `Encoder` ISR rate-limiting/button debounce, `RTClock` software clock & 30s hardware poll throttle, `Display` 1s `sync()` / 10s–300s backoff / 6h `needsPeriodicRecover()`, `UI` idle timeout / backlight / display-off / wake across a full 32-bit sleep wrap, `Terminal` 30s `clear_history` expiry, and `WateringController` relay dead-time, idle/wait/active EMI pin refresh, inter-zone wait, zone duration, and history logging.
* `test_soak_520_days_regular_usage_all_modes_dst_leap_and_rotations`: Simulates 520 continuous days (~10x 32-bit `millis()` wraparounds) transitioning through `INTENSO`, `MEDIO`, 4-day suspension, `FRACO`, and 4-slot `PERSONALIZADO` (including `01:30` across EU fall-back DST), verifying leap day `2028-02-29`, March/October DST transitions without duplicates or skipped cycles, zero suspended auto-cycles, relay exclusivity/20-min hard safety cap at every tick, and LittleFS rotation (`>1500` entries) surviving reboot reload.
* `test_soak_double_watering_and_edge_case_matrix`: Exhaustive regression matrix verifying: (A) October DST fall-back repeated `01:30` + `01:45` reboot blocked by persisted `sched_tr`; (B) blackout spanning both `01:30` instances catches up once and blocks second reboot; (C) brownout loop hitting `MAX_RECOVERY_RETRIES` stays aborted without scheduler catch-up restart; (D) multi-zone checkpoint recovery + outage during inter-zone wait preserves exact zone durations; (E) `MANUAL` interrupted across an `AUTO` slot aborts stale manual recovery and runs `AUTO` once; (F) suspension expiring mid-minute (`08:00:30`) triggers `08:00` once while `08:15` expiry does not catch up; (G) `EVERY_X_DAYS` (interval 3) midnight-wrap catch-up maintains exact 3-day cadence; (H) cold boot with dead RTC battery inhibits `AUTO` while `export_history` defers concurrent `history.record()` and flushes upon completion; and (I) transient DS3231 disconnect + I2C hotplug reconnect does not clear `sched_tr` or trigger duplicate `AUTO` watering on a subsequent blackout within 30 min.
* `test_soak_120_days_chaotic_blackouts_and_faults`: 120-day deterministic PRNG fault-injection soak injecting random short blackouts across idle/active states, transient DS3231 disconnects with hotplug reconnect, I2C read corruption bursts, transient LCD I2C errors under `BACKLIGHT_TIMEOUT_NEVER`, and concurrent terminal/encoder activity while asserting zero double `AUTO` triggers within `<2h`, valid RTC/history state, and relay safety invariants at every step.

---

## 4. Usage & Execution Commands

```bash
# Execute fast unit suites (149 test cases across 9 suites; skips test_soak)
pio test -e native

# Execute soak & wraparound suite last, only if the 9 unit suites pass (-O2)
pio test -e native && pio test -e native_soak

# Execute specific test suite
pio test -e native -f test_encoder
pio test -e native -f test_scheduler
pio test -e native -f test_watering
pio test -e native -f test_terminal
pio test -e native -f test_storage
pio test -e native -f test_history
pio test -e native -f test_rtclock
pio test -e native -f test_appstate
pio test -e native -f test_ui
pio test -e native_soak

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

   int main(void) {
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
* **Job 1 (`test-native`)**: Sets up Python, installs pinned PlatformIO (`6.2.0`), restores package cache, executes all 9 fast unit suites in both the default configuration (`LANG_UI_PT` + `LANG_SERIAL_EN`) and alternate flags (`LANG_UI_EN` + `LANG_SERIAL_PT` + `LOG_LEVEL=LVL_NONE` + `MENU_WRAP_AROUND=0`), and only if they pass runs `pio test -e native_soak` on an `ubuntu-latest` runner.
* **Job 2 (`build-esp32`)**: Restores toolchain/platform cache and verifies full embedded firmware compilation across alternate debug flags, `LOG_LEVEL=LVL_NONE`, and the default release configuration via `pio run -e esp32dev`.

---

## 7. Test Case Necessity & Redundancy Audit

A comprehensive evaluation across all 153 test cases (149 fast unit tests + 4 longevity soak tests) was conducted to verify test uniqueness, boundary coverage, and eliminate dead assertions:

| Suite | Tests | Analysis & Redundancy Assessment | Verdict |
|---|---|---|---|
| `test_appstate` | 4 | Checks compile-time struct alignment/padding (`sizeof` & `offsetof` for NVS v5 36B structs), uninitialized state defaults, static mode schedule definitions, human-readable serial name helpers (`i2cDeviceName`, `serialTriggerName`), and shared calendar & custom-schedule helpers (`daysInMonth`, `computeMinCustomSpacing`, `sortCustomSlots`, `hasCustomSlotOverlap`, `respaceCustomSlots`). | **100% Essential** |
| `test_encoder` | 4 | Covers 50ms pushbutton debounce, hold/release transitions, delta reset-on-read, ±127 clamping with remainder, 2000µs ISR contact bounce rate limiting (exercising IRAM-safe `gpio_ll_get_level`), early-boot ISR acceptance after `begin()`, and stuck-button boot warning. | **100% Essential** |
| `test_rtclock` | 17 | Covers seasonal months, March/October DST boundaries, UTC/local round-trips, October repeated-hour disambiguation, leap-year Sundays, year-end rollover, 30s throttle preservation, mid-run `begin()` RAM preservation/power-loss restore, instant DST toggle refresh, initial OSF preservation, `set()` validation & log dedup, hotplug validation/`revision()`/logs, `manualRevision()` isolation, `_batRecoveryWarned` dedup, CSV export poll deferral, verified I2C status/time read guards against corrupt-read hotplug overwrite, `isBatteryLow()` latch/periodic re-check/clear lifecycle, and real DS3231 Wire/BCD decoding (12h AM/PM, `0xFF` fault rejection, `_adjustAndVerify` value check, `allowSwFallback = false`, `suspended_until` clamp). | **100% Essential** |
| `test_scheduler` | 23 | Covers 1/2/3-slot daily schedules, disabled guard, custom multi-day modular math, blackout expiration (2h `+1s` cap & minute-based ceiling duration logging) & overflow bounds, auto-unsuspend, month/leap-year rollovers, instant mode recomputation & quiet `onModeChanged(false)`, DOW bitmasks, NVS-persisted autumn DST duplicate suppression across reboots, `rtclock.manualRevision()` trigger clearing (preserving `sched_tr` across forward RTC drift, DST hotplug glitches, and cold-boot delayed hotplug), unified spring-forward `getNextCycleUnix()`/`_tryCatchUp()` resolution, disabled-zones NVS write suppression, exact 60-min mid-minute jump evaluation, adjacent-minute slot preservation on completion, exact minute boundary guard, and 30-min missed-slot/DST catch-up window (`1800s`/`1801s` boundaries, 1-min skips, DST fall-back/spring-forward, `DOW_MASK`/`EVERY_X_DAYS`, dense slots). | **100% Essential** |
| `test_storage` | 18 | Covers v5 72-char hex codec, end-to-end round-trip, invalid input & strict import bounds/CRC rejection, active-watering import guard, `backlight_timeout_ms` upper-bound (`<= 3600000UL` / `NEVER`) import & defensive load fallback, recovery state binary persistence (`retry_count`, CRC-16, legacy-size/version/bool/CRC rejection), NVS load CRC failure & defensive clamping, corrupted boolean byte rejection, zero-duration `enabled` normalization, dormant vs active custom overlap handling & respacing log, schema version guard, namespace purge & uninitialized `clear()` guard, atomic `hblob` persistence (`fileSize`) & invalid-header log, and `sched_tr` round-trip. | **100% Essential** |
| `test_history` | 21 | Covers CSV serialization/deserialization, strict calendar/delimiter/non-numeric & negative/overflow bounds, `'T'` trigger exclusion, overlong line discard, RAM FIFO eviction, capacity rotation with dynamic trimming & tail preservation, post-close `fileSize` under ESP32 buffered-stdio emulation, partial-write rejection, multi-entry FIFO deferred queue draining via `update()`, write-failure & mid-rotation read-failure atomic preservation, fallback double-failure logging, missing column padding, `/hist_pend.txt` persistence & startup replay, streaming serial export & info-only suppression summary, guarded LittleFS format, 24h periodic remount retry (deferred during active watering) & `clear(true)` mounted/unmounted FS repair (`end()+format()`, `/hist_tmp.csv`, format-fail guard), `fileSize` cache verification, corrupted NVS cache fallback, and `!_ready`/`!rtc_valid` guards. | **100% Essential** |
| `test_terminal` | 25 | Covers CLI commands (`help`, `status` with `YYYY-MM-DD HH:MM` next-watering date, `set_time`, `stop`, `reboot` with Setup Wizard NVS atomicity, `i2c_scan` with active-watering guard, `export_config`, `import_config`, `export_history` including unmounted-FS error reporting, `clear_history [force]`), strict `set_time` token validation, active-watering import block, `syncSetupState()`/`refreshClockEditors()` hooks, truthful no-RTC, software-clock, and RTC battery-warning `status`, relay de-energizing on `reboot`, confirmation flows (`y`/`n`/30s expiry with blank-line spacing & `_pendingClearForce` reset, invalid arg rejection, `--force` alias, empty-FS `clear_history force` & failure repair), ANSI filtering, batched mid-line tail redraw coalescing, 256-byte overflow protection & Ctrl+C reset, case/whitespace tolerance, standardized log formatting/255-char truncation cap, and CSV export log suppression & pipelined input draining. | **100% Essential** |
| `test_ui` | 18 | Covers `Display` delta shadow buffer, `nullptr` row preservation across refreshes, wake without stale shadow flush, quiet/immediate `recover()`, 6h periodic re-init & active-watering/setup/fault guards, 1s `sync()` Always-On LCD fault detection, I2C fault abort, exponential recovery backoff (`10s -> 60s -> 300s` incl. 4th attempt & shared `recoverI2C()` throttle coordination), `_commErrWarned` dedup, wake/sleep `_i2cErr` clearance, immediate NVS save on non-setup edits with Setup Wizard NVS atomicity (including `goIdle()` guard), formatting helpers, `MenuBuilder` dynamic `"stop_now"` & live `MAIN`/`PROG` menu updates with shrink `_off` clamping, i18n/sanitization, Setup Wizard flows, LCD failed-write RTC no-fallback guard, unchanged `NUM_CYCLES` slot preservation & trailing-slot zeroing, post-sort `CUSTOM_CYCLE` log index, `SUSPEND` `4102444799UL` clamp, `cancel_susp` immediate `next_*` refresh, 4-cycle `horarios` 20-col formatting, `min_spacing` overlap guard, clock editor refresh, `FREQ_DAYS` missing-RTC guard, strict action index parsing, factory reset `history.clear(true)` repair, 350ms bounce guard, UI settings/action logs, unknown confirm tag error screen, and `ScreenIdle` status including same-minute hour/date refresh, last watering, history error, cold-boot/mid-run missing RTC, and battery warning priority. | **100% Essential** |
| `test_watering` | 19 | Covers queue compilation, custom selection & invalid-arg active-cycle safety/warnings, relay dead-time & OOB dead-time abort safety, emergency stop, test-run history exclusion, mid-cycle clock-set bogus-`2026-01-01` history guard, blackout recovery, aborted-recovery partial history logging (excluding corrupt blobs), retry cap (`MAX_RECOVERY_RETRIES`), periodic 2-min NVS recovery checkpoints, hard zone runtime cap & clamped activation logging, periodic GPIO refresh across idle/active/5s inter-zone pauses, zone-completion retry reset & single-zone `TXT_LOG_REC_STABLE` logging, cumulative early-stop logging on resumed zones, corrupted/suspended recovery rejection, multi-zone sequencing & `TXT_LOG_CYCLE_DONE` log order, inter-zone upcoming index sync, partial duration rounding, sub-minute second-precision & dead-time stop logging, empty selection guard, and `deenergizeRelays()`. | **100% Essential** |
| `test_soak` | 4 | Full-system integration & longevity suite (`env:native_soak`, `-O2`) testing 32-bit `millis()`/`micros()` rollover across all subsystems, a 520-day continuous multi-mode/DST/leap-year/history-rotation soak, a 9-scenario double-watering & edge-case matrix, and a 120-day chaotic blackout & I2C fault-injection soak. | **100% Essential** |

**Conclusion**: All 153 test cases are strictly necessary. None are redundant duplicates. Every test exercises a distinct contract, boundary condition, failure mode, or hardware safety rule.

---

## 8. Performance & Speed Optimization

### Benchmark Profile
* **Runtime Execution Time**: ~20–35 ms across the 149 unit test cases (`env:native`), plus ~2.9 s execution for `test_soak` under `[env:native_soak]` (`-O2`).
* **Compilation + Execution Time**: ~30 s for all 9 fast unit suites in `env:native`, plus ~12 s for `env:native_soak`.

### Applied Optimizations
1. **Isolated `-O2` Soak Environment & Conditional Chaining (`platformio.ini`, `ci.yml`)**:
   - `[env:native]` skips `test_soak` via `test_ignore = test_soak` so the 9 unit suites compile quickly at default `-O0`.
   - `[env:native_soak]` extends `env:native` with `test_filter = test_soak` and `-O2`, cutting `test_soak` binary execution from ~17.6 s to ~2.9 s and allowing CI (`ci.yml`) and CLI workflows (`&&`) to gate soak execution behind unit-test success.
2. **Adaptive DST-Window & 30 s Idle Stepping (`test_soak`)**:
   - Uses `30000 ms` idle steps (hitting `:00` / `:30` of every minute and matching the 30 s RTC poll cadence) and restricts `1000 ms` midnight/DST boundary scans to March and October months, eliminating ~3.5M redundant idle ticks without reducing schedule, DST, or fault coverage.
3. **Bulk Buffer Mock File Population (`test_history`)**:
   - In `test_history_rotation_triggered_at_capacity`, 1500 repeated `File::println()` calls with incremental dynamic allocations have been replaced by a single reserved `std::string` buffer moved directly into `LittleFS.files[HISTORY_FILE]`.
   - Setup time for the capacity rotation test drops from ~100 ms to <1 ms (>99% reduction).
4. **Compiler Inter-Process Pipelining (`platformio.ini`)**:
   - Added `-pipe` flag to `[env:native]`. Avoids writing intermediate temporary assembly files to disk between compiler stages, accelerating compilation on Windows file systems.
5. **Comprehensive Mock Reset (`resetAllTestMocks`)**:
   - Centralized `Wire.reset()`, `ESP.reset()`, `_log_suspended = false`, and `_log_suppressed_* = 0` inside `resetAllTestMocks()`. Removes ad-hoc manual resets in individual test suites and ensures cross-test isolation.
6. **Buffer Safe Serial Printf (`MockSerial::printf`)**:
   - Added size bounds clamping to prevent heap/stack over-read when formatting strings approaching 512 bytes.

