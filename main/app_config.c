#include "app_config.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "cfg";
static const char *NVS_NS = "deye_cfg";
static const char *NVS_NS_LEGACY = "deye";

static app_config_t s_cfg;
static bool s_setup_requested;

static void apply_kconfig_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    /* WiFi from Kconfig is compile-time example only — STA requires NVS. */
    strlcpy(s_cfg.wifi_ssid, CONFIG_DEYE_WIFI_SSID, sizeof(s_cfg.wifi_ssid));
    strlcpy(s_cfg.wifi_pass, CONFIG_DEYE_WIFI_PASSWORD, sizeof(s_cfg.wifi_pass));
    strlcpy(s_cfg.mqtt_host, CONFIG_DEYE_MQTT_HOST, sizeof(s_cfg.mqtt_host));
    if (s_cfg.mqtt_host[0] == '\0') {
        strlcpy(s_cfg.mqtt_host, "192.168.3.249", sizeof(s_cfg.mqtt_host));
    }
    s_cfg.mqtt_port = (uint16_t)CONFIG_DEYE_MQTT_PORT;
    if (s_cfg.mqtt_port == 0) {
        s_cfg.mqtt_port = 1883;
    }
    strlcpy(s_cfg.mqtt_user, CONFIG_DEYE_MQTT_USERNAME, sizeof(s_cfg.mqtt_user));
    strlcpy(s_cfg.mqtt_pass, CONFIG_DEYE_MQTT_PASSWORD, sizeof(s_cfg.mqtt_pass));
    strlcpy(s_cfg.mqtt_base, CONFIG_DEYE_MQTT_BASE_TOPIC, sizeof(s_cfg.mqtt_base));
    if (s_cfg.mqtt_base[0] == '\0') {
        strlcpy(s_cfg.mqtt_base, "iriv/ivt", sizeof(s_cfg.mqtt_base));
    }
    s_cfg.stale_timeout_s = CONFIG_DEYE_STALE_TIMEOUT_S;
    s_cfg.backlight_pct = (uint8_t)CONFIG_DEYE_BACKLIGHT_PCT;
    strlcpy(s_cfg.tz, CONFIG_DEYE_TZ, sizeof(s_cfg.tz));
    s_cfg.provisioned = false;
}

static void nvs_overlay_str(nvs_handle_t nvs, const char *key, char *dst, size_t dst_len, bool *any)
{
    size_t len = dst_len;
    if (nvs_get_str(nvs, key, dst, &len) == ESP_OK) {
        ESP_LOGI(TAG, "NVS override %s", key);
        if (any) {
            *any = true;
        }
    }
}

