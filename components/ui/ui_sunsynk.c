#include "ui_sunsynk.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_timer.h"

/* Sunsynk power-flow palette (dark card look) */
#define SK_BG 0x111111
#define SK_PANEL 0x1A1A1A
#define SK_TEXT 0xF0F0F0
#define SK_MUTED 0x8A8A8A
#define SK_SOLAR 0xF2A73A
#define SK_SOLAR_DIM 0xC9A227
#define SK_LOAD 0xF2A73A
#define SK_HOME 0x7EC8C8
#define SK_BATT 0xE88A7A
#define SK_BATT_BAR 0x3FB950
#define SK_GRID 0x5BA3D9
#define SK_HUB 0x6A6A6A
#define SK_LINE_DIM 0x333333
#define SK_OK 0x3FB950
#define SK_WARN 0xE3B341
#define SK_DANGER 0xF85149

#define SK_DOT 9
#define SK_FLOW_EDGES 4
#define SK_DOTS_PER_EDGE 3
#define SK_FLOW_THRESH_W 40.f
#define SK_FLOW_REF_W 2500.f
#define SK_LINE_PTS 3
#define SK_BATT_BARS 5
/* Keep dot centers outside node/hub hitboxes */
#define SK_ATTACH_GAP (SK_DOT / 2 + 4)
/* Shared |X| offset from hub center for all four vertical risers (mirrored L/R). */
#define SK_RISER_DX 48
#define SK_INV_BOX_W 150
#define SK_INV_BOX_H 68
/* Inv metric box top = cy - SK_INV_BOX_TOP; hub stem must stop above it. */
#define SK_INV_BOX_TOP 96
#define SK_INV_ICON_W 72
#define SK_INV_ICON_H 72
/* Gap between inv metric box bottom and icon top. */
#define SK_INV_ICON_GAP 6
/* Top hub Y = cy - clear (above inv W/A box); bottom = cy + clear (below icon). */
#define SK_HUB_CLEAR_TOP (SK_INV_BOX_TOP + SK_ATTACH_GAP + 4)
#define SK_HUB_CLEAR_BOT                                                                                     \
    (SK_INV_BOX_H - SK_INV_BOX_TOP + SK_INV_ICON_GAP + SK_INV_ICON_H + SK_ATTACH_GAP)
#define SK_SOLAR_BOX_W 140
#define SK_SOLAR_BOX_H 56
#define SK_HOME_BOX_W 140
#define SK_HOME_BOX_H 56
#define SK_BATT_BOX_W 140
#define SK_BATT_BOX_H 88
#define SK_GRID_BOX_W 270
#define SK_GRID_BOX_H 104
#define SK_SIDE_MARGIN 48
#define SK_ICON_GAP 10
#define SK_ICON_W 48
#define SK_BATT_ST_HOLD_US (5LL * 1000000LL)

typedef enum {
    SK_EDGE_SOLAR = 0,
    SK_EDGE_BATT = 1,
    SK_EDGE_HOME = 2,
    SK_EDGE_GRID = 3,
} sk_edge_id_t;

typedef struct {
    lv_obj_t *dot[SK_DOTS_PER_EDGE];
    float phase;
    float speed;
    bool active;
    bool toward_hub;
    uint32_t color;
    lv_point_precise_t outer;
    lv_point_precise_t mid;
    lv_point_precise_t hub;
    lv_obj_t *line;
} sk_flow_edge_t;

typedef struct {
    lv_obj_t *daily;
    lv_obj_t *daily_lbl;
    lv_obj_t *box;
    lv_obj_t *live;
    lv_obj_t *extra;
    lv_obj_t *icon;
    lv_obj_t *tag;
} sk_corner_t;

static lv_obj_t *s_host;
static sk_corner_t s_solar;
static sk_corner_t s_batt;
static sk_corner_t s_home;
static sk_corner_t s_grid;

static lv_obj_t *s_inv_box;
static lv_obj_t *s_inv_pwr;
static lv_obj_t *s_inv_icon;
static lv_obj_t *s_inv_status_dot;
static lv_obj_t *s_inv_status;
static lv_obj_t *s_batt_soc;
static lv_obj_t *s_batt_state;

typedef enum {
    BATT_ST_NONE = 0,
    BATT_ST_IDLE,
    BATT_ST_CHG,
    BATT_ST_DIS,
} batt_st_t;

static batt_st_t s_batt_st_shown = BATT_ST_NONE;
static batt_st_t s_batt_st_pending = BATT_ST_NONE;
static int64_t s_batt_st_pending_us;
static lv_obj_t *s_batt_temp;
static lv_obj_t *s_batt_shell;
static lv_obj_t *s_batt_bars[SK_BATT_BARS];

static sk_flow_edge_t s_edges[SK_FLOW_EDGES];
static lv_point_precise_t s_edge_poly[SK_FLOW_EDGES][SK_LINE_PTS];
static bool s_edges_inited;
static bool s_layout_done;

static void strip(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
}

static lv_obj_t *sk_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, "--");
    return l;
}

static lv_obj_t *make_live_box(lv_obj_t *parent, uint32_t border_col, int32_t w, int32_t h)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_bg_color(box, lv_color_hex(SK_PANEL), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(border_col), 0);
    lv_obj_set_style_radius(box, 10, 0);
    lv_obj_set_style_pad_all(box, 4, 0);
    lv_obj_set_style_shadow_width(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return box;
}

static float absf(float v)
{
    return v < 0.f ? -v : v;
}

static float flow_speed(float watts)
{
    float mag = absf(watts);
    if (mag < SK_FLOW_THRESH_W) {
        return 0.f;
    }
    float n = mag / SK_FLOW_REF_W;
    if (n > 1.f) {
        n = 1.f;
    }
    return 0.035f + n * 0.09f;
}

