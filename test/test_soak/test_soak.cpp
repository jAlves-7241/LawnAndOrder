#define HAS_TERMINAL_AWARE_LOG 1
#include <unity.h>
#include <new>
#include <vector>
#include "TestCommon.h"
#include "../../test/mocks/TestCommon.cpp"
#include "../../src/AppState.cpp"
#include "../../src/Storage.cpp"

#define private public
#include "RTClock.h"
#include "History.h"
#include "WateringController.h"
#include "Scheduler.h"
#include "Display.h"
#include "Encoder.h"
#include "UI.h"
#include "Terminal.h"
#undef private

Display display;
static Encoder encoder(PIN_CLK, PIN_DT, PIN_SW);
UI ui(display, encoder);

#include "../../src/RTClock.cpp"
#include "../../src/History.cpp"
#include "../../src/WateringController.cpp"
#include "../../src/Scheduler.cpp"
#include "../../src/Display.cpp"
#include "../../src/Encoder.cpp"
#include "../../src/ui/MenuBuilder.cpp"
#include "../../src/ui/ScreenCommon.cpp"
#include "../../src/ui/ScreenEditors.cpp"
#include "../../src/ui/ScreenIdle.cpp"
#include "../../src/ui/ScreenMenu.cpp"
#include "../../src/ui/ScreenSetup.cpp"
#include "../../src/UI.cpp"
#include "../../src/Terminal.cpp"

// ─────────────────────────────────────────────────────────
// Simulation helpers
// ─────────────────────────────────────────────────────────

static void resetSystem(bool wipeStorage = true) {
    resetAllTestMocks();
    if (!wipeStorage) {
        // Keep NVS/LittleFS contents if simulating a reboot; resetAllTestMocks wiped them,
        // so callers testing reboot should use simulateReboot() instead.
    }
    digitalWrite(PIN_CLK, HIGH);
    digitalWrite(PIN_DT, HIGH);
    digitalWrite(PIN_SW, HIGH);
    for (uint8_t p : { PIN_RELAY_1, PIN_RELAY_2, PIN_RELAY_3, PIN_RELAY_4 }) {
        digitalWrite(p, RELAY_OFF);
    }

    rtclock = RTClock();
    storage = Storage();
    history = History();
    wateringCtrl = WateringController();
    scheduler = Scheduler();
    terminal = Terminal();
    display = Display();
    new (&encoder) Encoder(PIN_CLK, PIN_DT, PIN_SW);
    ui.~UI();
    new (&ui) UI(display, encoder);

    initAppState();
    storage.begin();
    storage.load();
    history.begin();
    display.begin();
    encoder.begin();
    rtclock.begin();
    wateringCtrl.begin();
    scheduler.begin();
    ui.begin();
    terminal.begin();
}

// Simulates a full ESP32 power cycle preserving NVS (Preferences) and LittleFS,
// while resetting all RAM singletons and mock GPIOs/millis().
static void simulateReboot(uint32_t rtcUtcAtBoot, bool rtcPresent = true, bool rtcLostPower = false) {
    _mock_millis = 0;
    _mock_micros = 0;
    memset(_mock_gpio, HIGH, sizeof(_mock_gpio));
    digitalWrite(PIN_CLK, HIGH);
    digitalWrite(PIN_DT, HIGH);
    digitalWrite(PIN_SW, HIGH);
    for (uint8_t p : { PIN_RELAY_1, PIN_RELAY_2, PIN_RELAY_3, PIN_RELAY_4 }) {
        digitalWrite(p, RELAY_OFF);
    }
    Serial.reset();
    Wire.reset();
    ESP.reset();
    _log_suspended = false;
    _log_suppressed_total = 0;
    _log_suppressed_warn_err = 0;

    rtclock = RTClock();
    rtclock._rtc.mockPresent = rtcPresent;
    rtclock._rtc.mockLostPower = rtcLostPower;
    rtclock._rtc.mockI2cReadFail = false;
    rtclock._rtc.adjust(DateTime(rtcUtcAtBoot));
    if (rtcLostPower) {
        // adjust() clears mockLostPower in mock; re-set when testing dead battery at boot
        rtclock._rtc.mockLostPower = true;
    }

    storage = Storage();
    history = History();
    wateringCtrl = WateringController();
    scheduler = Scheduler();
    terminal = Terminal();
    display = Display();
    new (&encoder) Encoder(PIN_CLK, PIN_DT, PIN_SW);
    ui.~UI();
    new (&ui) UI(display, encoder);

    initAppState();
    storage.begin();
    storage.load();
    history.begin();
    display.begin();
    encoder.begin();
    rtclock.begin();

    if (gState.suspended_until > 0) {
        if (!gState.rtc_valid) {
            gState.suspended = true;
        } else if (gState.now.unix >= gState.suspended_until) {
            gState.suspended = false;
            gState.suspended_until = 0;
            storage.save();
        } else {
            gState.suspended = true;
        }
    } else {
        gState.suspended = false;
    }

    wateringCtrl.begin();
    scheduler.begin();
    ui.begin();
    terminal.begin();
}

static void stepLoopOnce(uint32_t stepMs) {
    advanceMockMillis(stepMs);
    advanceMockMicros(stepMs * 1000UL);
    // Advance mock DS3231 hardware clock in sync with simulated time
    // (handled by caller updating rtclock._rtc when needed or via _rtcUnix tracking)
    rtclock.update();
    scheduler.update();
    ui.update();
    wateringCtrl.update();
    history.update();
    terminal.update();
}

