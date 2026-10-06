#include "time_sntp.h"
#include "esp_log.h"

#define TAG "time sntp"

static void sntp_finish_callback(struct timeval *tv)
{
    struct tm t;
    localtime_r(&tv -> tv_sec,&t);
    //set_clock_time(&guider_ui,t.tm_year + 1900,t.tm_mon + 1,
        //t.tm_mday,t.tm_wday,t.tm_hour,t.tm_min,t.tm_sec);
}


void time_sntp_init(void)
{
    if(!esp_sntp_enabled())
    {
        ESP_LOGI(TAG,"start to time sntp");
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);//设置工作模式

        esp_sntp_setservername(0,"ntp1.ntsc.ac.cn");
        esp_sntp_setservername(1,"ntp1.cstnet.cn");
        esp_sntp_setservername(2,"cn.pool.ntp.org");

        esp_sntp_set_time_sync_notification_cb(sntp_finish_callback);
        esp_sntp_init();
    }
    setenv("TZ", "CST-8", 1);
    tzset();
}



