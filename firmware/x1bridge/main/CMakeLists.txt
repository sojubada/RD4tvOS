idf_component_register(
    SRCS "app_main.c" "diaglog.c" "x1_profile.c" "vsrc.c"
    INCLUDE_DIRS "."
    REQUIRES bt nvs_flash esp_timer driver
)