static void assertRelaySafetyInvariants(uint32_t& activeZoneTicksSec, uint32_t stepSec, uint8_t& lastActiveZone) {
    const uint8_t pins[NUM_ZONES] = { PIN_RELAY_1, PIN_RELAY_2, PIN_RELAY_3, PIN_RELAY_4 };
    uint8_t onCount = 0;
    uint8_t onZone = 0xFF;
    for (uint8_t i = 0; i < NUM_ZONES; i++) {
        if (digitalRead(pins[i]) == RELAY_ON) {
            onCount++;
            onZone = i;
        }
    }
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(1, onCount);
    if (!wateringCtrl.isActive() || wateringCtrl._isWaiting || wateringCtrl._isRelayDeadTimeWaiting) {
        TEST_ASSERT_EQUAL_UINT8(0, onCount);
        activeZoneTicksSec = 0;
        lastActiveZone = 0xFF;
    } else if (onCount == 1) {
        if (onZone != lastActiveZone) {
            activeZoneTicksSec = stepSec;
            lastActiveZone = onZone;
        } else {
            activeZoneTicksSec += stepSec;
        }
        // Hard safety cap: never exceed 20 minutes (+ step tolerance)
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(20UL * 60UL + stepSec, activeZoneTicksSec);
    } else {
        activeZoneTicksSec = 0;
        lastActiveZone = 0xFF;
    }
}

void setUp(void) {
    resetSystem(true);
}

void tearDown(void) {
    wateringCtrl.stop();
}

// ─────────────────────────────────────────────────────────
// 1. Exhaustive 32-bit millis() wraparound across all modules
// ─────────────────────────────────────────────────────────
void test_wraparound_49_day_millis_all_subsystems(void) {
    gState.setup_done = true;
    gState.backlight_timeout_ms = 120000UL;
    gState.zones[0] = { "Z1", true, 1 };
    gState.zones[1] = { "Z2", true, 1 };
    gState.zones[2].enabled = false;
    gState.zones[3].enabled = false;
    storage.save();

    // Start 256 ms before 32-bit millis() wraparound
    setMockMillis(0xFFFFFF00UL);
    setMockMicros(0xFFFFFF00UL);

    // 1. Encoder debounce & rotation across wrap
    encoder.begin();
    digitalWrite(PIN_SW, LOW);
    TEST_ASSERT_FALSE(encoder.getClick());
    advanceMockMillis(20); // 0xFFFFFF14
    TEST_ASSERT_FALSE(encoder.getClick());
    advanceMockMillis(250); // wrapped to ~0x0000000E
    TEST_ASSERT_TRUE(encoder.getClick());
    TEST_ASSERT_FALSE(encoder.getClick()); // held button must not double-trigger
    digitalWrite(PIN_SW, HIGH);
    advanceMockMillis(60);
    TEST_ASSERT_FALSE(encoder.getClick());
    encoder._delta = 3;
    TEST_ASSERT_EQUAL_INT8(3, encoder.getRotation());

    // 2. RTClock SW clock & 30s hardware poll & set() near wrap
    setMockMillis(0xFFFFF000UL);
    rtclock.begin();
    rtclock.set(2026, 6, 15, 10, 0, 0);
    uint32_t startUnix = gState.now.unix;
    rtclock._rtc.adjust(DateTime(startUnix + 31));
    // Step across 0xFFFFFFFF -> 0x00000000+ in 1s increments for 35 seconds
    for (int s = 1; s <= 35; s++) {
        advanceMockMillis(1000);
        if (s == 31) {
            rtclock._rtc.adjust(DateTime(startUnix + 31));
        } else {
            rtclock._rtc.adjust(DateTime(startUnix + s));
        }
        rtclock.update();
    }
    TEST_ASSERT_EQUAL_UINT32(startUnix + 35, gState.now.unix);

    // Call RTClock::set() right after wrap (millis() == 100 -> millis() - 30000 underflow)
    setMockMillis(100);
    TEST_ASSERT_TRUE(rtclock.set(2026, 6, 15, 10, 5, 0));
    advanceMockMillis(500);
    rtclock._rtc.adjust(DateTime(gState.now.unix + 1));
    rtclock.update();
    TEST_ASSERT_EQUAL_UINT32(10, gState.now.hour);

    // 3. Display sync(), 60s full refresh, and 10s I2C fault cooldown across wrap
    setMockMillis(0xFFFFE000UL);
    display.begin();
    display.setRows("A0", "A1", "A2", "A3");
    advanceMockMillis(10000); // wrapped to ~0x00000710
    display._lcd.clear();     // simulate glitch
    advanceMockMillis(60000);
    display.sync();
    TEST_ASSERT_EQUAL_STRING("A0                  ", display._lcd.getRow(0));

    setMockMillis(0xFFFFF000UL);
    uint8_t cbCalls = 0;
    static uint8_t* cbPtr = nullptr;
    cbPtr = &cbCalls;
    display.setErrorCallback([]() { (*cbPtr)++; });
    display._lcd.mockI2cError = 1;
    for (int i = 0; i < 3; i++) display.setRows("E0", "E1", "E2", "E3");
    TEST_ASSERT_EQUAL_UINT8(1, cbCalls);
    advanceMockMillis(5000); // inside 10s cooldown across wrap (~0x00000388)
    for (int i = 0; i < 3; i++) display.setRows("E0", "E1", "E2", "E3");
    TEST_ASSERT_EQUAL_UINT8(1, cbCalls);
    advanceMockMillis(6000); // past 10s cooldown
    for (int i = 0; i < 3; i++) display.setRows("E0", "E1", "E2", "E3");
    TEST_ASSERT_EQUAL_UINT8(2, cbCalls);
    display._lcd.mockI2cError = 0;
    display.setErrorCallback(nullptr);
    display.recover(true);

    // 4. UI idle timeout (30s), backlightOff (120s), displayOff (140s), and 49.7d sleep stability
    setMockMillis(0xFFFFF000UL);
    ui.begin();
    ui.goMenu(MenuID::MAIN);
    ui.update();
    TEST_ASSERT_TRUE(display.isBacklightOn());
    TEST_ASSERT_TRUE(display.isDisplayOn());

    advanceMockMillis(30000UL); // wrapped past 0 -> returns to Idle
    ui.update();
    TEST_ASSERT_EQUAL_PTR(&ui.getScreenIdle(), ui._currentScreen);

    advanceMockMillis(90000UL); // 120s total -> backlight OFF
    ui.update();
    TEST_ASSERT_FALSE(display.isBacklightOn());
    TEST_ASSERT_TRUE(display.isDisplayOn());

    advanceMockMillis(20000UL); // 140s total -> display OFF
    ui.update();
    TEST_ASSERT_FALSE(display.isDisplayOn());

    // Advance past a full 32-bit wrap while asleep -> must stay OFF cleanly
    advanceMockMillis(0xFFFF0000UL);
    ui.update();
    TEST_ASSERT_FALSE(display.isBacklightOn());
    TEST_ASSERT_FALSE(display.isDisplayOn());

    // 5. Terminal pending clear_history timeout across wrap
    HistoryEntry seedHist = { 2026, 6, 15, 8, 0, { 1, 0, 0, 0 }, WaterTrigger::MANUAL, 0 };
    history.record(seedHist);
    TEST_ASSERT_EQUAL_UINT16(1, history.entryCount());
    setMockMillis(0xFFFFE000UL);
    Serial.reset();
    Serial.feedInput("clear_history\n");
    terminal.update();
    TEST_ASSERT_TRUE(terminal._pendingClearHistory);
    advanceMockMillis(29000UL); // wrapped
    terminal.update();
    TEST_ASSERT_TRUE(terminal._pendingClearHistory);
    advanceMockMillis(2000UL);
    terminal.update();
    TEST_ASSERT_FALSE(terminal._pendingClearHistory);

    // 6. WateringController full 2-zone run across wrap (dead-time, EMI pin flip, wait, history)
    history.clear();
    setMockMillis(0xFFFFFFF0UL); // 16 ms before wrap!
    wateringCtrl.startGeneral(WaterTrigger::MANUAL);
    TEST_ASSERT_TRUE(wateringCtrl._isRelayDeadTimeWaiting);

    advanceMockMillis(RELAY_DEADTIME_MS + 4); // crosses 0xFFFFFFFF -> 0x00000008
    wateringCtrl.update();
    TEST_ASSERT_EQUAL_UINT8(RELAY_ON, digitalRead(PIN_RELAY_1));

    // Inject EMI glitch on inactive pin right after wrap -> 1s refresh repairs it
    digitalWrite(PIN_RELAY_2, RELAY_ON);
    advanceMockMillis(1000);
    wateringCtrl.update();
    TEST_ASSERT_EQUAL_UINT8(RELAY_OFF, digitalRead(PIN_RELAY_2));

    // Finish Zone 0 (remaining ~59s) across wrap
    advanceMockMillis(59000UL);
    wateringCtrl.update();
    TEST_ASSERT_TRUE(gState.watering.is_waiting);
    TEST_ASSERT_EQUAL_UINT8(RELAY_OFF, digitalRead(PIN_RELAY_1));

    // Cross wrap during 5s inter-zone wait
    setMockMillis(0xFFFFF800UL);
    wateringCtrl._waitStartMs = 0xFFFFF800UL;
    advanceMockMillis(ZONE_WAIT_DELAY_MS + 10); // wrapped
    wateringCtrl.update();
    advanceMockMillis(RELAY_DEADTIME_MS + 5);
    wateringCtrl.update();
    TEST_ASSERT_EQUAL_UINT8(RELAY_ON, digitalRead(PIN_RELAY_2));

    // Complete Zone 1 across wrap
    setMockMillis(0xFFFFE000UL);
    wateringCtrl._zoneStartMs = 0xFFFFE000UL;
    advanceMockMillis(60000UL);
    wateringCtrl.update();
    TEST_ASSERT_FALSE(wateringCtrl.isActive());
    TEST_ASSERT_EQUAL_UINT8(RELAY_OFF, digitalRead(PIN_RELAY_2));
    TEST_ASSERT_EQUAL_UINT16(1, history.entryCount());
}

