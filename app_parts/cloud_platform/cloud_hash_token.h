#ifndef __CLOUD_HASH_TOKEN_H__
#define __CLOUD_HASH_TOKEN_H__

#include "esp_err.h"

/* 用户层配置结构：此处仅作为指针使用，具体字段由实现方定义 */
typedef struct cloud_hash_user_config cloud_hash_user_config_t;

//基类接口
typedef struct base_cloud_hash_individual base_cloud_hash_individual_t;
struct base_cloud_hash_individual
{
    const char *name;
    cloud_hash_user_config_t *user_cfg; /* 指向用户层配置*/
    esp_err_t (*cloud_hash_token_generate)(base_cloud_hash_individual_t *self, char *key, char *msg, char *out_hex);
    esp_err_t (*cloud_hash_token_check)(base_cloud_hash_individual_t *self);
    
};

  

base_cloud_hash_individual_t* user_register_cloud_hash_function(void);





#endif
