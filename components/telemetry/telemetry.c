#include "telemetry.h"

#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static telemetry_snapshot_t s_snap;
static SemaphoreHandle_t s_mutex;

typedef struct {
    const char *suffix;
    metric_id_t id;
    uint16_t stale_s;
} topic_map_t;

/* Stale windows follow IRIV poll groups: 2s / 10s / 30s, with slack for jitter. */
static const topic_map_t k_map[] = {
    {"status", METRIC_STATUS, 25},
    {"battery/voltage", METRIC_BATT_V, 10},
    {"battery/soc", METRIC_BATT_SOC, 10},
    {"battery/power", METRIC_BATT_P, 10},
    {"battery/current", METRIC_BATT_I, 10},
    {"battery/temperature", METRIC_BATT_T, 25},
    {"battery/charge_today", METRIC_BATT_CHG_TODAY, 90},
    {"battery/discharge_today", METRIC_BATT_DIS_TODAY, 90},
    {"pv1/voltage", METRIC_PV1_V, 10},
    {"pv1/current", METRIC_PV1_I, 10},
    {"pv1/power", METRIC_PV1_P, 10},
    {"pv2/voltage", METRIC_PV2_V, 10},
    {"pv2/current", METRIC_PV2_I, 10},
    {"pv2/power", METRIC_PV2_P, 10},
    {"pv/energy_today", METRIC_PV_ENERGY_TODAY, 90},
    {"grid/frequency", METRIC_GRID_HZ, 10},
    {"grid/voltage", METRIC_GRID_V, 10},
    {"grid/current", METRIC_GRID_I, 10},
    {"grid/power_ct", METRIC_GRID_P_CT, 10},
    {"grid/buy_today", METRIC_GRID_BUY_TODAY, 90},
    {"grid/sell_today", METRIC_GRID_SELL_TODAY, 90},
    {"inverter/temperature", METRIC_INV_T, 25},
    {"inverter/voltage", METRIC_INV_V, 10},
    {"inverter/power", METRIC_INV_P, 10},
    {"inverter/frequency", METRIC_INV_HZ, 10},
    {"load/power", METRIC_LOAD_P, 10},
    {"load/voltage", METRIC_LOAD_V, 10},
    {"load/current", METRIC_LOAD_I, 10},
    {"load/energy_today", METRIC_LOAD_ENERGY_TODAY, 90},
};

static uint16_t s_stale_s[METRIC_COUNT];

void telemetry_init(uint32_t stale_timeout_s)
{
    (void)stale_timeout_s;
    memset(&s_snap, 0, sizeof(s_snap));
    s_mutex = xSemaphoreCreateMutex();
    for (size_t i = 0; i < sizeof(k_map) / sizeof(k_map[0]); i++) {
        s_stale_s[k_map[i].id] = k_map[i].stale_s;
    }
}

void telemetry_set_link(bool wifi_ok, bool mqtt_ok)
{
    if (!s_mutex) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_snap.wifi_connected = wifi_ok;
    s_snap.mqtt_connected = mqtt_ok;
    xSemaphoreGive(s_mutex);
}

static bool suffix_eq(const char *topic, size_t topic_len, const char *suffix)
{
    const char *base = "iriv/ivt/";
    const size_t base_len = 9;
    if (topic_len < base_len || memcmp(topic, base, base_len) != 0) {
        return false;
    }
    size_t slen = strlen(suffix);
    if (topic_len - base_len != slen) {
        return false;
    }
    return memcmp(topic + base_len, suffix, slen) == 0;
}

