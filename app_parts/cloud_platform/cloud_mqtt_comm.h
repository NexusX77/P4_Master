#ifndef __CLOUD_MQTT_COMM_H__
#define __CLOUD_MQTT_COMM_H__

#include "esp_err.h"
#include "cJSON.h"

#define CLOUD_MQTT_PORT 8883

#define CLOUD_CLIENT_ID CLOUD_USERNAME "_0_0_%s"

#define CLOUD_USERNAME "6a6b27271554565668210bf1_1111"

#define CLOUD_DEVICE_SCRECT_PASSWORD "Butyouknowhowmuchbrokemeapart"

#define CLOUD_CONNECT_TO_IP "mqtts://fef4d60b0d.iotda-device.cn-south-4.myhuaweicloud.com"

//用户层配置
typedef struct 
{   
    char max_connect_retry;
} cloud_mqtt_user_config_t;

//基类接口
typedef struct base_cloud_mqtt_individual base_cloud_mqtt_individual_t;
struct base_cloud_mqtt_individual
{
    const char *name;
    cloud_mqtt_user_config_t *user_cfg; /* 指向用户层配置*/
    
};



base_cloud_mqtt_individual_t* user_register_cloud_mqtt_function(void);






#endif