#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** SoftAP SSID written by wifi_prov_start (e.g. Deye-P4-A1B2). */
const char *wifi_prov_ap_ssid(void);

/**
 * Fill buf with MAC-suffix SoftAP name.
 * Prefer WIFI_IF_AP MAC after Wi-Fi init (ESP-Hosted C6); else base MAC.
 */
void wifi_prov_make_ap_ssid(char *buf, size_t buf_len);

/** Portal URL, typically http://192.168.4.1 */
const char *wifi_prov_portal_url(void);

bool wifi_prov_is_active(void);

/** Called once SoftAP SSID is known (after Wi-Fi start), before blocking on save. */
typedef void (*wifi_prov_ready_cb_t)(const char *ap_ssid, const char *portal_url);

/**
 * Start SoftAP + HTTP config portal (and lightweight captive DNS).
 * Invokes on_ready with the real SoftAP SSID when the AP is up.
 * Blocks until credentials are saved, then returns ESP_OK (caller should reboot),
 * or returns an error if SoftAP/HTTP failed to start.
 */
esp_err_t wifi_prov_start(wifi_prov_ready_cb_t on_ready);

#ifdef __cplusplus
}
#endif
