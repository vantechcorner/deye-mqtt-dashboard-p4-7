#include "ui.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_timer.h"
#include "lvgl.h"
#include "telemetry.h"
#include "ui_ha.h"

/* Soft dark palette — avoid high-contrast static blocks (TD2 image persistence). */
#define COL_BG 0x12161C
#define COL_BAR 0x0E1116
#define COL_CARD 0x1A1F27
#define COL_BORDER 0x2A303A
#define COL_TEXT 0xE6EDF3
#define COL_MUTED 0x8B949E
#define COL_POS 0x3FB950
#define COL_NEG 0xE3B341
#define COL_ACCENT 0x58A6FF
#define COL_PV 0xE3B341
#define COL_DANGER 0xF85149
#define COL_SEG_OFF 0x2A303A
#define COL_BTN 0x21262D
#define COL_BTN_ON 0x30363D

#define SB_FONT lv_font_montserrat_16
#define TITLE_FONT lv_font_montserrat_16
#define TITLE_LG lv_font_montserrat_20
#define VALUE_FONT lv_font_montserrat_24
#define VALUE_LG lv_font_montserrat_28
#define HERO_FONT lv_font_montserrat_36
#define GRID_ON_V_MIN 80.f
#define GRID_ON_HZ_MIN 45.f
#define GRID_ON_HZ_MAX 66.f

#define BATT_PWR_MAX_W 3500.f
#define BATT_PWR_SEGS 10
#define KPI_SUB_H 22

typedef enum {
    UI_MODE_SIMPLE = 0,
    UI_MODE_FULL = 1,
    UI_MODE_HA = 2,
} ui_mode_t;

typedef struct {
    lv_obj_t *label;
    metric_id_t id;
    bool signed_w;
} bound_label_t;

static ui_mode_t s_mode = UI_MODE_SIMPLE;
static lv_obj_t *s_view_simple;
static lv_obj_t *s_view_full;
static lv_obj_t *s_view_ha;
static lv_obj_t *s_btn_simple;
static lv_obj_t *s_btn_full;
static lv_obj_t *s_btn_ha;
static lv_obj_t *s_btn_setup;

static lv_obj_t *s_wifi;
static lv_obj_t *s_mqtt;
static lv_obj_t *s_status;
static lv_obj_t *s_grid_mode;
static lv_obj_t *s_clock;

static lv_obj_t *s_prov_overlay;
static bool s_provisioning;
static void (*s_setup_request_cb)(void);
static int s_clock_taps;
static int64_t s_clock_tap_us;

static lv_obj_t *s_simple_pv;
static lv_obj_t *s_simple_soc_arc;
static lv_obj_t *s_soc_bar_batt;
static lv_obj_t *s_pwr_segs[BATT_PWR_SEGS];
static lv_obj_t *s_full_pv;
static lv_obj_t *s_full_soc_arc;
static lv_obj_t *s_full_grid_dir;
static lv_obj_t *s_full_batt_dir;
static lv_obj_t *s_pv1_share;
static lv_obj_t *s_pv1_lbl;
static lv_obj_t *s_pv2_lbl;

static bound_label_t s_bound[64];
static size_t s_bound_n;

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, TELEMETRY_STALE_PLACEHOLDER);
    return l;
}

static void bind(lv_obj_t *label, metric_id_t id, bool signed_w)
{
    if (s_bound_n < sizeof(s_bound) / sizeof(s_bound[0])) {
        s_bound[s_bound_n++] = (bound_label_t){label, id, signed_w};
    }
}

static void strip_chrome(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
}

static lv_obj_t *make_card_shell(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_outline_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *make_card(lv_obj_t *parent, const char *title, const lv_font_t *title_font)
{
    lv_obj_t *card = make_card_shell(parent);
    lv_obj_t *t = make_label(card, title_font, COL_MUTED);
    lv_obj_set_width(t, LV_PCT(100));
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(t, title);
    return card;
}

static lv_obj_t *add_metric(lv_obj_t *card, metric_id_t id, bool signed_w, const lv_font_t *font)
{
    lv_obj_t *v = make_label(card, font, COL_TEXT);
    lv_obj_set_width(v, LV_PCT(100));
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
    bind(v, id, signed_w);
    return v;
}

static void style_root(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(COL_TEXT), 0);
}

static void set_sb_item(lv_obj_t *label, const char *symbol, const char *text)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "%s %s", symbol, text);
    lv_label_set_text(label, buf);
}

static const char *status_symbol(const telemetry_snapshot_t *snap)
{
    bool fresh = false;
    telemetry_status_text(snap, &fresh);
    if (!fresh) {
        return LV_SYMBOL_POWER;
    }
    int v = (int)snap->m[METRIC_STATUS].value;
    switch (v) {
    case 2:
        return LV_SYMBOL_OK;
    case 3:
        return LV_SYMBOL_WARNING;
    case 4:
        return LV_SYMBOL_CLOSE;
    case 1:
        return LV_SYMBOL_REFRESH;
    default:
        return LV_SYMBOL_POWER;
    }
}

static void grid_mode_text(const telemetry_snapshot_t *snap, const char **text, uint32_t *color)
{
    bool v_ok = telemetry_is_fresh(snap, METRIC_GRID_V);
    bool hz_ok = telemetry_is_fresh(snap, METRIC_GRID_HZ);
    bool on = false;
    if (v_ok && snap->m[METRIC_GRID_V].value >= GRID_ON_V_MIN) {
        on = true;
    } else if (hz_ok) {
        float hz = snap->m[METRIC_GRID_HZ].value;
        on = (hz >= GRID_ON_HZ_MIN && hz <= GRID_ON_HZ_MAX);
    }
    if (!v_ok && !hz_ok) {
        *text = "--";
        *color = COL_MUTED;
        return;
    }
    if (on) {
        *text = "On-Grid";
        *color = COL_POS;
    } else {
        *text = "Off-Grid";
        *color = COL_NEG;
    }
}

