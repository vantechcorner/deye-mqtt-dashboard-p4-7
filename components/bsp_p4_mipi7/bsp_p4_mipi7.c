#include "bsp_p4_mipi7.h"

#include <sys/param.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9881c.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/lcd_types.h"
#include "rpi_7inch_init_cmds.h"
#include "sdkconfig.h"

static const char *TAG = "bsp_td2";

#define I2C_SCL_GPIO                 8
#define I2C_SDA_GPIO                 7
#define MIPI_DSI_PHY_LDO_CHAN        3
#define MIPI_DSI_PHY_LDO_MV          2500
#define MIPI_DSI_LANE_NUM            2
#define MIPI_DSI_LANE_BITRATE_MBPS   1000

/* rpi_7inch_default_mode */
#define LCD_DPI_MHZ                  83
#define LCD_HFP                      239
#define LCD_HSYNC                    33
#define LCD_HBP                      50
#define LCD_VFP                      20
#define LCD_VSYNC                    2
#define LCD_VBP                      30

#define TD2_MCU_ADDR                 0x45
#define TD2_REG_ID                   0x01
#define TD2_REG_POWERON              0x02
#define TD2_REG_PWM                  0x03
#define TD2_LCD_RESET_BIT            (1u << 0)
#define TD2_CTP_RESET_BIT            (1u << 1)
#define TD2_PWM_BL_ENABLE            (1u << 7)
#define TD2_PWM_MAX                  0x1F

/* Landscape stand: panel stays native 720×1280; LVGL SW-rotates to 1280×720. */
#define BSP_LVGL_ROTATION            LV_DISPLAY_ROTATION_90
/* Partial strips sized for logical landscape width (after rotation). */
#define LVGL_DRAW_BUF_LINES          (BSP_P4_MIPI7_UI_V_RES / 8)
#define LVGL_TICK_PERIOD_MS          2
#define LVGL_TASK_MIN_DELAY_MS       2
#define LVGL_TASK_MAX_DELAY_MS       500
#define LVGL_TASK_STACK_SIZE         (10 * 1024)
#define LVGL_TASK_PRIORITY           5

static SemaphoreHandle_t s_lvgl_mux;
static lv_display_t *s_disp;
static esp_lcd_panel_handle_t s_panel;
static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_mcu;
static uint8_t *s_rotate_buf;
static bool s_ready;

/** Map a logical (rotated) dirty area to physical panel coordinates. */
static void rotate_area_to_panel(lv_display_t *disp, lv_area_t *area)
{
    lv_display_rotation_t rotation = lv_display_get_rotation(disp);
    int32_t w = lv_area_get_width(area);
    int32_t h = lv_area_get_height(area);
    int32_t hres = lv_display_get_horizontal_resolution(disp);
    int32_t vres = lv_display_get_vertical_resolution(disp);
    if (rotation == LV_DISPLAY_ROTATION_90 || rotation == LV_DISPLAY_ROTATION_270) {
        vres = lv_display_get_horizontal_resolution(disp);
        hres = lv_display_get_vertical_resolution(disp);
    }

    switch (rotation) {
    case LV_DISPLAY_ROTATION_0:
        return;
    case LV_DISPLAY_ROTATION_90:
        area->y2 = vres - area->x1 - 1;
        area->x1 = area->y1;
        area->x2 = area->x1 + h - 1;
        area->y1 = area->y2 - w + 1;
        break;
    case LV_DISPLAY_ROTATION_180:
        area->y2 = vres - area->y1 - 1;
        area->y1 = area->y2 - h + 1;
        area->x2 = hres - area->x1 - 1;
        area->x1 = area->x2 - w + 1;
        break;
    case LV_DISPLAY_ROTATION_270:
        area->x1 = hres - area->y2 - 1;
        area->y2 = area->x2;
        area->x2 = area->x1 + h - 1;
        area->y1 = area->y2 - w + 1;
        break;
    default:
        break;
    }
}

static esp_err_t enable_dsi_phy_power(void)
{
    static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = MIPI_DSI_PHY_LDO_CHAN,
        .voltage_mv = MIPI_DSI_PHY_LDO_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan), TAG, "LDO DPHY");
    ESP_LOGI(TAG, "MIPI DSI PHY LDO%d @ %dmV", MIPI_DSI_PHY_LDO_CHAN, MIPI_DSI_PHY_LDO_MV);
    return ESP_OK;
}

static esp_err_t i2c_write_u8(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev, buf, sizeof(buf), 100);
}

static esp_err_t i2c_read_u8(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *val)
{
    ESP_RETURN_ON_ERROR(i2c_master_transmit(dev, &reg, 1, 100), TAG, "reg addr");
    vTaskDelay(pdMS_TO_TICKS(8));
    return i2c_master_receive(dev, val, 1, 100);
}

