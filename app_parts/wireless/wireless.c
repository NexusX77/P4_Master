#include "wireless.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#define TAG     "wireless"  
#define WIFI_NVS_NAMESPACE  "wifi_value_pair_conf"   // NVS 命名空间
#define WIFI_NVS_SSID_KEY   "ssid"
#define WIFI_NVS_PASS_KEY   "pass"

//===== 派生类（实现层私有) =====
typedef struct
{
    base_wifi_individual_t base;                 /* 必须是第一个成员 */
    p_wifi_manual_trigger_callback wifi_manual_trigger_callback;     /* 用户层注入的事件回调（数据成员，不是 vtable） */
    bool wifi_init_state_flag;                   /* false = esp_wifi_stop 过，重连前要先 start */
    bool is_wifi_connected_flag;                 /* 拿到 IP 才算连上 */
    bool manually_disconnect_flag;               /* 手动断开标记：true 时不自动重连、不弹窗 */
    char actual_retry_count;
} wifi_esp32s3_t;

//WIFI事件回调函数,协议层
static void wifi_protocol_event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data)
{   
    wifi_esp32s3_t *self = (wifi_esp32s3_t *)arg;
    if(event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
            case WIFI_EVENT_STA_START:  
                ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
                break;
            case WIFI_EVENT_STA_CONNECTED:  //WIFI连上路由器后，触发此事件
                ESP_LOGI(TAG, "STA connected,next get ip");//设置事件组位，通知其他任务连接成功
                break;
            case WIFI_EVENT_STA_DISCONNECTED:   //WIFI从路由器断开连接后触发此事件
            {
                wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
                ESP_LOGI(TAG, "disconnected, reason=%d", disc->reason);
                self->is_wifi_connected_flag = false;
                //手动断开：清标记即可，不重连、不通知用户层
                if (self->manually_disconnect_flag)   
                {
                    self->manually_disconnect_flag = false;
                    break;
                }
                //自动断开：先重连，重试耗尽才通知用户层弹窗
                if (self->actual_retry_count < self->base.user_cfg->max_connect_retry && self->wifi_init_state_flag)
                {
                    self->actual_retry_count++;
                    ESP_LOGI(TAG, "reconnecting: attempt %d/%d", self->actual_retry_count, self->base.user_cfg->max_connect_retry);
                    esp_wifi_connect();
                }
                else if (self->wifi_manual_trigger_callback)   //判空保护：回调没注册就不能调，否则 PC=0 崩机
                {
                    self->wifi_manual_trigger_callback(&self->base, WIFI_STATE_DISCONNECTED);
                }
                break;
            }
            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG,"sta device connected");
                break;
            case WIFI_EVENT_AP_STADISCONNECTED:
                ESP_LOGI(TAG,"sta device disconnected");
                break;
            default:
                break;
        }
    }
    if(event_base == IP_EVENT)                  //IP相关事件
    {
        switch(event_id)
        {
            case IP_EVENT_STA_GOT_IP:           //只有获取到路由器分配的IP，才认为是连上了路由器
                ESP_LOGI(TAG,"Get ip address");
                self->is_wifi_connected_flag = true;
                self->actual_retry_count = 0;   //连上了，重连计数清零
                if (self->wifi_manual_trigger_callback)             //判空保护
                    self->wifi_manual_trigger_callback(&self->base, WIFI_STATE_CONNECTED);
                break;
            default:break;
        }
    }
}
// 把 SSID/密码写入 NVS（断电不丢）
static esp_err_t wifi_nvs_save_credentials(char* ssid,char* password)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGI(TAG, "nvs open fail: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_str(handle, WIFI_NVS_SSID_KEY, ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, WIFI_NVS_PASS_KEY, password);
    if (err == ESP_OK) err = nvs_commit(handle);   // 必须 commit 才真正落盘
    nvs_close(handle);
    return err;
}

