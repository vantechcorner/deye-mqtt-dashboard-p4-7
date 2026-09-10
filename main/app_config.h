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
    bool provisioned; /**< true when NVS has a Wi‑Fi SSID (not Kconfig alone) */
} app_config_t;

esp_err_t app_config_init(void);
const app_config_t *app_config_get(void);

/** True when NVS-provisioned Wi‑Fi SSID is present and setup was not forced. */
bool app_config_wifi_ready(void);
bool app_config_is_provisioned(void);

/** Persist full runtime config to NVS namespace deye_cfg and mark provisioned. */
esp_err_t app_config_save(const app_config_t *cfg);

/** Clear Wi‑Fi (+ optional MQTT) from NVS and mark unprovisioned. */
esp_err_t app_config_clear_provisioning(void);

/** Set NVS flag so next boot (or this boot) enters SoftAP setup. */
esp_err_t app_config_request_setup(void);
bool app_config_setup_requested(void);
esp_err_t app_config_clear_setup_request(void);

#ifdef __cplusplus
}
#endif
