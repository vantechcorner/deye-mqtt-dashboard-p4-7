#include "mqtt_sub.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "cJSON.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "telemetry.h"
#include "wifi_sta.h"

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t s_client;

static void apply_mqtt_link(bool mqtt_ok)
{
    telemetry_set_link(wifi_sta_is_connected(), mqtt_ok);
}

static void handle_data(esp_mqtt_event_handle_t event)
{
    cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse failed for %.*s", event->topic_len, event->topic);
        return;
    }
    const cJSON *val = cJSON_GetObjectItemCaseSensitive(root, "value");
    if (cJSON_IsNumber(val)) {
        bool ok = telemetry_update_topic(event->topic, (size_t)event->topic_len, (float)val->valuedouble);
        ESP_LOGD(TAG, "%.*s = %.3f%s", event->topic_len, event->topic, val->valuedouble, ok ? "" : " (unmapped)");
    } else {
        ESP_LOGW(TAG, "No numeric value in %.*s", event->topic_len, event->topic);
    }
    cJSON_Delete(root);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "Connected");
        apply_mqtt_link(true);
        const app_config_t *cfg = app_config_get();
        char filter[48];
        snprintf(filter, sizeof(filter), "%s/#", cfg->mqtt_base);
        int msg_id = esp_mqtt_client_subscribe(event->client, filter, 1);
        ESP_LOGI(TAG, "Subscribe %s id=%d", filter, msg_id);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Disconnected");
        apply_mqtt_link(false);
        break;
    case MQTT_EVENT_DATA:
        handle_data(event);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Error");
        apply_mqtt_link(false);
        break;
    default:
        break;
    }
}

esp_err_t mqtt_sub_start(void)
{
    const app_config_t *cfg = app_config_get();
    static char uri[96];
    snprintf(uri, sizeof(uri), "mqtt://%s:%u", cfg->mqtt_host, cfg->mqtt_port);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
        .session.keepalive = 30,
        .network.reconnect_timeout_ms = 5000,
        .credentials.client_id = "deye-p4-7",
        .buffer.size = 2048,
        .task.stack_size = 6144,
    };
    if (cfg->mqtt_user[0] != '\0') {
        mqtt_cfg.credentials.username = cfg->mqtt_user;
        mqtt_cfg.credentials.authentication.password = cfg->mqtt_pass;
    }

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client) {
        return ESP_FAIL;
    }
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_LOGI(TAG, "Start %s", uri);
    return esp_mqtt_client_start(s_client);
}
