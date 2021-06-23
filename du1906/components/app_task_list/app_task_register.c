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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "audio_mem.h"
#include "audio_thread.h"
#include "bdsc_tools.h"
#include "app_task_register.h"

#define TAG "TASK_REG"

#define     CORE0           (0)
#define     CORE1           (1)
#define     NO_AFFINITY     (-1)

typedef struct {
    int         task_id;
    char        *task_name;
    int         core_id;
    int         stack_sz;
    task_func_t func;
    int         prioriry;
    void        *arg;
} task_entry_t;

task_entry_t g_registed_task_list[] = {
    /*   global task id         task name           core id        stack size     task func   priotiry      arg*/
    {APP_TASK_ID_OTA,           "_ota_task",        NO_AFFINITY,    3072,           NULL,       10,         NULL},
    /*
     * auth_task Must keep task stack in internal RAM, because we will
     * operate external FLASH in auth_task.
     * 
     * 8K: https need more task heap size
     */
    {APP_TASK_ID_AUTH,          "auth_task",        NO_AFFINITY,    (1024 * 8),     NULL,       10,         NULL},

    /*
     * "app_music_task" and "next_music_task" must in different core
     * beacause it's going to pause 1&2 second when play url music, so request next song task run core 1, play task run core 0
     */
    {APP_TASK_ID_APP_MUSIC,     "app_music_task",     CORE0,       (1024 * 6),      NULL,       1,         NULL},
    {APP_TASK_ID_NEXT_MUSIC,    "next_music_task",    CORE1,       (1024 * 8),      NULL,       1,         NULL},
    {APP_TASK_ID_UPLOAD_INFO,   "upload_info_task",   CORE1,       (1024 * 4),      NULL,       1,         NULL},
    {APP_TASK_ID_GET_URL,       "get_url_by_id_task", CORE1,       (1024 * 8),      NULL,       1,         NULL},

    {APP_TASK_ID_IOT_MQTT,      "iot_mqtt",           CORE1,       (1024 * 4),      NULL,       10,        NULL},
    {APP_TASK_ID_CLOUD_LOG,     "app_cloud_log_task", CORE0,       (1024 * 10),     NULL,       1,         NULL},
    {APP_TASK_ID_AUDIO_MANAGER, "audio_manager_task", CORE0,       (1024 * 3),      NULL,       13,         NULL},
    {APP_TASK_ID_OTA_MONITOR,   "ota_monitor_task",   CORE1,       (1024 * 3),      NULL,       1,         NULL},
    {APP_TASK_ID_SYS_MONITOR,   "sys_monitor_task",   CORE1,       (1024 * 3),      NULL,       1,         NULL},
    /*
     * wk_task priority must be higher than IN_http
     */
    {APP_TASK_ID_WK,            "wk_task",            CORE0,       (1024 * 4),      NULL,       13,         NULL},
    {APP_TASK_ID_SOP,           "sop_task",           CORE0,       (1024 * 4),      NULL,       13,         NULL},
    {APP_TASK_ID_BDSC_ENGINE,   "engine_task",        CORE0,       (1024 * 8),      NULL,       10,         NULL},
    {APP_TASK_ID_DSP_FATAL_ERROR, "dsp_fatal",      NO_AFFINITY,    (1024 * 4),     NULL,       10,         NULL},
};

int app_task_regist(app_task_id_t id, task_func_t func, void *arg, xTaskHandle *created_task)
{
    if (id < 0 || id > APP_TASK_ID_MAX) {
        ERR_OUT(ERR_RET, "no task id found");
    }

    g_registed_task_list[id].func = func;
    g_registed_task_list[id].arg = arg;
    switch (g_registed_task_list[id].core_id) {
    case CORE0:
    case CORE1:
        return audio_thread_create(created_task, \
                                g_registed_task_list[id].task_name, \
                                g_registed_task_list[id].func, \
                                g_registed_task_list[id].arg, \
                                g_registed_task_list[id].stack_sz, \
                                g_registed_task_list[id].prioriry, true, \
                                g_registed_task_list[id].core_id);
    case NO_AFFINITY:
        return xTaskCreate(g_registed_task_list[id].func, \
                        g_registed_task_list[id].task_name, \
                        g_registed_task_list[id].stack_sz, \
                        g_registed_task_list[id].arg, \
                        g_registed_task_list[id].prioriry, \
                        created_task);
    default:
        ERR_OUT(ERR_RET, "invalid core id");
    }

    return 0;
ERR_RET:
    return -1;
}
