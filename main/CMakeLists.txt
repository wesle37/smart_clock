idf_component_register(
    SRCS "main.c" "lvgl_ui.c"
    INCLUDE_DIRS "."
    REQUIRES esp_hal_lcd
    REQUIRES esp_lcd
    REQUIRES esp_timer
    )
