#include "app_config.h"
#include "bsp_p4_mipi7.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_sub.h"
#include "nvs_flash.h"
#include "ntp_sync.h"
#include "sdkconfig.h"
#include "telemetry.h"
#include "ui.h"
#include "wifi_prov.h"
#include "wifi_sta.h"

#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "app";
static bool s_display_ok;

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

static void on_ui_request_setup(void)
{
    ESP_LOGW(TAG, "UI requested SoftAP setup — rebooting");
    app_config_request_setup();
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

static void on_prov_ready(const char *ap_ssid, const char *portal_url)
{
    if (!s_display_ok) {
        return;
    }
    ESP_ERROR_CHECK(bsp_p4_mipi7_lock(UINT32_MAX));
    ui_show_provisioning(ap_ssid, portal_url);
    bsp_p4_mipi7_unlock();
}

static void run_provisioning(void)
{
    ESP_LOGI(TAG, "Entering SoftAP provisioning");
    /* UI updates from on_prov_ready once SoftAP SSID is known (blocks until save). */
    esp_err_t err = wifi_prov_start(on_prov_ready);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Provisioning saved — reboot");
        esp_restart();
    }
    ESP_LOGE(TAG, "Provisioning failed: %s", esp_err_to_name(err));
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
    s_display_ok = false;
    err = bsp_p4_mipi7_init(cfg->backlight_pct);
    if (err == ESP_OK) {
        s_display_ok = true;
        ESP_ERROR_CHECK(bsp_p4_mipi7_lock(UINT32_MAX));
        ui_init();
        ui_set_setup_request_cb(on_ui_request_setup);
        bsp_p4_mipi7_unlock();
        ESP_LOGI(TAG, "UI started");
    } else {
        ESP_LOGW(TAG, "Display not ready (%s) — network path still usable",
                 esp_err_to_name(err));
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

#if CONFIG_DEYE_NET_ETHERNET
    /* Ethernet path: register IP_EVENT_ETH_GOT_IP and start eth driver in a later commit. */
    ESP_LOGW(TAG, "CONFIG_DEYE_NET_ETHERNET=y but Ethernet driver not wired yet");
#else
    if (!app_config_wifi_ready()) {
        run_provisioning();
        return;
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL, NULL));
    err = wifi_sta_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi STA not started — falling back to SoftAP setup");
        run_provisioning();
    }
#endif
}
