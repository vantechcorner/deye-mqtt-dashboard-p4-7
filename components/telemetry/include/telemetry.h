#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TELEMETRY_STALE_PLACEHOLDER "—"

typedef enum {
    METRIC_STATUS = 0,
    METRIC_BATT_V,
    METRIC_BATT_SOC,
    METRIC_BATT_P,
    METRIC_BATT_I,
    METRIC_BATT_T,
    METRIC_BATT_CHG_TODAY,
    METRIC_BATT_DIS_TODAY,
    METRIC_PV1_V,
    METRIC_PV1_I,
    METRIC_PV1_P,
    METRIC_PV2_V,
    METRIC_PV2_I,
    METRIC_PV2_P,
    METRIC_PV_ENERGY_TODAY,
    METRIC_GRID_HZ,
    METRIC_GRID_V,
    METRIC_GRID_I,
    METRIC_GRID_P_CT,
    METRIC_GRID_BUY_TODAY,
    METRIC_GRID_SELL_TODAY,
    METRIC_INV_T,
    METRIC_INV_V,
    METRIC_INV_P,
    METRIC_INV_HZ,
    METRIC_LOAD_P,
    METRIC_LOAD_V,
    METRIC_LOAD_I,
    METRIC_LOAD_ENERGY_TODAY,
    METRIC_COUNT
} metric_id_t;

typedef struct {
    float value;
    int64_t last_us;
    bool valid;
} metric_sample_t;

typedef struct {
    metric_sample_t m[METRIC_COUNT];
    bool wifi_connected;
    bool mqtt_connected;
} telemetry_snapshot_t;

void telemetry_init(uint32_t stale_timeout_s);
void telemetry_set_link(bool wifi_ok, bool mqtt_ok);
bool telemetry_update_topic(const char *topic, size_t topic_len, float value);
void telemetry_get_snapshot(telemetry_snapshot_t *out);
bool telemetry_is_fresh(const telemetry_snapshot_t *snap, metric_id_t id);
float telemetry_pv_total_w(const telemetry_snapshot_t *snap, bool *fresh);
float telemetry_pv_voltage(const telemetry_snapshot_t *snap, bool *fresh);
float telemetry_pv_current(const telemetry_snapshot_t *snap, bool *fresh);
float telemetry_load_voltage(const telemetry_snapshot_t *snap, bool *fresh);
float telemetry_load_current(const telemetry_snapshot_t *snap, bool *fresh);
/* House load (W): LOAD port + grid import that is not charging the battery. */
float telemetry_house_power_w(const telemetry_snapshot_t *snap, bool *fresh);
/* GRID_UNKNOWN if voltage/Hz are stale; otherwise on/off from the same thresholds as the status bar. */
typedef enum {
    TELEMETRY_GRID_UNKNOWN = 0,
    TELEMETRY_GRID_ON,
    TELEMETRY_GRID_OFF,
} telemetry_grid_link_t;
telemetry_grid_link_t telemetry_grid_link(const telemetry_snapshot_t *snap);
/* HA energy-distribution Home: PV + buy + discharge − sell − charge (kWh). */
float telemetry_home_energy_today(const telemetry_snapshot_t *snap, bool *fresh);
const char *telemetry_status_text(const telemetry_snapshot_t *snap, bool *fresh);
uint32_t telemetry_status_color(const telemetry_snapshot_t *snap);
void telemetry_format(const telemetry_snapshot_t *snap, metric_id_t id, char *buf, size_t len);
void telemetry_format_signed_w(const telemetry_snapshot_t *snap, metric_id_t id, char *buf, size_t len);
void telemetry_format_pv_total(const telemetry_snapshot_t *snap, char *buf, size_t len);

#ifdef __cplusplus
}
#endif
