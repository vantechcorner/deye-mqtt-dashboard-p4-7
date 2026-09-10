/*
 * Experimental: Raspberry Pi Touch Display 2 (7") on Waveshare ESP32-P4-Nano.
 *
 * Critical: TD2 has an MCU @ I2C 0x45 that must:
 *   - release LCD reset / enable panel rails (REG_POWERON)
 *   - enable backlight PWM (REG_PWM)
 * before ILI9881 will answer DSI. Without that, esp_lcd_ili9881c hangs on ID read.
 *
 * Refs:
 *  - drivers/regulator/rpi-panel-v2-regulator.c
 *  - vc4-kms-dsi-ili9881-7inch-overlay.dts
 *  - panel-ilitek-ili9881c.c rpi_7inch_default_mode
 */

#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_ili9881c.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/lcd_types.h"
#include "hal/mipi_dsi_types.h"
#include "rpi_7inch_init_cmds.h"

static const char *TAG = "rpi_td2";

#define I2C_SCL_GPIO                 8
#define I2C_SDA_GPIO                 7
#define MIPI_DSI_PHY_LDO_CHAN        3
#define MIPI_DSI_PHY_LDO_MV          2500
#define MIPI_DSI_LANE_NUM            2
#define MIPI_DSI_LANE_BITRATE_MBPS   1000

/* rpi_7inch_default_mode */
#define LCD_H_RES                    720
#define LCD_V_RES                    1280
#define LCD_DPI_MHZ                  83
#define LCD_HFP                      239
#define LCD_HSYNC                    33
#define LCD_HBP                      50
#define LCD_VFP                      20
#define LCD_VSYNC                    2
#define LCD_VBP                      30

/* rpi-panel-v2 MCU @ 0x45 */
#define TD2_MCU_ADDR                 0x45
#define TD2_REG_ID                   0x01
#define TD2_REG_POWERON              0x02
#define TD2_REG_PWM                  0x03
#define TD2_LCD_RESET_BIT            (1u << 0) /* ACTIVE_LOW in DT */
#define TD2_CTP_RESET_BIT            (1u << 1) /* also enables touch 3V3 rail */
#define TD2_PWM_BL_ENABLE            (1u << 7)
#define TD2_PWM_MAX                  0x1F

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_mcu;

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
    /* TD2 MCU needs a pause after address write (Linux driver sleeps 5–10 ms). */
    ESP_RETURN_ON_ERROR(i2c_master_transmit(dev, &reg, 1, 100), TAG, "reg addr");
    vTaskDelay(pdMS_TO_TICKS(8));
    return i2c_master_receive(dev, val, 1, 100);
}

static void i2c_scan(void)
{
    ESP_LOGI(TAG, "I2C scan SDA=GPIO%d SCL=GPIO%d", I2C_SDA_GPIO, I2C_SCL_GPIO);
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (i2c_master_probe(s_i2c, a, 50) == ESP_OK) {
            ESP_LOGI(TAG, "  found 0x%02X%s", a,
                     a == TD2_MCU_ADDR ? " (TD2 MCU)" :
                     a == 0x5D ? " (GT911)" :
                     a == 0x14 ? " (GT911 alt)" : "");
        }
    }
}

