/*
 * Youyeetoo YYT-MIPI7LCD2203 bring-up on Waveshare ESP32-P4-Nano.
 *
 * Patterned after Waveshare 13_Displaycolorbar (hardware MIPI color bars,
 * no LVGL) but with R1 wiki timings for the Youyeetoo 7" panel.
 *
 * Refs:
 *  - https://github.com/waveshareteam/esp32-p4-platform/tree/main/examples/esp-idf/13_Displaycolorbar
 *  - https://wiki.youyeetoo.com/en/r1/OUHDMI#h-7-inch-mipi-screen
 *  - https://docs.waveshare.com/ESP32-P4-NANO
 */

#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/lcd_types.h"
#include "hal/mipi_dsi_types.h"

static const char *TAG = "mipi7_cb";

/* --- Nano board (same as Waveshare BSP) --- */
#define I2C_SCL_GPIO                 8
#define I2C_SDA_GPIO                 7
#define MIPI_DSI_PHY_LDO_CHAN        3
#define MIPI_DSI_PHY_LDO_MV          2500
#define MIPI_DSI_LANE_NUM            2
#define MIPI_DSI_LANE_BITRATE_MBPS   1000

/* --- Youyeetoo YYT-MIPI7LCD2203 (from R1 DT) --- */
#define LCD_H_RES                    1024
#define LCD_V_RES                    600
#define LCD_DPI_MHZ                  52   /* ~51.67 MHz DCLK */
#define LCD_HSYNC                    10
#define LCD_HBP                      160
#define LCD_HFP                      160
#define LCD_VSYNC                    10
#define LCD_VBP                      23
#define LCD_VFP                      12

#define BL_I2C_ADDR                  0x45
#define GT911_I2C_ADDR_A             0x5D
#define GT911_I2C_ADDR_B             0x14

typedef struct {
    uint8_t type;   /* Rockchip DSI packet type: 0x05 / 0x15 / 0x39 */
    uint8_t delay_ms;
    uint8_t len;
    const uint8_t *data;
} dsi_cmd_t;

/* Wiki snippet (likely truncated) + standard Sleep-Out / Display-On. */
static const uint8_t CMD_80[] = {0x80, 0xAC};
static const uint8_t CMD_81[] = {0x81, 0xB8};
static const uint8_t CMD_82[] = {0x82, 0x09};
static const uint8_t CMD_83[] = {0x83, 0x78};
static const uint8_t CMD_84[] = {0x84, 0x7F};
static const uint8_t CMD_85[] = {0x85, 0xBB};
static const uint8_t CMD_86[] = {0x86, 0x70};
static const uint8_t CMD_11[] = {0x11};
static const uint8_t CMD_29[] = {0x29};

static const dsi_cmd_t s_init_cmds[] = {
    {0x15, 0, 2, CMD_80},
    {0x15, 0, 2, CMD_81},
    {0x15, 0, 2, CMD_82},
    {0x15, 0, 2, CMD_83},
    {0x15, 0, 2, CMD_84},
    {0x15, 0, 2, CMD_85},
    {0x15, 0, 2, CMD_86},
    {0x05, 120, 1, CMD_11}, /* Sleep Out */
    {0x05, 20, 1, CMD_29},  /* Display On */
};

static i2c_master_bus_handle_t s_i2c;

static esp_err_t enable_dsi_phy_power(void)
{
    static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = MIPI_DSI_PHY_LDO_CHAN,
        .voltage_mv = MIPI_DSI_PHY_LDO_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan), TAG, "LDO for DPHY failed");
    ESP_LOGI(TAG, "MIPI DSI PHY LDO%d @ %dmV OK", MIPI_DSI_PHY_LDO_CHAN, MIPI_DSI_PHY_LDO_MV);
    return ESP_OK;
}

static esp_err_t i2c_bus_init(void)
{
    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_1,
        .scl_io_num = I2C_SCL_GPIO,
        .sda_io_num = I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_i2c);
}

static bool i2c_probe(uint8_t addr7)
{
    return i2c_master_probe(s_i2c, addr7, 50) == ESP_OK;
}

static void i2c_scan_report(void)
{
    ESP_LOGI(TAG, "I2C scan on SDA=GPIO%d SCL=GPIO%d (cable / touch / BL check)", I2C_SDA_GPIO, I2C_SCL_GPIO);
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_probe(addr)) {
            ESP_LOGI(TAG, "  found 0x%02X%s%s", addr,
                     addr == GT911_I2C_ADDR_A || addr == GT911_I2C_ADDR_B ? " (GT911?)" : "",
                     addr == BL_I2C_ADDR ? " (BL chip Waveshare-style?)" : "");
            found++;
        }
    }
    if (found == 0) {
        ESP_LOGW(TAG, "  no I2C devices — FPC may not route I2C, or panel power/cable mismatch");
    }
}

