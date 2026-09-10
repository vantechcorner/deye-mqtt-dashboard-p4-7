#include "wifi_sta.h"

#include <string.h>

#include "app_config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "telemetry.h"

static const char *TAG = "wifi";
static EventGroupHandle_t s_wifi_events;
static bool s_connected;

#define WIFI_GOT_IP_BIT BIT0

static void apply_link(void)
{
    telemetry_set_link(s_connected, false);
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        apply_link();
        ESP_LOGW(TAG, "Disconnected, retrying");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP " IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;
        telemetry_set_link(true, false);
        if (s_wifi_events) {
            xEventGroupSetBits(s_wifi_events, WIFI_GOT_IP_BIT);
        }
    }
}

esp_err_t wifi_sta_start(void)
{
    const app_config_t *cfg = app_config_get();
    if (cfg->wifi_ssid[0] == '\0') {
        ESP_LOGW(TAG, "WiFi SSID empty — set CONFIG_DEYE_WIFI_SSID or NVS wifi_ssid");
        s_connected = false;
        telemetry_set_link(false, false);
        return ESP_ERR_INVALID_STATE;
    }

    s_wifi_events = xEventGroupCreate();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, cfg->wifi_ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, cfg->wifi_pass, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = (cfg->wifi_pass[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Connecting to SSID %s", cfg->wifi_ssid);
    return ESP_OK;
}

bool wifi_sta_is_connected(void)
{
    return s_connected;
}
