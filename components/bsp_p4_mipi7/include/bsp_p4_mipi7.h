#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Raspberry Pi Touch Display 2 (7") — ILI9881C native portrait on P4-Nano. */
#define BSP_P4_MIPI7_H_RES 720
#define BSP_P4_MIPI7_V_RES 1280
/** Logical UI size after LVGL software rotation 90° (landscape stand). */
#define BSP_P4_MIPI7_UI_H_RES BSP_P4_MIPI7_V_RES
#define BSP_P4_MIPI7_UI_V_RES BSP_P4_MIPI7_H_RES

/**
 * Initialize TD2 MCU power/reset/backlight, MIPI-DSI ILI9881C, LVGL (PSRAM
 * double partial buffers + DMA2D + SW landscape rotation), and GT911 touch.
 */
esp_err_t bsp_p4_mipi7_init(uint8_t backlight_pct);

/** LVGL lock — timeout_ms 0 = try once; UINT32_MAX = wait forever. */
esp_err_t bsp_p4_mipi7_lock(uint32_t timeout_ms);
void bsp_p4_mipi7_unlock(void);

lv_display_t *bsp_p4_mipi7_display(void);

/** Set backlight 5–100% via TD2 MCU PWM. */
void bsp_p4_mipi7_set_backlight(uint8_t percent);

#ifdef __cplusplus
}
#endif
