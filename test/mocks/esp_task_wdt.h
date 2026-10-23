#pragma once

#include <stdint.h>

inline void esp_task_wdt_reset() {}
inline int esp_task_wdt_add(void* handle) { (void)handle; return 0; }
inline int esp_task_wdt_delete(void* handle) { (void)handle; return 0; }
