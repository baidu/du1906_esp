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
#include "app_cloud_log.h"
#include "app_task_register.h"

#define TAG "cloud_LOG_TASK"

#define LOG_UPLOAD_TIMEOUT  (10*1000)
#define MQTT_CH_RING_BUFF_SIZE  (30 * 1024)        //mqtt public max length 32KB
#define HTTP_CH_RING_BUFF_SIZE  (40 * 1024)
#define RING_BUFF_MINI_REMAIN   (0.1f)            // 10% Minimum remaining percentage
#define LINE_LOG_MAX_SIZE       (2*1024)
#define LOG_QUEUE_LEN           (2*1024)
#define APP_CLOUD_LOG_TASK_STACK_SZ (10*1024)

static cloud_log_data_t *pCloud_log_data = NULL;
static char *last_coredump_buff = NULL;
static SemaphoreHandle_t g_vprintf_lock = NULL;
int g_cloud_log_ring_buf_sz = -1;

typedef struct {
    char    *buf;
    size_t  buf_len;
} log_elem_t;

void set_cloud_log_level(esp_log_level_t level)
{
    if(level > ESP_LOG_VERBOSE)
        ESP_LOGE(TAG, "set_cloud_log_level fail,set level value=%d", level);  
    // if(level > 0)
    //     level = level - 1;   //sync level with sdk
        pCloud_log_data->cloud_local_level = level;
}

void cloud_log_vprintf(esp_log_level_t level,const char *fmt, va_list ap)
{
    // NOTICE: dont use ESP_LOGx in this function, or it will cause stack overflow
    int cnt = 0;
    if(!pCloud_log_data || !pCloud_log_data->initialized || pCloud_log_data->cloud_local_level < level)
        return;
    
    if (uxQueueSpacesAvailable(pCloud_log_data->g_upload_log_queue) == 0) {
        //printf("g_upload_log_queue full! quit");
        return;
    }
    xSemaphoreTake(g_vprintf_lock, portMAX_DELAY);
    cnt = vsnprintf(pCloud_log_data->p_line_buff, LINE_LOG_MAX_SIZE, fmt, ap);

    log_elem_t mlog;
    mlog.buf = audio_strdup(pCloud_log_data->p_line_buff);
    mlog.buf_len = cnt;

    xSemaphoreGive(g_vprintf_lock);
    if (pdTRUE != xQueueSend(pCloud_log_data->g_upload_log_queue, (void *)&mlog, 0)) {
        //printf("g_upload_log_queue send fail!");
        audio_free(mlog.buf);
    }
}
__attribute__((weak))  esp_err_t esp_core_dump_get_nvs_data(char **buf)
{
    ESP_LOGE(TAG, "can't get last coredump log, if you want to use this feature, please updating the IDF and setting menuconfig coredump to nvs");
    return ESP_FAIL;
}

static int cloud_log_init(void *parms)
{
    int ret =0;
    cloud_log_cfg_t *cfg = (cloud_log_cfg_t*)parms;

    pCloud_log_data = audio_calloc(1, sizeof(cloud_log_data_t));
    if(pCloud_log_data == NULL) {
        ret = -1;
        ERR_OUT(cloud_data_fail, "cloud_data_fail");
    }

    switch (cfg->type) {
    case LOG_CHANNEL_TYPE_HTTP:
        g_cloud_log_ring_buf_sz = HTTP_CH_RING_BUFF_SIZE;
        pCloud_log_data->pCloud_log_init = http_channel_init;
        pCloud_log_data->pSend_func = http_send_log;
        break;
    case LOG_CHANNEL_TYPE_HTTPS:
        g_cloud_log_ring_buf_sz = HTTP_CH_RING_BUFF_SIZE;
        pCloud_log_data->pCloud_log_init = https_channel_init;
        pCloud_log_data->pSend_func = https_send_log;
        break;
    case LOG_CHANNEL_TYPE_MQTT:
        g_cloud_log_ring_buf_sz = MQTT_CH_RING_BUFF_SIZE;
        pCloud_log_data->pCloud_log_init = mqtt_channel_init;
        pCloud_log_data->pSend_func = mqtt_send_log;
        break;
    case LOG_CHANNEL_TYPE_MQTTS:
        // not supported yet
        g_cloud_log_ring_buf_sz = MQTT_CH_RING_BUFF_SIZE;
        pCloud_log_data->pCloud_log_init = NULL;
        pCloud_log_data->pSend_func = NULL;
        break;
    default:
        break;
    }

    pCloud_log_data->p_line_buff = audio_calloc(1, LINE_LOG_MAX_SIZE);
    if(pCloud_log_data->p_line_buff == NULL) {
        ret = -2;
        ERR_OUT(line_buff_fail, "line_buff_fail");
    }

    pCloud_log_data->ringbuf_handle = rb_create(g_cloud_log_ring_buf_sz, 1);
    if(pCloud_log_data->ringbuf_handle == NULL) {
        ret = -3;
        ERR_OUT(ringbuf_handle_fail, "ringbuf_handle_fail");
    }
        
    pCloud_log_data->msg_buff = audio_calloc(1, g_cloud_log_ring_buf_sz+1);
    if(pCloud_log_data->msg_buff == NULL) {
        ret = -4;
        ERR_OUT(log_data_fail, "log_data_fail");
    }

    StaticQueue_t *log_queue_buffer = audio_calloc(1, sizeof(StaticQueue_t));
    assert(log_queue_buffer != NULL);
    uint8_t *log_queue_storage = audio_calloc(1, (LOG_QUEUE_LEN * sizeof(log_elem_t)));
    assert(log_queue_storage != NULL);
    pCloud_log_data->g_upload_log_queue = xQueueCreateStatic(LOG_QUEUE_LEN,
                                    sizeof(log_elem_t),
                                    (uint8_t *)log_queue_storage,
                                    log_queue_buffer);
    assert(pCloud_log_data->g_upload_log_queue != NULL);

    g_vprintf_lock = xSemaphoreCreateMutex();

    if(pCloud_log_data->pCloud_log_init != NULL) {
        if(pCloud_log_data->pCloud_log_init()) {
            ret = -5;
            ERR_OUT(cloud_channel_init_fail, "https_channel_init_fail");
        }
    }
    pCloud_log_data->cloud_local_level = cfg->level;
    pCloud_log_data->initialized = true;
    return ret;

cloud_channel_init_fail:
    audio_free(pCloud_log_data->msg_buff);
    pCloud_log_data->msg_buff = NULL;
log_data_fail:
    audio_free(pCloud_log_data->ringbuf_handle);
    pCloud_log_data->ringbuf_handle = NULL;
ringbuf_handle_fail:
    audio_free(pCloud_log_data->p_line_buff);
    pCloud_log_data->p_line_buff = NULL;
line_buff_fail:
    audio_free(pCloud_log_data);
    pCloud_log_data = NULL;
cloud_data_fail:
    return ret;
}

