#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];
    char mqtt_host[64];
    uint16_t mqtt_port;
    char mqtt_user[33];
    char mqtt_pass[65];
    char mqtt_base[32];
    uint32_t stale_timeout_s;
    uint8_t backlight_pct;
    char tz[32];
} app_config_t;

esp_err_t app_config_init(void);
const app_config_t *app_config_get(void);
bool app_config_wifi_ready(void);

#ifdef __cplusplus
}
#endif
