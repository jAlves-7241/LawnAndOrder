#pragma once

#ifdef unix
#undef unix
#endif
#ifdef linux
#undef linux
#endif

#include "Arduino.h"
#include "RTClib.h"
#include "Preferences.h"
#include "LittleFS.h"
#include "AppState.h"
#include "Wire.h"

void resetAllTestMocks();

// Runs a UnitTestAccess static test method while reporting the clean test name to Unity.
#define RUN_UT(fn) UnityDefaultTestRun(UnitTestAccess::fn, #fn, __LINE__)