bool telemetry_update_topic(const char *topic, size_t topic_len, float value)
{
    metric_id_t id = METRIC_COUNT;
    for (size_t i = 0; i < sizeof(k_map) / sizeof(k_map[0]); i++) {
        if (suffix_eq(topic, topic_len, k_map[i].suffix)) {
            id = k_map[i].id;
            break;
        }
    }
    if (id == METRIC_COUNT) {
        return false;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_snap.m[id].value = value;
    s_snap.m[id].last_us = esp_timer_get_time();
    s_snap.m[id].valid = true;
    xSemaphoreGive(s_mutex);
    return true;
}

void telemetry_get_snapshot(telemetry_snapshot_t *out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_snap;
    xSemaphoreGive(s_mutex);
}

bool telemetry_is_fresh(const telemetry_snapshot_t *snap, metric_id_t id)
{
    if (!snap->m[id].valid) {
        return false;
    }
    int64_t age = esp_timer_get_time() - snap->m[id].last_us;
    int64_t limit_us = (int64_t)s_stale_s[id] * 1000000LL;
    if (limit_us <= 0) {
        limit_us = 5 * 1000000LL;
    }
    return age >= 0 && age <= limit_us;
}

float telemetry_pv_total_w(const telemetry_snapshot_t *snap, bool *fresh)
{
    bool f1 = telemetry_is_fresh(snap, METRIC_PV1_P);
    bool f2 = telemetry_is_fresh(snap, METRIC_PV2_P);
    *fresh = f1 || f2;
    float total = 0.f;
    if (f1) {
        total += snap->m[METRIC_PV1_P].value;
    }
    if (f2) {
        total += snap->m[METRIC_PV2_P].value;
    }
    return total;
}

float telemetry_pv_voltage(const telemetry_snapshot_t *snap, bool *fresh)
{
    bool f1 = telemetry_is_fresh(snap, METRIC_PV1_V);
    bool f2 = telemetry_is_fresh(snap, METRIC_PV2_V);
    *fresh = f1 || f2;
    if (f1 && f2) {
        /* Prefer the string that is currently producing. */
        bool p1 = telemetry_is_fresh(snap, METRIC_PV1_P);
        bool p2 = telemetry_is_fresh(snap, METRIC_PV2_P);
        float w1 = p1 ? snap->m[METRIC_PV1_P].value : 0.f;
        float w2 = p2 ? snap->m[METRIC_PV2_P].value : 0.f;
        if (w2 > w1) {
            return snap->m[METRIC_PV2_V].value;
        }
        return snap->m[METRIC_PV1_V].value;
    }
    if (f1) {
        return snap->m[METRIC_PV1_V].value;
    }
    if (f2) {
        return snap->m[METRIC_PV2_V].value;
    }
    return 0.f;
}

float telemetry_pv_current(const telemetry_snapshot_t *snap, bool *fresh)
{
    bool f1 = telemetry_is_fresh(snap, METRIC_PV1_I);
    bool f2 = telemetry_is_fresh(snap, METRIC_PV2_I);
    *fresh = f1 || f2;
    float total = 0.f;
    if (f1) {
        total += snap->m[METRIC_PV1_I].value;
    }
    if (f2) {
        total += snap->m[METRIC_PV2_I].value;
    }
    return total;
}

float telemetry_load_voltage(const telemetry_snapshot_t *snap, bool *fresh)
{
    if (telemetry_is_fresh(snap, METRIC_LOAD_V)) {
        *fresh = true;
        return snap->m[METRIC_LOAD_V].value;
    }
    if (telemetry_is_fresh(snap, METRIC_INV_V)) {
        *fresh = true;
        return snap->m[METRIC_INV_V].value;
    }
    if (telemetry_is_fresh(snap, METRIC_GRID_V)) {
        *fresh = true;
        return snap->m[METRIC_GRID_V].value;
    }
    *fresh = false;
    return 0.f;
}

float telemetry_load_current(const telemetry_snapshot_t *snap, bool *fresh)
{
    if (telemetry_is_fresh(snap, METRIC_LOAD_I)) {
        *fresh = true;
        return snap->m[METRIC_LOAD_I].value;
    }
    *fresh = false;
    return 0.f;
}

telemetry_grid_link_t telemetry_grid_link(const telemetry_snapshot_t *snap)
{
    bool v_ok = telemetry_is_fresh(snap, METRIC_GRID_V);
    bool hz_ok = telemetry_is_fresh(snap, METRIC_GRID_HZ);
    if (!v_ok && !hz_ok) {
        return TELEMETRY_GRID_UNKNOWN;
    }
    if (v_ok && snap->m[METRIC_GRID_V].value >= 80.f) {
        return TELEMETRY_GRID_ON;
    }
    if (hz_ok) {
        float hz = snap->m[METRIC_GRID_HZ].value;
        if (hz >= 45.f && hz <= 66.f) {
            return TELEMETRY_GRID_ON;
        }
    }
    return TELEMETRY_GRID_OFF;
}

float telemetry_house_power_w(const telemetry_snapshot_t *snap, bool *fresh)
{
    /* load/power is the LOAD/UPS port only. grid/power_ct > 0 is import at the
     * CT, which also includes AC drawn to charge the battery.
     * battery/power < 0 means DC power into the pack (charging).
     * PV is assumed to cover that charge before the grid does, so off-grid
     * solar charging does not shrink the LOAD-port figure.
     * While the grid is the charge source, the CT reading is inverter intake
     * (charge + conversion loss), not house consumption — Deye shows that as
     * Load 0 W and UPS-Load = load/power. */
    bool load_ok = telemetry_is_fresh(snap, METRIC_LOAD_P);
    bool grid_ok = telemetry_is_fresh(snap, METRIC_GRID_P_CT);
    *fresh = load_ok || grid_ok;
    if (!*fresh) {
        return 0.f;
    }

    float essential = load_ok ? snap->m[METRIC_LOAD_P].value : 0.f;
    if (essential < 0.f) {
        essential = 0.f;
    }

    float grid_import = 0.f;
    if (grid_ok && snap->m[METRIC_GRID_P_CT].value > 0.f) {
        grid_import = snap->m[METRIC_GRID_P_CT].value;
    }

    float charge = 0.f;
    if (telemetry_is_fresh(snap, METRIC_BATT_P) && snap->m[METRIC_BATT_P].value < 0.f) {
        charge = -snap->m[METRIC_BATT_P].value;
    }
    bool pv_ok = false;
    float pv = telemetry_pv_total_w(snap, &pv_ok);
    if (!pv_ok || pv < 0.f) {
        pv = 0.f;
    }
    float charge_from_grid = charge - pv;
    if (charge_from_grid < 0.f) {
        charge_from_grid = 0.f;
    }

    float grid_for_house = grid_import;
    if (charge_from_grid >= 40.f) {
        grid_for_house = 0.f;
    }
    return essential + grid_for_house;
}

float telemetry_home_energy_today(const telemetry_snapshot_t *snap, bool *fresh)
{
    /* Match Home Assistant energy distribution Home bubble:
     * solar production + grid import + battery discharge − grid export − battery charge. */
    bool any = false;
    float home = 0.f;

    if (telemetry_is_fresh(snap, METRIC_PV_ENERGY_TODAY)) {
        home += snap->m[METRIC_PV_ENERGY_TODAY].value;
        any = true;
    }
    if (telemetry_is_fresh(snap, METRIC_GRID_BUY_TODAY)) {
        home += snap->m[METRIC_GRID_BUY_TODAY].value;
        any = true;
    }
    if (telemetry_is_fresh(snap, METRIC_BATT_DIS_TODAY)) {
        home += snap->m[METRIC_BATT_DIS_TODAY].value;
        any = true;
    }
    if (telemetry_is_fresh(snap, METRIC_GRID_SELL_TODAY)) {
        home -= snap->m[METRIC_GRID_SELL_TODAY].value;
        any = true;
    }
    if (telemetry_is_fresh(snap, METRIC_BATT_CHG_TODAY)) {
        home -= snap->m[METRIC_BATT_CHG_TODAY].value;
        any = true;
    }

    if (any) {
        *fresh = true;
        if (home < 0.f) {
            home = 0.f;
        }
        return home;
    }

    /* Fallback: inverter day-load register (often under-reports vs HA Home). */
    if (telemetry_is_fresh(snap, METRIC_LOAD_ENERGY_TODAY)) {
        *fresh = true;
        return snap->m[METRIC_LOAD_ENERGY_TODAY].value;
    }
    *fresh = false;
    return 0.f;
}

const char *telemetry_status_text(const telemetry_snapshot_t *snap, bool *fresh)
{
    *fresh = telemetry_is_fresh(snap, METRIC_STATUS);
    if (!*fresh) {
        return TELEMETRY_STALE_PLACEHOLDER;
    }
    int v = (int)snap->m[METRIC_STATUS].value;
    switch (v) {
    case 0:
        return "Standby";
    case 1:
        return "Self-Test";
    case 2:
        return "Normal";
    case 3:
        return "Alarm";
    case 4:
        return "Fault";
    default:
        return "Unknown";
    }
}

uint32_t telemetry_status_color(const telemetry_snapshot_t *snap)
{
    bool fresh = false;
    const char *txt = telemetry_status_text(snap, &fresh);
    (void)txt;
    if (!fresh) {
        return 0x7F8C8D;
    }
    int v = (int)snap->m[METRIC_STATUS].value;
    switch (v) {
    case 2:
        return 0x2ECC71;
    case 3:
        return 0xF1C40F;
    case 4:
        return 0xE74C3C;
    case 1:
        return 0x3498DB;
    default:
        return 0x95A5A6;
    }
}

static void format_num(char *buf, size_t len, float v, const char *fmt, const char *unit)
{
    char tmp[32];
    snprintf(tmp, sizeof(tmp), fmt, v);
    snprintf(buf, len, "%s %s", tmp, unit);
}

void telemetry_format(const telemetry_snapshot_t *snap, metric_id_t id, char *buf, size_t len)
{
    if (!telemetry_is_fresh(snap, id)) {
        snprintf(buf, len, "%s", TELEMETRY_STALE_PLACEHOLDER);
        return;
    }
    float v = snap->m[id].value;
    switch (id) {
    case METRIC_BATT_V:
        format_num(buf, len, v, "%.2f", "V");
        break;
    case METRIC_PV1_V:
    case METRIC_PV2_V:
    case METRIC_GRID_V:
    case METRIC_INV_V:
    case METRIC_LOAD_V:
        format_num(buf, len, v, "%.1f", "V");
        break;
    case METRIC_BATT_I:
    case METRIC_PV1_I:
    case METRIC_PV2_I:
    case METRIC_GRID_I:
    case METRIC_LOAD_I:
        format_num(buf, len, v, "%.2f", "A");
        break;
    case METRIC_BATT_T:
    case METRIC_INV_T:
        format_num(buf, len, v, "%.1f", "C");
        break;
    case METRIC_GRID_HZ:
    case METRIC_INV_HZ:
        format_num(buf, len, v, "%.2f", "Hz");
        break;
    case METRIC_BATT_CHG_TODAY:
    case METRIC_BATT_DIS_TODAY:
    case METRIC_PV_ENERGY_TODAY:
    case METRIC_GRID_BUY_TODAY:
    case METRIC_GRID_SELL_TODAY:
    case METRIC_LOAD_ENERGY_TODAY:
        format_num(buf, len, v, "%.1f", "kWh");
        break;
    case METRIC_BATT_SOC:
        snprintf(buf, len, "%.0f%%", v);
        break;
    case METRIC_STATUS:
        {
            bool fresh = true;
            snprintf(buf, len, "%s", telemetry_status_text(snap, &fresh));
        }
        break;
    default:
        format_num(buf, len, v, "%.0f", "W");
        break;
    }
}

void telemetry_format_signed_w(const telemetry_snapshot_t *snap, metric_id_t id, char *buf, size_t len)
{
    if (!telemetry_is_fresh(snap, id)) {
        snprintf(buf, len, "%s", TELEMETRY_STALE_PLACEHOLDER);
        return;
    }
    snprintf(buf, len, "%+.0f W", snap->m[id].value);
}

void telemetry_format_pv_total(const telemetry_snapshot_t *snap, char *buf, size_t len)
{
    bool fresh = false;
    float total = telemetry_pv_total_w(snap, &fresh);
    if (!fresh) {
        snprintf(buf, len, "%s", TELEMETRY_STALE_PLACEHOLDER);
        return;
    }
    snprintf(buf, len, "%.0f W", total);
}