static esp_err_t try_backlight_waveshare_style(void)
{
    if (!i2c_probe(BL_I2C_ADDR)) {
        ESP_LOGW(TAG, "No I2C 0x45 backlight chip (normal for some third-party panels)");
        return ESP_ERR_NOT_FOUND;
    }

    /* Try both Waveshare register maps at full brightness. */
    const struct {
        uint8_t reg;
        uint8_t val;
        const char *name;
    } attempts[] = {
        {0x96, 0xFF, "reg 0x96 (DSI-TOUCH-A style)"},
        {0x86, 0xFF, "reg 0x86 (10.1 IPS-CT-K style)"},
        {0xAB, 0x00, "reg 0xAB inverted (C-series style)"},
    };

    i2c_device_config_t dev_cfg = {
        .device_address = BL_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t dev = NULL;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &dev_cfg, &dev), TAG, "add BL device");

    esp_err_t last = ESP_FAIL;
    for (size_t i = 0; i < sizeof(attempts) / sizeof(attempts[0]); i++) {
        uint8_t buf[2] = {attempts[i].reg, attempts[i].val};
        last = i2c_master_transmit(dev, buf, sizeof(buf), 50);
        ESP_LOGI(TAG, "BL write %s -> %s", attempts[i].name, esp_err_to_name(last));
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    i2c_master_bus_rm_device(dev);
    return last;
}

static esp_err_t send_panel_init(esp_lcd_panel_io_handle_t io)
{
    for (size_t i = 0; i < sizeof(s_init_cmds) / sizeof(s_init_cmds[0]); i++) {
        const dsi_cmd_t *c = &s_init_cmds[i];
        esp_err_t err;
        if (c->len == 1) {
            err = esp_lcd_panel_io_tx_param(io, c->data[0], NULL, 0);
        } else {
            err = esp_lcd_panel_io_tx_param(io, c->data[0], &c->data[1], c->len - 1);
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "init cmd 0x%02X failed: %s (continuing)", c->data[0], esp_err_to_name(err));
        }
        if (c->delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(c->delay_ms));
        }
    }
    ESP_LOGI(TAG, "Panel init commands sent (wiki snippet may be incomplete)");
    return ESP_OK;
}

static esp_err_t start_colorbar(void)
{
    ESP_RETURN_ON_ERROR(enable_dsi_phy_power(), TAG, "PHY power");

    esp_lcd_dsi_bus_handle_t bus = NULL;
    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id = 0,
        .num_data_lanes = MIPI_DSI_LANE_NUM,
        .lane_bit_rate_mbps = MIPI_DSI_LANE_BITRATE_MBPS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &bus), TAG, "DSI bus");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(bus, &dbi_cfg, &io), TAG, "DBI IO");

    /* Give panel time after power / cable settle. */
    vTaskDelay(pdMS_TO_TICKS(60));
    send_panel_init(io);

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = LCD_DPI_MHZ,
        .in_color_format = LCD_COLOR_FMT_RGB888,
        .num_fbs = 1,
        .video_timing = {
            .h_size = LCD_H_RES,
            .v_size = LCD_V_RES,
            .hsync_back_porch = LCD_HBP,
            .hsync_pulse_width = LCD_HSYNC,
            .hsync_front_porch = LCD_HFP,
            .vsync_back_porch = LCD_VBP,
            .vsync_pulse_width = LCD_VSYNC,
            .vsync_front_porch = LCD_VFP,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_dpi(bus, &dpi_cfg, &panel), TAG, "DPI panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "panel init");
    /* DPI panels do not implement disp_on_off — skip it. */

    ESP_LOGI(TAG, "Show hardware color bars @ %dx%d / %d-lane / %d MHz DPI",
             LCD_H_RES, LCD_V_RES, MIPI_DSI_LANE_NUM, LCD_DPI_MHZ);

    const mipi_dsi_pattern_type_t patterns[] = {
        MIPI_DSI_PATTERN_BAR_VERTICAL,
        MIPI_DSI_PATTERN_BAR_HORIZONTAL,
        MIPI_DSI_PATTERN_BER_VERTICAL,
    };
    const char *names[] = {"VERTICAL", "HORIZONTAL", "BER_VERTICAL"};

    int idx = 0;
    while (1) {
        ESP_LOGI(TAG, "Pattern: %s", names[idx]);
        ESP_ERROR_CHECK(esp_lcd_dpi_panel_set_pattern(panel, patterns[idx]));
        idx = (idx + 1) % 3;
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== MIPI7 colorbar test (P4-Nano + Youyeetoo 7\") ===");
    ESP_LOGI(TAG, "Nano LCD: 15-pin / 2-lane; Youyeetoo panel: 30-pin / R1 uses 4-lane");
    ESP_LOGI(TAG, "Pi display cable is often incomplete for this pair — see reference/blank-screen-diagnosis.md");

    ESP_ERROR_CHECK(i2c_bus_init());
    i2c_scan_report();
    bool has_gt911 = i2c_probe(GT911_I2C_ADDR_A) || i2c_probe(GT911_I2C_ADDR_B);
    try_backlight_waveshare_style();

    if (has_gt911) {
        ESP_LOGW(TAG, "DIAG: GT911 OK => I2C+3V3 on FPC work; blank screen => DSI/BL/5V/lanes/init mismatch");
    } else {
        ESP_LOGW(TAG, "DIAG: no GT911 => check FPC seating / orientation / panel power first");
    }

    esp_err_t err = start_colorbar();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Colorbar start failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "Check: FPC orientation, 5V/3V3 on cable, lane mapping, panel power");
    }
}
