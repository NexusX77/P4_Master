#include "cloud_json.h"

#include "mqtt_client.h"

void mqtt_user_event_callback(int32_t event_id)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) 
    {
        case MQTT_EVENT_CONNECTED:

            break;
    }

}









//启动入口：main.c 只调这一个函数
base_cloud_json_individual_t *app_cloud_start(void)
{
    base_cloud_json_individual_t *dev = user_register_cloud_function();      //多态：拿基类指针(vtable已填满)
    return dev;
}
