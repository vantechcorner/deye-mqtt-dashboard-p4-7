idf_component_register(
    SRCS "ui.c" "ui_ha.c"
    INCLUDE_DIRS "include"
    REQUIRES
        lvgl
        telemetry
        log
        esp_timer
)