static void style_mode_btn(lv_obj_t *btn, bool active)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(active ? COL_BTN_ON : COL_BTN), 0);
    lv_obj_set_style_border_width(btn, active ? 2 : 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(active ? COL_POS : COL_BORDER), 0);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (lbl) {
        lv_obj_set_style_text_color(lbl, lv_color_hex(active ? COL_TEXT : COL_MUTED), 0);
    }
}

static void apply_mode(ui_mode_t mode)
{
    s_mode = mode;
    if (mode == UI_MODE_SIMPLE) {
        lv_obj_clear_flag(s_view_simple, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_view_full, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_view_ha, LV_OBJ_FLAG_HIDDEN);
    } else if (mode == UI_MODE_FULL) {
        lv_obj_add_flag(s_view_simple, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_view_full, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_view_ha, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_view_simple, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_view_full, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_view_ha, LV_OBJ_FLAG_HIDDEN);
    }
    style_mode_btn(s_btn_simple, mode == UI_MODE_SIMPLE);
    style_mode_btn(s_btn_full, mode == UI_MODE_FULL);
    style_mode_btn(s_btn_ha, mode == UI_MODE_HA);
}

static void on_mode_btn(lv_event_t *e)
{
    ui_mode_t mode = (ui_mode_t)(uintptr_t)lv_event_get_user_data(e);
    apply_mode(mode);
}

static void request_setup_from_ui(void)
{
    if (s_setup_request_cb) {
        s_setup_request_cb();
    }
}

static void on_setup_btn(lv_event_t *e)
{
    (void)e;
    request_setup_from_ui();
}

static void on_clock_tap(lv_event_t *e)
{
    (void)e;
    int64_t now = esp_timer_get_time();
    if (now - s_clock_tap_us > 2500000) {
        s_clock_taps = 0;
    }
    s_clock_tap_us = now;
    s_clock_taps++;
    if (s_clock_taps >= 5) {
        s_clock_taps = 0;
        request_setup_from_ui();
    }
}

static lv_obj_t *make_mode_btn(lv_obj_t *parent, const char *text, ui_mode_t mode)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 86, 40);
    lv_obj_set_style_min_width(btn, 86, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &TITLE_FONT, 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, on_mode_btn, LV_EVENT_CLICKED, (void *)(uintptr_t)mode);
    return btn;
}

static lv_obj_t *build_status_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), 56);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COL_BAR), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 12, 0);
    lv_obj_set_style_pad_ver(bar, 6, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 10, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Left status chips — content-sized so mode buttons are never squashed. */
    lv_obj_t *left = lv_obj_create(bar);
    strip_chrome(left);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_PCT(100));
    lv_obj_set_flex_grow(left, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left, 12, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    s_wifi = make_label(left, &SB_FONT, COL_MUTED);
    set_sb_item(s_wifi, LV_SYMBOL_WIFI, "--");
    s_mqtt = make_label(left, &SB_FONT, COL_MUTED);
    set_sb_item(s_mqtt, LV_SYMBOL_ENVELOPE, "--");
    s_status = make_label(left, &SB_FONT, COL_MUTED);
    set_sb_item(s_status, LV_SYMBOL_POWER, "--");
    s_grid_mode = make_label(left, &SB_FONT, COL_MUTED);
    set_sb_item(s_grid_mode, LV_SYMBOL_CHARGE, "--");

    /* Center clock takes remaining width between chips and mode buttons.
     * Tap the clock 5× within ~2.5s to re-enter SoftAP setup. */
    s_clock = make_label(bar, &SB_FONT, COL_TEXT);
    lv_obj_set_flex_grow(s_clock, 1);
    lv_obj_set_width(s_clock, 0);
    lv_obj_set_style_text_align(s_clock, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_clock, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_clock, "--, --- --, ---- --:--:--");
    lv_obj_add_flag(s_clock, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_clock, on_clock_tap, LV_EVENT_CLICKED, NULL);

    /* Fixed-width mode toggle — Simple / Full / HA + Setup. */
    lv_obj_t *toggle = lv_obj_create(bar);
    strip_chrome(toggle);
    lv_obj_set_size(toggle, 340, LV_PCT(100));
    lv_obj_set_flex_grow(toggle, 0);
    lv_obj_set_flex_flow(toggle, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toggle, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(toggle, 6, 0);
    lv_obj_clear_flag(toggle, LV_OBJ_FLAG_SCROLLABLE);

    s_btn_simple = make_mode_btn(toggle, "Simple", UI_MODE_SIMPLE);
    s_btn_full = make_mode_btn(toggle, "Full", UI_MODE_FULL);
    s_btn_ha = make_mode_btn(toggle, "HA", UI_MODE_HA);

    s_btn_setup = lv_button_create(toggle);
    lv_obj_set_size(s_btn_setup, 48, 40);
    lv_obj_set_style_radius(s_btn_setup, 10, 0);
    lv_obj_set_style_pad_all(s_btn_setup, 0, 0);
    lv_obj_set_style_shadow_width(s_btn_setup, 0, 0);
    lv_obj_set_style_bg_color(s_btn_setup, lv_color_hex(COL_BTN), 0);
    lv_obj_set_style_border_width(s_btn_setup, 1, 0);
    lv_obj_set_style_border_color(s_btn_setup, lv_color_hex(COL_BORDER), 0);
    lv_obj_t *setup_lbl = lv_label_create(s_btn_setup);
    lv_label_set_text(setup_lbl, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(setup_lbl, &SB_FONT, 0);
    lv_obj_set_style_text_color(setup_lbl, lv_color_hex(COL_MUTED), 0);
    lv_obj_center(setup_lbl);
    lv_obj_add_event_cb(s_btn_setup, on_setup_btn, LV_EVENT_CLICKED, NULL);
    return bar;
}

static lv_obj_t *make_soc_arc(lv_obj_t *parent, int size, int width, lv_obj_t **out_arc, const lv_font_t *val_font)
{
    lv_obj_t *gauge = lv_obj_create(parent);
    lv_obj_set_size(gauge, size, size);
    strip_chrome(gauge);
    lv_obj_clear_flag(gauge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(gauge, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    lv_obj_t *arc = lv_arc_create(gauge);
    lv_obj_set_size(arc, size - 8, size - 8);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc, 0, 0);
    lv_obj_set_style_pad_all(arc, width, 0);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COL_SEG_OFF), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COL_POS), LV_PART_INDICATOR);
    lv_obj_set_style_opa(arc, LV_OPA_0, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_width(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_height(arc, 0, LV_PART_KNOB);
    *out_arc = arc;

    lv_obj_t *soc_val = make_label(gauge, val_font, COL_TEXT);
    lv_obj_set_style_text_align(soc_val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(soc_val);
    bind(soc_val, METRIC_BATT_SOC, false);
    return gauge;
}

static void style_soc_bar(lv_obj_t *bar, int h)
{
    lv_obj_set_size(bar, LV_PCT(100), h);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COL_SEG_OFF), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COL_POS), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
}

static void build_batt_power_segs(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 24);
    strip_chrome(row);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < BATT_PWR_SEGS; i++) {
        s_pwr_segs[i] = lv_obj_create(row);
        lv_obj_set_height(s_pwr_segs[i], 16);
        lv_obj_set_flex_grow(s_pwr_segs[i], 1);
        lv_obj_set_style_radius(s_pwr_segs[i], 3, 0);
        lv_obj_set_style_border_width(s_pwr_segs[i], 0, 0);
        lv_obj_set_style_outline_width(s_pwr_segs[i], 0, 0);
        lv_obj_set_style_pad_all(s_pwr_segs[i], 0, 0);
        lv_obj_set_style_bg_color(s_pwr_segs[i], lv_color_hex(COL_SEG_OFF), 0);
        lv_obj_clear_flag(s_pwr_segs[i], LV_OBJ_FLAG_SCROLLABLE);
    }
}

