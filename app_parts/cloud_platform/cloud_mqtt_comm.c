#include "cloud_mqtt_comm.h"
#include "cloud_hash_token.h"
#include "cloud_property.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h" 
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

#define TAG "cloud_mqtt_comm"

typedef struct
{
    base_cloud_mqtt_individual_t base;  
    esp_mqtt_client_handle_t mqtt_handle;
    TaskHandle_t cloud_report_task_handle;
    volatile bool mqtt_connected;
    volatile bool mqtt_stop_requested;
    volatile uint32_t report_interval_ms;


}cloud_mqtt_variable_t; 

static esp_mqtt_client_handle_t mqtt_handle = NULL;
static TaskHandle_t cloud_report_task_handle = NULL;
static volatile bool mqtt_connected = false;       // MQTT 是否已连接
static volatile bool mqtt_stop_requested = false;  // 认证被拒时置位，由上报任务执行 stop
static volatile uint32_t report_interval_ms = 1000; // 上报间隔(ms)，默认 1s
char request_id[128] = {0};

static void mqtt_protocol_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) 
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        mqtt_connected = true;   
        
        set_app_valid(true);
        cloud_subscribe_topic();
        cJSON* upload_json = cloud_attribute_data_upload();//单创建cjson
        char* data = cJSON_PrintUnformatted(upload_json);
        cloud_attribute_report_topic(data);
        cJSON_free(data);
        cJSON_Delete(upload_json);
        const char* version = get_cloud_version();
        ESP_LOGI(TAG, "version: %s", version);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        mqtt_connected = false;
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED");
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED");
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED");
        break;
    case MQTT_EVENT_DATA:
    {
        // event->topic 和 event->data 不以 \0 结尾，必须拷贝到本地缓冲区
        char topic_buf[256] = {0};
        char data_buf[1024] = {0};
        snprintf(topic_buf, sizeof(topic_buf), "%.*s", event->topic_len, event->topic);
        snprintf(data_buf, sizeof(data_buf), "%.*s", event->data_len, event->data);

        //平台下发主题
        //Topic: $oc/devices/{device_id}/sys/properties/set/request_id={request_id} 
        if(strstr(topic_buf,"/properties/set/"))
        {
            sscanf(topic_buf,"%*[^=]=%127s",request_id);
            ESP_LOGI(TAG,"%s",request_id);
            cJSON* attribute = cJSON_Parse(data_buf);
            cloud_attribute_data_parse(attribute);
            cloud_attribute_analysis_ack();

            cJSON* upload_json = cloud_attribute_data_upload();//重新生成一次json数据
            char* attribute_data = cJSON_PrintUnformatted(upload_json);
            cloud_attribute_report_topic(attribute_data);//需要自己再次上报到平台更新属性值
            cJSON_free(attribute_data);
            cJSON_Delete(upload_json);
        }
        else if(strstr(topic_buf,"/sys/events/down"))//ota主题
        {
            cJSON* ota_json = cJSON_Parse(data_buf);
            ESP_LOGI(TAG, "ota_json parse: %s", ota_json ? "OK" : "FAIL");
            cloud_analysis_status ota_status = cloud_ota_inform_analysis(ota_json);
            if(ota_status == CLOUD_OTA_NOTICE_VERSION)
            {
                cJSON* ota_version_json = cloud_ota_version_upload_generate();
                char* ota_data = cJSON_PrintUnformatted(ota_version_json);
                cloud_ota_version_ack(ota_data);
                cJSON_free(ota_data);
                cJSON_Delete(ota_version_json);
            }
            else if(ota_status == CLOUD_OTA_NOTICE_OTA)
            {
                cloud_ota_task_start();
            }
            cJSON_Delete(ota_json);
        }       
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        break;
    }
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR, type=%d", event->error_handle->error_type);
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
        {
            // 认证被拒（如时间戳/密码错误）：内部自动重连只会反复消耗堆导致 OOM。
            // esp_mqtt_client_stop 不能在本回调（MQTT 任务上下文）里调用，会返回 ESP_FAIL，
            // 所以置标志位，交给 cloud_report_task 在任务上下文里执行 stop。
            ESP_LOGE(TAG, "Auth rejected, break reconnect storm");
            mqtt_stop_requested = true;
        }
        break;
    case MQTT_EVENT_BEFORE_CONNECT:
        // event id 7：每次发起连接前触发，之前被 default 分支误报成"Time Stamp expired"
        ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT, free internal heap=%d",
                 (int)esp_get_free_internal_heap_size());
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", (int)event->event_id);
        break;
    }
}

