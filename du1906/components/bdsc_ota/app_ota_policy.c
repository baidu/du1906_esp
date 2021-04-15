/*
 * Copyright (c) 2020 Baidu.com, Inc. All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); 
 * you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on
 * an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 */
#include <sys/unistd.h>
#include <sys/stat.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <strings.h>
#include "esp_log.h"
#include "errno.h"
#include "http_raw_stream.h"
#include "audio_mem.h"
#include "audio_element.h"
#include "esp_system.h"
#include "esp_http_client.h"
#include "bdsc_engine.h"
#include "bdsc_tools.h"
#include "app_ota_policy.h"
#include "audio_thread.h"
#include "app_task_register.h"

static const char *TAG = "OTA_POLICY";

char* create_ota_download_status_string(int code);
int ota_timeout_checkpoint(int delay)
{
    char *dl_timeout = NULL;
    ESP_LOGI(TAG, "++++++ ota_timeout_checkpoint: %d", (xTaskGetTickCount() - g_bdsc_engine->ota_time_begin)/1000);
    if (g_bdsc_engine->ota_download_st == OTA_DOWNLOAD_STAGE_INIT && (xTaskGetTickCount() - g_bdsc_engine->ota_time_begin)/1000 + delay > OTA_DOWNLOAD_MID_WAIT_TIME) {
        ESP_LOGI(TAG, "++++++ enter  OTA_DOWNLOAD_STAGE_MID");
        g_bdsc_engine->ota_download_st = OTA_DOWNLOAD_STAGE_MID;
        bdsc_play_hint(BDSC_HINT_OTA_BAD_NET_REPORT);
        return ESP_OK;
    } else if (g_bdsc_engine->ota_download_st == OTA_DOWNLOAD_STAGE_MID && (xTaskGetTickCount() - g_bdsc_engine->ota_time_begin)/1000 + delay > OTA_DOWNLOAD_TOTAL_WAIT_TIME) {
        ESP_LOGI(TAG, "++++++ enter  OTA_DOWNLOAD_STAGE_END");
        g_bdsc_engine->ota_download_st = OTA_DOWNLOAD_STAGE_END;
        
        if ((dl_timeout = create_ota_download_status_string(-1))) {
            bdsc_engine_channel_data_upload((uint8_t*)dl_timeout, strlen(dl_timeout) + 1);
            free(dl_timeout);
        }
        return ESP_FAIL;
    } else {
        return ESP_OK;
    }
}


static int g_exponent_backoff_delay;
static int g_exponent_backoff_limit;
void ota_exponent_backoff_init(int limit)
{
    g_exponent_backoff_delay = 1;
    g_exponent_backoff_limit = limit;
}

int ota_exponent_backoff_get_next_delay()
{
    if (g_exponent_backoff_delay >= g_exponent_backoff_limit) {
        return g_exponent_backoff_delay;
    }
    return (g_exponent_backoff_delay = g_exponent_backoff_delay * 2);
}


static void ota_monitor_task(void *para)
{
    int cnt = 0;
    int percent = 0;
    char *dl_process = NULL;
    while (1) {
        vTaskDelay(1 * 1000 / portTICK_PERIOD_MS);
        cnt++;

        if (!g_bdsc_engine->in_ota_process_flag || g_bdsc_engine->ota_fail_flag) {
            // do some cleaning
            g_bdsc_engine->ota_read_bytes = 0;
            g_bdsc_engine->ota_total_bytes = 0;
            break;
        }
        if (!(cnt % 10)) {
            if (g_bdsc_engine->ota_total_bytes) {
                percent = g_bdsc_engine->ota_read_bytes * 100 / g_bdsc_engine->ota_total_bytes;
            }
            if ((dl_process = create_ota_download_status_string(percent))) {
                bdsc_engine_channel_data_upload((uint8_t*)dl_process, strlen(dl_process) + 1);
                free(dl_process);
            }
        }
    }
    vTaskDelete(NULL);
}

void start_ota_monitor(void)
{
    int ret = app_task_regist(APP_TASK_ID_OTA_MONITOR, ota_monitor_task, NULL, NULL); 
    if (ret == ESP_FAIL) {
        ESP_LOGE(TAG, "Couldn't create ota_monitor_task");
    }
}
