#pragma once
#include <cstdint>
#define CONFIG_ESP_TASK_WDT_TIMEOUT_S 5
struct esp_task_wdt_config_t { uint32_t timeout_ms; uint32_t idle_core_mask; bool trigger_panic; };
inline int esp_task_wdt_reconfigure(const esp_task_wdt_config_t*) { return 0; }
