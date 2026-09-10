#include "app_config.h"
#include "bsp_p4_mipi7.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "mqtt_sub.h"
#include "nvs_flash.h"
#include "ntp_sync.h"
#include "sdkconfig.h"
#include "telemetry.h"
#include "ui.h"
#include "wifi_sta.h"

#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "app";

static void on_got_ip(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_id;
    (void)event_data;
    static bool started = false;
    if (started) {
        return;
    }
    started = true;
    ntp_sync_start();
    ESP_ERROR_CHECK(mqtt_sub_start());
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(app_config_init());

    const app_config_t *cfg = app_config_get();
    telemetry_init(cfg->stale_timeout_s);

    ESP_LOGI(TAG, "Bring-up TD2 MIPI panel (%dx%d, %d lanes)",
             CONFIG_DEYE_PANEL_H_RES, CONFIG_DEYE_PANEL_V_RES, CONFIG_DEYE_DSI_LANES);
    err = bsp_p4_mipi7_init(cfg->backlight_pct);
    if (err == ESP_OK) {
        ESP_ERROR_CHECK(bsp_p4_mipi7_lock(UINT32_MAX));
        ui_init();
        bsp_p4_mipi7_unlock();
        ESP_LOGI(TAG, "UI started");
    } else {
        ESP_LOGW(TAG, "Display not ready (%s) — MQTT path still usable once network is up",
                 esp_err_to_name(err));
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL, NULL));
#if CONFIG_DEYE_NET_ETHERNET
    /* Ethernet path: register IP_EVENT_ETH_GOT_IP and start eth driver in a later commit. */
    ESP_LOGW(TAG, "CONFIG_DEYE_NET_ETHERNET=y but Ethernet driver not wired yet");
#else
    err = wifi_sta_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi not started; dashboard stays offline until Hosted Wi-Fi / Ethernet / NVS is set");
    }
#endif
}