esp_err_t cloud_connect_start(void)
{
    static bool hmac_tested = false;
    if (!hmac_tested)
    {
        hmac_tested = true;
        cloud_hmac_self_check();   // 开机跑一次 HMAC 已知向量自检
    }
    // 二次调用时不要重复 init，否则会泄漏旧句柄
    if (mqtt_handle == NULL)
    {
        
        char timestamp[20] = {0};
        char client_id[128] = {0};
        char hash_value[65] = {0};

        ESP_LOGI(TAG,"waitting for time sync");
        if (!cloud_wait_until_time_synced(15000))
        {
            // 时间未同步就用 1970 时间戳生成密码，华为云必拒，还会陷入重连风暴。
            // 直接跳过本轮连接，等 wifi 重连事件再次触发 cloud_connect_start。
            ESP_LOGE(TAG, "time not synced, skip MQTT connect this round");
            return ESP_ERR_INVALID_STATE;
        }
        ESP_LOGI(TAG,"time sync done");
        cloud_get_utc_timestamp(timestamp, sizeof(timestamp));
        ESP_LOGI(TAG, "timestamp: %s", timestamp);
        snprintf(client_id, sizeof(client_id), CLOUD_CLIENT_ID, timestamp);
        ESP_LOGI(TAG, "client_id=%s", client_id);

        cloud_sha256_value_generate(timestamp,CLOUD_DEVICE_SCRECT_PASSWORD, hash_value);
        ESP_LOGI(TAG, "hash_value=%s", hash_value);
        esp_mqtt_client_config_t mqtt_config =
        {
            .broker.address.uri  = CLOUD_CONNECT_TO_IP,
            .broker.address.port = CLOUD_MQTT_PORT,
            .task.priority       = 4,             // 4 < LVGL 的 5，UI 永远优先
            // —— 以下为新增：TLS 服务器证书校验 ——
            .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
            .broker.verification.skip_cert_common_name_check = false,   // 生产环境建议 false
    
        };
        // —— 以下为新增：TLS 服务器证书校验 ——
        mqtt_config.credentials.client_id = client_id;
        mqtt_config.credentials.username = CLOUD_USERNAME;
        mqtt_config.credentials.authentication.password = hash_value;
        mqtt_config.network.reconnect_timeout_ms = 60000;
        mqtt_handle = esp_mqtt_client_init(&mqtt_config);
        esp_mqtt_client_register_event(mqtt_handle, ESP_EVENT_ANY_ID, mqtt_comm_event_handler, NULL);
    }

    esp_mqtt_client_start(mqtt_handle);
    // 定时上报任务只创建一次（任务内部用 report_interval_ms 和 mqtt_connected 自控）
    if (cloud_report_task_handle == NULL)
    {
        ESP_LOGI(TAG,"cloud_report_task_handle");
        xTaskCreatePinnedToCore(cloud_report_task, "cloud_report_task", 8192, NULL, 5, &cloud_report_task_handle, 0);
    }

    return ESP_OK;
}

void cloud_connect_stop(void)
{
    if (mqtt_handle != NULL)
    {
        esp_mqtt_client_stop(mqtt_handle);   // 停止客户端，并取消内部自动重连
    }
}

// 设置定时上报间隔（ms），由 UI 的 +/- 按钮调用
void cloud_set_report_interval(uint32_t ms)
{
    if (ms > 0)
    {
        report_interval_ms = ms;
    }
}

