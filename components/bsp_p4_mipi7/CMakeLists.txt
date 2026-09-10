idf_component_register(
    SRCS "bsp_p4_mipi7.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_lcd
        driver
        freertos
        log
        esp_timer
        esp_hw_support
    PRIV_REQUIRES
        lvgl
        esp_lcd_ili9881c
        esp_lcd_touch
        esp_lcd_touch_gt911
)