// ─────────────────────────────────────────────────────────
// 2. 520-day continuous soak test (regular usage, all modes,
//    leap day, DST fall-back & spring-forward, 10x millis() wrap,
//    and LittleFS history rotation > 1500 entries)
// ─────────────────────────────────────────────────────────
void test_soak_520_days_regular_usage_all_modes_dst_leap_and_rotations(void) {
    gState.setup_done = true;
    gState.auto_dst = true;
    gState.zones[0] = { "Z1", true, 5 };
    gState.zones[1] = { "Z2", true, 4 };
    gState.zones[2] = { "Z3", true, 3 };
    gState.zones[3] = { "Z4", false, 0 };
    gState.mode = AppMode::INTENSO;
    storage.save();

    // Start 2027-09-01 00:00:00 local (UTC+1 summer DST -> 2027-08-31 23:00:00 UTC)
    DateTime startUtc = RTClock::localToUtc(DateTime(2027, 9, 1, 0, 0, 0));
    uint32_t simUtc = startUtc.unixtime();
    rtclock._rtc.adjust(DateTime(simUtc));
    rtclock.begin();
    scheduler.begin();
    ui.goIdle();

    uint32_t totalCycles = 0;
    uint32_t prevCycleStartUtc = 0;
    uint32_t activeZoneTicksSec = 0;
    uint8_t  lastActiveZone = 0xFF;
    bool     wasActive = false;

    uint32_t leapDayCycles = 0;
    uint32_t oct2027DstCycles = 0;
    uint32_t mar2028DstCycles = 0;
    uint32_t oct2028DstCycles = 0;
    uint32_t suspendedCycles = 0;
    bool     suspendStarted = false;
    bool     suspendEnded = false;

    // 520 days total -> triggers ~1,632 cycles, exceeding HISTORY_MAX_ENTRIES (1500)
    const uint32_t totalDays = 520;
    const uint32_t endUtc = simUtc + totalDays * 86400UL;
    uint32_t subSecMsAcc = 0;

    while (simUtc < endUtc) {
        // Choose adaptive step:
        // - 20 ms during relay dead-time
        // - 1 s during active watering, history rotation/export, or DST transition hours (00:58-02:05 UTC)
        // - 10 s during quiet idle (hits :00/:10/:20/:30/:40/:50 of every minute & exact 30s RTC polls)
        uint32_t stepMs = 10000UL;
        if (wateringCtrl._isRelayDeadTimeWaiting) {
            stepMs = 20UL;
        } else if (wateringCtrl.isActive() ||
                   history._rotState != History::RotState::IDLE ||
                   history._exportState != History::ExportState::IDLE) {
            stepMs = 1000UL;
        } else {
            uint32_t todUtc = simUtc % 86400UL;
            if (todUtc < 2UL * 3600UL + 300UL) {
                stepMs = 1000UL;
            }
        }

        // Advance simulation time and hardware RTC
        uint32_t prevMs = millis();
        uint32_t newMs = prevMs + stepMs;
        // Track whole seconds elapsed cleanly across sub-second dead-time steps
        if (stepMs == 10000UL || stepMs == 1000UL) {
            simUtc += stepMs / 1000UL;
            subSecMsAcc = 0;
        } else {
            subSecMsAcc += stepMs;
            if (subSecMsAcc >= 1000UL) {
                simUtc += subSecMsAcc / 1000UL;
                subSecMsAcc %= 1000UL;
            }
        }
        (void)newMs;

        rtclock._rtc.adjust(DateTime(simUtc));
        stepLoopOnce(stepMs);

        // Mode schedule transitions across the 450-day horizon:
        uint32_t dayNum = (simUtc - startUtc.unixtime()) / 86400UL;
        if (dayNum == 250 && gState.mode == AppMode::INTENSO) {
            // Switch to MEDIO (2x/day)
            gState.mode = AppMode::MEDIO;
            storage.save();
            scheduler.onModeChanged(false);
        } else if (dayNum == 270 && !suspendStarted && !wateringCtrl.isActive() && !gState.suspended) {
            // Suspend for 4 days
            suspendStarted = true;
            gState.suspended = true;
            gState.suspended_until = simUtc + 4UL * 86400UL;
            storage.save();
        } else if (dayNum == 276 && gState.suspended == false) {
            suspendEnded = true;
        } else if (dayNum == 300 && gState.mode == AppMode::MEDIO) {
            // Switch to FRACO (1x/day)
            gState.mode = AppMode::FRACO;
            storage.save();
            scheduler.onModeChanged(false);
        } else if (dayNum == 330 && gState.mode == AppMode::FRACO) {
            // Switch to PERSONALIZADO: 4 cycles/day daily, including 01:30 (hits Oct 2028 fall-back!)
            ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
            cs.slot_count = 4;
            cs.interval_days = 1;
            cs.day_pattern = DayPattern::DAILY;
            cs.slots[0] = { 1, 30 };
            cs.slots[1] = { 7, 30 };
            cs.slots[2] = { 13, 30 };
            cs.slots[3] = { 19, 30 };
            gState.mode = AppMode::PERSONALIZADO;
            storage.save();
            scheduler.onModeChanged(false);
        }

        // Track cycle starts and double-watering invariants
        bool isActive = wateringCtrl.isActive();
        if (isActive && !wasActive) {
            totalCycles++;
            // Two AUTO cycles must never fire back-to-back within 30 minutes
            if (prevCycleStartUtc > 0) {
                TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1800UL, simUtc - prevCycleStartUtc);
            }
            prevCycleStartUtc = simUtc;

            DateTime loc = RTClock::utcToLocal(DateTime(simUtc));
            if (loc.year() == 2028 && loc.month() == 2 && loc.day() == 29) leapDayCycles++;
            if (loc.year() == 2027 && loc.month() == 10 && loc.day() == 31) oct2027DstCycles++;
            if (loc.year() == 2028 && loc.month() == 3 && loc.day() == 26) mar2028DstCycles++;
            if (loc.year() == 2028 && loc.month() == 10 && loc.day() == 29) oct2028DstCycles++;
            if (gState.suspended) suspendedCycles++;
        }
        wasActive = isActive;

        assertRelaySafetyInvariants(activeZoneTicksSec, (stepMs >= 1000UL) ? (stepMs / 1000UL) : 1UL, lastActiveZone);

        // Keep mock Serial buffer bounded during long soak
        if (Serial.outputBuffer.size() > 16384) {
            Serial.outputBuffer.clear();
        }
    }

    // Drain any final active cycle or pending history rotation
    uint32_t drainGuard = 3600;
    while ((wateringCtrl.isActive() || history._rotState != History::RotState::IDLE) && drainGuard-- > 0) {
        uint32_t sMs = wateringCtrl._isRelayDeadTimeWaiting ? 20UL : 1000UL;
        if (sMs == 1000UL) simUtc++;
        rtclock._rtc.adjust(DateTime(simUtc));
        stepLoopOnce(sMs);
        assertRelaySafetyInvariants(activeZoneTicksSec, 1UL, lastActiveZone);
    }
    TEST_ASSERT_FALSE(wateringCtrl.isActive());
    TEST_ASSERT_EQUAL(History::RotState::IDLE, history._rotState);

    // Verify expected DST, leap day, and suspension counts
    TEST_ASSERT_TRUE(suspendStarted);
    TEST_ASSERT_TRUE(suspendEnded);
    TEST_ASSERT_EQUAL_UINT32(0, suspendedCycles);
    TEST_ASSERT_EQUAL_UINT32(3, leapDayCycles);    // INTENSO on 2028-02-29
    TEST_ASSERT_EQUAL_UINT32(3, oct2027DstCycles); // INTENSO on fall-back day -> no duplicates
    TEST_ASSERT_EQUAL_UINT32(3, mar2028DstCycles); // INTENSO on spring-forward day -> all 3
    TEST_ASSERT_EQUAL_UINT32(4, oct2028DstCycles); // PERSONALIZADO with 01:30 slot on Oct 2028 fall-back -> 4, NOT 5!
    TEST_ASSERT_GREATER_THAN_UINT32(1500UL, totalCycles);

    // Verify LittleFS history rotated cleanly and survives reboot reload intact
    uint16_t preRebootCount = history.entryCount();
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(HISTORY_MAX_ENTRIES, preRebootCount);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(HISTORY_MAX_ENTRIES - (HISTORY_MAX_ENTRIES / 10), preRebootCount);

    HistoryEntry lastBefore[HISTORY_DISPLAY] = {};
    uint8_t nBefore = history.readLast(HISTORY_DISPLAY, lastBefore);
    TEST_ASSERT_EQUAL_UINT8(HISTORY_DISPLAY, nBefore);

    simulateReboot(simUtc, true, false);
    TEST_ASSERT_TRUE(history.isReady());
    TEST_ASSERT_EQUAL_UINT16(preRebootCount, history.entryCount());
    HistoryEntry lastAfter[HISTORY_DISPLAY] = {};
    uint8_t nAfter = history.readLast(HISTORY_DISPLAY, lastAfter);
    TEST_ASSERT_EQUAL_UINT8(nBefore, nAfter);
    for (uint8_t i = 0; i < nAfter; i++) {
        TEST_ASSERT_EQUAL_UINT16(lastBefore[i].year, lastAfter[i].year);
        TEST_ASSERT_EQUAL_UINT8(lastBefore[i].month, lastAfter[i].month);
        TEST_ASSERT_EQUAL_UINT8(lastBefore[i].day, lastAfter[i].day);
        TEST_ASSERT_EQUAL_UINT8(lastBefore[i].hour, lastAfter[i].hour);
        TEST_ASSERT_EQUAL_UINT8(lastBefore[i].min, lastAfter[i].min);
    }
}