// 定时上报任务：每隔 report_interval_ms 把最新 BLE 特征值上报到华为云
extern QueueHandle_t ble_chara_value_queue;
static void cloud_report_task(void *arg)
{
    ESP_LOGI(TAG,"cloud_report_task");
    
    while (1)
    {
        UBaseType_t uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
        ESP_LOGI("STACK", "cloud_report_task remaining stack: %d bytes", uxHighWaterMark);
        ble_upload_data_t upload_data = {0};
        if (mqtt_stop_requested)
        {
            // 认证被拒：在本任务上下文停止 MQTT 客户端，终止内部重连风暴
            mqtt_stop_requested = false;
            cloud_connect_stop();
        }
        if (ble_chara_value_queue == NULL)
        {
            vTaskDelay(pdMS_TO_TICKS(100));   // 队列还没建，等一下
            continue;
        }
        // 1s 超时轮询，保证 stop 请求能被及时处理
        if (xQueueReceive(ble_chara_value_queue, &upload_data, pdMS_TO_TICKS(1000)) != pdTRUE)
            continue;
        if (!mqtt_connected)
            continue;
        ESP_LOGI(TAG,"start uploading");
              
        cJSON *upload_json = cloud_attribute_data_upload();
        char *data = cJSON_PrintUnformatted(upload_json);
        cloud_attribute_report_topic(data);
        cJSON_free(data);
        cJSON_Delete(upload_json);
    }
}

//订阅主题
static void cloud_subscribe_topic(void)
{
    char topic[256];
    //平台设置设备 回复 主题
    snprintf(topic,256,"$oc/devices/%s/sys/properties/set/#",CLOUD_USERNAME);
    ESP_LOGI(TAG,"subscribed setting device topic");
    esp_mqtt_client_subscribe_single(mqtt_handle,topic,1);
    //订阅平台ota下发信息 主题
    snprintf(topic,256,"$oc/devices/%s/sys/events/down",CLOUD_USERNAME);
    ESP_LOGI(TAG,"subscribed ota inform topic");
    esp_mqtt_client_subscribe_single(mqtt_handle,topic,1);

}

//平台设置设备属性 上报
static void cloud_attribute_analysis_ack(void)
{
    char topic[256];
    snprintf(topic,256,"$oc/devices/%s/sys/properties/set/response/request_id=%s",CLOUD_USERNAME,request_id);
    ESP_LOGI(TAG,"request id:%s",request_id);
    cJSON* reply_json = cJSON_CreateObject();
    cJSON_AddNumberToObject(reply_json,"result_code",0);
    cJSON_AddStringToObject(reply_json,"result_desc","success");
  
    char* data = cJSON_PrintUnformatted(reply_json);
    esp_mqtt_client_publish(mqtt_handle,topic,data,strlen(data),1,0);
    cJSON_free(data);
    cJSON_Delete(reply_json);
    ESP_LOGI(TAG,"attribute ack finished");
}

//设备属性上报
void cloud_attribute_report_topic(const char* data)
{
    char topic[256];
    //发送主题和数据
    snprintf(topic,256,"$oc/devices/%s/sys/properties/report",CLOUD_USERNAME);
    ESP_LOGI(TAG,"report attribute topic");
    ESP_LOGI(TAG,"upload topic:%s,payload:%s",topic,data);
    esp_mqtt_client_publish(mqtt_handle,topic,data,strlen(data),1,0);

}

//ota版本上报
static void cloud_ota_version_ack(const char* data)
{

    char topic[256];
    snprintf(topic,256,"$oc/devices/%s/sys/events/up",CLOUD_USERNAME);
    ESP_LOGI(TAG,"report ota topic");

    esp_mqtt_client_publish(mqtt_handle,topic,data,strlen(data),1,0);
}

//ota升级状态上报
void cloud_ota_upload_status_ack(const char* data)
{

    char topic[256];
    snprintf(topic,256,"$oc/devices/%s/sys/events/up",CLOUD_USERNAME);
    ESP_LOGI(TAG,"report ota upload status");

    esp_mqtt_client_publish(mqtt_handle,topic,data,strlen(data),1,0);
}

