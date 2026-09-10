idf_component_register(
    SRCS "mipi7_colorbar_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_lcd
        esp_driver_i2c
        esp_hw_support
        log
        freertos
)
