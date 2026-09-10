#include "ntp_sync.h"

#include <stdlib.h>
#include <time.h>

#include "app_config.h"
#include "esp_log.h"
#include "esp_sntp.h"

static const char *TAG = "ntp";

void ntp_sync_start(void)
{
    const app_config_t *cfg = app_config_get();
    setenv("TZ", cfg->tz, 1);
    tzset();
    ESP_LOGI(TAG, "SNTP start TZ=%s", cfg->tz);
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
}
