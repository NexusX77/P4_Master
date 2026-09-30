#include "wireless.h"
#include "esp_log.h"

/*用户应用层 */
//自己的事件回调：wifi 状态变化时被实现层调用
static void wifi_manual_trigger_callback(base_wifi_individual_t *self, WIFI_STATE wifi_state)
{
    switch (wifi_state)
    {
        case WIFI_STATE_CONNECTED:
        {
            ESP_LOGI("user", "WiFi 已连接"); break;
        }
        case WIFI_STATE_DISCONNECTED:
        {
            ESP_LOGI("user", "WiFi 断开(重试耗尽)");   break;
        }
        case WIFI_STATE_GOT_IP:
        {
            ESP_LOGI("user", "WiFi 已获取IP"); break;
        }
        case WIFI_STATE_GOT_DHCP:
        {
            ESP_LOGI("user", "WiFi 已获取DHCP"); break;
        }
        default: break;
    }
}

//用户可调参数（原来 #define MAX_CONNECT_RETRY 5 的新家）
static wifi_user_config_t wifi_customize_cfg =
{
    .max_connect_retry = 5,
};

//启动入口：main.c 只调这一个函数
base_wifi_individual_t *app_wifi_start(void)
{
    base_wifi_individual_t *dev = user_register_wifi_function();      //多态：拿基类指针(vtable已填满)
    dev->user_cfg = &wifi_customize_cfg;                              //绑定用户参数
    dev->wifi_app_event_callback(dev, wifi_manual_trigger_callback);  //注册自己的回调(走setter,存进派生类event_cb)
    return dev;
}