static void place_dot(lv_obj_t *dot, float x, float y)
{
    lv_obj_set_pos(dot, (int32_t)(x - SK_DOT / 2), (int32_t)(y - SK_DOT / 2));
}

static lv_point_precise_t lerp_point(const lv_point_precise_t *a, const lv_point_precise_t *b, float t)
{
    lv_point_precise_t out;
    out.x = (lv_value_precise_t)((float)a->x + ((float)b->x - (float)a->x) * t);
    out.y = (lv_value_precise_t)((float)a->y + ((float)b->y - (float)a->y) * t);
    return out;
}

static float seg_len(const lv_point_precise_t *a, const lv_point_precise_t *b)
{
    float dx = (float)b->x - (float)a->x;
    float dy = (float)b->y - (float)a->y;
    return sqrtf(dx * dx + dy * dy);
}

static lv_point_precise_t point_on_seg(const sk_flow_edge_t *edge, float t)
{
    /* Length-weighted L-path: outer -> elbow -> hub (visible connector only). */
    float l1 = seg_len(&edge->outer, &edge->mid);
    float l2 = seg_len(&edge->mid, &edge->hub);
    float total = l1 + l2;
    if (total < 1.f) {
        return edge->outer;
    }
    float d = t * total;
    if (d <= l1) {
        float u = (l1 > 0.f) ? (d / l1) : 0.f;
        return lerp_point(&edge->outer, &edge->mid, u);
    }
    float u = (l2 > 0.f) ? ((d - l1) / l2) : 0.f;
    return lerp_point(&edge->mid, &edge->hub, u);
}

/* Attachment on left/right edge of a live value box, inset so dots stay on the line. */
static lv_point_precise_t box_edge_attach(int32_t box_x, int32_t box_y, int32_t box_w, int32_t box_h,
                                          bool attach_right)
{
    lv_point_precise_t p;
    p.y = (lv_value_precise_t)(box_y + box_h / 2);
    if (attach_right) {
        p.x = (lv_value_precise_t)(box_x + box_w + SK_ATTACH_GAP);
    } else {
        p.x = (lv_value_precise_t)(box_x - SK_ATTACH_GAP);
    }
    return p;
}

static void make_flow_dots(lv_obj_t *host, sk_flow_edge_t *edge, uint32_t color)
{
    for (int i = 0; i < SK_DOTS_PER_EDGE; i++) {
        lv_obj_t *d = lv_obj_create(host);
        lv_obj_set_size(d, SK_DOT, SK_DOT);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(color), 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_style_pad_all(d, 0, 0);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
        edge->dot[i] = d;
    }
}

static void set_edge_points(sk_flow_edge_t *edge, int edge_idx, lv_point_precise_t outer, lv_point_precise_t mid,
                            lv_point_precise_t hub)
{
    edge->outer = outer;
    edge->mid = mid;
    edge->hub = hub;
    s_edge_poly[edge_idx][0] = outer;
    s_edge_poly[edge_idx][1] = mid;
    s_edge_poly[edge_idx][2] = hub;
    if (edge->line) {
        lv_line_set_points(edge->line, s_edge_poly[edge_idx], SK_LINE_PTS);
    }
}

static void init_edge(sk_flow_edge_t *edge, lv_obj_t *host, uint32_t color, int edge_idx)
{
    edge->color = color;
    edge->line = lv_line_create(host);
    lv_line_set_points(edge->line, s_edge_poly[edge_idx], SK_LINE_PTS);
    lv_obj_set_style_line_width(edge->line, 3, 0);
    lv_obj_set_style_line_color(edge->line, lv_color_hex(SK_LINE_DIM), 0);
    lv_obj_set_style_line_rounded(edge->line, true, 0);
    lv_obj_clear_flag(edge->line, LV_OBJ_FLAG_CLICKABLE);
    make_flow_dots(host, edge, color);
    edge->phase = 0.f;
    edge->speed = 0.f;
    edge->active = false;
    edge->toward_hub = true;
}

