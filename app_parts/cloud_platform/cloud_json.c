#include "cloud_json.h"
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"

#define TAG "cloud_json"

typedef struct
{
    base_cloud_json_individual_t base;   
                  
}cloud_json_variable_t; 

esp_err_t cloud_property_data_parse(base_cloud_json_individual_t *self,cJSON* property_json)
{
    /*{
  "name": null,
  "id": "e48fb462-4d28-4607-a2aa-ba39adecdf58",
  "content": {
    "object_device_id": "{object_device_id}",
    "services": [
    {
      "service_id": "Gateway_server",
      "properties": 
      {
        "Interval": "00",
        
      }
    }
  ]
  }
}*/
    cJSON* content_json = cJSON_GetObjectItem(property_json,"content");
    if(content_json)
    {
        cJSON* service_json = cJSON_GetObjectItem(content_json,"services");
        if(service_json)
        {
            cJSON* server_items_json = service_json->child;
            while(server_items_json)
            {
                cJSON* service_id_json = cJSON_GetObjectItem(server_items_json, "service_id");
                if(strcmp(service_id_json -> valuestring,"Gateway_server") == 0)
                {
                    ESP_LOGI(TAG,"Getting Gateway_server service");

                    cJSON* properties_json = cJSON_GetObjectItem(server_items_json, "properties");
                    cJSON* property_items_json = properties_json -> child;
                    while(property_items_json)
                    {
                        if(strcmp(property_items_json -> string,"Interval") == 0)
                        {
                            const char *interval = cJSON_GetStringValue(property_items_json);
                            if (interval != NULL) 
                            {
                                //snprintf(ble_upload_data.interval_data, sizeof(ble_upload_data.interval_data), "%s", interval);
                            }
                            ESP_LOGI(TAG,"Getting Interval properties");
                            //ESP_LOGI(TAG,"Interval:%s",ble_upload_data.interval_data);
                        }
                        property_items_json = property_items_json->next;
                    }
                }
                server_items_json = server_items_json -> next;
            }
        }
    }
    return ESP_OK;
}

esp_err_t cloud_property_data_upload(base_cloud_json_individual_t *self,cJSON* property_json)
{
/*
{
  "services": [
    {
      "service_id": "ws2812_server",
      "properties": {
        "Brightness": 34,
        "Switch": true,
        "RGB_Adjustment":{
          "Red":77,
          "Blue":81,
          "Green":97
        }
      }
      
    }
  ]
}*/

    cJSON* root_json = cJSON_CreateObject();
    //创建services
    cJSON* services_json = cJSON_CreateArray();
    cJSON_AddItemToObject(root_json,"services",services_json);

    //创建数组成员
    cJSON* Gateway_service_json = cJSON_CreateObject();
    cJSON_AddItemToArray(services_json,Gateway_service_json);
    cJSON_AddStringToObject(Gateway_service_json,"service_id","Gateway_server"); 
    cJSON* properties_json = cJSON_CreateObject();
    cJSON_AddItemToObject(Gateway_service_json,"properties",properties_json);

    //添加成员
    //cJSON_AddStringToObject(properties_json,"Raw_data",ble_upload_data.raw_data);
    //cJSON_AddStringToObject(properties_json,"Interval",ble_upload_data.interval_data);
    //cJSON_AddStringToObject(properties_json,"Device_name",ble_upload_data.device_name_data);
    //cJSON_AddStringToObject(properties_json,"RSSI",ble_upload_data.rssi_data);
    //cJSON_AddNumberToObject(properties_json,"Huart_beat",ble_upload_data.huart_beat_data);
    
    root_json = property_json;
    return ESP_OK;
}

base_cloud_json_individual_t* user_register_cloud_function(void)
{
    static cloud_json_variable_t inst;
    memset(&inst, 0, sizeof(inst));
    inst.base.name                     = "esp32p4";
    inst.base.cloud_property_data_upload = cloud_property_data_upload;
    inst.base.cloud_property_data_parse = cloud_property_data_parse;   

    base_cloud_json_individual_t *dev = &inst.base;
    assert(dev->cloud_property_data_upload && dev->cloud_property_data_parse);
    return dev;
}