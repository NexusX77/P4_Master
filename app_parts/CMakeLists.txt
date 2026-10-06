file(GLOB_RECURSE SOURCES ./*.c)

set(parts_files 
    wireless
    cloud_platform
    lvgl_display
)

set(component_requires
    bt
    nvs_flash
    esp_lvgl_adapter 
    spi_flash
    heap 
    esp_system
    esp_timer
    esp_lcd 
    lvgl 
    app_update
    esp_https_ota
    spiffs
    esp_http_server 
    esp_http_client 
    log
    esp_wifi
    driver
    mbedtls
    cjson
    mqtt
)

idf_component_register(
    SRCS         ${SOURCES}
    INCLUDE_DIRS ${parts_files} 
    REQUIRES     ${component_requires} 
)
