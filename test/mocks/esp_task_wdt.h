#pragma once

#include <stdint.h>

inline uint32_t _mock_wdt_delete_calls = 0;
inline uint32_t _mock_wdt_add_calls = 0;
inline bool     _mock_wdt_paused = false;

inline void esp_task_wdt_reset() {}
inline int esp_task_wdt_add(void* handle) {
    (void)handle;
    _mock_wdt_add_calls++;
    _mock_wdt_paused = false;
    return 0;
}
inline int esp_task_wdt_delete(void* handle) {
    (void)handle;
    _mock_wdt_delete_calls++;
    _mock_wdt_paused = true;
    return 0;
}
