#include "app_config.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "cfg";
static app_config_t s_cfg;

static void nvs_overlay_str(nvs_handle_t nvs, const char *key, char *dst, size_t dst_len)
{
    size_t len = dst_len;
    if (nvs_get_str(nvs, key, dst, &len) == ESP_OK) {
        ESP_LOGI(TAG, "NVS override %s", key);
    }
}

esp_err_t app_config_init(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    strlcpy(s_cfg.wifi_ssid, CONFIG_DEYE_WIFI_SSID, sizeof(s_cfg.wifi_ssid));
    strlcpy(s_cfg.wifi_pass, CONFIG_DEYE_WIFI_PASSWORD, sizeof(s_cfg.wifi_pass));
    strlcpy(s_cfg.mqtt_host, CONFIG_DEYE_MQTT_HOST, sizeof(s_cfg.mqtt_host));
    s_cfg.mqtt_port = (uint16_t)CONFIG_DEYE_MQTT_PORT;
    strlcpy(s_cfg.mqtt_user, CONFIG_DEYE_MQTT_USERNAME, sizeof(s_cfg.mqtt_user));
    strlcpy(s_cfg.mqtt_pass, CONFIG_DEYE_MQTT_PASSWORD, sizeof(s_cfg.mqtt_pass));
    strlcpy(s_cfg.mqtt_base, CONFIG_DEYE_MQTT_BASE_TOPIC, sizeof(s_cfg.mqtt_base));
    s_cfg.stale_timeout_s = CONFIG_DEYE_STALE_TIMEOUT_S;
    s_cfg.backlight_pct = (uint8_t)CONFIG_DEYE_BACKLIGHT_PCT;
    strlcpy(s_cfg.tz, CONFIG_DEYE_TZ, sizeof(s_cfg.tz));

    nvs_handle_t nvs;
    if (nvs_open("deye", NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGI(TAG, "No NVS namespace yet, using Kconfig defaults");
        return ESP_OK;
    }
    nvs_overlay_str(nvs, "wifi_ssid", s_cfg.wifi_ssid, sizeof(s_cfg.wifi_ssid));
    nvs_overlay_str(nvs, "wifi_pass", s_cfg.wifi_pass, sizeof(s_cfg.wifi_pass));
    nvs_overlay_str(nvs, "mqtt_host", s_cfg.mqtt_host, sizeof(s_cfg.mqtt_host));
    nvs_overlay_str(nvs, "mqtt_user", s_cfg.mqtt_user, sizeof(s_cfg.mqtt_user));
    nvs_overlay_str(nvs, "mqtt_pass", s_cfg.mqtt_pass, sizeof(s_cfg.mqtt_pass));
    nvs_overlay_str(nvs, "mqtt_base", s_cfg.mqtt_base, sizeof(s_cfg.mqtt_base));
    nvs_overlay_str(nvs, "tz", s_cfg.tz, sizeof(s_cfg.tz));
    uint16_t port = 0;
    if (nvs_get_u16(nvs, "mqtt_port", &port) == ESP_OK && port > 0) {
        s_cfg.mqtt_port = port;
    }
    uint8_t bl = 0;
    if (nvs_get_u8(nvs, "backlight", &bl) == ESP_OK && bl >= 5 && bl <= 100) {
        s_cfg.backlight_pct = bl;
    }
    uint8_t stale = 0;
    if (nvs_get_u8(nvs, "stale_s", &stale) == ESP_OK && stale >= 5) {
        s_cfg.stale_timeout_s = stale;
    }
    nvs_close(nvs);
    return ESP_OK;
}

const app_config_t *app_config_get(void)
{
    return &s_cfg;
}

bool app_config_wifi_ready(void)
{
    return s_cfg.wifi_ssid[0] != '\0';
}
