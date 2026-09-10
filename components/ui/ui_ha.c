#include "ui_ha.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Home Assistant–like energy palette */
#define HA_BG 0x111111
#define HA_CARD 0x1C1C1C
#define HA_BORDER 0x2A2A2A
#define HA_TEXT 0xE8E8E8
#define HA_MUTED 0x9E9E9E
#define HA_SOLAR 0xE0A028
#define HA_GRID 0x44739E
#define HA_HOME 0xD37E2A
#define HA_BATT 0x4DB3A2
#define HA_DISCH 0xE07050
#define HA_HUB 0x555555
#define HA_LINE_DIM 0x3A3A3A
#define HA_POS 0x3FB950
#define HA_NEG 0xE3B341
#define HA_DANGER 0xF85149

#define HA_NODE 112
#define HA_HUB_R 18.f
#define HA_DOT 10
#define HA_FLOW_EDGES 4
#define HA_DOTS_PER_EDGE 3
#define HA_FLOW_THRESH_W 40.f
#define HA_FLOW_REF_W 2500.f
#define HA_METRICS 10
#define HA_LINE_PTS 2

typedef enum {
    HA_EDGE_SOLAR = 0,
    HA_EDGE_GRID = 1,
    HA_EDGE_HOME = 2,
    HA_EDGE_BATT = 3,
} ha_edge_id_t;

typedef struct {
    lv_obj_t *dot[HA_DOTS_PER_EDGE];
    float phase;
    float speed;
    bool active;
    bool toward_hub;
    uint32_t color;
    lv_point_precise_t outer;
    lv_point_precise_t hub;
    lv_obj_t *line;
} ha_flow_edge_t;

typedef struct {
    lv_obj_t *circle;
    lv_obj_t *icon;
    lv_obj_t *value;
    lv_obj_t *label;
    lv_obj_t *extra;
} ha_node_t;

typedef struct {
    lv_obj_t *card;
    lv_obj_t *title;
    lv_obj_t *value;
} ha_power_card_t;

typedef struct {
    lv_obj_t *card;
    lv_obj_t *title;
    lv_obj_t *value;
} ha_metric_card_t;

static lv_obj_t *s_dist_host;
static ha_node_t s_node_solar;
static ha_node_t s_node_grid;
static ha_node_t s_node_home;
static ha_node_t s_node_batt;
static lv_obj_t *s_hub_h;
static lv_obj_t *s_hub_v;
static ha_flow_edge_t s_edges[HA_FLOW_EDGES];

static ha_power_card_t s_pwr_pv;
static ha_power_card_t s_pwr_load;
static ha_power_card_t s_pwr_grid;
static ha_power_card_t s_pwr_batt;

static ha_metric_card_t s_met[HA_METRICS];

static bool s_edges_inited;
static bool s_layout_done;

static lv_point_precise_t s_edge_poly[HA_FLOW_EDGES][HA_LINE_PTS];

static void strip(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
}

static lv_obj_t *ha_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, "—");
    return l;
}