static esp_err_t td2_mcu_panel_on(void)
{
    i2c_device_config_t cfg = {
        .device_address = TD2_MCU_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &cfg, &s_mcu), TAG, "add MCU");

    uint8_t id = 0;
    esp_err_t err = i2c_read_u8(s_mcu, TD2_REG_ID, &id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MCU ID read failed: %s — check 5V J1 + I2C", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "TD2 MCU ID=0x%02X", id);

    ESP_RETURN_ON_ERROR(i2c_write_u8(s_mcu, TD2_REG_PWM, 0), TAG, "PWM off");
    ESP_RETURN_ON_ERROR(i2c_write_u8(s_mcu, TD2_REG_POWERON, 0), TAG, "POWERON 0");
    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_RETURN_ON_ERROR(i2c_write_u8(s_mcu, TD2_REG_POWERON, TD2_CTP_RESET_BIT), TAG, "rail on");
    vTaskDelay(pdMS_TO_TICKS(60));

    ESP_RETURN_ON_ERROR(i2c_write_u8(s_mcu, TD2_REG_POWERON, TD2_CTP_RESET_BIT | TD2_LCD_RESET_BIT),
                        TAG, "reset release");
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

void bsp_p4_mipi7_set_backlight(uint8_t percent)
{
    if (!s_mcu) {
        return;
    }
    if (percent > 100) {
        percent = 100;
    }
    if (percent < 5) {
        percent = 5;
    }
    uint8_t duty = (uint8_t)((percent * TD2_PWM_MAX) / 100);
    if (duty == 0) {
        duty = 1;
    }
    i2c_write_u8(s_mcu, TD2_REG_PWM, (uint8_t)(duty | TD2_PWM_BL_ENABLE));
}

static bool notify_lvgl_flush_ready(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata,
                                    void *user_ctx)
{
    (void)panel;
    (void)edata;
    lv_display_flush_ready((lv_display_t *)user_ctx);
    return false;
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = lv_display_get_user_data(disp);
    lv_display_rotation_t rot = lv_display_get_rotation(disp);
    lv_area_t panel_area = *area;
    uint8_t *out = px_map;

    if (rot != LV_DISPLAY_ROTATION_0 && s_rotate_buf) {
        int32_t ww = lv_area_get_width(area);
        int32_t hh = lv_area_get_height(area);
        lv_color_format_t cf = lv_display_get_color_format(disp);
        uint32_t w_stride = lv_draw_buf_width_to_stride(ww, cf);
        uint32_t h_stride = lv_draw_buf_width_to_stride(hh, cf);
        if (rot == LV_DISPLAY_ROTATION_180) {
            lv_draw_sw_rotate(px_map, s_rotate_buf, hh, ww, h_stride, h_stride, LV_DISPLAY_ROTATION_180, cf);
        } else if (rot == LV_DISPLAY_ROTATION_90) {
            lv_draw_sw_rotate(px_map, s_rotate_buf, ww, hh, w_stride, h_stride, LV_DISPLAY_ROTATION_90, cf);
        } else if (rot == LV_DISPLAY_ROTATION_270) {
            lv_draw_sw_rotate(px_map, s_rotate_buf, ww, hh, w_stride, h_stride, LV_DISPLAY_ROTATION_270, cf);
        }
        out = s_rotate_buf;
        rotate_area_to_panel(disp, &panel_area);
    }

    esp_lcd_panel_draw_bitmap(panel, panel_area.x1, panel_area.y1, panel_area.x2 + 1, panel_area.y2 + 1, out);
}

static void lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_point_data_t points[1] = {0};
    uint8_t cnt = 0;
    esp_lcd_touch_handle_t tp = lv_indev_get_user_data(indev);
    esp_lcd_touch_read_data(tp);
    if (esp_lcd_touch_get_data(tp, points, &cnt, 1) == ESP_OK && cnt > 0) {
        data->point.x = points[0].x;
        data->point.y = points[0].y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_port_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "LVGL task start");
    while (1) {
        xSemaphoreTake(s_lvgl_mux, portMAX_DELAY);
        uint32_t delay_ms = lv_timer_handler();
        xSemaphoreGive(s_lvgl_mux);
        delay_ms = MAX(delay_ms, LVGL_TASK_MIN_DELAY_MS);
        delay_ms = MIN(delay_ms, LVGL_TASK_MAX_DELAY_MS);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

static esp_err_t init_touch(void)
{
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_config.scl_speed_hz = 400000;
    esp_err_t err = esp_lcd_new_panel_io_i2c(s_i2c, &tp_io_config, &tp_io);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GT911 IO failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_P4_MIPI7_H_RES,
        .y_max = BSP_P4_MIPI7_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    esp_lcd_touch_handle_t tp = NULL;
    err = esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &tp);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GT911 init failed: %s (UI continues without touch)", esp_err_to_name(err));
        return err;
    }

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, s_disp);
    lv_indev_set_user_data(indev, tp);
    lv_indev_set_read_cb(indev, lvgl_touch_cb);
    ESP_LOGI(TAG, "GT911 touch ready");
    return ESP_OK;
}

