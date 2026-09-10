#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_sta_start(void);
bool wifi_sta_is_connected(void);

#ifdef __cplusplus
}
#endif
