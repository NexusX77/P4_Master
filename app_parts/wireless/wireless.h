#ifndef _WIRELESS_H_
#define _WIRELESS_H_

#include <stdbool.h>
#include "esp_err.h"

//wifi状态
typedef enum
{
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_GOT_IP,
    WIFI_STATE_GOT_DHCP,
}WIFI_STATE;

//用户层配置
typedef struct 
{   
    char max_connect_retry;
} wifi_user_config_t;

//基类接口
typedef struct base_wifi_individual base_wifi_individual_t;
typedef void(*p_wifi_manual_trigger_callback)(base_wifi_individual_t *self,WIFI_STATE wifi_state);
struct base_wifi_individual
{
    const char *name;
    wifi_user_config_t *user_cfg; /* 指向用户层配置*/
    esp_err_t  (*wifi_init)(base_wifi_individual_t *self);
    esp_err_t  (*wifi_initiated_connect)(base_wifi_individual_t *self, char *ssid, char *password);
    esp_err_t  (*wifi_initiated_disconnect)(base_wifi_individual_t *self);
    esp_err_t  (*wifi_app_event_callback)(base_wifi_individual_t *self, p_wifi_manual_trigger_callback cb); /* 注册用户事件回调的 setter */
};
// 实现层接口
base_wifi_individual_t *user_register_wifi_function(void);
// 应用层入口
base_wifi_individual_t *app_wifi_start(void);

#endif