static lv_obj_t *ha_card_shell(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_style_bg_color(card, lv_color_hex(HA_CARD), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(HA_BORDER), 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_point_precise_t point_on_circle(lv_point_precise_t center, lv_point_precise_t toward, float radius)
{
    float dx = (float)toward.x - (float)center.x;
    float dy = (float)toward.y - (float)center.y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.f) {
        len = 1.f;
    }
    lv_point_precise_t out;
    out.x = (lv_value_precise_t)((float)center.x + (dx / len) * radius);
    out.y = (lv_value_precise_t)((float)center.y + (dy / len) * radius);
    return out;
}

static lv_point_precise_t lerp_point(const lv_point_precise_t *a, const lv_point_precise_t *b, float t)
{
    lv_point_precise_t out;
    out.x = (lv_value_precise_t)((float)a->x + ((float)b->x - (float)a->x) * t);
    out.y = (lv_value_precise_t)((float)a->y + ((float)b->y - (float)a->y) * t);
    return out;
}

static void place_dot(lv_obj_t *dot, float x, float y)
{
    lv_obj_set_pos(dot, (int32_t)(x - HA_DOT / 2), (int32_t)(y - HA_DOT / 2));
}

static void make_flow_dots(lv_obj_t *host, ha_flow_edge_t *edge, uint32_t color)
{
    for (int i = 0; i < HA_DOTS_PER_EDGE; i++) {
        lv_obj_t *d = lv_obj_create(host);
        lv_obj_set_size(d, HA_DOT, HA_DOT);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(color), 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_style_pad_all(d, 0, 0);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
        edge->dot[i] = d;
    }
}

static void edge_geometry(ha_flow_edge_t *edge, lv_point_precise_t node_c, lv_point_precise_t hub_c, int edge_idx)
{
    /* Straight segment between node perimeter and hub perimeter. */
    edge->outer = point_on_circle(node_c, hub_c, (float)(HA_NODE / 2));
    edge->hub = point_on_circle(hub_c, node_c, HA_HUB_R);

    s_edge_poly[edge_idx][0] = edge->outer;
    s_edge_poly[edge_idx][1] = edge->hub;
    if (edge->line) {
        lv_line_set_points(edge->line, s_edge_poly[edge_idx], HA_LINE_PTS);
    }
}

static void init_edge(ha_flow_edge_t *edge, lv_obj_t *host, uint32_t color, int edge_idx)
{
    edge->color = color;
    edge->line = lv_line_create(host);
    lv_line_set_points(edge->line, s_edge_poly[edge_idx], HA_LINE_PTS);
    lv_obj_set_style_line_width(edge->line, 4, 0);
    lv_obj_set_style_line_color(edge->line, lv_color_hex(HA_LINE_DIM), 0);
    lv_obj_set_style_line_rounded(edge->line, true, 0);
    lv_obj_clear_flag(edge->line, LV_OBJ_FLAG_CLICKABLE);
    make_flow_dots(host, edge, color);
    edge->phase = 0.f;
    edge->speed = 0.f;
    edge->active = false;
    edge->toward_hub = true;
}

static ha_node_t make_node(lv_obj_t *host, int32_t cx, int32_t cy, uint32_t color, const char *icon,
                           const char *name, bool with_extra)
{
    ha_node_t n = {0};
    const int32_t r = HA_NODE / 2;

    n.circle = lv_obj_create(host);
    lv_obj_set_size(n.circle, HA_NODE, HA_NODE);
    lv_obj_set_pos(n.circle, cx - r, cy - r);
    lv_obj_set_style_radius(n.circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(n.circle, lv_color_hex(HA_CARD), 0);
    lv_obj_set_style_border_width(n.circle, 3, 0);
    lv_obj_set_style_border_color(n.circle, lv_color_hex(color), 0);
    lv_obj_set_style_pad_all(n.circle, 0, 0);
    lv_obj_clear_flag(n.circle, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    n.icon = ha_label(n.circle, &lv_font_montserrat_24, color);
    lv_label_set_text(n.icon, icon);
    lv_obj_align(n.icon, LV_ALIGN_CENTER, 0, -14);

    /* 16px keeps 4–5 digit values (e.g. "10.00 kWh", "100%") inside the node. */
    n.value = ha_label(n.circle, &lv_font_montserrat_16, HA_TEXT);
    lv_obj_set_width(n.value, HA_NODE - 20);
    lv_obj_set_style_text_align(n.value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(n.value, LV_ALIGN_CENTER, 0, 16);

    n.label = ha_label(host, &lv_font_montserrat_16, HA_MUTED);
    lv_label_set_text(n.label, name);
    lv_obj_set_style_text_align(n.label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(n.label, 140);
    lv_obj_set_pos(n.label, cx - 70, cy + r + 6);

    if (with_extra) {
        n.extra = ha_label(host, &lv_font_montserrat_14, HA_MUTED);
        lv_obj_set_width(n.extra, 200);
        lv_obj_set_style_text_align(n.extra, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(n.extra, cx - 100, cy + r + 28);
        lv_label_set_text(n.extra, "");
    }
    return n;
}

static void layout_distribution(lv_obj_t *host)
{
    lv_obj_update_layout(host);
    int32_t w = lv_obj_get_content_width(host);
    int32_t h = lv_obj_get_content_height(host);
    if (w < 220 || h < 180) {
        return;
    }

    int32_t cx = w / 2;
    int32_t cy = h / 2 - 8;
    int32_t span_x = (w / 2) - (HA_NODE / 2) - 28;
    int32_t span_y = (h / 2) - (HA_NODE / 2) - 44;
    if (span_x < 150) {
        span_x = 150;
    }
    if (span_y < 110) {
        span_y = 110;
    }

    lv_point_precise_t hub = {.x = (lv_value_precise_t)cx, .y = (lv_value_precise_t)cy};
    lv_point_precise_t solar = {.x = (lv_value_precise_t)cx, .y = (lv_value_precise_t)(cy - span_y)};
    lv_point_precise_t grid = {.x = (lv_value_precise_t)(cx - span_x), .y = (lv_value_precise_t)cy};
    lv_point_precise_t home = {.x = (lv_value_precise_t)(cx + span_x), .y = (lv_value_precise_t)cy};
    lv_point_precise_t batt = {.x = (lv_value_precise_t)cx, .y = (lv_value_precise_t)(cy + span_y)};

    const int32_t hub_arm = 48;
    lv_obj_set_size(s_hub_h, hub_arm, 5);
    lv_obj_set_pos(s_hub_h, cx - hub_arm / 2, cy - 2);
    lv_obj_set_size(s_hub_v, 5, hub_arm);
    lv_obj_set_pos(s_hub_v, cx - 2, cy - hub_arm / 2);

    lv_obj_set_pos(s_node_solar.circle, (int32_t)solar.x - HA_NODE / 2, (int32_t)solar.y - HA_NODE / 2);
    lv_obj_set_pos(s_node_solar.label, (int32_t)solar.x - 70, (int32_t)solar.y - HA_NODE / 2 - 26);
    lv_obj_set_pos(s_node_grid.circle, (int32_t)grid.x - HA_NODE / 2, (int32_t)grid.y - HA_NODE / 2);
    lv_obj_set_pos(s_node_grid.label, (int32_t)grid.x - 70, (int32_t)grid.y + HA_NODE / 2 + 6);
    lv_obj_set_pos(s_node_home.circle, (int32_t)home.x - HA_NODE / 2, (int32_t)home.y - HA_NODE / 2);
    lv_obj_set_pos(s_node_home.label, (int32_t)home.x - 70, (int32_t)home.y + HA_NODE / 2 + 6);
    lv_obj_set_pos(s_node_batt.circle, (int32_t)batt.x - HA_NODE / 2, (int32_t)batt.y - HA_NODE / 2);
    lv_obj_set_pos(s_node_batt.label, (int32_t)batt.x - 70, (int32_t)batt.y + HA_NODE / 2 + 6);
    if (s_node_batt.extra) {
        lv_obj_set_pos(s_node_batt.extra, (int32_t)batt.x - 100, (int32_t)batt.y + HA_NODE / 2 + 28);
    }

    if (!s_edges_inited) {
        init_edge(&s_edges[HA_EDGE_SOLAR], host, HA_SOLAR, HA_EDGE_SOLAR);
        init_edge(&s_edges[HA_EDGE_GRID], host, HA_GRID, HA_EDGE_GRID);
        init_edge(&s_edges[HA_EDGE_HOME], host, HA_HOME, HA_EDGE_HOME);
        init_edge(&s_edges[HA_EDGE_BATT], host, HA_BATT, HA_EDGE_BATT);
        s_edges_inited = true;
    }

    edge_geometry(&s_edges[HA_EDGE_SOLAR], solar, hub, HA_EDGE_SOLAR);
    edge_geometry(&s_edges[HA_EDGE_GRID], grid, hub, HA_EDGE_GRID);
    edge_geometry(&s_edges[HA_EDGE_HOME], home, hub, HA_EDGE_HOME);
    edge_geometry(&s_edges[HA_EDGE_BATT], batt, hub, HA_EDGE_BATT);

    lv_obj_move_foreground(s_node_solar.circle);
    lv_obj_move_foreground(s_node_grid.circle);
    lv_obj_move_foreground(s_node_home.circle);
    lv_obj_move_foreground(s_node_batt.circle);
    lv_obj_move_foreground(s_node_solar.label);
    lv_obj_move_foreground(s_node_grid.label);
    lv_obj_move_foreground(s_node_home.label);
    lv_obj_move_foreground(s_node_batt.label);
    if (s_node_batt.extra) {
        lv_obj_move_foreground(s_node_batt.extra);
    }
    lv_obj_move_foreground(s_hub_h);
    lv_obj_move_foreground(s_hub_v);
    s_layout_done = true;
}

static void on_dist_size(lv_event_t *e)
{
    (void)e;
    if (s_dist_host) {
        layout_distribution(s_dist_host);
    }
}

static void fmt_kwh(char *buf, size_t len, float v, bool fresh)
{
    if (!fresh) {
        snprintf(buf, len, "—");
        return;
    }
    /* Two decimals for small day totals so 0.66 / 7.76 read correctly. */
    if (v < 10.f) {
        snprintf(buf, len, "%.2f kWh", v);
    } else {
        snprintf(buf, len, "%.1f kWh", v);
    }
}

static void fmt_soc(char *buf, size_t len, const telemetry_snapshot_t *snap)
{
    if (!telemetry_is_fresh(snap, METRIC_BATT_SOC)) {
        snprintf(buf, len, "—");
        return;
    }
    snprintf(buf, len, "%.0f%%", snap->m[METRIC_BATT_SOC].value);
}

static float absf(float v)
{
    return v < 0.f ? -v : v;
}

static float flow_speed(float watts)
{
    float mag = absf(watts);
    if (mag < HA_FLOW_THRESH_W) {
        return 0.f;
    }
    float n = mag / HA_FLOW_REF_W;
    if (n > 1.f) {
        n = 1.f;
    }
    /* Slower dots: ~half prior speed. */
    return 0.035f + n * 0.09f;
}

static void update_flow_edge(ha_flow_edge_t *edge)
{
    uint32_t line_col = edge->active ? edge->color : HA_LINE_DIM;
    lv_opa_t line_opa = edge->active ? LV_OPA_COVER : LV_OPA_50;
    lv_obj_set_style_line_color(edge->line, lv_color_hex(line_col), 0);
    lv_obj_set_style_opa(edge->line, line_opa, 0);

    if (!edge->active || edge->speed <= 0.f) {
        for (int i = 0; i < HA_DOTS_PER_EDGE; i++) {
            lv_obj_add_flag(edge->dot[i], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    edge->phase += edge->speed;
    if (edge->phase >= 1.f) {
        edge->phase -= 1.f;
    }

    for (int i = 0; i < HA_DOTS_PER_EDGE; i++) {
        float t = edge->phase + (float)i / (float)HA_DOTS_PER_EDGE;
        if (t >= 1.f) {
            t -= 1.f;
        }
        if (!edge->toward_hub) {
            t = 1.f - t;
        }
        lv_point_precise_t p = lerp_point(&edge->outer, &edge->hub, t);
        lv_obj_clear_flag(edge->dot[i], LV_OBJ_FLAG_HIDDEN);
        place_dot(edge->dot[i], (float)p.x, (float)p.y);
        lv_obj_set_style_bg_color(edge->dot[i], lv_color_hex(edge->color), 0);
        lv_obj_move_foreground(edge->dot[i]);
    }
}

static ha_power_card_t make_power_card(lv_obj_t *parent, const char *title, uint32_t accent)
{
    ha_power_card_t c;
    c.card = ha_card_shell(parent);
    lv_obj_set_width(c.card, LV_PCT(100));
    lv_obj_set_flex_grow(c.card, 1);
    lv_obj_set_style_pad_row(c.card, 6, 0);
    lv_obj_set_style_border_color(c.card, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(c.card, 2, 0);

    c.title = ha_label(c.card, &lv_font_montserrat_20, HA_MUTED);
    lv_label_set_text(c.title, title);
    lv_obj_set_style_text_align(c.title, LV_TEXT_ALIGN_CENTER, 0);

    c.value = ha_label(c.card, &lv_font_montserrat_36, HA_TEXT);
    lv_obj_set_style_text_align(c.value, LV_TEXT_ALIGN_CENTER, 0);
    return c;
}

static ha_metric_card_t make_metric_card(lv_obj_t *parent, const char *title)
{
    ha_metric_card_t c;
    c.card = ha_card_shell(parent);
    lv_obj_set_flex_grow(c.card, 1);
    lv_obj_set_height(c.card, LV_PCT(100));
    lv_obj_set_style_pad_all(c.card, 8, 0);

    c.title = ha_label(c.card, &lv_font_montserrat_14, HA_MUTED);
    lv_label_set_text(c.title, title);
    lv_obj_set_style_text_align(c.title, LV_TEXT_ALIGN_CENTER, 0);

    c.value = ha_label(c.card, &lv_font_montserrat_20, HA_TEXT);
    lv_obj_set_style_text_align(c.value, LV_TEXT_ALIGN_CENTER, 0);
    return c;
}

static void build_distribution_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_style_bg_color(card, lv_color_hex(HA_CARD), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(HA_BORDER), 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_flex_grow(card, 1);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = ha_label(card, &lv_font_montserrat_16, HA_MUTED);
    lv_label_set_text(t, "Energy distribution");
    lv_obj_set_width(t, LV_PCT(100));

    s_dist_host = lv_obj_create(card);
    strip(s_dist_host);
    lv_obj_set_width(s_dist_host, LV_PCT(100));
    lv_obj_set_flex_grow(s_dist_host, 1);
    lv_obj_set_style_bg_color(s_dist_host, lv_color_hex(HA_CARD), 0);
    lv_obj_set_style_bg_opa(s_dist_host, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_dist_host, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_dist_host, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    s_hub_h = lv_obj_create(s_dist_host);
    lv_obj_set_style_bg_color(s_hub_h, lv_color_hex(HA_HUB), 0);
    lv_obj_set_style_border_width(s_hub_h, 0, 0);
    lv_obj_set_style_radius(s_hub_h, 2, 0);
    lv_obj_clear_flag(s_hub_h, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_hub_v = lv_obj_create(s_dist_host);
    lv_obj_set_style_bg_color(s_hub_v, lv_color_hex(HA_HUB), 0);
    lv_obj_set_style_border_width(s_hub_v, 0, 0);
    lv_obj_set_style_radius(s_hub_v, 2, 0);
    lv_obj_clear_flag(s_hub_v, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_node_solar = make_node(s_dist_host, 280, 70, HA_SOLAR, LV_SYMBOL_CHARGE, "Solar", false);
    s_node_grid = make_node(s_dist_host, 70, 180, HA_GRID, LV_SYMBOL_GPS, "Grid", false);
    s_node_home = make_node(s_dist_host, 490, 180, HA_HOME, LV_SYMBOL_HOME, "Home", false);
    s_node_batt = make_node(s_dist_host, 280, 300, HA_BATT, LV_SYMBOL_BATTERY_FULL, "Battery", true);

    lv_obj_add_event_cb(s_dist_host, on_dist_size, LV_EVENT_SIZE_CHANGED, NULL);
}

static void set_power_card(ha_power_card_t *card, const char *text, uint32_t color)
{
    lv_label_set_text(card->value, text);
    lv_obj_set_style_text_color(card->value, lv_color_hex(color), 0);
}

static void set_metric(ha_metric_card_t *card, const char *text, bool ok)
{
    lv_label_set_text(card->value, text);
    lv_obj_set_style_text_color(card->value, lv_color_hex(ok ? HA_TEXT : HA_MUTED), 0);
}

static uint32_t signed_power_color(metric_id_t id, float v, bool fresh)
{
    if (!fresh) {
        return HA_MUTED;
    }
    if (id == METRIC_BATT_P) {
        if (v < -0.5f) {
            return HA_POS;
        }
        if (v > 0.5f) {
            return HA_DANGER;
        }
        return HA_TEXT;
    }
    if (id == METRIC_GRID_P_CT) {
        if (v > 0.5f) {
            return HA_NEG;
        }
        if (v < -0.5f) {
            return HA_POS;
        }
        return HA_TEXT;
    }
    if (id == METRIC_LOAD_P) {
        return HA_HOME;
    }
    return HA_SOLAR;
}

void ui_ha_build(lv_obj_t *root)
{
    s_edges_inited = false;
    s_layout_done = false;

    lv_obj_set_style_bg_color(root, lv_color_hex(HA_BG), 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, 10, 0);
    lv_obj_set_style_pad_row(root, 10, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    /* Top band: wide flow (~3) + narrow power cards (~1) */
    lv_obj_t *top = lv_obj_create(root);
    strip(top);
    lv_obj_set_width(top, LV_PCT(100));
    lv_obj_set_flex_grow(top, 7);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(top, 10, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *left = lv_obj_create(top);
    strip(left);
    lv_obj_set_flex_grow(left, 3);
    lv_obj_set_height(left, LV_PCT(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    build_distribution_card(left);

    lv_obj_t *right = lv_obj_create(top);
    strip(right);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, 8, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    s_pwr_pv = make_power_card(right, "PV Power", HA_SOLAR);
    s_pwr_load = make_power_card(right, "Load Power", HA_HOME);
    s_pwr_grid = make_power_card(right, "Grid Power", HA_GRID);
    s_pwr_batt = make_power_card(right, "Battery Power", HA_BATT);

    /* Bottom: full-width 2×5 equal cards (V/I + temps/freq/inv power) */
    lv_obj_t *bottom = lv_obj_create(root);
    strip(bottom);
    lv_obj_set_width(bottom, LV_PCT(100));
    lv_obj_set_flex_grow(bottom, 2);
    lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(bottom, 8, 0);
    lv_obj_clear_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *row0 = lv_obj_create(bottom);
    strip(row0);
    lv_obj_set_width(row0, LV_PCT(100));
    lv_obj_set_flex_grow(row0, 1);
    lv_obj_set_flex_flow(row0, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row0, 8, 0);
    lv_obj_clear_flag(row0, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *row1 = lv_obj_create(bottom);
    strip(row1);
    lv_obj_set_width(row1, LV_PCT(100));
    lv_obj_set_flex_grow(row1, 1);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row1, 8, 0);
    lv_obj_clear_flag(row1, LV_OBJ_FLAG_SCROLLABLE);

    s_met[0] = make_metric_card(row0, "PV Voltage");
    s_met[1] = make_metric_card(row0, "Battery Voltage");
    s_met[2] = make_metric_card(row0, "Load Voltage");
    s_met[3] = make_metric_card(row0, "Inverter Temp");
    s_met[4] = make_metric_card(row0, "Battery Temp");

    s_met[5] = make_metric_card(row1, "PV Current");
    s_met[6] = make_metric_card(row1, "Battery Current");
    s_met[7] = make_metric_card(row1, "Load Current");
    s_met[8] = make_metric_card(row1, "Inverter Freq");
    s_met[9] = make_metric_card(row1, "Inverter Power");

    lv_obj_update_layout(root);
    layout_distribution(s_dist_host);
}

void ui_ha_update(const telemetry_snapshot_t *snap)
{
    if (!s_dist_host) {
        return;
    }

    if (!s_layout_done) {
        layout_distribution(s_dist_host);
    }

    char buf[80];

    /* Solar: PV energy today */
    bool pv_e_ok = telemetry_is_fresh(snap, METRIC_PV_ENERGY_TODAY);
    fmt_kwh(buf, sizeof(buf), pv_e_ok ? snap->m[METRIC_PV_ENERGY_TODAY].value : 0.f, pv_e_ok);
    lv_label_set_text(s_node_solar.value, buf);
    lv_obj_set_style_text_color(s_node_solar.value, lv_color_hex(pv_e_ok ? HA_TEXT : HA_MUTED), 0);

    /* Grid: imported / buy today (same as Full-mode Buy tile) */
    bool buy_ok = telemetry_is_fresh(snap, METRIC_GRID_BUY_TODAY);
    fmt_kwh(buf, sizeof(buf), buy_ok ? snap->m[METRIC_GRID_BUY_TODAY].value : 0.f, buy_ok);
    lv_label_set_text(s_node_grid.value, buf);
    lv_obj_set_style_text_color(s_node_grid.value, lv_color_hex(buy_ok ? HA_TEXT : HA_MUTED), 0);

    /* Home: HA energy-balance, not inverter load/energy_today alone */
    bool home_ok = false;
    float home_kwh = telemetry_home_energy_today(snap, &home_ok);
    fmt_kwh(buf, sizeof(buf), home_kwh, home_ok);
    lv_label_set_text(s_node_home.value, buf);
    lv_obj_set_style_text_color(s_node_home.value, lv_color_hex(home_ok ? HA_TEXT : HA_MUTED), 0);

    fmt_soc(buf, sizeof(buf), snap);
    lv_label_set_text(s_node_batt.value, buf);

    if (s_node_batt.extra) {
        char chg[24];
        char dis[24];
        if (telemetry_is_fresh(snap, METRIC_BATT_CHG_TODAY)) {
            snprintf(chg, sizeof(chg), LV_SYMBOL_DOWN " %.2f", snap->m[METRIC_BATT_CHG_TODAY].value);
        } else {
            snprintf(chg, sizeof(chg), LV_SYMBOL_DOWN " —");
        }
        if (telemetry_is_fresh(snap, METRIC_BATT_DIS_TODAY)) {
            snprintf(dis, sizeof(dis), LV_SYMBOL_UP " %.2f", snap->m[METRIC_BATT_DIS_TODAY].value);
        } else {
            snprintf(dis, sizeof(dis), LV_SYMBOL_UP " —");
        }
        snprintf(buf, sizeof(buf), "#4DB3A2 %s#  #E07050 %s# kWh", chg, dis);
        lv_label_set_recolor(s_node_batt.extra, true);
        lv_label_set_text(s_node_batt.extra, buf);
    }

    /* Live power cards */
    bool pf = false;
    float pv_w = telemetry_pv_total_w(snap, &pf);
    if (pf) {
        snprintf(buf, sizeof(buf), "%.0f W", pv_w);
    } else {
        snprintf(buf, sizeof(buf), "—");
    }
    set_power_card(&s_pwr_pv, buf, signed_power_color(METRIC_PV1_P, pv_w, pf));

    bool load_ok = telemetry_is_fresh(snap, METRIC_LOAD_P);
    float load_w = load_ok ? snap->m[METRIC_LOAD_P].value : 0.f;
    if (load_ok) {
        snprintf(buf, sizeof(buf), "%+.0f W", load_w);
    } else {
        snprintf(buf, sizeof(buf), "—");
    }
    set_power_card(&s_pwr_load, buf, signed_power_color(METRIC_LOAD_P, load_w, load_ok));

    bool grid_ok = telemetry_is_fresh(snap, METRIC_GRID_P_CT);
    float grid_w = grid_ok ? snap->m[METRIC_GRID_P_CT].value : 0.f;
    if (grid_ok) {
        snprintf(buf, sizeof(buf), "%+.0f W", grid_w);
    } else {
        snprintf(buf, sizeof(buf), "—");
    }
    set_power_card(&s_pwr_grid, buf, signed_power_color(METRIC_GRID_P_CT, grid_w, grid_ok));

    bool batt_ok = telemetry_is_fresh(snap, METRIC_BATT_P);
    float batt_w = batt_ok ? snap->m[METRIC_BATT_P].value : 0.f;
    if (batt_ok) {
        snprintf(buf, sizeof(buf), "%+.0f W", batt_w);
    } else {
        snprintf(buf, sizeof(buf), "—");
    }
    set_power_card(&s_pwr_batt, buf, signed_power_color(METRIC_BATT_P, batt_w, batt_ok));

    /* Bottom metric grid: V/I + inverter/battery extras */
    bool v_ok = false;
    float pv_v = telemetry_pv_voltage(snap, &v_ok);
    if (v_ok) {
        snprintf(buf, sizeof(buf), "%.1f V", pv_v);
    } else {
        snprintf(buf, sizeof(buf), "—");
    }
    set_metric(&s_met[0], buf, v_ok);

    bool bv_ok = telemetry_is_fresh(snap, METRIC_BATT_V);
    if (bv_ok) {
        snprintf(buf, sizeof(buf), "%.2f V", snap->m[METRIC_BATT_V].value);
    } else {
        snprintf(buf, sizeof(buf), "—");
    }
    set_metric(&s_met[1], buf, bv_ok);

    bool lv_ok = false;
    float load_v = telemetry_load_voltage(snap, &lv_ok);
    if (lv_ok) {
        snprintf(buf, sizeof(buf), "%.1f V", load_v);
    } else {
        snprintf(buf, sizeof(buf), "—");
    }
    set_metric(&s_met[2], buf, lv_ok);

    bool it_ok = telemetry_is_fresh(snap, METRIC_INV_T);
    if (it_ok) {
        snprintf(buf, sizeof(buf), "%.1f C", snap->m[METRIC_INV_T].value);
    } else {
        snprintf(buf, sizeof(buf), "—");
    }
    set_metric(&s_met[3], buf, it_ok);

    bool bt_ok = telemetry_is_fresh(snap, METRIC_BATT_T);
    if (bt_ok) {
        snprintf(buf, sizeof(buf), "%.1f C", snap->m[METRIC_BATT_T].value);
    } else {
        snprintf(buf, sizeof(buf), "—");
    }
    set_metric(&s_met[4], buf, bt_ok);

    bool pi_ok = false;
    float pv_i = telemetry_pv_current(snap, &pi_ok);
    if (pi_ok) {
        snprintf(buf, sizeof(buf), "%.2f A", pv_i);
    } else {
        snprintf(buf, sizeof(buf), "—");
    }
    set_metric(&s_met[5], buf, pi_ok);

    bool bi_ok = telemetry_is_fresh(snap, METRIC_BATT_I);
    if (bi_ok) {
        snprintf(buf, sizeof(buf), "%+.2f A", snap->m[METRIC_BATT_I].value);
    } else {
        snprintf(buf, sizeof(buf), "—");
    }
    set_metric(&s_met[6], buf, bi_ok);

    bool li_ok = false;
    float load_i = telemetry_load_current(snap, &li_ok);
    if (li_ok) {
        snprintf(buf, sizeof(buf), "%+.2f A", load_i);
    } else {
        snprintf(buf, sizeof(buf), "—");
    }
    set_metric(&s_met[7], buf, li_ok);

    bool hz_ok = telemetry_is_fresh(snap, METRIC_INV_HZ);
    if (hz_ok) {
        snprintf(buf, sizeof(buf), "%.2f Hz", snap->m[METRIC_INV_HZ].value);
    } else {
        snprintf(buf, sizeof(buf), "—");
    }
    set_metric(&s_met[8], buf, hz_ok);

    bool ip_ok = telemetry_is_fresh(snap, METRIC_INV_P);
    if (ip_ok) {
        snprintf(buf, sizeof(buf), "%.0f W", snap->m[METRIC_INV_P].value);
    } else {
        snprintf(buf, sizeof(buf), "—");
    }
    set_metric(&s_met[9], buf, ip_ok);

    /* Live flow directions */
    float flow_load = load_w;
    if (flow_load < 0.f) {
        flow_load = -flow_load;
    }

    s_edges[HA_EDGE_SOLAR].active = pf && pv_w >= HA_FLOW_THRESH_W;
    s_edges[HA_EDGE_SOLAR].toward_hub = true;
    s_edges[HA_EDGE_SOLAR].speed = flow_speed(pv_w);
    s_edges[HA_EDGE_SOLAR].color = HA_SOLAR;

    if (grid_ok && absf(grid_w) >= HA_FLOW_THRESH_W) {
        s_edges[HA_EDGE_GRID].active = true;
        s_edges[HA_EDGE_GRID].toward_hub = (grid_w > 0.f);
        s_edges[HA_EDGE_GRID].speed = flow_speed(grid_w);
    } else {
        s_edges[HA_EDGE_GRID].active = false;
        s_edges[HA_EDGE_GRID].speed = 0.f;
    }

    s_edges[HA_EDGE_HOME].active = load_ok && flow_load >= HA_FLOW_THRESH_W;
    s_edges[HA_EDGE_HOME].toward_hub = false;
    s_edges[HA_EDGE_HOME].speed = flow_speed(flow_load);
    s_edges[HA_EDGE_HOME].color = HA_HOME;

    if (batt_ok && absf(batt_w) >= HA_FLOW_THRESH_W) {
        s_edges[HA_EDGE_BATT].active = true;
        s_edges[HA_EDGE_BATT].toward_hub = (batt_w > 0.f);
        s_edges[HA_EDGE_BATT].speed = flow_speed(batt_w);
        s_edges[HA_EDGE_BATT].color = (batt_w < 0.f) ? HA_BATT : HA_DISCH;
    } else {
        s_edges[HA_EDGE_BATT].active = false;
        s_edges[HA_EDGE_BATT].speed = 0.f;
    }

    for (int i = 0; i < HA_FLOW_EDGES; i++) {
        if (s_edges_inited) {
            update_flow_edge(&s_edges[i]);
        }
    }
}
