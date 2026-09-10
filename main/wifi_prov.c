#include "wifi_prov.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"
#include <unistd.h>

static const char *TAG = "wifi_prov";

#define WIFI_PROV_DONE_BIT BIT0
#define DNS_PORT 53

static char s_ap_ssid[32];
static const char *s_portal_url = "http://192.168.4.1";
static EventGroupHandle_t s_events;
static httpd_handle_t s_httpd;
static bool s_active;
static TaskHandle_t s_dns_task;

static const char PROV_HTML[] =
    "<!DOCTYPE html><html><head><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>Deye P4 Setup</title>"
    "<style>"
    "body{font-family:system-ui,sans-serif;margin:0;background:#12161c;color:#e6edf3}"
    "main{max-width:420px;margin:0 auto;padding:20px}"
    "h1{font-size:1.35rem;margin:0 0 8px}"
    "p{color:#8b949e;font-size:.95rem}"
    "label{display:block;margin:14px 0 6px;font-size:.85rem;color:#8b949e}"
    "input{width:100%%;box-sizing:border-box;padding:12px;border-radius:10px;"
    "border:1px solid #2a303a;background:#1a1f27;color:#e6edf3;font-size:1rem}"
    "button{width:100%%;margin-top:22px;padding:14px;border:0;border-radius:10px;"
    "background:#58a6ff;color:#0d1117;font-weight:600;font-size:1rem}"
    ".hint{font-size:.8rem;margin-top:16px}"
    "</style></head><body><main>"
    "<h1>Deye P4 Setup</h1>"
    "<p>Configure Wi‑Fi and MQTT, then Save. The device will reboot.</p>"
    "<form method=POST action=/save>"
    "<label>Wi‑Fi SSID *</label><input name=wifi_ssid required maxlength=32 value=\"%s\">"
    "<label>Wi‑Fi password</label><input name=wifi_pass type=password maxlength=64 value=\"%s\">"
    "<label>MQTT host *</label><input name=mqtt_host required maxlength=63 value=\"%s\">"
    "<label>MQTT port</label><input name=mqtt_port type=number min=1 max=65535 value=\"%u\">"
    "<label>MQTT username</label><input name=mqtt_user maxlength=32 value=\"%s\">"
    "<label>MQTT password</label><input name=mqtt_pass type=password maxlength=64 value=\"%s\">"
    "<label>MQTT base topic *</label><input name=mqtt_topic required maxlength=31 value=\"%s\">"
    "<button type=submit>Save &amp; Reboot</button>"
    "</form>"
    "<p class=hint>Portal: %s — AP: %s</p>"
    "</main></body></html>";

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static void url_decode(char *dst, size_t dst_len, const char *src, size_t src_len)
{
    size_t di = 0;
    for (size_t i = 0; i < src_len && di + 1 < dst_len; i++) {
        char c = src[i];
        if (c == '+') {
            dst[di++] = ' ';
        } else if (c == '%' && i + 2 < src_len) {
            int hi = hex_nibble(src[i + 1]);
            int lo = hex_nibble(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                dst[di++] = (char)((hi << 4) | lo);
                i += 2;
            } else {
                dst[di++] = c;
            }
        } else {
            dst[di++] = c;
        }
    }
    dst[di] = '\0';
}

static bool form_get(const char *body, size_t body_len, const char *key, char *out, size_t out_len)
{
    size_t key_len = strlen(key);
    const char *p = body;
    const char *end = body + body_len;
    while (p < end) {
        const char *amp = memchr(p, '&', (size_t)(end - p));
        size_t pair_len = amp ? (size_t)(amp - p) : (size_t)(end - p);
        const char *eq = memchr(p, '=', pair_len);
        if (eq) {
            size_t klen = (size_t)(eq - p);
            if (klen == key_len && strncmp(p, key, key_len) == 0) {
                url_decode(out, out_len, eq + 1, pair_len - klen - 1);
                return true;
            }
        }
        p = amp ? amp + 1 : end;
    }
    if (out_len) {
        out[0] = '\0';
    }
    return false;
}

