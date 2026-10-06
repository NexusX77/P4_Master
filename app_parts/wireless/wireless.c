#include "wireless.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "esp_log.h"
#include "esp_err.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define TAG     "wireless"
#define WIFI_NVS_NAMESPACE  "wifi_value_pair_conf"   // NVS 命名空间
#define WIFI_NVS_PROFILE_KEY  "profiles"             // 档案表 blob 的 key 


//===== 派生类（实现层私有) =====
typedef struct
{
    base_wifi_individual_t base;                 /* 必须是第一个成员 */
    QueueHandle_t wifi_event_queue;
    bool wifi_init_state_flag;                   /* false = esp_wifi_stop 过，重连前要先 start */
    bool is_wifi_connected_flag;                 /* 拿到 IP 才算连上 */
    bool manually_disconnect_flag;               /* 手动断开标记：true 时不自动重连、不弹窗 */
    char actual_retry_count;
    /* 多档案凭据管理 */
    char now_ssid[32];                           /* 本次要连的 SSID */
    char now_pass[32];                           /* 本次要连的密码 */
    wifi_profile_table_t table;                  /* NVS 档案表的 RAM 缓存 */
    uint8_t auto_index;                           /* 开机轮询：当前尝试的档案下标 */
    bool auto_connect;                            /* 开机轮询进行中 */
}Derivative_wifi_individual_t;

// 前置声明：专职任务在这些函数定义之前使用它们
static esp_err_t wifi_table_save_nvs(const wifi_profile_table_t *table);
static void wifi_load_nvs_saved(wifi_profile_table_t *table);
static bool wifi_table_update(Derivative_wifi_individual_t *self, const char *ssid, const char *pass);
static esp_err_t wifi_manually_initiated_connect(base_wifi_individual_t *dev,char* ssid,char* password);

//WIFI事件回调函数,协议层 —— 只做「记录与转发」，不做任何决策
static void wifi_protocol_event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data)
{
    wifi_app_msg_t msg = { .event = WIFI_APP_EVT_NONE };
    Derivative_wifi_individual_t *self = (Derivative_wifi_individual_t *)arg;

    if(event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "STA connected,next get ip");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
            {
                wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
                msg.event  = WIFI_APP_EVT_DISCONNECTED;
                msg.reason = disc->reason;
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
    else if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        msg.event = WIFI_APP_EVT_GOT_IP;
    }

    if (msg.event == WIFI_APP_EVT_NONE)
        return;                                  // 不关心的事件：直接返回

    // 只转发、不等待：事件循环任务里禁止阻塞
    if (xQueueSend(self->wifi_event_queue, &msg, 0) != pdTRUE)
    {
        ESP_LOGE(TAG, "app queue full, event %d dropped", (int)msg.event);
    }
}

