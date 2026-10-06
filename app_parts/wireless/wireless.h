#ifndef _WIRELESS_H_
#define _WIRELESS_H_

#include <stdbool.h>
#include "esp_err.h"

#define WIFI_PROFILE_MAX     5                       // 最多保存 5 个 WiFi 档案
//wifi状态
typedef enum
{
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_GOT_IP,
    WIFI_STATE_GOT_DHCP,
}WIFI_STATE;

//===== 队列消息：事件回调 → 专职任务的唯一通道 =====
typedef enum
{
    WIFI_APP_EVT_NONE = 0,
    WIFI_APP_EVT_GOT_IP,                          /* 拿到 IP */
    WIFI_APP_EVT_DISCONNECTED,                    /* 断开，reason 字段有效 */
}wifi_app_event_t;

typedef struct
{
    wifi_app_event_t event;
    uint16_t reason;                              /* 断线原因：event_data 出了回调就失效，必须当场拷贝 */
}wifi_app_msg_t;

//wifi键值对结构体
typedef struct
{
    char ssid[33];        /* 与 wifi_config 长度对齐 */
    char pass[33];
}wifi_profile_t;

//wifi总键值对表结构体
typedef struct
{
    uint8_t count;                            
    wifi_profile_t profile[WIFI_PROFILE_MAX];
}wifi_profile_table_t;


typedef struct base_wifi_individual base_wifi_individual_t;
typedef void(*p_wifi_user_event_handler)(base_wifi_individual_t *self,WIFI_STATE wifi_state);

//用户层配置
typedef struct 
{   
    p_wifi_user_event_handler wifi_user_event_handler;
    uint8_t wifi_event_queue_len;
    uint16_t wifi_event_task_stack;
    uint8_t wifi_event_task_prio;
    char max_connect_retry;
   
} wifi_user_config_t;

struct base_wifi_individual
{
    const char *name;
    wifi_user_config_t *user_config; /* 指向用户层配置*/
    esp_err_t  (*wifi_init)(base_wifi_individual_t *self);
    esp_err_t  (*wifi_initiated_connect)(base_wifi_individual_t *self, char *ssid, char *password);
    esp_err_t  (*wifi_initiated_disconnect)(base_wifi_individual_t *self);
};

// 实现层接口
base_wifi_individual_t *user_register_wifi_function(void);
// 应用层入口
base_wifi_individual_t *wifi_app_start(void);

#endif