static void update_flow_edge(sk_flow_edge_t *edge)
{
    uint32_t line_col = edge->active ? edge->color : SK_LINE_DIM;
    lv_opa_t line_opa = edge->active ? LV_OPA_COVER : LV_OPA_50;
    lv_obj_set_style_line_color(edge->line, lv_color_hex(line_col), 0);
    lv_obj_set_style_opa(edge->line, line_opa, 0);

    if (!edge->active || edge->speed <= 0.f) {
        for (int i = 0; i < SK_DOTS_PER_EDGE; i++) {
            lv_obj_add_flag(edge->dot[i], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    edge->phase += edge->speed;
    if (edge->phase >= 1.f) {
        edge->phase -= 1.f;
    }

    for (int i = 0; i < SK_DOTS_PER_EDGE; i++) {
        float t = edge->phase + (float)i / (float)SK_DOTS_PER_EDGE;
        if (t >= 1.f) {
            t -= 1.f;
        }
        if (!edge->toward_hub) {
            t = 1.f - t;
        }
        lv_point_precise_t p = point_on_seg(edge, t);
        lv_obj_clear_flag(edge->dot[i], LV_OBJ_FLAG_HIDDEN);
        place_dot(edge->dot[i], (float)p.x, (float)p.y);
        lv_obj_set_style_bg_color(edge->dot[i], lv_color_hex(edge->color), 0);
        lv_obj_move_foreground(edge->dot[i]);
    }
}

static void fmt_kwh(char *buf, size_t len, float v, bool fresh)
{
    if (!fresh) {
        snprintf(buf, len, "--");
        return;
    }
    if (v < 10.f) {
        snprintf(buf, len, "%.1f kWh", v);
    } else {
        snprintf(buf, len, "%.0f kWh", v);
    }
}

static void fmt_w(char *buf, size_t len, float v, bool fresh, bool signed_out)
{
    if (!fresh) {
        snprintf(buf, len, "--");
        return;
    }
    if (signed_out) {
        snprintf(buf, len, "%+.0f W", v);
    } else {
        snprintf(buf, len, "%.0f W", absf(v));
    }
}

static void build_batt_icon(lv_obj_t *host)
{
    s_batt_temp = sk_label(host, &lv_font_montserrat_16, SK_BATT);
    lv_label_set_text(s_batt_temp, "--");

    s_batt_shell = lv_obj_create(host);
    lv_obj_set_size(s_batt_shell, 36, 72);
    lv_obj_set_style_bg_opa(s_batt_shell, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_batt_shell, 2, 0);
    lv_obj_set_style_border_color(s_batt_shell, lv_color_hex(SK_BATT), 0);
    lv_obj_set_style_radius(s_batt_shell, 6, 0);
    lv_obj_set_style_pad_all(s_batt_shell, 4, 0);
    lv_obj_set_style_pad_row(s_batt_shell, 3, 0);
    lv_obj_set_flex_flow(s_batt_shell, LV_FLEX_FLOW_COLUMN);
    /* First child is the top bar; green fills from the bottom (last child). */
    lv_obj_set_flex_align(s_batt_shell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_batt_shell, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Cap on top */
    lv_obj_t *cap = lv_obj_create(host);
    lv_obj_set_size(cap, 14, 5);
    lv_obj_set_style_bg_color(cap, lv_color_hex(SK_BATT), 0);
    lv_obj_set_style_border_width(cap, 0, 0);
    lv_obj_set_style_radius(cap, 2, 0);
    lv_obj_clear_flag(cap, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    /* cap repositioned in layout */

    for (int i = 0; i < SK_BATT_BARS; i++) {
        s_batt_bars[i] = lv_obj_create(s_batt_shell);
        lv_obj_set_size(s_batt_bars[i], 24, 9);
        lv_obj_set_style_bg_color(s_batt_bars[i], lv_color_hex(SK_LINE_DIM), 0);
        lv_obj_set_style_border_width(s_batt_bars[i], 0, 0);
        lv_obj_set_style_radius(s_batt_bars[i], 2, 0);
        lv_obj_clear_flag(s_batt_bars[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }

    s_batt_soc = sk_label(host, &lv_font_montserrat_28, SK_TEXT);
    s_batt_state = sk_label(host, &lv_font_montserrat_16, SK_BATT);
    lv_label_set_text(s_batt_state, "BATTERY");

    /* Store cap as user_data on shell for layout */
    lv_obj_set_user_data(s_batt_shell, cap);
}

static void set_batt_bars(int soc)
{
    int lit = 0;
    if (soc > 0) {
        lit = (soc + 19) / 20;
    }
    if (lit > SK_BATT_BARS) {
        lit = SK_BATT_BARS;
    }
    /* Child 0 is the top bar. Light the bottom bars first so empty bars drain from the top. */
    for (int i = 0; i < SK_BATT_BARS; i++) {
        bool on = i >= (SK_BATT_BARS - lit);
        lv_obj_set_style_bg_color(s_batt_bars[i], lv_color_hex(on ? SK_BATT_BAR : SK_LINE_DIM), 0);
    }
}

static int32_t s_right_edge;

static int32_t label_w(lv_obj_t *label)
{
    lv_obj_update_layout(label);
    return lv_obj_get_width(label);
}

static void align_label_right(lv_obj_t *label, int32_t right, int32_t y)
{
    if (!label) {
        return;
    }
    lv_obj_set_pos(label, right - label_w(label), y);
}

/* Daily values sit 10px from their icons. Icons match the daily value + caption stack. */
static void pin_icon_right(lv_obj_t *icon, int32_t y)
{
    lv_obj_set_pos(icon, s_right_edge - label_w(icon), y);
}

static void snug_corner_icons(void)
{
    if (!s_solar.daily || !s_solar.icon || !s_home.icon || !s_grid.icon || s_right_edge <= 0) {
        return;
    }

    lv_obj_set_pos(s_solar.icon, lv_obj_get_x(s_solar.daily) + label_w(s_solar.daily) + SK_ICON_GAP,
                   lv_obj_get_y(s_solar.daily));

    int32_t home_y = lv_obj_get_y(s_home.icon);
    pin_icon_right(s_home.icon, home_y);
    int32_t home_right = lv_obj_get_x(s_home.icon) - SK_ICON_GAP;
    align_label_right(s_home.daily, home_right, home_y);
    align_label_right(s_home.daily_lbl, home_right, home_y + 32);

    int32_t grid_y = lv_obj_get_y(s_grid.icon);
    pin_icon_right(s_grid.icon, grid_y);
    int32_t grid_right = lv_obj_get_x(s_grid.icon) - SK_ICON_GAP;
    align_label_right(s_grid.daily, grid_right, grid_y);
    align_label_right(s_grid.daily_lbl, grid_right, grid_y + 32);
}

static void layout_sunsynk(lv_obj_t *host)
{
    lv_obj_update_layout(host);
    int32_t w = lv_obj_get_content_width(host);
    int32_t h = lv_obj_get_content_height(host);
    if (w < 400 || h < 280) {
        return;
    }

    int32_t cx = w / 2;
    int32_t cy = h / 2 + 8;

    /* Corner anchors — nudged inward so larger boxes clear the edges */
    int32_t solar_x = 56;
    int32_t solar_y = 32;
    int32_t home_y = 32;
    int32_t batt_x = SK_SIDE_MARGIN;
    int32_t batt_y = h - 235;

    int32_t right_edge = w - SK_SIDE_MARGIN;
    s_right_edge = right_edge;
    /* DAILY CHARGE is the left-column reference for PV and battery value boxes. */
    int32_t left_col = batt_x;
    int32_t solar_box_x = left_col;
    int32_t solar_box_y = solar_y + 64;
    int32_t home_box_x = right_edge - SK_HOME_BOX_W;
    int32_t home_box_y = home_y + 64;
    int32_t batt_box_x = left_col;
    int32_t batt_box_y = batt_y + 96;
    int32_t grid_box_x = right_edge - SK_GRID_BOX_W;
    /* Shared bottom flow lane Y — keep Battery & Grid horizontals collinear. */
    int32_t bottom_lane_y = batt_box_y + SK_BATT_BOX_H / 2;
    int32_t grid_box_y = bottom_lane_y - SK_GRID_BOX_H / 2;

    /* --- Solar (top-left) --- */
    lv_obj_set_pos(s_solar.daily, solar_x, solar_y);
    lv_obj_set_pos(s_solar.daily_lbl, solar_x, solar_y + 32);
    lv_obj_set_pos(s_solar.icon, solar_x + 168, solar_y + 8);
    lv_obj_set_pos(s_solar.box, solar_box_x, solar_box_y);
    lv_obj_set_pos(s_solar.extra, left_col, solar_box_y + SK_SOLAR_BOX_H + 8);

    /* --- Home (top-right): box and icon share right_edge --- */
    lv_obj_set_pos(s_home.daily, home_box_x, home_y);
    lv_obj_set_pos(s_home.daily_lbl, home_box_x, home_y + 32);
    lv_obj_set_pos(s_home.icon, right_edge - SK_ICON_W, home_y);
    lv_obj_set_pos(s_home.box, home_box_x, home_box_y);

    /* --- Battery (bottom-left) --- */
    lv_obj_set_pos(s_batt.daily, left_col, batt_y);
    lv_obj_set_pos(s_batt.daily_lbl, left_col, batt_y + 28);
    lv_obj_set_pos(s_batt.extra, left_col, batt_y + 56); /* discharge line reused */
    lv_obj_set_pos(s_batt.box, batt_box_x, batt_box_y);
    int32_t batt_shell_x = batt_x + 172;
    int32_t batt_shell_y = batt_y + 110;
    int32_t batt_cap_y = batt_shell_y - 6;
    lv_obj_set_pos(s_batt_shell, batt_shell_x, batt_shell_y);
    /* 2px gap between temperature label and the battery cap. */
    lv_obj_set_pos(s_batt_temp, batt_shell_x - 2, batt_cap_y - 20);
    lv_obj_t *cap = (lv_obj_t *)lv_obj_get_user_data(s_batt_shell);
    if (cap) {
        lv_obj_set_pos(cap, batt_shell_x + 11, batt_cap_y);
    }
    lv_obj_set_pos(s_batt_soc, batt_x + 230, batt_y + 124);
    lv_obj_set_pos(s_batt_state, batt_x + 230, batt_y + 164);

    /* --- Grid (bottom-right) --- */
    /* Caption-to-box gap matches Home: DAILY LOAD sits 32px above its rectangle. */
    int32_t grid_head_y = grid_box_y - 64;
    lv_obj_set_pos(s_grid.daily, grid_box_x, grid_head_y);
    lv_obj_set_pos(s_grid.daily_lbl, grid_box_x, grid_head_y + 32);
    lv_obj_set_pos(s_grid.icon, right_edge - SK_ICON_W, grid_head_y);
    lv_obj_set_pos(s_grid.box, grid_box_x, grid_box_y);
    lv_obj_set_pos(s_grid.extra, grid_box_x, grid_box_y + SK_GRID_BOX_H + 8);

    /* --- Inverter hub --- */
    int32_t inv_box_x = cx - SK_INV_BOX_W / 2;
    int32_t inv_box_y = cy - SK_INV_BOX_TOP;
    int32_t inv_icon_x = cx - SK_INV_ICON_W / 2;
    int32_t inv_icon_y = inv_box_y + SK_INV_BOX_H + SK_INV_ICON_GAP;
    lv_obj_set_pos(s_inv_box, inv_box_x, inv_box_y);
    lv_obj_set_pos(s_inv_icon, inv_icon_x, inv_icon_y);
    /* Status sits to the right of the inverter icon (vertically centered). */
    int32_t status_y = inv_icon_y + (SK_INV_ICON_H - 22) / 2;
    lv_obj_set_pos(s_inv_status_dot, inv_icon_x + SK_INV_ICON_W + 10, status_y + 6);
    lv_obj_set_pos(s_inv_status, inv_icon_x + SK_INV_ICON_W + 26, status_y);

    /* Force layout so box positions are current before attach math. */
    lv_obj_update_layout(host);

    /* Hub stem ends just below the inverter icon (status is now beside the icon). */
    int32_t hub_bot_y = inv_icon_y + SK_INV_ICON_H + SK_ATTACH_GAP + 4;

    /* Flow geometry: mirrored L-paths — same |SK_RISER_DX| left/right, clear of hub. */
    lv_point_precise_t solar_outer =
        box_edge_attach(solar_box_x, solar_box_y, SK_SOLAR_BOX_W, SK_SOLAR_BOX_H, true);
    lv_point_precise_t solar_hub = {.x = (lv_value_precise_t)(cx - SK_RISER_DX),
                                    .y = (lv_value_precise_t)(cy - SK_HUB_CLEAR_TOP)};
    lv_point_precise_t solar_mid = {.x = solar_hub.x, .y = solar_outer.y};
    set_edge_points(&s_edges[SK_EDGE_SOLAR], SK_EDGE_SOLAR, solar_outer, solar_mid, solar_hub);

    /* Battery: start past icon + SOC/state; horizontal on shared bottom_lane_y. */
    lv_point_precise_t batt_outer = {.x = (lv_value_precise_t)(batt_x + 320 + SK_ATTACH_GAP),
                                     .y = (lv_value_precise_t)bottom_lane_y};
    lv_point_precise_t batt_hub = {.x = (lv_value_precise_t)(cx - SK_RISER_DX),
                                   .y = (lv_value_precise_t)hub_bot_y};
    lv_point_precise_t batt_mid = {.x = batt_hub.x, .y = batt_outer.y};
    set_edge_points(&s_edges[SK_EDGE_BATT], SK_EDGE_BATT, batt_outer, batt_mid, batt_hub);

    lv_point_precise_t home_outer =
        box_edge_attach(home_box_x, home_box_y, SK_HOME_BOX_W, SK_HOME_BOX_H, false);
    lv_point_precise_t home_hub = {.x = (lv_value_precise_t)(cx + SK_RISER_DX),
                                   .y = (lv_value_precise_t)(cy - SK_HUB_CLEAR_TOP)};
    lv_point_precise_t home_mid = {.x = home_hub.x, .y = home_outer.y};
    set_edge_points(&s_edges[SK_EDGE_HOME], SK_EDGE_HOME, home_outer, home_mid, home_hub);

    lv_point_precise_t grid_outer = {.x = (lv_value_precise_t)(grid_box_x - SK_ATTACH_GAP),
                                     .y = (lv_value_precise_t)bottom_lane_y};
    lv_point_precise_t grid_hub = {.x = (lv_value_precise_t)(cx + SK_RISER_DX),
                                   .y = (lv_value_precise_t)hub_bot_y};
    lv_point_precise_t grid_mid = {.x = grid_hub.x, .y = grid_outer.y};
    set_edge_points(&s_edges[SK_EDGE_GRID], SK_EDGE_GRID, grid_outer, grid_mid, grid_hub);

    /* Raise nodes above lines; keep dots above lines but under boxes via z-order after update */
    lv_obj_move_foreground(s_solar.box);
    lv_obj_move_foreground(s_home.box);
    lv_obj_move_foreground(s_batt.box);
    lv_obj_move_foreground(s_grid.box);
    lv_obj_move_foreground(s_inv_box);
    lv_obj_move_foreground(s_inv_icon);
    lv_obj_move_foreground(s_inv_status_dot);
    lv_obj_move_foreground(s_inv_status);
    lv_obj_move_foreground(s_batt_shell);
    if (cap) {
        lv_obj_move_foreground(cap);
    }
    lv_obj_move_foreground(s_batt_soc);
    lv_obj_move_foreground(s_batt_state);
    lv_obj_move_foreground(s_batt_temp);

    snug_corner_icons();
    s_layout_done = true;
}

static void on_host_size(lv_event_t *e)
{
    (void)e;
    if (s_host) {
        layout_sunsynk(s_host);
    }
}

static sk_corner_t make_corner(lv_obj_t *host, uint32_t accent, const char *icon, const char *daily_lbl,
                               int32_t box_w, int32_t box_h, bool with_tag)
{
    sk_corner_t c = {0};
    c.daily = sk_label(host, &lv_font_montserrat_28, accent);
    c.daily_lbl = sk_label(host, &lv_font_montserrat_16, accent);
    lv_label_set_text(c.daily_lbl, daily_lbl);

    c.icon = sk_label(host, &lv_font_montserrat_48, accent);
    lv_label_set_text(c.icon, icon);

    c.box = make_live_box(host, accent, box_w, box_h);
    c.live = sk_label(c.box, &lv_font_montserrat_24, SK_TEXT);
    lv_obj_center(c.live);

    c.extra = sk_label(host, &lv_font_montserrat_16, SK_SOLAR_DIM);
    lv_label_set_text(c.extra, "");

    if (with_tag) {
        c.tag = sk_label(host, &lv_font_montserrat_16, accent);
        lv_label_set_text(c.tag, "Essential");
    }
    return c;
}

void ui_sunsynk_build(lv_obj_t *root)
{
    s_edges_inited = false;
    s_layout_done = false;

    lv_obj_set_style_bg_color(root, lv_color_hex(SK_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    s_host = lv_obj_create(root);
    strip(s_host);
    lv_obj_set_size(s_host, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_host, lv_color_hex(SK_BG), 0);
    lv_obj_set_style_bg_opa(s_host, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_host, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_host, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    /* Init edges first so lines sit under nodes */
    init_edge(&s_edges[SK_EDGE_SOLAR], s_host, SK_SOLAR, SK_EDGE_SOLAR);
    init_edge(&s_edges[SK_EDGE_BATT], s_host, SK_BATT, SK_EDGE_BATT);
    init_edge(&s_edges[SK_EDGE_HOME], s_host, SK_LOAD, SK_EDGE_HOME);
    init_edge(&s_edges[SK_EDGE_GRID], s_host, SK_GRID, SK_EDGE_GRID);
    s_edges_inited = true;

    s_solar = make_corner(s_host, SK_SOLAR, LV_SYMBOL_CHARGE, "DAILY SOLAR", SK_SOLAR_BOX_W, SK_SOLAR_BOX_H,
                          false);
    s_home = make_corner(s_host, SK_LOAD, LV_SYMBOL_HOME, "DAILY LOAD", SK_HOME_BOX_W, SK_HOME_BOX_H, false);
    lv_obj_set_style_text_color(s_home.daily, lv_color_hex(SK_HOME), 0);
    lv_obj_set_style_text_color(s_home.daily_lbl, lv_color_hex(SK_HOME), 0);

    /* Battery corner: custom fields */
    s_batt.daily = sk_label(s_host, &lv_font_montserrat_24, SK_BATT);
    s_batt.daily_lbl = sk_label(s_host, &lv_font_montserrat_16, SK_BATT);
    lv_label_set_text(s_batt.daily_lbl, "DAILY CHARGE");
    s_batt.extra = sk_label(s_host, &lv_font_montserrat_16, SK_BATT);
    lv_label_set_text(s_batt.extra, "-- DAILY DISCHARGE");
    s_batt.box = make_live_box(s_host, SK_BATT, SK_BATT_BOX_W, SK_BATT_BOX_H);
    s_batt.live = sk_label(s_batt.box, &lv_font_montserrat_20, SK_TEXT);
    lv_obj_set_style_text_align(s_batt.live, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_batt.live);
    lv_label_set_text(s_batt.live, "--\n--\n--");
    build_batt_icon(s_host);

    s_grid = make_corner(s_host, SK_GRID, LV_SYMBOL_GPS, "DAILY GRID BUY", SK_GRID_BOX_W, SK_GRID_BOX_H, false);
    lv_obj_set_style_text_font(s_grid.live, &lv_font_montserrat_24, 0);
    lv_obj_set_style_pad_hor(s_grid.box, 12, 0);
    lv_obj_set_style_pad_ver(s_grid.box, 8, 0);
    lv_label_set_text(s_grid.live, "--  --\n--  --");
    lv_label_set_text(s_grid.extra, "");

    /* Inverter hub */
    s_inv_box = make_live_box(s_host, SK_HUB, SK_INV_BOX_W, SK_INV_BOX_H);
    s_inv_pwr = sk_label(s_inv_box, &lv_font_montserrat_24, SK_TEXT);
    lv_obj_set_style_text_align(s_inv_pwr, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_all(s_inv_box, 6, 0);
    lv_obj_center(s_inv_pwr);
    lv_label_set_text(s_inv_pwr, "--\n--");

    s_inv_icon = lv_obj_create(s_host);
    lv_obj_set_size(s_inv_icon, SK_INV_ICON_W, SK_INV_ICON_H);
    lv_obj_set_style_bg_color(s_inv_icon, lv_color_hex(0x3A3A3A), 0);
    lv_obj_set_style_border_width(s_inv_icon, 0, 0);
    lv_obj_set_style_radius(s_inv_icon, 10, 0);
    lv_obj_clear_flag(s_inv_icon, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *inv_sym = sk_label(s_inv_icon, &lv_font_montserrat_36, SK_MUTED);
    lv_label_set_text(inv_sym, LV_SYMBOL_REFRESH);
    lv_obj_center(inv_sym);

    s_inv_status_dot = lv_obj_create(s_host);
    lv_obj_set_size(s_inv_status_dot, 12, 12);
    lv_obj_set_style_radius(s_inv_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_inv_status_dot, lv_color_hex(SK_MUTED), 0);
    lv_obj_set_style_border_width(s_inv_status_dot, 0, 0);
    lv_obj_clear_flag(s_inv_status_dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_inv_status = sk_label(s_host, &lv_font_montserrat_20, SK_MUTED);
    lv_label_set_text(s_inv_status, "--");

    lv_obj_add_event_cb(s_host, on_host_size, LV_EVENT_SIZE_CHANGED, NULL);
    lv_obj_update_layout(root);
    layout_sunsynk(s_host);
}

void ui_sunsynk_update(const telemetry_snapshot_t *snap)
{
    if (!s_host) {
        return;
    }
    if (!s_layout_done) {
        layout_sunsynk(s_host);
    }

    char buf[96];
    char line[48];

    /* Solar */
    bool pv_e_ok = telemetry_is_fresh(snap, METRIC_PV_ENERGY_TODAY);
    fmt_kwh(buf, sizeof(buf), pv_e_ok ? snap->m[METRIC_PV_ENERGY_TODAY].value : 0.f, pv_e_ok);
    lv_label_set_text(s_solar.daily, buf);
    lv_obj_set_style_text_color(s_solar.daily, lv_color_hex(pv_e_ok ? SK_SOLAR : SK_MUTED), 0);

    bool pf = false;
    float pv_w = telemetry_pv_total_w(snap, &pf);
    fmt_w(buf, sizeof(buf), pv_w, pf, false);
    lv_label_set_text(s_solar.live, buf);
    lv_obj_set_style_text_color(s_solar.live, lv_color_hex(pf ? SK_TEXT : SK_MUTED), 0);

    bool pv1v = telemetry_is_fresh(snap, METRIC_PV1_V);
    bool pv1i = telemetry_is_fresh(snap, METRIC_PV1_I);
    bool pv1p = telemetry_is_fresh(snap, METRIC_PV1_P);
    bool pv2p = telemetry_is_fresh(snap, METRIC_PV2_P);
    float p1 = pv1p ? snap->m[METRIC_PV1_P].value : 0.f;
    float p2 = pv2p ? snap->m[METRIC_PV2_P].value : 0.f;
    float sum = p1 + p2;
    if (pv1v || pv1i || (pv1p && sum > 0.5f)) {
        int pct = (sum > 0.5f) ? (int)((p1 / sum) * 100.f + 0.5f) : 0;
        if (pct < 0) {
            pct = 0;
        }
        if (pct > 100) {
            pct = 100;
        }
        snprintf(buf, sizeof(buf), "PV1 %d%%  ", (pv1p && sum > 0.5f) ? pct : 0);
        if (pv1v) {
            snprintf(line, sizeof(line), "%.1f V ", snap->m[METRIC_PV1_V].value);
            strncat(buf, line, sizeof(buf) - strlen(buf) - 1);
        }
        if (pv1i) {
            snprintf(line, sizeof(line), "%.1f A", snap->m[METRIC_PV1_I].value);
            strncat(buf, line, sizeof(buf) - strlen(buf) - 1);
        }
        lv_label_set_text(s_solar.extra, buf);
        lv_obj_set_style_text_color(s_solar.extra, lv_color_hex(SK_SOLAR_DIM), 0);
    } else {
        lv_label_set_text(s_solar.extra, "");
    }

    /* Home / Essential */
    bool home_ok = false;
    float home_kwh = telemetry_home_energy_today(snap, &home_ok);
    fmt_kwh(buf, sizeof(buf), home_kwh, home_ok);
    lv_label_set_text(s_home.daily, buf);
    lv_obj_set_style_text_color(s_home.daily, lv_color_hex(home_ok ? SK_HOME : SK_MUTED), 0);

    bool load_ok = telemetry_is_fresh(snap, METRIC_LOAD_P);
    float load_w = load_ok ? snap->m[METRIC_LOAD_P].value : 0.f;
    fmt_w(buf, sizeof(buf), load_w, load_ok, false);
    lv_label_set_text(s_home.live, buf);

    /* Battery */
    bool chg_ok = telemetry_is_fresh(snap, METRIC_BATT_CHG_TODAY);
    if (chg_ok) {
        snprintf(buf, sizeof(buf), "%.1f kWh", snap->m[METRIC_BATT_CHG_TODAY].value);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(s_batt.daily, buf);

    bool dis_ok = telemetry_is_fresh(snap, METRIC_BATT_DIS_TODAY);
    if (dis_ok) {
        snprintf(buf, sizeof(buf), "%.1f kWh DAILY DISCHARGE", snap->m[METRIC_BATT_DIS_TODAY].value);
    } else {
        snprintf(buf, sizeof(buf), "-- DAILY DISCHARGE");
    }
    lv_label_set_text(s_batt.extra, buf);

    bool bv = telemetry_is_fresh(snap, METRIC_BATT_V);
    bool bi = telemetry_is_fresh(snap, METRIC_BATT_I);
    bool bp = telemetry_is_fresh(snap, METRIC_BATT_P);
    float batt_w = bp ? snap->m[METRIC_BATT_P].value : 0.f;
    {
        char vbuf[24] = "--";
        char ibuf[24] = "--";
        char wbuf[24] = "--";
        if (bv) {
            snprintf(vbuf, sizeof(vbuf), "%.1f V", snap->m[METRIC_BATT_V].value);
        }
        if (bi) {
            snprintf(ibuf, sizeof(ibuf), "%+.1f A", snap->m[METRIC_BATT_I].value);
        }
        if (bp) {
            snprintf(wbuf, sizeof(wbuf), "%.0f W", absf(batt_w));
        }
        snprintf(buf, sizeof(buf), "%s\n%s\n%s", vbuf, ibuf, wbuf);
        lv_label_set_text(s_batt.live, buf);
    }

    bool soc_ok = telemetry_is_fresh(snap, METRIC_BATT_SOC);
    int soc = 0;
    if (soc_ok) {
        soc = (int)snap->m[METRIC_BATT_SOC].value;
        if (soc < 0) {
            soc = 0;
        }
        if (soc > 100) {
            soc = 100;
        }
        snprintf(buf, sizeof(buf), "%d%%", soc);
        set_batt_bars(soc);
    } else {
        snprintf(buf, sizeof(buf), "--");
        set_batt_bars(0);
    }
    lv_label_set_text(s_batt_soc, buf);

    bool bt = telemetry_is_fresh(snap, METRIC_BATT_T);
    if (bt) {
        snprintf(buf, sizeof(buf), "%.1f C", snap->m[METRIC_BATT_T].value);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(s_batt_temp, buf);

    /* Hold last CHARGING/IDLE/DISCHARGING. A new state must stay stable 5s before it replaces the old one. */
    if (snap->m[METRIC_BATT_P].valid) {
        float pw = snap->m[METRIC_BATT_P].value;
        batt_st_t cand = BATT_ST_IDLE;
        if (pw < -40.f) {
            cand = BATT_ST_CHG;
        } else if (pw > 40.f) {
            cand = BATT_ST_DIS;
        }
        int64_t now = esp_timer_get_time();
        if (s_batt_st_shown == BATT_ST_NONE) {
            s_batt_st_shown = cand;
            s_batt_st_pending = cand;
            s_batt_st_pending_us = now;
        } else if (cand == s_batt_st_shown) {
            s_batt_st_pending = cand;
            s_batt_st_pending_us = now;
        } else if (s_batt_st_pending != cand) {
            s_batt_st_pending = cand;
            s_batt_st_pending_us = now;
        } else if ((now - s_batt_st_pending_us) >= SK_BATT_ST_HOLD_US) {
            s_batt_st_shown = cand;
        }
    }
    switch (s_batt_st_shown) {
    case BATT_ST_CHG:
        lv_label_set_text(s_batt_state, "CHARGING");
        lv_obj_set_style_text_color(s_batt_state, lv_color_hex(SK_OK), 0);
        break;
    case BATT_ST_DIS:
        lv_label_set_text(s_batt_state, "DISCHARGING");
        lv_obj_set_style_text_color(s_batt_state, lv_color_hex(SK_BATT), 0);
        break;
    case BATT_ST_IDLE:
        lv_label_set_text(s_batt_state, "IDLE");
        lv_obj_set_style_text_color(s_batt_state, lv_color_hex(SK_MUTED), 0);
        break;
    default:
        lv_label_set_text(s_batt_state, "BATTERY");
        lv_obj_set_style_text_color(s_batt_state, lv_color_hex(SK_MUTED), 0);
        break;
    }

    /* Grid */
    bool buy_ok = telemetry_is_fresh(snap, METRIC_GRID_BUY_TODAY);
    fmt_kwh(buf, sizeof(buf), buy_ok ? snap->m[METRIC_GRID_BUY_TODAY].value : 0.f, buy_ok);
    lv_label_set_text(s_grid.daily, buf);
    lv_obj_set_style_text_color(s_grid.daily, lv_color_hex(buy_ok ? SK_GRID : SK_MUTED), 0);

    bool grid_ok = telemetry_is_fresh(snap, METRIC_GRID_P_CT);
    float grid_w = grid_ok ? snap->m[METRIC_GRID_P_CT].value : 0.f;
    bool gv = telemetry_is_fresh(snap, METRIC_GRID_V);
    /* Synk-local sticky Hz: keep last valid sample even after MQTT freshness expires. */
    bool ghz = snap->m[METRIC_GRID_HZ].valid;
    float grid_hz = snap->m[METRIC_GRID_HZ].value;
    bool gi = telemetry_is_fresh(snap, METRIC_GRID_I);
    float grid_i = gi ? snap->m[METRIC_GRID_I].value : 0.f;
    {
        /* Row1: power + Hz; Row2: voltage + CT current */
        char r1_w[20] = "--";
        char r1_hz[20] = "--";
        char r2_v[20] = "--";
        char r2_i[20] = "--";
        if (grid_ok) {
            snprintf(r1_w, sizeof(r1_w), "%.0f W", absf(grid_w));
        }
        if (ghz) {
            snprintf(r1_hz, sizeof(r1_hz), "%.2f Hz", grid_hz);
        }
        if (gv) {
            snprintf(r2_v, sizeof(r2_v), "%.1f V", snap->m[METRIC_GRID_V].value);
        }
        if (gi) {
            snprintf(r2_i, sizeof(r2_i), "%+.2f A", grid_i);
        }
        snprintf(buf, sizeof(buf), "%s  %s\n%s  %s", r1_w, r1_hz, r2_v, r2_i);
        lv_label_set_text(s_grid.live, buf);
    }
    /* Non-essential omitted: no separate MQTT metric */

    /* Inverter hub */
    bool ip = telemetry_is_fresh(snap, METRIC_INV_P);
    bool ii = false;
    float inv_i = 0.f;
    /* Prefer load current as inverter AC current proxy if INV current absent */
    if (telemetry_is_fresh(snap, METRIC_LOAD_I)) {
        ii = true;
        inv_i = snap->m[METRIC_LOAD_I].value;
    }
    {
        char wbuf[24] = "--";
        char ibuf[24] = "--";
        if (ip) {
            snprintf(wbuf, sizeof(wbuf), "%.0f W", snap->m[METRIC_INV_P].value);
        }
        if (ii) {
            snprintf(ibuf, sizeof(ibuf), "%.1f A", absf(inv_i));
        }
        snprintf(buf, sizeof(buf), "%s\n%s", wbuf, ibuf);
        lv_label_set_text(s_inv_pwr, buf);
    }

    bool st_fresh = false;
    const char *st = telemetry_status_text(snap, &st_fresh);
    if (!st_fresh) {
        lv_label_set_text(s_inv_status, "--");
        lv_obj_set_style_bg_color(s_inv_status_dot, lv_color_hex(SK_MUTED), 0);
        lv_obj_set_style_text_color(s_inv_status, lv_color_hex(SK_MUTED), 0);
    } else {
        lv_label_set_text(s_inv_status, st);
        uint32_t col = telemetry_status_color(snap);
        lv_obj_set_style_bg_color(s_inv_status_dot, lv_color_hex(col), 0);
        lv_obj_set_style_text_color(s_inv_status, lv_color_hex(col), 0);
    }

    /* Flows */
    s_edges[SK_EDGE_SOLAR].active = pf && pv_w >= SK_FLOW_THRESH_W;
    s_edges[SK_EDGE_SOLAR].toward_hub = true;
    s_edges[SK_EDGE_SOLAR].speed = flow_speed(pv_w);
    s_edges[SK_EDGE_SOLAR].color = SK_SOLAR;

    float flow_load = absf(load_w);
    s_edges[SK_EDGE_HOME].active = load_ok && flow_load >= SK_FLOW_THRESH_W;
    s_edges[SK_EDGE_HOME].toward_hub = false;
    s_edges[SK_EDGE_HOME].speed = flow_speed(flow_load);
    s_edges[SK_EDGE_HOME].color = SK_LOAD;

    if (grid_ok && absf(grid_w) >= SK_FLOW_THRESH_W) {
        s_edges[SK_EDGE_GRID].active = true;
        s_edges[SK_EDGE_GRID].toward_hub = (grid_w > 0.f); /* import toward hub */
        s_edges[SK_EDGE_GRID].speed = flow_speed(grid_w);
        s_edges[SK_EDGE_GRID].color = SK_GRID;
    } else {
        s_edges[SK_EDGE_GRID].active = false;
        s_edges[SK_EDGE_GRID].speed = 0.f;
    }

    if (bp && absf(batt_w) >= SK_FLOW_THRESH_W) {
        s_edges[SK_EDGE_BATT].active = true;
        s_edges[SK_EDGE_BATT].toward_hub = (batt_w > 0.f); /* discharge toward hub */
        s_edges[SK_EDGE_BATT].speed = flow_speed(batt_w);
        s_edges[SK_EDGE_BATT].color = SK_BATT;
    } else {
        s_edges[SK_EDGE_BATT].active = false;
        s_edges[SK_EDGE_BATT].speed = 0.f;
    }

    for (int i = 0; i < SK_FLOW_EDGES; i++) {
        if (s_edges_inited) {
            update_flow_edge(&s_edges[i]);
        }
    }
    snug_corner_icons();
}