//专职任务：状态决策、重连、用户回调全在这里跑
//（与事件循环任务解耦——用户回调再慢，也只拖慢本任务，不堵系统事件）
static void wifi_app_task(void *arg)
{
    Derivative_wifi_individual_t *self = (Derivative_wifi_individual_t *)arg;
    wifi_app_msg_t msg;

    for (;;)
    {
        xQueueReceive(self->wifi_event_queue, &msg, portMAX_DELAY);   // 没消息就休眠，零 CPU 占用

        switch (msg.event)
        {
            case WIFI_APP_EVT_GOT_IP:
            {
                wifi_user_config_t *config = self->base.user_config;
                ESP_LOGI(TAG,"Get ip address");
                self->is_wifi_connected_flag = true;
                self->actual_retry_count = 0;    //连上了，重连计数清零
                self->auto_connect = false;       //连上了：开机轮询结束
                /* 成功才入库：同一个 WiFi（同SSID同密码）不重复写；
                   换密码→原地更新；全新→追加（满则挤掉最旧的） */
                if (wifi_table_update(self, self->now_ssid, self->now_pass))
                {
                    esp_err_t err = wifi_table_save_nvs(&self->table);
                    if (err == ESP_OK)
                        ESP_LOGI(TAG, "wifi profile saved: %s (%d total)", self->now_ssid, self->table.count);
                    else
                        ESP_LOGE(TAG, "save profile fail: %s", esp_err_to_name(err));
                }
                if (config && config->wifi_user_event_handler)      //判空保护：回调没注册就不能调，否则 PC=0 崩机
                    config->wifi_user_event_handler(&self->base, WIFI_STATE_CONNECTED);
                break;
            }
            case WIFI_APP_EVT_DISCONNECTED:
            {
                wifi_user_config_t *config = self->base.user_config;
                ESP_LOGI(TAG, "disconnected, reason=%d", msg.reason);
                self->is_wifi_connected_flag = false;

                //手动断开：清标记即可，不重连、不通知用户层
                if (self->manually_disconnect_flag)
                {
                    self->manually_disconnect_flag = false;
                    break;
                }
                //自动断开：先重连，重试耗尽才走下面的换档案/通知逻辑
                if (config && self->actual_retry_count < config->max_connect_retry && self->wifi_init_state_flag)
                {
                    self->actual_retry_count++;
                    ESP_LOGI(TAG, "reconnecting: attempt %d/%d", self->actual_retry_count, config->max_connect_retry);
                    esp_wifi_connect();
                    break;
                }
                //上电重连：换下一个档案再试，now_ssid 必须仍是本档案（说明期间用户没发起新的手动连接，轮询没被打断）*/
                if (self->auto_connect &&
                    strcmp(self->now_ssid, self->table.profile[self->auto_index].ssid) == 0 &&
                    self->auto_index + 1 < self->table.count)
                {
                    self->auto_index++;
                    self->actual_retry_count = 0;   //新档案重新计数
                    wifi_profile_t *next = &self->table.profile[self->auto_index];
                    ESP_LOGI(TAG, "auto connect: trying profile %d/%d: %s",self->auto_index + 1, self->table.count, next->ssid);
                    wifi_manually_initiated_connect(&self->base, next->ssid, next->pass);
                    break;
                }
                //全部档案都失败（或非轮询模式）：轮询结束，通知用户层
                self->auto_connect = false;
                if (config && config->wifi_user_event_handler)
                {
                    config->wifi_user_event_handler(&self->base, WIFI_STATE_DISCONNECTED);
                }
                break;
            }
            default:
                break;
        }
    }
}

// 档案入库：返回 true 表示表有变化、需要写回 NVS
// 同 SSID 同密码→false（不重复保存）；同 SSID 换密码→更新；全新→追加
static bool wifi_table_update(Derivative_wifi_individual_t *self, const char *ssid, const char *pass)
{
    for (uint8_t i = 0; i < self->table.count; i++)
    {
        if (strcmp(self->table.profile[i].ssid, ssid) == 0)
        {
            if (strcmp(self->table.profile[i].pass, pass) == 0)
                return false;                              //完全相同：不写
            strcpy(self->table.profile[i].pass, pass);     //密码变了：原地更新
            return true;
        }
    }
    if (self->table.count >= WIFI_PROFILE_MAX)             //满了：挤掉最旧的
    {
        memmove(&self->table.profile[0], &self->table.profile[1],
                (WIFI_PROFILE_MAX - 1) * sizeof(wifi_profile_t));
        self->table.count = WIFI_PROFILE_MAX - 1;
    }
    strcpy(self->table.profile[self->table.count].ssid, ssid);
    strcpy(self->table.profile[self->table.count].pass, pass);
    self->table.count++;
    return true;
}

// 把整张档案表写入 NVS（断电不丢）
static esp_err_t wifi_table_save_nvs(const wifi_profile_table_t *table)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGI(TAG, "nvs open fail: %s", esp_err_to_name(err));
        return err;
    }
    //写入档案表    
    err = nvs_set_blob(handle, WIFI_NVS_PROFILE_KEY, table, sizeof(*table));
    if (err == ESP_OK) err = nvs_commit(handle);   // 必须 commit 才真正落盘
    nvs_close(handle);
    return err;
}

// 从 NVS 读档案表；没存过/读失败都按空表处理（count=0）
static void wifi_load_nvs_saved(wifi_profile_table_t *table)
{
    memset(table, 0, sizeof(*table));
    nvs_handle_t handle;
    //只读打开WIFI_NVS_NAMESPACE命名空间
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK)
    {
        size_t len = sizeof(*table);
        //读取档案表    
        err = nvs_get_blob(handle, WIFI_NVS_PROFILE_KEY, table, &len);
        nvs_close(handle);
    }
    if (err != ESP_OK)
    {
        if (err != ESP_ERR_NVS_NOT_FOUND)          //没存过属正常，不用报
            ESP_LOGW(TAG, "load profiles: %s", esp_err_to_name(err));
        memset(table, 0, sizeof(*table));
    }
    else if (table->count > WIFI_PROFILE_MAX)      //读取成功，但 count 异常：清空
    {
        memset(table, 0, sizeof(*table));
    }
}