bool g_send_overtime_flag = false;
TimerHandle_t timer_handle = NULL;
#define MAX_INTERVAL_TIME (10*60*1000/portTICK_PERIOD_MS)
void send_cloud_overtime_callback( TimerHandle_t xTimer )
{
    g_send_overtime_flag = true;
}

int64_t sys_get_time_ms(void);
static void app_cloud_log_task(void *pvParameters)
{
    uint32_t used_length = 0; 
    int ret = cloud_log_init(pvParameters);
    if(ret){
        ERR_OUT(exit, "cloud_log_init fail,code:%d\n", ret);
    }

    // if boot from crash, get dump info.
    if ((rtc_get_reset_reason(0) != POWERON_RESET) &&
        (last_coredump_buff != NULL)) {

            ESP_LOGE(TAG, "the coredump infomation last time:%s",last_coredump_buff);
            rb_write(pCloud_log_data->ringbuf_handle, "the coredump infomation last time:", strlen("the coredump infomation last time:"), 0);
            rb_write(pCloud_log_data->ringbuf_handle, last_coredump_buff, strlen(last_coredump_buff), 0);
            rb_write(pCloud_log_data->ringbuf_handle, "\n", strlen("\n"), 0);
            free(last_coredump_buff);
            last_coredump_buff = NULL;
    }

    log_elem_t mlog;
    int64_t before, after;
    while (1) {
        if (xQueueReceive(pCloud_log_data->g_upload_log_queue, &mlog, portMAX_DELAY) == pdPASS) {

            if (pCloud_log_data->ringbuf_handle != NULL){
                rb_write(pCloud_log_data->ringbuf_handle, mlog.buf, mlog.buf_len, 0);
                audio_free(mlog.buf);
            }
            if (rb_bytes_available(pCloud_log_data->ringbuf_handle) < (g_cloud_log_ring_buf_sz * RING_BUFF_MINI_REMAIN) || g_send_overtime_flag){
                // under water mark, sending
                used_length = rb_bytes_filled(pCloud_log_data->ringbuf_handle);
                if (used_length != 0 && pCloud_log_data->msg_buff != NULL) {
                    used_length = rb_read(pCloud_log_data->ringbuf_handle, pCloud_log_data->msg_buff, used_length, 0);
                    before = sys_get_time_ms();
                    pCloud_log_data->pSend_func(pCloud_log_data->msg_buff, used_length);
                    after = sys_get_time_ms();
                    printf("after: %llu ms, ++++use time: %llu ms\n", sys_get_time_ms(), after - before);
                    if (after - before >= LOG_UPLOAD_TIMEOUT) {
                        // weak network, flush queue, release system pressure
                        int recv_cnt =  LOG_QUEUE_LEN - uxQueueSpacesAvailable(pCloud_log_data->g_upload_log_queue);
                        memset(&mlog, 0, sizeof(log_elem_t));
                        int i = 0;
                        printf("flush log queue! recvd: %d\n", recv_cnt);
                        for (i = 0; i < recv_cnt; i++) {
                            if (xQueueReceive(pCloud_log_data->g_upload_log_queue, &mlog, 0) == pdPASS) {
                                audio_free(mlog.buf);
                            }
                        }
                    }
                }
                g_send_overtime_flag = false;
                xTimerReset(timer_handle, 0);
                ESP_LOGW(TAG, "cloud msg log: %d\r", used_length);
            }
        }
    }
exit:
    vTaskDelete(NULL);
}

int app_cloud_log_task_init(cloud_log_cfg_t *p_cfg)
{
    static cloud_log_cfg_t s_cfg;

    timer_handle = xTimerCreate("overtime",MAX_INTERVAL_TIME, pdTRUE,0,send_cloud_overtime_callback);
    if(timer_handle == NULL) {
        ESP_LOGE(TAG, "Couldn't create cloud log timer_handle");
        return -1;
    }
    s_cfg = *p_cfg;
    int ret = app_task_regist(APP_TASK_ID_CLOUD_LOG, app_cloud_log_task, &s_cfg, NULL); 
    if (ret == ESP_FAIL) {
        ESP_LOGE(TAG, "Couldn't create app_cloud_log_task_init");
    }
    esp_core_dump_get_nvs_data(&last_coredump_buff);
    return 0;
}