static void update_batt_power_segs(const telemetry_snapshot_t *snap)
{
    int lit = 0;
    uint32_t col = COL_POS;
    if (telemetry_is_fresh(snap, METRIC_BATT_P)) {
        float w = snap->m[METRIC_BATT_P].value;
        float mag = w < 0.f ? -w : w;
        if (mag > 0.5f) {
            lit = (int)ceilf((mag / BATT_PWR_MAX_W) * (float)BATT_PWR_SEGS);
            if (lit < 1) {
                lit = 1;
            }
            if (lit > BATT_PWR_SEGS) {
                lit = BATT_PWR_SEGS;
            }
            /* Negative W = charging (green); positive W = discharging (red). */
            col = (w < 0.f) ? COL_POS : COL_DANGER;
        }
    }
    for (int i = 0; i < BATT_PWR_SEGS; i++) {
        if (s_pwr_segs[i]) {
            lv_obj_set_style_bg_color(s_pwr_segs[i], lv_color_hex(i < lit ? col : COL_SEG_OFF), 0);
        }
    }
}

static lv_obj_t *make_hero_card(lv_obj_t *parent, const char *title)
{
    lv_obj_t *card = make_card(parent, title, &TITLE_LG);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return card;
}

static void style_tabview(lv_obj_t *tv)
{
    lv_obj_set_style_bg_color(tv, lv_color_hex(COL_BG), 0);

    lv_obj_t *bar = lv_tabview_get_tab_bar(tv);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COL_BAR), 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_text_font(bar, &TITLE_LG, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COL_BAR), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COL_CARD), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(bar, lv_color_hex(COL_MUTED), LV_PART_ITEMS);
    lv_obj_set_style_text_color(bar, lv_color_hex(COL_TEXT), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(bar, 3, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(bar, lv_color_hex(COL_POS), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_radius(bar, 0, LV_PART_ITEMS);
}

static void build_overview(lv_obj_t *tab)
{
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(tab, 16, 0);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *soc = make_card(tab, "Battery SOC", &TITLE_LG);
    lv_obj_set_size(soc, 460, LV_PCT(100));
    lv_obj_set_flex_align(soc, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(soc, 14, 0);
    lv_obj_set_style_pad_row(soc, 12, 0);

    make_soc_arc(soc, 310, 20, &s_simple_soc_arc, &HERO_FONT);
    build_batt_power_segs(soc);
    add_metric(soc, METRIC_BATT_P, true, &HERO_FONT);

    lv_obj_t *right = lv_obj_create(tab);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, LV_PCT(100));
    strip_chrome(right);
    lv_obj_set_style_pad_row(right, 14, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *pv = make_hero_card(right, "PV total");
    lv_obj_set_style_pad_all(pv, 14, 0);
    s_simple_pv = make_label(pv, &HERO_FONT, COL_PV);
    lv_obj_set_width(s_simple_pv, LV_PCT(100));
    lv_obj_set_style_text_align(s_simple_pv, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *grid = make_hero_card(right, "Grid");
    lv_obj_set_style_pad_all(grid, 14, 0);
    lv_obj_t *grid_v = make_label(grid, &HERO_FONT, COL_TEXT);
    lv_obj_set_width(grid_v, LV_PCT(100));
    lv_obj_set_style_text_align(grid_v, LV_TEXT_ALIGN_CENTER, 0);
    bind(grid_v, METRIC_GRID_P_CT, true);

    lv_obj_t *load = make_hero_card(right, "Load");
    lv_obj_set_style_pad_all(load, 14, 0);
    lv_obj_t *load_v = make_label(load, &HERO_FONT, COL_ACCENT);
    lv_obj_set_width(load_v, LV_PCT(100));
    lv_obj_set_style_text_align(load_v, LV_TEXT_ALIGN_CENTER, 0);
    bind(load_v, METRIC_LOAD_P, true);
}

/* Detail-tab metric card that grows to fill available row/column space. */
static lv_obj_t *make_detail_card(lv_obj_t *parent, const char *title)
{
    lv_obj_t *c = make_card(parent, title, &TITLE_LG);
    lv_obj_set_flex_grow(c, 1);
    lv_obj_set_width(c, 0);
    lv_obj_set_height(c, LV_PCT(100));
    lv_obj_set_style_pad_all(c, 14, 0);
    lv_obj_set_style_pad_row(c, 10, 0);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return c;
}

static lv_obj_t *make_detail_row(lv_obj_t *tab)
{
    lv_obj_t *row = lv_obj_create(tab);
    strip_chrome(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_grow(row, 1);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 14, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static void style_detail_tab(lv_obj_t *tab)
{
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab, 14, 0);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
}

static void build_battery(lv_obj_t *tab)
{
    style_detail_tab(tab);

    lv_obj_t *row1 = make_detail_row(tab);
    const char *t1[] = {"Voltage", "Current", "Power"};
    metric_id_t i1[] = {METRIC_BATT_V, METRIC_BATT_I, METRIC_BATT_P};
    bool s1[] = {false, false, true};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = make_detail_card(row1, t1[i]);
        add_metric(c, i1[i], s1[i], &HERO_FONT);
    }

    lv_obj_t *row2 = make_detail_row(tab);
    lv_obj_t *soc = make_detail_card(row2, "SOC");
    add_metric(soc, METRIC_BATT_SOC, false, &HERO_FONT);
    s_soc_bar_batt = lv_bar_create(soc);
    style_soc_bar(s_soc_bar_batt, 18);

    lv_obj_t *temp = make_detail_card(row2, "Temp");
    add_metric(temp, METRIC_BATT_T, false, &HERO_FONT);

    lv_obj_t *today = make_detail_card(row2, "Charge / Disch.");
    add_metric(today, METRIC_BATT_CHG_TODAY, false, &VALUE_LG);
    add_metric(today, METRIC_BATT_DIS_TODAY, false, &VALUE_LG);
}

static void add_string_card(lv_obj_t *parent, const char *title, metric_id_t v, metric_id_t i, metric_id_t p)
{
    lv_obj_t *c = make_detail_card(parent, title);
    add_metric(c, v, false, &HERO_FONT);
    add_metric(c, i, false, &HERO_FONT);
    add_metric(c, p, true, &HERO_FONT);
}

static void build_pv(lv_obj_t *tab)
{
    style_detail_tab(tab);

    lv_obj_t *row = make_detail_row(tab);
    add_string_card(row, "PV1 V / A / W", METRIC_PV1_V, METRIC_PV1_I, METRIC_PV1_P);
    add_string_card(row, "PV2 V / A / W", METRIC_PV2_V, METRIC_PV2_I, METRIC_PV2_P);

    lv_obj_t *c = lv_obj_create(tab);
    lv_obj_set_size(c, LV_PCT(100), 96);
    lv_obj_set_style_bg_color(c, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_outline_width(c, 0, 0);
    lv_obj_set_style_shadow_width(c, 0, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_pad_hor(c, 20, 0);
    lv_obj_set_style_pad_ver(c, 14, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = make_label(c, &TITLE_LG, COL_MUTED);
    lv_label_set_text(t, "PV energy today");
    lv_obj_t *v = make_label(c, &HERO_FONT, COL_TEXT);
    bind(v, METRIC_PV_ENERGY_TODAY, false);
}

static void build_grid(lv_obj_t *tab)
{
    style_detail_tab(tab);
    const char *titles[] = {"Voltage", "Frequency", "Current", "CT power", "Buy today", "Sell today"};
    metric_id_t ids[] = {METRIC_GRID_V, METRIC_GRID_HZ, METRIC_GRID_I, METRIC_GRID_P_CT, METRIC_GRID_BUY_TODAY,
                         METRIC_GRID_SELL_TODAY};
    bool signed_w[] = {false, false, false, true, false, false};

    lv_obj_t *row1 = make_detail_row(tab);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = make_detail_card(row1, titles[i]);
        add_metric(c, ids[i], signed_w[i], &HERO_FONT);
    }
    lv_obj_t *row2 = make_detail_row(tab);
    for (int i = 3; i < 6; i++) {
        lv_obj_t *c = make_detail_card(row2, titles[i]);
        add_metric(c, ids[i], signed_w[i], &HERO_FONT);
    }
}

static void build_load(lv_obj_t *tab)
{
    style_detail_tab(tab);
    const char *titles[] = {"Inv temp", "Inv power", "Inv Hz", "Load power", "Load today"};
    metric_id_t ids[] = {METRIC_INV_T, METRIC_INV_P, METRIC_INV_HZ, METRIC_LOAD_P, METRIC_LOAD_ENERGY_TODAY};
    bool signed_w[] = {false, true, false, true, false};

    lv_obj_t *row1 = make_detail_row(tab);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = make_detail_card(row1, titles[i]);
        add_metric(c, ids[i], signed_w[i], &HERO_FONT);
    }
    lv_obj_t *row2 = make_detail_row(tab);
    for (int i = 3; i < 5; i++) {
        lv_obj_t *c = make_detail_card(row2, titles[i]);
        add_metric(c, ids[i], signed_w[i], &HERO_FONT);
    }
}

static void build_simple(lv_obj_t *root)
{
    strip_chrome(root);
    lv_obj_set_style_bg_color(root, lv_color_hex(COL_BG), 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tv = lv_tabview_create(root);
    lv_obj_set_size(tv, LV_PCT(100), LV_PCT(100));
    lv_tabview_set_tab_bar_position(tv, LV_DIR_BOTTOM);
    lv_tabview_set_tab_bar_size(tv, 56);
    style_tabview(tv);

    lv_obj_t *home = lv_tabview_add_tab(tv, "Home");
    lv_obj_t *batt = lv_tabview_add_tab(tv, "Batt");
    lv_obj_t *pv = lv_tabview_add_tab(tv, "PV");
    lv_obj_t *grid = lv_tabview_add_tab(tv, "Grid");
    lv_obj_t *load = lv_tabview_add_tab(tv, "Load");

    lv_obj_t *tabs[] = {home, batt, pv, grid, load};
    for (size_t i = 0; i < 5; i++) {
        lv_obj_set_style_bg_color(tabs[i], lv_color_hex(COL_BG), 0);
        lv_obj_set_style_pad_all(tabs[i], 14, 0);
    }

    build_overview(home);
    build_battery(batt);
    build_pv(pv);
    build_grid(grid);
    build_load(load);
}

/* Full-mode KPI: value on top, optional subtext, label on shared bottom baseline. */
static lv_obj_t *make_kpi(lv_obj_t *parent, const char *title, uint32_t color, metric_id_t id, bool signed_w, bool is_pv,
                          lv_obj_t **pv_out, lv_obj_t **sub_out)
{
    lv_obj_t *card = make_card_shell(parent);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_height(card, LV_PCT(100));
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_row(card, 2, 0);

    lv_obj_t *body = lv_obj_create(card);
    strip_chrome(body);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *v = make_label(body, &HERO_FONT, color);
    lv_obj_set_width(v, LV_PCT(100));
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
    if (is_pv) {
        *pv_out = v;
    } else {
        bind(v, id, signed_w);
    }

    lv_obj_t *sub = make_label(card, &SB_FONT, COL_MUTED);
    lv_obj_set_width(sub, LV_PCT(100));
    lv_obj_set_height(sub, KPI_SUB_H);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(sub, " ");
    if (sub_out) {
        *sub_out = sub;
    }

    lv_obj_t *t = make_label(card, &TITLE_FONT, COL_MUTED);
    lv_obj_set_width(t, LV_PCT(100));
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(t, title);
    return card;
}

static lv_obj_t *make_today_tile(lv_obj_t *parent, const char *title, metric_id_t id, uint32_t accent)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_set_flex_grow(tile, 1);
    lv_obj_set_height(tile, 86);
    lv_obj_set_style_bg_color(tile, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_radius(tile, 10, 0);
    lv_obj_set_style_pad_all(tile, 10, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = make_label(tile, &TITLE_FONT, COL_MUTED);
    lv_label_set_text(t, title);
    lv_obj_t *v = make_label(tile, &VALUE_LG, accent);
    bind(v, id, false);
    return tile;
}

static void build_full(lv_obj_t *root)
{
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, 12, 0);
    lv_obj_set_style_pad_row(root, 10, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(COL_BG), 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *kpi = lv_obj_create(root);
    strip_chrome(kpi);
    lv_obj_set_size(kpi, LV_PCT(100), 200);
    lv_obj_set_flex_flow(kpi, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(kpi, 10, 0);
    lv_obj_clear_flag(kpi, LV_OBJ_FLAG_SCROLLABLE);

    make_kpi(kpi, "PV total", COL_PV, METRIC_PV1_P, false, true, &s_full_pv, NULL);
    make_kpi(kpi, "Load", COL_ACCENT, METRIC_LOAD_P, true, false, NULL, NULL);
    make_kpi(kpi, "Grid CT", COL_TEXT, METRIC_GRID_P_CT, true, false, NULL, &s_full_grid_dir);
    make_kpi(kpi, "Battery", COL_TEXT, METRIC_BATT_P, true, false, NULL, &s_full_batt_dir);

    /* SoC: larger arc on top, spacer for subtext height, label on same baseline. */
    lv_obj_t *soc = make_card_shell(kpi);
    lv_obj_set_flex_grow(soc, 1);
    lv_obj_set_height(soc, LV_PCT(100));
    lv_obj_set_flex_align(soc, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(soc, 10, 0);
    lv_obj_set_style_pad_row(soc, 2, 0);

    lv_obj_t *soc_body = lv_obj_create(soc);
    strip_chrome(soc_body);
    lv_obj_set_width(soc_body, LV_PCT(100));
    lv_obj_set_flex_grow(soc_body, 1);
    lv_obj_set_flex_flow(soc_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(soc_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(soc_body, LV_OBJ_FLAG_SCROLLABLE);
    make_soc_arc(soc_body, 118, 12, &s_full_soc_arc, &VALUE_LG);

    lv_obj_t *soc_sub = make_label(soc, &SB_FONT, COL_MUTED);
    lv_obj_set_width(soc_sub, LV_PCT(100));
    lv_obj_set_height(soc_sub, KPI_SUB_H);
    lv_obj_set_style_text_align(soc_sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(soc_sub, " ");

    lv_obj_t *soc_title = make_label(soc, &TITLE_FONT, COL_MUTED);
    lv_obj_set_width(soc_title, LV_PCT(100));
    lv_obj_set_style_text_align(soc_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(soc_title, "SOC");

    lv_obj_t *mid = lv_obj_create(root);
    strip_chrome(mid);
    lv_obj_set_width(mid, LV_PCT(100));
    lv_obj_set_flex_grow(mid, 1);
    lv_obj_set_flex_flow(mid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(mid, 10, 0);
    lv_obj_clear_flag(mid, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *today = make_card(mid, "Energy today", &TITLE_LG);
    lv_obj_set_width(today, LV_PCT(100));
    lv_obj_set_flex_grow(today, 1);
    lv_obj_set_style_pad_row(today, 8, 0);

    lv_obj_t *row1 = lv_obj_create(today);
    strip_chrome(row1);
    lv_obj_set_width(row1, LV_PCT(100));
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row1, 8, 0);
    lv_obj_clear_flag(row1, LV_OBJ_FLAG_SCROLLABLE);
    make_today_tile(row1, "PV", METRIC_PV_ENERGY_TODAY, COL_PV);
    make_today_tile(row1, "Load", METRIC_LOAD_ENERGY_TODAY, COL_ACCENT);
    make_today_tile(row1, "Charge", METRIC_BATT_CHG_TODAY, COL_POS);

    lv_obj_t *row2 = lv_obj_create(today);
    strip_chrome(row2);
    lv_obj_set_width(row2, LV_PCT(100));
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row2, 8, 0);
    lv_obj_clear_flag(row2, LV_OBJ_FLAG_SCROLLABLE);
    make_today_tile(row2, "Discharge", METRIC_BATT_DIS_TODAY, COL_DANGER);
    make_today_tile(row2, "Buy", METRIC_GRID_BUY_TODAY, COL_NEG);
    make_today_tile(row2, "Sell", METRIC_GRID_SELL_TODAY, COL_POS);

    lv_obj_t *share = make_card(mid, "PV string split", &TITLE_FONT);
    lv_obj_set_width(share, LV_PCT(100));
    lv_obj_set_height(share, 96);
    lv_obj_set_style_pad_row(share, 8, 0);

    lv_obj_t *bar_bg = lv_obj_create(share);
    lv_obj_set_size(bar_bg, LV_PCT(100), 18);
    lv_obj_set_style_bg_color(bar_bg, lv_color_hex(COL_SEG_OFF), 0);
    lv_obj_set_style_radius(bar_bg, 8, 0);
    lv_obj_set_style_border_width(bar_bg, 0, 0);
    lv_obj_set_style_pad_all(bar_bg, 0, 0);
    lv_obj_clear_flag(bar_bg, LV_OBJ_FLAG_SCROLLABLE);

    s_pv1_share = lv_obj_create(bar_bg);
    lv_obj_set_size(s_pv1_share, LV_PCT(50), LV_PCT(100));
    lv_obj_set_style_bg_color(s_pv1_share, lv_color_hex(COL_PV), 0);
    lv_obj_set_style_radius(s_pv1_share, 8, 0);
    lv_obj_set_style_border_width(s_pv1_share, 0, 0);
    lv_obj_align(s_pv1_share, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_clear_flag(s_pv1_share, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *legend = lv_obj_create(share);
    strip_chrome(legend);
    lv_obj_set_width(legend, LV_PCT(100));
    lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(legend, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(legend, LV_OBJ_FLAG_SCROLLABLE);
    s_pv1_lbl = make_label(legend, &VALUE_FONT, COL_TEXT);
    lv_label_set_text(s_pv1_lbl, "PV1 —");
    s_pv2_lbl = make_label(legend, &VALUE_FONT, COL_TEXT);
    lv_label_set_text(s_pv2_lbl, "PV2 —");
}

static uint32_t signed_color(const telemetry_snapshot_t *snap, metric_id_t id)
{
    if (!telemetry_is_fresh(snap, id)) {
        return COL_MUTED;
    }
    float v = snap->m[id].value;
    if (id == METRIC_BATT_P) {
        if (v < -0.5f) {
            return COL_POS;
        }
        if (v > 0.5f) {
            return COL_DANGER;
        }
        return COL_TEXT;
    }
    if (id == METRIC_GRID_P_CT) {
        if (v > 0.5f) {
            return COL_NEG;
        }
        if (v < -0.5f) {
            return COL_POS;
        }
        return COL_TEXT;
    }
    if (v > 0.5f) {
        return COL_POS;
    }
    if (v < -0.5f) {
        return COL_NEG;
    }
    return COL_TEXT;
}

static uint32_t soc_color(int soc)
{
    if (soc < 20) {
        return COL_DANGER;
    }
    if (soc < 40) {
        return COL_NEG;
    }
    return COL_POS;
}

static void refresh_clock(void)
{
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    if (tm_info.tm_year < (2024 - 1900)) {
        lv_label_set_text(s_clock, "--, --- --, ---- --:--:--");
        return;
    }
    char buf[40];
    /* e.g. Wed, Sep 09, 2026 16:51:13 — English abbrev via C locale. */
    strftime(buf, sizeof(buf), "%a, %b %d, %Y %H:%M:%S", &tm_info);
    lv_label_set_text(s_clock, buf);
}

static void update_pv_label(lv_obj_t *label, const telemetry_snapshot_t *snap)
{
    if (!label) {
        return;
    }
    char pvbuf[24];
    telemetry_format_pv_total(snap, pvbuf, sizeof(pvbuf));
    lv_label_set_text(label, pvbuf);
    lv_obj_set_style_text_color(label,
                                lv_color_hex(telemetry_is_fresh(snap, METRIC_PV1_P) || telemetry_is_fresh(snap, METRIC_PV2_P)
                                                 ? COL_PV
                                                 : COL_MUTED),
                                0);
}

static void update_soc_arc(lv_obj_t *arc, const telemetry_snapshot_t *snap)
{
    if (!arc || !telemetry_is_fresh(snap, METRIC_BATT_SOC)) {
        return;
    }
    int soc = (int)snap->m[METRIC_BATT_SOC].value;
    if (soc < 0) {
        soc = 0;
    }
    if (soc > 100) {
        soc = 100;
    }
    lv_arc_set_value(arc, soc);
    lv_obj_set_style_arc_color(arc, lv_color_hex(soc_color(soc)), LV_PART_INDICATOR);
}

static void update_dir_labels(const telemetry_snapshot_t *snap)
{
    if (s_full_grid_dir) {
        if (!telemetry_is_fresh(snap, METRIC_GRID_P_CT)) {
            lv_label_set_text(s_full_grid_dir, "—");
            lv_obj_set_style_text_color(s_full_grid_dir, lv_color_hex(COL_MUTED), 0);
        } else {
            float v = snap->m[METRIC_GRID_P_CT].value;
            if (v > 0.5f) {
                lv_label_set_text(s_full_grid_dir, "Import (+)");
            } else if (v < -0.5f) {
                lv_label_set_text(s_full_grid_dir, "Export (-)");
            } else {
                lv_label_set_text(s_full_grid_dir, "Idle");
            }
            lv_obj_set_style_text_color(s_full_grid_dir, lv_color_hex(COL_MUTED), 0);
        }
    }
    if (s_full_batt_dir) {
        if (!telemetry_is_fresh(snap, METRIC_BATT_P)) {
            lv_label_set_text(s_full_batt_dir, "—");
            lv_obj_set_style_text_color(s_full_batt_dir, lv_color_hex(COL_MUTED), 0);
        } else {
            float v = snap->m[METRIC_BATT_P].value;
            if (v < -0.5f) {
                lv_label_set_text(s_full_batt_dir, "Charging");
                lv_obj_set_style_text_color(s_full_batt_dir, lv_color_hex(COL_POS), 0);
            } else if (v > 0.5f) {
                lv_label_set_text(s_full_batt_dir, "Discharging");
                lv_obj_set_style_text_color(s_full_batt_dir, lv_color_hex(COL_DANGER), 0);
            } else {
                lv_label_set_text(s_full_batt_dir, "Idle");
                lv_obj_set_style_text_color(s_full_batt_dir, lv_color_hex(COL_MUTED), 0);
            }
        }
    }
}

static void update_pv_split(const telemetry_snapshot_t *snap)
{
    char b1[32];
    char b2[32];
    float p1 = 0.f;
    float p2 = 0.f;
    bool f1 = telemetry_is_fresh(snap, METRIC_PV1_P);
    bool f2 = telemetry_is_fresh(snap, METRIC_PV2_P);
    if (f1) {
        p1 = snap->m[METRIC_PV1_P].value;
        if (p1 < 0.f) {
            p1 = 0.f;
        }
    }
    if (f2) {
        p2 = snap->m[METRIC_PV2_P].value;
        if (p2 < 0.f) {
            p2 = 0.f;
        }
    }
    if (f1) {
        snprintf(b1, sizeof(b1), "PV1 %.0f W", p1);
    } else {
        snprintf(b1, sizeof(b1), "PV1 —");
    }
    if (f2) {
        snprintf(b2, sizeof(b2), "PV2 %.0f W", p2);
    } else {
        snprintf(b2, sizeof(b2), "PV2 —");
    }
    if (s_pv1_lbl) {
        lv_label_set_text(s_pv1_lbl, b1);
    }
    if (s_pv2_lbl) {
        lv_label_set_text(s_pv2_lbl, b2);
    }
    if (s_pv1_share) {
        float sum = p1 + p2;
        int pct = (sum > 0.5f) ? (int)((p1 / sum) * 100.f + 0.5f) : 0;
        if (pct < 0) {
            pct = 0;
        }
        if (pct > 100) {
            pct = 100;
        }
        lv_obj_set_width(s_pv1_share, LV_PCT(pct));
    }
}

static void ui_refresh_cb(lv_timer_t *timer)
{
    (void)timer;
    telemetry_snapshot_t snap;
    telemetry_get_snapshot(&snap);

    lv_label_set_text(s_wifi, snap.wifi_connected ? LV_SYMBOL_WIFI " OK"
                      : (s_provisioning ? LV_SYMBOL_WIFI " AP" : LV_SYMBOL_WIFI " --"));
    lv_obj_set_style_text_color(s_wifi, lv_color_hex(snap.wifi_connected ? COL_POS
                                                     : (s_provisioning ? COL_ACCENT : COL_MUTED)), 0);
    lv_label_set_text(s_mqtt, snap.mqtt_connected ? LV_SYMBOL_ENVELOPE " OK" : LV_SYMBOL_ENVELOPE " --");
    lv_obj_set_style_text_color(s_mqtt, lv_color_hex(snap.mqtt_connected ? COL_POS : COL_DANGER), 0);

    bool st_fresh = false;
    const char *st = telemetry_status_text(&snap, &st_fresh);
    set_sb_item(s_status, status_symbol(&snap), st);
    lv_obj_set_style_text_color(s_status, lv_color_hex(telemetry_status_color(&snap)), 0);

    const char *grid_txt = "--";
    uint32_t grid_col = COL_MUTED;
    grid_mode_text(&snap, &grid_txt, &grid_col);
    set_sb_item(s_grid_mode, LV_SYMBOL_CHARGE, grid_txt);
    lv_obj_set_style_text_color(s_grid_mode, lv_color_hex(grid_col), 0);

    update_pv_label(s_simple_pv, &snap);
    update_pv_label(s_full_pv, &snap);
    update_soc_arc(s_simple_soc_arc, &snap);
    update_soc_arc(s_full_soc_arc, &snap);
    if (telemetry_is_fresh(&snap, METRIC_BATT_SOC) && s_soc_bar_batt) {
        int soc = (int)snap.m[METRIC_BATT_SOC].value;
        if (soc < 0) {
            soc = 0;
        }
        if (soc > 100) {
            soc = 100;
        }
        lv_bar_set_value(s_soc_bar_batt, soc, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_soc_bar_batt, lv_color_hex(soc_color(soc)), LV_PART_INDICATOR);
    }
    update_batt_power_segs(&snap);
    update_dir_labels(&snap);
    update_pv_split(&snap);
    ui_ha_update(&snap);

    char buf[32];
    for (size_t i = 0; i < s_bound_n; i++) {
        if (s_bound[i].signed_w) {
            telemetry_format_signed_w(&snap, s_bound[i].id, buf, sizeof(buf));
            lv_obj_set_style_text_color(s_bound[i].label, lv_color_hex(signed_color(&snap, s_bound[i].id)), 0);
        } else {
            telemetry_format(&snap, s_bound[i].id, buf, sizeof(buf));
            uint32_t col = telemetry_is_fresh(&snap, s_bound[i].id) ? COL_TEXT : COL_MUTED;
            if (s_bound[i].id == METRIC_BATT_SOC && telemetry_is_fresh(&snap, METRIC_BATT_SOC)) {
                col = soc_color((int)snap.m[METRIC_BATT_SOC].value);
            } else if (s_bound[i].id == METRIC_PV_ENERGY_TODAY && telemetry_is_fresh(&snap, METRIC_PV_ENERGY_TODAY)) {
                col = COL_PV;
            } else if (s_bound[i].id == METRIC_LOAD_ENERGY_TODAY && telemetry_is_fresh(&snap, METRIC_LOAD_ENERGY_TODAY)) {
                col = COL_ACCENT;
            } else if (s_bound[i].id == METRIC_BATT_CHG_TODAY && telemetry_is_fresh(&snap, METRIC_BATT_CHG_TODAY)) {
                col = COL_POS;
            } else if (s_bound[i].id == METRIC_BATT_DIS_TODAY && telemetry_is_fresh(&snap, METRIC_BATT_DIS_TODAY)) {
                col = COL_DANGER;
            } else if (s_bound[i].id == METRIC_GRID_BUY_TODAY && telemetry_is_fresh(&snap, METRIC_GRID_BUY_TODAY)) {
                col = COL_NEG;
            } else if (s_bound[i].id == METRIC_GRID_SELL_TODAY && telemetry_is_fresh(&snap, METRIC_GRID_SELL_TODAY)) {
                col = COL_POS;
            }
            lv_obj_set_style_text_color(s_bound[i].label, lv_color_hex(col), 0);
        }
        lv_label_set_text(s_bound[i].label, buf);
    }
    refresh_clock();
}

void ui_init(void)
{
    lv_obj_t *scr = lv_screen_active();
    style_root(scr);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_pad_row(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    build_status_bar(scr);

    lv_obj_t *content = lv_obj_create(scr);
    strip_chrome(content);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_color(content, lv_color_hex(COL_BG), 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    s_view_simple = lv_obj_create(content);
    lv_obj_set_size(s_view_simple, LV_PCT(100), LV_PCT(100));
    lv_obj_align(s_view_simple, LV_ALIGN_TOP_LEFT, 0, 0);
    strip_chrome(s_view_simple);
    build_simple(s_view_simple);

    s_view_full = lv_obj_create(content);
    lv_obj_set_size(s_view_full, LV_PCT(100), LV_PCT(100));
    lv_obj_align(s_view_full, LV_ALIGN_TOP_LEFT, 0, 0);
    strip_chrome(s_view_full);
    build_full(s_view_full);

    s_view_ha = lv_obj_create(content);
    lv_obj_set_size(s_view_ha, LV_PCT(100), LV_PCT(100));
    lv_obj_align(s_view_ha, LV_ALIGN_TOP_LEFT, 0, 0);
    strip_chrome(s_view_ha);
    ui_ha_build(s_view_ha);

    apply_mode(UI_MODE_SIMPLE);
    lv_timer_create(ui_refresh_cb, 200, NULL);
}

void ui_set_setup_request_cb(void (*cb)(void))
{
    s_setup_request_cb = cb;
}

bool ui_is_provisioning(void)
{
    return s_provisioning;
}

void ui_show_provisioning(const char *ap_ssid, const char *portal_url)
{
    s_provisioning = true;
    lv_obj_t *scr = lv_screen_active();
    if (s_prov_overlay) {
        lv_obj_delete(s_prov_overlay);
        s_prov_overlay = NULL;
    }

    s_prov_overlay = lv_obj_create(scr);
    lv_obj_set_size(s_prov_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_prov_overlay, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s_prov_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_prov_overlay, 0, 0);
    lv_obj_set_style_radius(s_prov_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_prov_overlay, 28, 0);
    lv_obj_set_flex_flow(s_prov_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_prov_overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_prov_overlay, 14, 0);
    lv_obj_clear_flag(s_prov_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(s_prov_overlay);

    /* ASCII-only: Montserrat subset lacks Unicode hyphens/bullets (tofu boxes). */
    lv_obj_t *title = make_label(s_prov_overlay, &HERO_FONT, COL_TEXT);
    lv_label_set_text(title, "WiFi Setup");
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *hint = make_label(s_prov_overlay, &TITLE_LG, COL_MUTED);
    lv_label_set_text(hint, "Connect your phone to this AP,");
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);

    char line[96];
    snprintf(line, sizeof(line), "then open %s", portal_url ? portal_url : "http://192.168.4.1");
    lv_obj_t *url = make_label(s_prov_overlay, &VALUE_FONT, COL_ACCENT);
    lv_label_set_text(url, line);
    lv_obj_set_style_text_align(url, LV_TEXT_ALIGN_CENTER, 0);

    snprintf(line, sizeof(line), "AP: %s", ap_ssid ? ap_ssid : "Deye-P4-Setup");
    lv_obj_t *ap = make_label(s_prov_overlay, &VALUE_LG, COL_POS);
    lv_label_set_text(ap, line);
    lv_obj_set_style_text_align(ap, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *fields = make_label(s_prov_overlay, &TITLE_FONT, COL_MUTED);
    lv_label_set_text(fields, "Form: WiFi SSID/pass | MQTT host/port/user/pass | topic");
    lv_obj_set_style_text_align(fields, LV_TEXT_ALIGN_CENTER, 0);

    if (s_wifi) {
        lv_label_set_text(s_wifi, LV_SYMBOL_WIFI " AP");
        lv_obj_set_style_text_color(s_wifi, lv_color_hex(COL_ACCENT), 0);
    }
}