static void load_namespace(const char *ns, bool *got_wifi)
{
    nvs_handle_t nvs;
    if (nvs_open(ns, NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }

    bool any = false;
    char ssid[sizeof(s_cfg.wifi_ssid)] = {0};
    size_t len = sizeof(ssid);
    if (nvs_get_str(nvs, "wifi_ssid", ssid, &len) == ESP_OK && ssid[0] != '\0') {
        strlcpy(s_cfg.wifi_ssid, ssid, sizeof(s_cfg.wifi_ssid));
        *got_wifi = true;
        any = true;
        ESP_LOGI(TAG, "NVS WiFi SSID from %s", ns);
    }

    nvs_overlay_str(nvs, "wifi_pass", s_cfg.wifi_pass, sizeof(s_cfg.wifi_pass), &any);
    nvs_overlay_str(nvs, "mqtt_host", s_cfg.mqtt_host, sizeof(s_cfg.mqtt_host), &any);
    nvs_overlay_str(nvs, "mqtt_user", s_cfg.mqtt_user, sizeof(s_cfg.mqtt_user), &any);
    nvs_overlay_str(nvs, "mqtt_pass", s_cfg.mqtt_pass, sizeof(s_cfg.mqtt_pass), &any);
    nvs_overlay_str(nvs, "mqtt_base", s_cfg.mqtt_base, sizeof(s_cfg.mqtt_base), &any);
    nvs_overlay_str(nvs, "mqtt_topic", s_cfg.mqtt_base, sizeof(s_cfg.mqtt_base), &any);
    nvs_overlay_str(nvs, "tz", s_cfg.tz, sizeof(s_cfg.tz), &any);

    uint16_t port = 0;
    if (nvs_get_u16(nvs, "mqtt_port", &port) == ESP_OK && port > 0) {
        s_cfg.mqtt_port = port;
        any = true;
    }
    uint8_t bl = 0;
    if (nvs_get_u8(nvs, "backlight", &bl) == ESP_OK && bl >= 5 && bl <= 100) {
        s_cfg.backlight_pct = bl;
    }
    uint8_t stale = 0;
    if (nvs_get_u8(nvs, "stale_s", &stale) == ESP_OK && stale >= 5) {
        s_cfg.stale_timeout_s = stale;
    }
    uint8_t force = 0;
    if (nvs_get_u8(nvs, "force_setup", &force) == ESP_OK && force != 0) {
        s_setup_requested = true;
    }
    (void)any;
    nvs_close(nvs);
}

esp_err_t app_config_init(void)
{
    apply_kconfig_defaults();
    s_setup_requested = false;

    bool got_wifi = false;
    load_namespace(NVS_NS, &got_wifi);
    if (!got_wifi) {
        /* Migrate / accept older namespace used by early builds. */
        load_namespace(NVS_NS_LEGACY, &got_wifi);
    }

    s_cfg.provisioned = got_wifi && !s_setup_requested;
    if (s_setup_requested) {
        ESP_LOGW(TAG, "Setup requested — SoftAP provisioning will start");
    } else if (!got_wifi) {
        ESP_LOGI(TAG, "No NVS WiFi SSID — SoftAP provisioning required");
        /* Do not treat Kconfig WiFi as provisioned. */
        s_cfg.wifi_ssid[0] = '\0';
        s_cfg.wifi_pass[0] = '\0';
    } else {
        ESP_LOGI(TAG, "Provisioned WiFi SSID=%s MQTT=%s:%u topic=%s",
                 s_cfg.wifi_ssid, s_cfg.mqtt_host, s_cfg.mqtt_port, s_cfg.mqtt_base);
    }
    return ESP_OK;
}

const app_config_t *app_config_get(void)
{
    return &s_cfg;
}

bool app_config_is_provisioned(void)
{
    return s_cfg.provisioned;
}

bool app_config_wifi_ready(void)
{
    return s_cfg.provisioned && s_cfg.wifi_ssid[0] != '\0';
}

esp_err_t app_config_save(const app_config_t *cfg)
{
    if (!cfg || cfg->wifi_ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs, "wifi_ssid", cfg->wifi_ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "wifi_pass", cfg->wifi_pass);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "mqtt_host", cfg->mqtt_host);
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(nvs, "mqtt_port", cfg->mqtt_port ? cfg->mqtt_port : 1883);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "mqtt_user", cfg->mqtt_user);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "mqtt_pass", cfg->mqtt_pass);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "mqtt_topic", cfg->mqtt_base);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "mqtt_base", cfg->mqtt_base);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, "force_setup", 0);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (err == ESP_OK) {
        s_cfg = *cfg;
        s_cfg.provisioned = true;
        s_setup_requested = false;
        ESP_LOGI(TAG, "Saved provisioning to NVS (%s)", NVS_NS);
    }
    return err;
}

esp_err_t app_config_clear_provisioning(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    nvs_erase_key(nvs, "wifi_ssid");
    nvs_erase_key(nvs, "wifi_pass");
    nvs_set_u8(nvs, "force_setup", 1);
    err = nvs_commit(nvs);
    nvs_close(nvs);

    s_cfg.wifi_ssid[0] = '\0';
    s_cfg.wifi_pass[0] = '\0';
    s_cfg.provisioned = false;
    s_setup_requested = true;
    return err;
}

esp_err_t app_config_request_setup(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(nvs, "force_setup", 1);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    s_setup_requested = true;
    s_cfg.provisioned = false;
    return err;
}

bool app_config_setup_requested(void)
{
    return s_setup_requested;
}

esp_err_t app_config_clear_setup_request(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(nvs, "force_setup", 0);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    s_setup_requested = false;
    return err;
}