esp_err_t bsp_p4_mipi7_lock(uint32_t timeout_ms)
{
    if (!s_lvgl_mux) {
        return ESP_ERR_INVALID_STATE;
    }
    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_lvgl_mux, ticks) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void bsp_p4_mipi7_unlock(void)
{
    if (s_lvgl_mux) {
        xSemaphoreGive(s_lvgl_mux);
    }
}

lv_display_t *bsp_p4_mipi7_display(void)
{
    return s_ready ? s_disp : NULL;
}

esp_err_t bsp_p4_mipi7_init(uint8_t backlight_pct)
{
    ESP_LOGI(TAG, "TD2 bring-up native %dx%d -> UI %dx%d (SW rot 90) 2-lane DSI + LVGL",
             BSP_P4_MIPI7_H_RES, BSP_P4_MIPI7_V_RES, BSP_P4_MIPI7_UI_H_RES, BSP_P4_MIPI7_UI_V_RES);

    s_lvgl_mux = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lvgl_mux, ESP_ERR_NO_MEM, TAG, "lvgl mux");

    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_1,
        .scl_io_num = I2C_SCL_GPIO,
        .sda_io_num = I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c), TAG, "i2c");

    ESP_RETURN_ON_ERROR(enable_dsi_phy_power(), TAG, "PHY");
    ESP_RETURN_ON_ERROR(td2_mcu_panel_on(), TAG, "MCU");

    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t dsi_cfg = ILI9881C_PANEL_BUS_DSI_2CH_CONFIG();
    dsi_cfg.lane_bit_rate_mbps = MIPI_DSI_LANE_BITRATE_MBPS;
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&dsi_cfg, &dsi_bus), TAG, "DSI bus");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_dbi_io_config_t dbi_cfg = ILI9881C_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &io), TAG, "DBI");

    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = LCD_DPI_MHZ,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 2,
        .video_timing = {
            .h_size = BSP_P4_MIPI7_H_RES,
            .v_size = BSP_P4_MIPI7_V_RES,
            .hsync_back_porch = LCD_HBP,
            .hsync_pulse_width = LCD_HSYNC,
            .hsync_front_porch = LCD_HFP,
            .vsync_back_porch = LCD_VBP,
            .vsync_pulse_width = LCD_VSYNC,
            .vsync_front_porch = LCD_VFP,
        },
        .flags.use_dma2d = true,
    };

    ili9881c_vendor_config_t vendor = {
        .init_cmds = rpi_7inch_init_cmds,
        .init_cmds_size = RPI_7INCH_INIT_CMDS_SIZE,
        .mipi_config = {
            .dsi_bus = dsi_bus,
            .dpi_config = &dpi_cfg,
            .lane_num = MIPI_DSI_LANE_NUM,
        },
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9881c(io, &panel_cfg, &s_panel), TAG, "ili9881c");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");

    bsp_p4_mipi7_set_backlight(backlight_pct);
    ESP_LOGI(TAG, "Panel init OK, backlight %u%%", backlight_pct);

    lv_init();
    /* Create with native panel size; SW rotation makes logical UI 1280×720. */
    s_disp = lv_display_create(BSP_P4_MIPI7_H_RES, BSP_P4_MIPI7_V_RES);
    lv_display_set_user_data(s_disp, s_panel);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);

    size_t draw_buffer_sz = BSP_P4_MIPI7_UI_H_RES * LVGL_DRAW_BUF_LINES * sizeof(lv_color16_t);
    void *buf1 = heap_caps_aligned_calloc(64, 1, draw_buffer_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *buf2 = heap_caps_aligned_calloc(64, 1, draw_buffer_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_rotate_buf = heap_caps_aligned_calloc(64, 1, draw_buffer_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(buf1 && buf2 && s_rotate_buf, ESP_ERR_NO_MEM, TAG,
                        "PSRAM draw/rotate buffers (%u bytes each)", (unsigned)draw_buffer_sz);
    lv_display_set_buffers(s_disp, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, lvgl_flush_cb);
    lv_display_set_rotation(s_disp, BSP_LVGL_ROTATION);

    esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cbs, s_disp), TAG, "dpi cbs");

    const esp_timer_create_args_t tick_args = {
        .callback = &lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_args, &tick_timer), TAG, "tick");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000), TAG, "tick start");

    (void)init_touch();

    BaseType_t ok = xTaskCreate(lvgl_port_task, "lvgl", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "lvgl task");

    s_ready = true;
    ESP_LOGI(TAG, "Display ready UI %dx%d (panel %dx%d) RGB565 DMA2D + SW rotate + PSRAM partial",
             BSP_P4_MIPI7_UI_H_RES, BSP_P4_MIPI7_UI_V_RES, BSP_P4_MIPI7_H_RES, BSP_P4_MIPI7_V_RES);
    return ESP_OK;
}
