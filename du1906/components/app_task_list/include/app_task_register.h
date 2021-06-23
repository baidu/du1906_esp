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
#ifndef _TASK_REGISTER_H__
#define _TASK_REGISTER_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_TASK_ID_OTA,
    APP_TASK_ID_AUTH,
    APP_TASK_ID_APP_MUSIC,
    APP_TASK_ID_NEXT_MUSIC,
    APP_TASK_ID_UPLOAD_INFO,
    APP_TASK_ID_GET_URL,
    APP_TASK_ID_IOT_MQTT,
    APP_TASK_ID_CLOUD_LOG,
    APP_TASK_ID_AUDIO_MANAGER,
    APP_TASK_ID_OTA_MONITOR,
    APP_TASK_ID_SYS_MONITOR,
    APP_TASK_ID_WK,
    APP_TASK_ID_SOP,
    APP_TASK_ID_BDSC_ENGINE,
    APP_TASK_ID_DSP_FATAL_ERROR,
    APP_TASK_ID_MAX
} app_task_id_t;

typedef void (*task_func_t)(void* arg);

int app_task_regist(app_task_id_t id, task_func_t func, void *arg, xTaskHandle *created_task);

#ifdef __cplusplus
}
#endif

#endif