#include "cloud_hash_token.h"
#include "esp_log.h" 
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "mbedtls/md.h"
#include <time.h>
#include "esp_crt_bundle.h"
#include "esp_system.h"


#define TAG "cloud_hash_token"
typedef struct
{
    base_cloud_hash_individual_t base;                 
}cloud_hash_variable_t; 

static esp_err_t cloud_sha256_value_generate(char *key, char *msg, char *out_hex)
{
    
    unsigned char hash_value[32] = {0};
    size_t mac_len = 0;
    psa_status_t status;
    mbedtls_svc_key_id_t key_id;

    psa_algorithm_t alg = PSA_ALG_HMAC(PSA_ALG_SHA_256);
    psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
    //声明这个密钥"允许用来对消息签名"。
    psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_SIGN_MESSAGE);
    //声明这个密钥"只允许配合 HMAC-SHA256 使用
    psa_set_key_algorithm(&attrs, alg); 
    //声明这个密钥的类型是"HMAC 密钥"。
    psa_set_key_type(&attrs, PSA_KEY_TYPE_HMAC);

    // 导入 HMAC 密钥（key = 时间戳）
    status = psa_import_key(&attrs, (const uint8_t *)key, strlen(key), &key_id);
    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_import_key failed: %ld", (long)status);
        return ESP_ERR_INVALID_STATE;
    }

    // 一次性计算 HMAC-SHA256（msg = 设备密钥），输出 32 字节
    status = psa_mac_compute(key_id, alg,
                             (const uint8_t *)msg, strlen(msg),
                             hash_value, sizeof(hash_value), &mac_len);
    psa_destroy_key(key_id);

    if (status != PSA_SUCCESS || mac_len != sizeof(hash_value)) {
        ESP_LOGE(TAG, "psa_mac_compute failed: %ld", (long)status);
        return ESP_ERR_INVALID_STATE;
    }

    for (int i = 0; i < 32; i++)
        sprintf(out_hex + i * 2, "%02x", hash_value[i]);
    out_hex[64] = '\0';
    return ESP_OK;
}

/* 开机自检：用华为云文档的已知向量验证 HMAC-SHA256 实现是否正确。
 * HMAC-SHA256(key="2025041401", msg="12345678") 应等于下面这个值 */
static esp_err_t cloud_hmac_self_check(void)
{
    static const char expect[] = "c75150e6cb841417396819e4d2ee4358a416344a03a083e3a8567074ddec820a";
    char out[65] = {0};
    cloud_sha256_value_generate("2025041401", "12345678", out);
    if (strcmp(out, expect) != 0) 
    {
        ESP_LOGE(TAG, "HMAC selftest FAILED: 检查 cloud_sha256_value_generate 的 key/msg 顺序");
        return ESP_FAIL;
    } 
    else 
    {
        ESP_LOGI(TAG, "HMAC selftest OK: 算法正确，若仍认证失败请核对 CLOUD_DEVICE_SCRECT_PASSWORD 和时间");
        return ESP_OK;
    }
}

/* 生成 UTC 的 YYYYMMDDHH 时间戳。必须用 gmtime_r（UTC），
 * 不能用 localtime_r —— 因为 main.c 里 setenv("TZ","CST-8") 会 +8 小时 */
static void cloud_get_utc_timestamp(char *out, size_t out_len)
{
    time_t now = time(NULL);
    struct tm t;
    gmtime_r(&now, &t);
    strftime(out, out_len, "%Y%m%d%H", &t);   // 输出 YYYYMMDDHH
}

/* 等 SNTP 同步完成（年份 > 2024 视为已同步），RTC 上电默认是 1970 年 */
static bool cloud_wait_until_time_synced(uint32_t timeout_ms)
{
    uint32_t waited = 0;
    while (waited < timeout_ms)
    {
        time_t now = time(NULL);
        struct tm t; gmtime_r(&now, &t);
        if (t.tm_year + 1900 >= 2025) return true;
        vTaskDelay(pdMS_TO_TICKS(500));
        waited += 500;
    }
    ESP_LOGW(TAG, "SNTP 未在 %lu ms 内同步，跳过本轮 MQTT 连接", (unsigned long)timeout_ms);
    return false;
}

base_cloud_hash_individual_t* user_register_cloud_function(void)
{
    static cloud_hash_variable_t inst;
    memset(&inst, 0, sizeof(inst));
    inst.base.name                     = "esp32p4";
    inst.base.cloud_hash_token_generate = cloud_sha256_value_generate;
    inst.base.cloud_hash_token_check = cloud_hmac_self_check;   

    base_cloud_hash_individual_t *dev = &inst.base;
    assert(dev->cloud_hash_token_generate && dev->cloud_hash_token_check);
    return dev;
}