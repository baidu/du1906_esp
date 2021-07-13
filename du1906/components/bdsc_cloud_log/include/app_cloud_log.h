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
#ifndef __APP_CLOUD_LOG_H__
#define __APP_CLOUD_LOG_H__

#include "esp_log.h"
#include "math.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio_mem.h"
#include <string.h>
#include "ringbuf.h"
#include "cJSON.h"
#include "bdsc_tools.h"
#include <sys/time.h>
#include "bdsc_engine.h"
#include "bdsc_http.h"
#include "app_cloud_log.h"
#include "audio_thread.h"
#include "rom/rtc.h"
#include "audio_mutex.h"

typedef int (*cloud_send_log_func)(char *msg, uint32_t len);
typedef int (*cloud_channel_init)(void);

typedef struct {
    int                 cloud_local_level;
    char                *msg_buff;
    char                *p_line_buff;
    cloud_channel_init  pCloud_log_init;
    cloud_send_log_func pSend_func;
    ringbuf_handle_t    ringbuf_handle;
    QueueHandle_t       g_upload_log_queue;
    bool                initialized;
} cloud_log_data_t;

typedef enum {
    LOG_CHANNEL_TYPE_HTTP,
    LOG_CHANNEL_TYPE_HTTPS,
    LOG_CHANNEL_TYPE_MQTT,
    LOG_CHANNEL_TYPE_MQTTS,
} channel_type_t;

typedef enum {
    CLOUD_LOG_NONE,
    CLOUD_LOG_ERROR,
    CLOUD_LOG_WARN,
    CLOUD_LOG_INFO,
    CLOUD_LOG_DEBUG,
    CLOUD_LOG_VERBOSE
} cloud_log_level_t;

typedef struct {
    channel_type_t      type;
    cloud_log_level_t   level;
} cloud_log_cfg_t;

void set_cloud_log_level(esp_log_level_t level);
int app_cloud_log_task_init(cloud_log_cfg_t *cfg);
bool app_cloud_log_task_check_inited();

// channels
int mqtt_channel_init(void);
int mqtt_send_log(char *msg, uint32_t len);

int http_channel_init(void);
int http_send_log(char *msg, uint32_t len);

int https_channel_init(void);
int https_send_log(char *msg, uint32_t len);

extern int g_cloud_log_ring_buf_sz;

// global log level config
esp_err_t uart_print_on();
esp_err_t uart_print_off();
esp_err_t cloud_print_on();
void start_log_upload(esp_log_level_t level);
#endif