/**
 * Power / reset / backlight via display MCU — required before DSI init.
 * Matches vc4-kms-dsi-ili9881-7inch-overlay.dts + rpi-panel-v2-regulator.c
 */
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
        ESP_LOGE(TAG, "MCU ID read failed: %s — is 5V J1 connected?", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "TD2 MCU ID=0x%02X (low nibble 0x01/0x04 = 7\", 0x09/0x08 = 5\")", id);

    /* All off */
    ESP_RETURN_ON_ERROR(i2c_write_u8(s_mcu, TD2_REG_PWM, 0), TAG, "PWM off");
    ESP_RETURN_ON_ERROR(i2c_write_u8(s_mcu, TD2_REG_POWERON, 0), TAG, "POWERON 0");
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Enable CTP/touch 3V3 rail (gpio1 ACTIVE_HIGH) while holding LCD reset */
    ESP_RETURN_ON_ERROR(i2c_write_u8(s_mcu, TD2_REG_POWERON, TD2_CTP_RESET_BIT), TAG, "rail on");
    vTaskDelay(pdMS_TO_TICKS(60));

    /* Release LCD reset (ACTIVE_LOW → bit set = released) */
    ESP_RETURN_ON_ERROR(i2c_write_u8(s_mcu, TD2_REG_POWERON, TD2_CTP_RESET_BIT | TD2_LCD_RESET_BIT),
                        TAG, "reset release");
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Backlight max */
    ESP_RETURN_ON_ERROR(i2c_write_u8(s_mcu, TD2_REG_PWM, TD2_PWM_MAX | TD2_PWM_BL_ENABLE), TAG, "BL on");
    ESP_LOGI(TAG, "TD2 MCU: POWERON=0x03, PWM backlight enabled — panel should glow");
    return ESP_OK;
}

static esp_err_t start_colorbar(void)
{
    ESP_LOGI(TAG, "Step1: DSI PHY power");
    ESP_RETURN_ON_ERROR(enable_dsi_phy_power(), TAG, "PHY");

    ESP_LOGI(TAG, "Step2: TD2 MCU power/reset/backlight");
    ESP_RETURN_ON_ERROR(td2_mcu_panel_on(), TAG, "MCU");

    ESP_LOGI(TAG, "Step3: DSI bus");
    esp_lcd_dsi_bus_handle_t bus = NULL;
    esp_lcd_dsi_bus_config_t bus_cfg = ILI9881C_PANEL_BUS_DSI_2CH_CONFIG();
    bus_cfg.lane_bit_rate_mbps = MIPI_DSI_LANE_BITRATE_MBPS;
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &bus), TAG, "DSI bus");

    ESP_LOGI(TAG, "Step4: DBI IO");
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_dbi_io_config_t dbi_cfg = ILI9881C_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(bus, &dbi_cfg, &io), TAG, "DBI");

    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = LCD_DPI_MHZ,
        .in_color_format = LCD_COLOR_FMT_RGB565, /* smaller FB; colorbar is HW-generated */
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

    ili9881c_vendor_config_t vendor = {
        .init_cmds = rpi_7inch_init_cmds,
        .init_cmds_size = RPI_7INCH_INIT_CMDS_SIZE,
        .mipi_config = {
            .dsi_bus = bus,
            .dpi_config = &dpi_cfg,
            .lane_num = MIPI_DSI_LANE_NUM,
        },
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1, /* reset via MCU I2C already done */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };

    ESP_LOGI(TAG, "Step5: Create ILI9881C panel object (RPi vendor init, %u cmds)",
             (unsigned)RPI_7INCH_INIT_CMDS_SIZE);
    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9881c(io, &panel_cfg, &panel), TAG, "ili9881c new");

    ESP_LOGI(TAG, "Step6: panel reset (soft) + init (ID read + RPi vendor cmds)");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "init");

    /* Re-assert MCU backlight PWM after panel init (in case init sequence affected rails). */
    ESP_RETURN_ON_ERROR(i2c_write_u8(s_mcu, TD2_REG_PWM, TD2_PWM_MAX | TD2_PWM_BL_ENABLE), TAG, "BL reassert");
    ESP_LOGI(TAG, "RPi vendor init active; MCU backlight re-asserted");

    ESP_LOGI(TAG, "Step7: hardware color bars @ %dx%d", LCD_H_RES, LCD_V_RES);
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
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== RPi Touch Display 2 7\" colorbar (MCU power + DSI) ===");

    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_1,
        .scl_io_num = I2C_SCL_GPIO,
        .sda_io_num = I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_i2c));
    i2c_scan();

    esp_err_t err = start_colorbar();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bring-up failed: %s", esp_err_to_name(err));
    }
}
