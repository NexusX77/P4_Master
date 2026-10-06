#include "cloud_property.h"
#include "cloud_mqtt_comm.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include <string.h>
#include "esp_log.h"

#define TAG "cloud_property"


/*
void set_device_name_to_cloud_attribute(char* device_name_data)
{
    if (device_name_data == NULL) 
    {
        ble_upload_data.device_name_data[0] = '\0';
        return;
    }
    snprintf(ble_upload_data.device_name_data, sizeof(ble_upload_data.device_name_data), "%s", device_name_data);
}

void set_uuid_to_cloud_attribute(char* uuid)
{
    if (uuid == NULL) 
    {
        ble_upload_data.uuid[0] = '\0';
        return;
    }
    snprintf(ble_upload_data.uuid, sizeof(ble_upload_data.uuid), "%s", uuid);
}

void set_raw_data_to_cloud_attribute(char* raw_data)
{
    if (raw_data == NULL) 
    {
        ble_upload_data.raw_data[0] = '\0';
        return;
    }
    snprintf(ble_upload_data.raw_data, sizeof(ble_upload_data.raw_data), "%s", raw_data);
}

void set_interval_to_cloud_attribute(char* interval_data)
{
    if (interval_data == NULL) 
    {
        ble_upload_data.interval_data[0] = '\0';
        return;
    }
    snprintf(ble_upload_data.interval_data, sizeof(ble_upload_data.interval_data), "%s", interval_data);
}

void set_rssi_to_cloud_attribute(char* rssi_data)
{
    if (rssi_data == NULL) 
    {
        ble_upload_data.rssi_data[0] = '\0';
        return;
    }
    snprintf(ble_upload_data.rssi_data, sizeof(ble_upload_data.rssi_data), "%s", rssi_data);
}

void set_huart_beat_to_cloud_attribute(int huart_beat)
{
    ble_upload_data.huart_beat_data = huart_beat;
}
*/