static esp_err_t send_form(httpd_req_t *req)
{
    const app_config_t *cfg = app_config_get();
    char *page = malloc(3072);
    if (!page) {
        return ESP_ERR_NO_MEM;
    }
    const char *mqtt_host = cfg->mqtt_host[0] ? cfg->mqtt_host : "192.168.3.249";
    const char *mqtt_topic = cfg->mqtt_base[0] ? cfg->mqtt_base : "iriv/ivt";
    unsigned mqtt_port = cfg->mqtt_port ? cfg->mqtt_port : 1883;
    snprintf(page, 3072, PROV_HTML,
             cfg->wifi_ssid,
             "", /* never echo wifi password */
             mqtt_host,
             mqtt_port,
             cfg->mqtt_user,
             "",
             mqtt_topic,
             s_portal_url,
             s_ap_ssid);
    httpd_resp_set_type(req, "text/html");
    esp_err_t err = httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
    free(page);
    return err;
}

static esp_err_t root_get(httpd_req_t *req)
{
    return send_form(req);
}

static esp_err_t captive_get(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t save_post(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 2048) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
        return ESP_FAIL;
    }
    char *body = calloc(1, req->content_len + 1);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }
    int received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, body + received, req->content_len - received);
        if (r <= 0) {
            free(body);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Recv failed");
            return ESP_FAIL;
        }
        received += r;
    }

    app_config_t cfg = *app_config_get();
    char port_buf[16] = {0};
    form_get(body, (size_t)received, "wifi_ssid", cfg.wifi_ssid, sizeof(cfg.wifi_ssid));
    form_get(body, (size_t)received, "wifi_pass", cfg.wifi_pass, sizeof(cfg.wifi_pass));
    form_get(body, (size_t)received, "mqtt_host", cfg.mqtt_host, sizeof(cfg.mqtt_host));
    form_get(body, (size_t)received, "mqtt_user", cfg.mqtt_user, sizeof(cfg.mqtt_user));
    form_get(body, (size_t)received, "mqtt_pass", cfg.mqtt_pass, sizeof(cfg.mqtt_pass));
    form_get(body, (size_t)received, "mqtt_topic", cfg.mqtt_base, sizeof(cfg.mqtt_base));
    if (form_get(body, (size_t)received, "mqtt_port", port_buf, sizeof(port_buf))) {
        int p = atoi(port_buf);
        if (p > 0 && p <= 65535) {
            cfg.mqtt_port = (uint16_t)p;
        }
    }
    free(body);

    if (cfg.wifi_ssid[0] == '\0' || cfg.mqtt_host[0] == '\0' || cfg.mqtt_base[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID, MQTT host and topic required");
        return ESP_FAIL;
    }

    esp_err_t err = app_config_save(&cfg);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS save failed");
        return err;
    }

    const char *ok =
        "<!DOCTYPE html><html><body style=\"font-family:sans-serif;background:#12161c;color:#e6edf3;padding:24px\">"
        "<h1>Saved</h1><p>Rebooting into station mode…</p></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, ok, HTTPD_RESP_USE_STRLEN);

    if (s_events) {
        xEventGroupSetBits(s_events, WIFI_PROV_DONE_BIT);
    }
    return ESP_OK;
}

static void dns_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        vTaskDelete(NULL);
        return;
    }
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    uint8_t buf[512];
    while (s_active) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fromlen);
        if (len < 12) {
            continue;
        }
        /* Build a minimal DNS response pointing A records to 192.168.4.1 */
        uint8_t resp[512];
        if ((size_t)len + 16 > sizeof(resp)) {
            continue;
        }
        memcpy(resp, buf, (size_t)len);
        resp[2] = 0x81; /* response, recursion available-ish */
        resp[3] = 0x80;
        resp[4] = buf[4];
        resp[5] = buf[5];
        resp[6] = buf[4]; /* ANCOUNT = QDCOUNT */
        resp[7] = buf[5];
        resp[8] = 0;
        resp[9] = 0;
        resp[10] = 0;
        resp[11] = 0;

        int pos = len;
        /* Append one A answer using pointer to name at offset 12 */
        resp[pos++] = 0xC0;
        resp[pos++] = 0x0C;
        resp[pos++] = 0x00;
        resp[pos++] = 0x01; /* A */
        resp[pos++] = 0x00;
        resp[pos++] = 0x01; /* IN */
        resp[pos++] = 0x00;
        resp[pos++] = 0x00;
        resp[pos++] = 0x00;
        resp[pos++] = 0x1E; /* TTL */
        resp[pos++] = 0x00;
        resp[pos++] = 0x04;
        resp[pos++] = 192;
        resp[pos++] = 168;
        resp[pos++] = 4;
        resp[pos++] = 1;
        sendto(sock, resp, pos, 0, (struct sockaddr *)&from, fromlen);
    }
    close(sock);
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