// ─────────────────────────────────────────────────────────
// 3. Exhaustive double-watering & edge-case matrix
// ─────────────────────────────────────────────────────────
void test_soak_double_watering_and_edge_case_matrix(void) {
    // Common zone setup: 3 zones x 6 min
    auto setupCommonZones = []() {
        gState.setup_done = true;
        gState.auto_dst = true;
        gState.zones[0] = { "Z1", true, 6 };
        gState.zones[1] = { "Z2", true, 6 };
        gState.zones[2] = { "Z3", true, 6 };
        gState.zones[3] = { "Z4", false, 0 };
        storage.save();
    };

    // A. October DST fall-back repeated 01:30 + reboot at 01:45 UTC (STD) must NOT double-water
    {
        resetSystem(true);
        setupCommonZones();
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        cs.slot_count = 1;
        cs.interval_days = 1;
        cs.day_pattern = DayPattern::DAILY;
        cs.slots[0] = { 1, 30 };
        gState.mode = AppMode::PERSONALIZADO;
        storage.save();

        // 2026-10-25 00:29:59 UTC == 01:29:59 local (DST)
        uint32_t utc = DateTime(2026, 10, 25, 0, 29, 59).unixtime();
        simulateReboot(utc);
        utc++;
        rtclock._rtc.adjust(DateTime(utc));
        stepLoopOnce(1000); // 01:30:00 local (DST) -> fires
        TEST_ASSERT_TRUE(wateringCtrl.isActive());
        wateringCtrl.stop();

        // Reboot inside repeated hour at 01:45:00 UTC (01:45:00 local STD, 15 min after 2nd 01:30)
        uint32_t rebootUtc = DateTime(2026, 10, 25, 1, 45, 0).unixtime();
        simulateReboot(rebootUtc);
        stepLoopOnce(1000);
        TEST_ASSERT_FALSE(wateringCtrl.isActive()); // blocked by persisted sched_tr!
    }

    // B. Outage spanning 1st 01:30 (DST) and 2nd 01:30 (STD) -> reboot at 01:45 UTC catches up ONCE
    {
        resetSystem(true);
        setupCommonZones();
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        cs.slot_count = 1;
        cs.interval_days = 1;
        cs.day_pattern = DayPattern::DAILY;
        cs.slots[0] = { 1, 30 };
        gState.mode = AppMode::PERSONALIZADO;
        storage.save();

        uint32_t rebootUtc = DateTime(2026, 10, 25, 1, 45, 0).unixtime();
        simulateReboot(rebootUtc);
        stepLoopOnce(1000);
        TEST_ASSERT_TRUE(wateringCtrl.isActive());
        wateringCtrl.stop();

        // Reboot again at 01:55 UTC -> must NOT fire a 2nd time
        simulateReboot(DateTime(2026, 10, 25, 1, 55, 0).unixtime());
        stepLoopOnce(1000);
        TEST_ASSERT_FALSE(wateringCtrl.isActive());
    }

    // C. Brownout retry loop hits MAX_RECOVERY_RETRIES (3) -> aborts AND catch-up must NOT restart it
    {
        resetSystem(true);
        setupCommonZones();
        gState.mode = AppMode::MEDIO; // 08:00, 20:00
        storage.save();

        uint32_t slotUtc = RTClock::localToUtc(DateTime(2026, 6, 15, 7, 59, 59)).unixtime();
        simulateReboot(slotUtc);
        slotUtc++;
        rtclock._rtc.adjust(DateTime(slotUtc));
        stepLoopOnce(1000); // 08:00:00 -> AUTO starts
        TEST_ASSERT_TRUE(wateringCtrl.isActive());

        // 3 consecutive brownout reboots within 2 min (08:01, 08:02, 08:03)
        for (int r = 1; r <= MAX_RECOVERY_RETRIES; r++) {
            simulateReboot(slotUtc + (uint32_t)r * 60UL);
            stepLoopOnce(1000);
            TEST_ASSERT_TRUE(wateringCtrl.isActive());
        }
        // 4th boot at 08:04 -> recovery aborts AND Scheduler catch-up stays blocked by sched_tr!
        simulateReboot(slotUtc + 240UL);
        stepLoopOnce(1000);
        TEST_ASSERT_FALSE(wateringCtrl.isActive());
    }

    // D. Multi-zone blackout recovery: mid-Z1 checkpoint + outage during inter-zone wait -> exact Z1/Z2/Z3 mins
    {
        resetSystem(true);
        setupCommonZones();
        gState.mode = AppMode::MEDIO;
        storage.save();

        uint32_t utc = RTClock::localToUtc(DateTime(2026, 6, 15, 7, 59, 59)).unixtime();
        simulateReboot(utc);
        utc++;
        rtclock._rtc.adjust(DateTime(utc));
        stepLoopOnce(1000); // 08:00:00 -> AUTO starts
        stepLoopOnce(RELAY_DEADTIME_MS + 5);
        // Run Z1 for 3 minutes (triggers 2-min checkpoint: Z1 recorded=2, remaining=4m)
        for (int s = 0; s < 180; s++) {
            utc++;
            rtclock._rtc.adjust(DateTime(utc));
            stepLoopOnce(1000);
        }

        // Power outage for 1 minute -> reboot at 08:04
        utc += 60;
        simulateReboot(utc);
        TEST_ASSERT_TRUE(wateringCtrl.isActive());
        TEST_ASSERT_EQUAL_UINT8(0, wateringCtrl.currentZone());
        stepLoopOnce(RELAY_DEADTIME_MS + 5);
        // Run remaining 4 minutes of Z1 until inter-zone wait starts
        for (int s = 0; s < 240; s++) {
            utc++;
            rtclock._rtc.adjust(DateTime(utc));
            stepLoopOnce(1000);
        }
        TEST_ASSERT_TRUE(gState.watering.is_waiting);
        TEST_ASSERT_EQUAL_UINT8(1, wateringCtrl.currentZone()); // upcoming Z2

        // Second blackout DURING inter-zone wait -> reboot resumes at Z2 (never re-runs Z1!)
        utc += 30;
        simulateReboot(utc);
        TEST_ASSERT_TRUE(wateringCtrl.isActive());
        TEST_ASSERT_TRUE(gState.watering.is_waiting);
        TEST_ASSERT_EQUAL_UINT8(1, wateringCtrl.currentZone());

        // Drain rest of cycle (Z2 + Z3)
        while (wateringCtrl.isActive()) {
            uint32_t sMs = wateringCtrl._isRelayDeadTimeWaiting ? 20UL : 1000UL;
            if (sMs == 1000UL) utc++;
            rtclock._rtc.adjust(DateTime(utc));
            stepLoopOnce(sMs);
        }
        HistoryEntry latest[1] = {};
        TEST_ASSERT_EQUAL_UINT8(1, history.readLast(1, latest));
        TEST_ASSERT_EQUAL_UINT8(6, latest[0].zone_dur[0]);
        TEST_ASSERT_EQUAL_UINT8(6, latest[0].zone_dur[1]);
        TEST_ASSERT_EQUAL_UINT8(6, latest[0].zone_dur[2]);
    }

    // E. MANUAL collision at 08:00 + blackout on MANUAL before 08:00 -> aborts stale MANUAL, catches AUTO once
    {
        resetSystem(true);
        setupCommonZones();
        gState.mode = AppMode::MEDIO;
        storage.save();

        uint32_t utc = RTClock::localToUtc(DateTime(2026, 6, 15, 7, 58, 0)).unixtime();
        simulateReboot(utc);
        wateringCtrl.startGeneral(WaterTrigger::MANUAL);
        stepLoopOnce(RELAY_DEADTIME_MS + 5);

        // Power outage at 07:59 -> back at 08:05 (crossed 08:00 AUTO slot)
        utc = RTClock::localToUtc(DateTime(2026, 6, 15, 8, 5, 0)).unixtime();
        simulateReboot(utc);
        // Recovery of MANUAL aborted because 08:00 scheduled slot passed during outage;
        // Scheduler catch-up runs 08:00 AUTO ONCE.
        TEST_ASSERT_FALSE(wateringCtrl.isActive());
        stepLoopOnce(1000);
        TEST_ASSERT_TRUE(wateringCtrl.isActive());
        TEST_ASSERT_EQUAL(WaterTrigger::AUTO, wateringCtrl._runTrigger);
        wateringCtrl.stop();
    }

    // F. SUSPEND expiration boundaries:
    //    1) expires mid-minute (08:00:30) -> triggers 08:00 once
    //    2) expires AFTER slot (08:15:00) -> does NOT catch up stale slot from suspension period
    {
        resetSystem(true);
        setupCommonZones();
        gState.mode = AppMode::MEDIO;
        uint32_t slotUtc = RTClock::localToUtc(DateTime(2026, 6, 15, 8, 0, 0)).unixtime();
        gState.suspended = true;
        gState.suspended_until = slotUtc + 30;
        storage.save();

        simulateReboot(slotUtc + 20);
        stepLoopOnce(1000);
        TEST_ASSERT_TRUE(gState.suspended);
        TEST_ASSERT_FALSE(wateringCtrl.isActive());

        // Step second-by-second across suspension expiry at 08:00:30 -> triggers 08:00 once
        for (uint32_t u = slotUtc + 22; u <= slotUtc + 35; u++) {
            rtclock._rtc.adjust(DateTime(u));
            stepLoopOnce(1000);
        }
        TEST_ASSERT_FALSE(gState.suspended);
        TEST_ASSERT_TRUE(wateringCtrl.isActive());
        wateringCtrl.stop();

        // Next second at 08:00:36 -> must not re-trigger
        rtclock._rtc.adjust(DateTime(slotUtc + 36));
        stepLoopOnce(1000);
        TEST_ASSERT_FALSE(wateringCtrl.isActive());

        // Case F2: suspension spans 08:00 and expires at 08:15 -> NO catch-up
        resetSystem(true);
        setupCommonZones();
        gState.mode = AppMode::MEDIO;
        gState.suspended = true;
        gState.suspended_until = slotUtc + 900; // 08:15:00
        storage.save();
        simulateReboot(slotUtc + 895);
        stepLoopOnce(1000);
        TEST_ASSERT_TRUE(gState.suspended);
        for (uint32_t u = slotUtc + 897; u <= slotUtc + 905; u++) {
            rtclock._rtc.adjust(DateTime(u));
            stepLoopOnce(1000);
        }
        TEST_ASSERT_FALSE(gState.suspended);
        TEST_ASSERT_FALSE(wateringCtrl.isActive());
    }

    // G. EVERY_X_DAYS (interval=3) midnight wrap catch-up maintains exact 3-day cadence
    {
        resetSystem(true);
        setupCommonZones();
        ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
        cs.slot_count = 1;
        cs.interval_days = 3;
        cs.day_pattern = DayPattern::EVERY_X_DAYS;
        cs.slots[0] = { 23, 50 };
        gState.mode = AppMode::PERSONALIZADO;
        gState.custom_ref_day = DateTime(2026, 6, 15, 0, 0, 0).unixtime() / 86400UL;
        storage.save();

        // Day 3 (2026-06-18): outage at 23:45 -> reboot 2026-06-19 00:10 -> catches up Day 3's 23:50!
        uint32_t catchUtc = RTClock::localToUtc(DateTime(2026, 6, 19, 0, 10, 0)).unixtime();
        simulateReboot(catchUtc);
        stepLoopOnce(1000);
        TEST_ASSERT_TRUE(wateringCtrl.isActive());
        wateringCtrl.stop();

        // Step continuously through Day 4 (Jun 19), Day 5 (Jun 20), and Day 6 (Jun 21 23:51):
        // Day 4 & Day 5 must NOT fire; Day 6 MUST fire once at 23:50.
        uint32_t u = catchUtc + 1;
        uint32_t day6TriggerUtc = RTClock::localToUtc(DateTime(2026, 6, 21, 23, 50, 0)).unixtime();
        uint32_t endDay6Utc = day6TriggerUtc + 60UL;
        uint32_t firedAtUtc = 0;
        uint32_t fireCount = 0;
        while (u < endDay6Utc) {
            uint32_t sMs = (u + 30 >= day6TriggerUtc) ? 1000UL : 10000UL;
            u += sMs / 1000UL;
            rtclock._rtc.adjust(DateTime(u));
            stepLoopOnce(sMs);
            if (wateringCtrl.isActive()) {
                fireCount++;
                firedAtUtc = u;
                wateringCtrl.stop();
            }
        }
        TEST_ASSERT_EQUAL_UINT32(1, fireCount);
        TEST_ASSERT_EQUAL_UINT32(day6TriggerUtc, firedAtUtc);
    }

    // H. Dead RTC battery on cold boot inhibits AUTO watering, and export_history defers recording cleanly
    {
        resetSystem(true);
        setupCommonZones();
        gState.mode = AppMode::MEDIO;
        storage.save();

        uint32_t utc = RTClock::localToUtc(DateTime(2026, 6, 15, 8, 0, 0)).unixtime();
        simulateReboot(utc, true, true); // lostPower = true on cold boot
        TEST_ASSERT_FALSE(gState.rtc_valid);
        stepLoopOnce(1000);
        TEST_ASSERT_FALSE(wateringCtrl.isActive()); // fail-safe: no AUTO without valid clock

        // Manual run + export_history active simultaneously -> deferred entry flushes once export ends
        rtclock.set(2026, 6, 15, 9, 0, 0);
        HistoryEntry seed = { 2026, 6, 14, 8, 0, { 5, 0, 0, 0 }, WaterTrigger::AUTO, 0 };
        history.record(seed);
        TEST_ASSERT_EQUAL_UINT16(1, history.entryCount());

        Serial.feedInput("export_history\n");
        terminal.update();
        TEST_ASSERT_TRUE(history.isExporting());

        wateringCtrl.startGeneral(WaterTrigger::MANUAL);
        stepLoopOnce(RELAY_DEADTIME_MS + 5);
        // Stop after 1 minute so history records while export is still active
        advanceMockMillis(60000UL);
        wateringCtrl.stop();
        TEST_ASSERT_NOT_EQUAL(history._defHead, history._defTail);

        // Drain export -> deferred history record flushes automatically
        int guard = 100;
        while ((history.isExporting() || history._defHead != history._defTail) && guard-- > 0) {
            history.update();
        }
        TEST_ASSERT_FALSE(history.isExporting());
        TEST_ASSERT_EQUAL_UINT16(2, history.entryCount());
    }
}