// 从 NVS 读取 SSID/密码；没有保存过则返回 ESP_ERR_NVS_NOT_FOUND
static esp_err_t wifi_nvs_load_credentials(char* ssid, size_t ssid_len,char* password, size_t pass_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return err;   // 从未保存过，正常情况
    if (err != ESP_OK) return err;

    size_t len = ssid_len;
    err = nvs_get_str(handle, WIFI_NVS_SSID_KEY, ssid, &len);
    if (err == ESP_OK)
    {
        len = pass_len;
        err = nvs_get_str(handle, WIFI_NVS_PASS_KEY, password, &len);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t wifi_manually_initiated_connect(base_wifi_individual_t *dev,char* ssid,char* password);
static esp_err_t wifi_sta_init(base_wifi_individual_t *dev)
{
    wifi_esp32s3_t *self = (wifi_esp32s3_t *)dev;
    // 这些全局初始化（esp_netif_init/esp_wifi_init）只能执行一次，防重入
    static bool wifi_inited = false;
    if (wifi_inited) 
    {
        ESP_LOGI(TAG, "wifi already initialized, skip");
        return ESP_OK;
    }
    wifi_inited = true;

    ESP_ERROR_CHECK(esp_netif_init());  //用于初始化tcpip协议栈
    ESP_ERROR_CHECK(esp_event_loop_create_default());       //创建一个默认系统事件调度循环，之后可以注册回调函数来处理系统的一些事件
    esp_netif_create_default_wifi_sta();    //使用默认配置创建STA对象
    //初始化WIFI
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    //注册事件回调函数：把 self 传给 handler，事件来了才知道自己是谁（之前传的是未初始化的局部变量，会崩）
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_protocol_event_handler, self));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_protocol_event_handler, self));
    
    //启动WIFI
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );         //设置工作模式为STA
    ESP_ERROR_CHECK(esp_wifi_start());    //启动WIFI

    self->wifi_init_state_flag = true;
    ESP_LOGI(TAG, "wifi_init finished.");

    // 上电读取 NVS 中保存的 WiFi 配置，存在则自动发起一次连接
    char saved_ssid[65]  = {0};
    char saved_pass[129]  = {0};
    if (wifi_nvs_load_credentials(saved_ssid, sizeof(saved_ssid),saved_pass, sizeof(saved_pass)) == ESP_OK)
    {
        ESP_LOGI(TAG, "auto connect saved wifi: %s", saved_ssid);
        wifi_manually_initiated_connect(dev,saved_ssid, saved_pass);
    }
    else
    {
        ESP_LOGI(TAG, "no saved wifi credentials, skip auto connect");
    }
    return ESP_OK;
}

static esp_err_t wifi_manually_initiated_connect(base_wifi_individual_t *dev,char* ssid,char* password)
{
    wifi_esp32s3_t *self = (wifi_esp32s3_t *)dev;
    //wifi_nvs_save_credentials(ssid, password);   // 每次用户发起连接都写入 NVS
    esp_wifi_disconnect();                    // 断开旧连接（别用 ESP_ERROR_CHECK，可能未启动）
    self->actual_retry_count = 0 ;
    wifi_config_t wifi_config = 
    {
        .sta = 
        { 
            .threshold.authmode = WIFI_AUTH_OPEN 
        },
    };
    snprintf((char*)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", ssid);
    snprintf((char*)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", password);

    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

    if (!self->wifi_init_state_flag)// 之前被 stop 过，先重新启动
    {                       
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
        self->wifi_init_state_flag = true;
    }
    //确保为sta模式连接
    wifi_mode_t wifi_mode;
    esp_wifi_get_mode(&wifi_mode);
    if(wifi_mode == WIFI_MODE_STA)
    {
        esp_wifi_connect();
    }
    else
    {
        ESP_LOGI(TAG,"not in STA mode,setting STA mode");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
        esp_wifi_connect();
    }
    return ESP_OK;
}

static esp_err_t wifi_manually_initiated_stop(base_wifi_individual_t *dev)
{
    wifi_esp32s3_t *self = (wifi_esp32s3_t *)dev;
    self->manually_disconnect_flag = true;   //标记手动断开：事件里不重连、不通知
    esp_err_t err = esp_wifi_disconnect();

    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED)
    {
        ESP_LOGI(TAG, "disconnect err: %s", esp_err_to_name(err));
    }
    err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED)
    {
        ESP_LOGI(TAG, "stop err: %s", esp_err_to_name(err));
    }
    self->is_wifi_connected_flag = false;
    self->wifi_init_state_flag = false;
    return ESP_OK;
}

//保存用户层注入的事件回调（vtable 的 wifi_app_event_callback 槽位指向这里）
static esp_err_t set_wifi_event_callback(base_wifi_individual_t *dev, p_wifi_manual_trigger_callback callback)
{
    wifi_esp32s3_t *self = (wifi_esp32s3_t *)dev;
    self->wifi_manual_trigger_callback = callback;
    return ESP_OK;
}

//构造函数：填满 vtable 每一个槽位（漏一个就是定时炸弹）
base_wifi_individual_t* user_register_wifi_function(void)
{
    static wifi_esp32s3_t inst;
    memset(&inst, 0, sizeof(inst));
    inst.base.name                     = "esp32s3";
    inst.base.wifi_init                = wifi_sta_init;
    inst.base.wifi_initiated_connect   = wifi_manually_initiated_connect;
    inst.base.wifi_initiated_disconnect = wifi_manually_initiated_stop;
    inst.base.wifi_app_event_callback  = set_wifi_event_callback;

    base_wifi_individual_t *dev = &inst.base;
    assert(dev->wifi_init && dev->wifi_initiated_connect &&
           dev->wifi_initiated_disconnect && dev->wifi_app_event_callback);
    return dev;
}