static bool mac_nonzero(const uint8_t mac[6])
{
    return (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) != 0;
}

void wifi_prov_make_ap_ssid(char *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) {
        return;
    }
    uint8_t mac[6] = {0};
    /* P4 SoftAP is via ESP-Hosted C6 — use Wi-Fi iface MAC when available. */
    if (esp_wifi_get_mac(WIFI_IF_AP, mac) != ESP_OK || !mac_nonzero(mac)) {
        if (esp_wifi_get_mac(WIFI_IF_STA, mac) != ESP_OK || !mac_nonzero(mac)) {
            esp_read_mac(mac, ESP_MAC_BASE);
        }
    }
    if (!mac_nonzero(mac)) {
        esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);
    }
    snprintf(buf, buf_len, "Deye-P4-%02X%02X", mac[4], mac[5]);
}

static void build_ap_ssid(void)
{
    wifi_prov_make_ap_ssid(s_ap_ssid, sizeof(s_ap_ssid));
}

const char *wifi_prov_ap_ssid(void)
{
    return s_ap_ssid[0] ? s_ap_ssid : "Deye-P4-Setup";
}

const char *wifi_prov_portal_url(void)
{
    return s_portal_url;
}

bool wifi_prov_is_active(void)
{
    return s_active;
}

esp_err_t wifi_prov_start(wifi_prov_ready_cb_t on_ready)
{
    s_events = xEventGroupCreate();
    if (!s_events) {
        return ESP_ERR_NO_MEM;
    }

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set mode");

    /* MAC is valid after set_mode on hosted Wi-Fi; build SSID before set_config. */
    build_ap_ssid();

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.ap.ssid, s_ap_ssid, sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(s_ap_ssid);
    wifi_config.ap.channel = 1;
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wifi_config), TAG, "set config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");

    /* Re-read SoftAP MAC after start in case hosted slave filled it late. */
    char started_ssid[sizeof(s_ap_ssid)];
    wifi_prov_make_ap_ssid(started_ssid, sizeof(started_ssid));
    if (strcmp(started_ssid, s_ap_ssid) != 0) {
        const char *suffix = strrchr(started_ssid, '-');
        bool new_ok = suffix && strcmp(suffix, "-0000") != 0;
        if (new_ok) {
            strlcpy(s_ap_ssid, started_ssid, sizeof(s_ap_ssid));
            strlcpy((char *)wifi_config.ap.ssid, s_ap_ssid, sizeof(wifi_config.ap.ssid));
            wifi_config.ap.ssid_len = strlen(s_ap_ssid);
            esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
        }
    }

    s_active = true;
    xTaskCreate(dns_task, "prov_dns", 3072, NULL, 5, &s_dns_task);

    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.server_port = 80;
    http_cfg.max_uri_handlers = 8;
    http_cfg.lru_purge_enable = true;
    ESP_RETURN_ON_ERROR(httpd_start(&s_httpd, &http_cfg), TAG, "httpd");

    const httpd_uri_t uri_root = {.uri = "/", .method = HTTP_GET, .handler = root_get};
    const httpd_uri_t uri_save = {.uri = "/save", .method = HTTP_POST, .handler = save_post};
    const httpd_uri_t uri_gen = {.uri = "/generate_204", .method = HTTP_GET, .handler = captive_get};
    const httpd_uri_t uri_hotspot = {.uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_get};
    const httpd_uri_t uri_ncsi = {.uri = "/ncsi.txt", .method = HTTP_GET, .handler = captive_get};
    httpd_register_uri_handler(s_httpd, &uri_root);
    httpd_register_uri_handler(s_httpd, &uri_save);
    httpd_register_uri_handler(s_httpd, &uri_gen);
    httpd_register_uri_handler(s_httpd, &uri_hotspot);
    httpd_register_uri_handler(s_httpd, &uri_ncsi);

    ESP_LOGI(TAG, "SoftAP \"%s\" open — open %s", s_ap_ssid, s_portal_url);
    if (on_ready) {
        on_ready(s_ap_ssid, s_portal_url);
    }

    /* Wait until form save succeeds. */
    xEventGroupWaitBits(s_events, WIFI_PROV_DONE_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

    /* Brief delay so browser gets the OK page. */
    vTaskDelay(pdMS_TO_TICKS(800));
    s_active = false;
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
    return ESP_OK;
}