static esp_err_t wifi_sta_init(base_wifi_individual_t *dev)
{
    Derivative_wifi_individual_t *self = (Derivative_wifi_individual_t *)dev;
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
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));

    //企业模式三件套：先建队列和专职任务，再注册事件（保证事件一来就有地方转发）
    self->wifi_event_queue = xQueueCreate(self->base.user_config->wifi_event_queue_len, sizeof(wifi_app_msg_t));
    assert(self->wifi_event_queue != NULL);        //失败说明堆耗尽，停在初始化阶段最好排查
    BaseType_t task_ok = xTaskCreate(wifi_app_task, "wifi_app", self->base.user_config->wifi_event_task_stack, self, self->base.user_config->wifi_event_task_prio, NULL);
    assert(task_ok == pdPASS);

    //注册事件回调函数：回调只转发进队列，真正干活的是 wifi_app_task
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_protocol_event_handler, self));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_protocol_event_handler, self));

    //启动WIFI
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));         //设置工作模式为STA
    ESP_ERROR_CHECK(esp_wifi_start());    //启动WIFI

    self->wifi_init_state_flag = true;
    ESP_LOGI(TAG, "wifi_init finished.");

    // 上电轮询：读档案表，从第 0 个开始尝试；失败的档案由专职任务在
    // "重试耗尽"时自动切换下一个，全部失败才通知用户层
    wifi_load_nvs_saved(&self->table);
    if (self->table.count > 0)
    {
        self->auto_connect = true;
        self->auto_index = 0;
        ESP_LOGI(TAG, "auto connect: trying profile 1/%d: %s",self->table.count, self->table.profile[0].ssid);
        wifi_manually_initiated_connect(dev, self->table.profile[0].ssid,self->table.profile[0].pass);
    }
    else
    {
        ESP_LOGI(TAG, "no saved wifi profiles, skip auto connect");
    }
    return ESP_OK;
}

static esp_err_t wifi_manually_initiated_connect(base_wifi_individual_t *dev,char* ssid,char* password)
{
    Derivative_wifi_individual_t *self = (Derivative_wifi_individual_t *)dev;
    esp_wifi_disconnect();                    // 断开旧连接（别用 ESP_ERROR_CHECK，可能未启动）
    self->actual_retry_count = 0 ;

    // 凭据存 RAM：拿到 IP 后由专职任务入库（连不上就不入库）
    snprintf(self->now_ssid, sizeof(self->now_ssid), "%s", ssid);
    snprintf(self->now_pass, sizeof(self->now_pass), "%s", password);

    wifi_config_t wifi_config =
    {
        .sta =
        {
            .threshold.authmode = WIFI_AUTH_OPEN
        },
    };
    snprintf((char*)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", self->now_ssid);
    snprintf((char*)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", self->now_pass);

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
    Derivative_wifi_individual_t *self = (Derivative_wifi_individual_t *)dev;
    self->manually_disconnect_flag = true;   //标记手动断开：专职任务里不重连、不通知
    esp_err_t err = esp_wifi_disconnect();

    if (err == ESP_ERR_WIFI_NOT_STARTED)
    {
        //WiFi 根本没启动：DISCONNECTED 事件不会来，标记当场回收，避免残留成 true 误伤下次断线
        self->manually_disconnect_flag = false;
    }
    else if (err != ESP_OK)
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


base_wifi_individual_t* user_register_wifi_function(void)
{
    static Derivative_wifi_individual_t inst;
    memset(&inst, 0, sizeof(inst));
    inst.base.name                     = "esp32p4";
    inst.base.wifi_init                = wifi_sta_init;
    inst.base.wifi_initiated_connect   = wifi_manually_initiated_connect;
    inst.base.wifi_initiated_disconnect = wifi_manually_initiated_stop;
    inst.manually_disconnect_flag = false;
    inst.actual_retry_count = 0;
    inst.is_wifi_connected_flag = false;
    inst.wifi_init_state_flag = false;
    inst.wifi_event_queue = NULL;
    inst.auto_connect = false;
    inst.auto_index = 0;

    base_wifi_individual_t *dev = &inst.base;
    assert(dev->wifi_init && dev->wifi_initiated_connect && dev->wifi_initiated_disconnect);
    return dev;
}