// ─────────────────────────────────────────────────────────
// 4. 120-day chaotic fault-injection soak test
//    (random power outages, RTC drops/corrupt reads/OSF,
//     LCD faults under Always-On, user menu/terminal inputs)
// ─────────────────────────────────────────────────────────
void test_soak_120_days_chaotic_blackouts_and_faults(void) {
    gState.setup_done = true;
    gState.auto_dst = true;
    gState.backlight_timeout_ms = BACKLIGHT_TIMEOUT_NEVER;
    gState.zones[0] = { "Z1", true, 4 };
    gState.zones[1] = { "Z2", true, 3 };
    gState.zones[2] = { "Z3", false, 0 };
    gState.zones[3] = { "Z4", false, 0 };
    gState.mode = AppMode::INTENSO; // 07:00, 13:00, 19:00
    storage.save();

    // Span Feb 1 2026 -> Jun 1 2026 (covers March DST spring-forward + ~2.4x millis wraparounds)
    uint32_t simUtc = RTClock::localToUtc(DateTime(2026, 2, 1, 0, 0, 0)).unixtime();
    const uint32_t endUtc = simUtc + 120UL * 86400UL;
    simulateReboot(simUtc);

    uint32_t rng = 0xCAFEBABEUL;
    auto nextRand = [&rng]() -> uint32_t {
        rng = rng * 1664525UL + 1013904223UL;
        return rng;
    };

    uint32_t activeZoneTicksSec = 0;
    uint8_t  lastActiveZone = 0xFF;
    uint32_t prevAutoStartUtc = 0;
    bool     wasActive = false;

    while (simUtc < endUtc) {
        uint32_t r = nextRand();

        // ~0.04% chance per step to inject a random fault or power outage
        if ((r % 2500U) == 0U) {
            uint32_t kind = (r >> 12) % 5U;
            if (kind == 0) {
                // Short blackout (15s - 25min) across arbitrary states (idle, active, or slot)
                uint32_t outageSec = 15UL + ((r >> 16) % 1500UL);
                simUtc += outageSec;
                simulateReboot(simUtc, true, false);
                wasActive = wateringCtrl.isActive();
                activeZoneTicksSec = 0;
                lastActiveZone = 0xFF;
                continue;
            } else if (kind == 1) {
                // Transient RTC disconnect for 3 minutes, then hotplug reconnect
                rtclock._rtc.mockPresent = false;
                for (int i = 0; i < 6; i++) {
                    simUtc += 30;
                    rtclock._rtc.adjust(DateTime(simUtc));
                    stepLoopOnce(30000UL);
                    assertRelaySafetyInvariants(activeZoneTicksSec, 30UL, lastActiveZone);
                }
                rtclock._rtc.mockPresent = true;
            } else if (kind == 2) {
                // Transient I2C read corruption burst, then clear
                rtclock._rtc.mockI2cReadFail = true;
                for (int i = 0; i < 4; i++) {
                    simUtc += 10;
                    rtclock._rtc.adjust(DateTime(simUtc));
                    stepLoopOnce(10000UL);
                }
                rtclock._rtc.mockI2cReadFail = false;
            } else if (kind == 3) {
                // LCD transient I2C fault under Always-On mode
                display._lcd.mockI2cError = 1;
                for (int i = 0; i < 4; i++) {
                    simUtc += 1;
                    rtclock._rtc.adjust(DateTime(simUtc));
                    stepLoopOnce(1000UL);
                }
                display._lcd.mockI2cError = 0;
                display.recover(true);
            } else {
                // User serial status check & menu wake
                Serial.feedInput("status\n");
                encoder._delta = 1;
            }
        }

        uint32_t stepMs = wateringCtrl._isRelayDeadTimeWaiting
            ? 20UL
            : (wateringCtrl.isActive() ? 1000UL : 10000UL);
        if (stepMs >= 1000UL) simUtc += stepMs / 1000UL;
        rtclock._rtc.adjust(DateTime(simUtc));
        stepLoopOnce(stepMs);

        bool isActive = wateringCtrl.isActive();
        if (isActive && !wasActive) {
            if (wateringCtrl._runTrigger == WaterTrigger::AUTO && prevAutoStartUtc > 0) {
                // INTENSO slots are 6h apart; never allow duplicate AUTO trigger within < 2h
                TEST_ASSERT_GREATER_OR_EQUAL_UINT32(7200UL, simUtc - prevAutoStartUtc);
            }
            if (wateringCtrl._runTrigger == WaterTrigger::AUTO) {
                prevAutoStartUtc = simUtc;
            }
        }
        wasActive = isActive;

        assertRelaySafetyInvariants(activeZoneTicksSec, (stepMs >= 1000UL) ? (stepMs / 1000UL) : 1UL, lastActiveZone);
        if (Serial.outputBuffer.size() > 16384) Serial.outputBuffer.clear();
    }

    // Cleanly finish any in-flight watering at end of soak
    uint32_t drain = 1200;
    while (wateringCtrl.isActive() && drain-- > 0) {
        uint32_t sMs = wateringCtrl._isRelayDeadTimeWaiting ? 20UL : 1000UL;
        if (sMs >= 1000UL) simUtc++;
        rtclock._rtc.adjust(DateTime(simUtc));
        stepLoopOnce(sMs);
        assertRelaySafetyInvariants(activeZoneTicksSec, 1UL, lastActiveZone);
    }

    TEST_ASSERT_FALSE(wateringCtrl.isActive());
    TEST_ASSERT_TRUE(gState.rtc_valid);
    TEST_ASSERT_TRUE(history.isReady());
    TEST_ASSERT_GREATER_THAN_UINT16(300, history.entryCount());
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(HISTORY_MAX_ENTRIES, history.entryCount());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_wraparound_49_day_millis_all_subsystems);
    RUN_TEST(test_soak_520_days_regular_usage_all_modes_dst_leap_and_rotations);
    RUN_TEST(test_soak_double_watering_and_edge_case_matrix);
    RUN_TEST(test_soak_120_days_chaotic_blackouts_and_faults);
    return UNITY_END();
}
