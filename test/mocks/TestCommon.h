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

void resetAllTestMocks();
