#include "main_port.h"
#include <stdio.h>
#include "nvs_flash.h"
#include "esp_log.h"

#define TAG "app_main"
 
void app_main(void)
{

    ESP_LOGI(TAG,"app_main start");
    //nvs初始化
    esp_err_t nvs_ret = nvs_flash_init();
    if(nvs_ret != ESP_OK)
    {
        ESP_LOGI(TAG, "nvs_flash_init error");
    }

    base_wifi_individual_t* wifi_dev = wifi_app_start();
    wifi_dev->wifi_init(wifi_dev);
    wifi_dev->wifi_initiated_connect(wifi_dev,"401","22219207");

    

}
