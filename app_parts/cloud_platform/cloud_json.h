#ifndef __CLOUD_JSON_H__
#define __CLOUD_JSON_H__

#include "esp_err.h"
#include "cJSON.h"

/* 用户层配置结构：此处仅作为指针使用，具体字段由实现方定义 */
typedef struct cloud_json_user_config cloud_json_user_config_t;

//基类接口
typedef struct base_cloud_json_individual base_cloud_json_individual_t;
struct base_cloud_json_individual
{
    const char *name;
    cloud_json_user_config_t *user_cfg; /* 指向用户层配置*/
    esp_err_t  (*cloud_property_data_upload)(base_cloud_json_individual_t *self, cJSON* property_data);
    esp_err_t  (*cloud_property_data_parse)(base_cloud_json_individual_t *self, cJSON* property_data);
};



base_cloud_json_individual_t* user_register_cloud_json_function(void);



#endif
