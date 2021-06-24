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
#ifndef __APP_MUSIC__H
#define __APP_MUSIC__H

#include <inttypes.h>
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "freertos/event_groups.h"
#include "esp_event_loop.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <string.h>
#ifdef CONFIG_MIGU_MUSIC
#include "migu_music_service.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLAY_MUSIC,
    NEXT_MUSIC,
    CACHE_MUSIC,
    CHANGE_TO_NEXT_MUSIC,
    UNKNOW_ACTION,
} music_action_t;

typedef enum {
    ID_MUSIC,
    URL_MUSIC,
    ALL_TYPE,
    SPEECH_MUSIC,
    RAW_TTS_DATA,
    TONE_MUSIC,
    MQTT_URL,
    A2DP_PLAY,
    ACTIVE_TTS,
    MUSIC_CTL_CONTINUE,
    MUSIC_CTL_PAUSE,
    MUSIC_CTL_STOP,
} music_type_t;

typedef enum {
    RAW_TTS,
    RAW_MIX,
    TTS_URL,
    ERR_TYPE,
} action_type_t;

typedef struct {
    uint8_t *raw_data;
    size_t  raw_data_len;
    bool    is_end;
} raw_data_t;

#ifndef _music_user_cb_
typedef int (*music_user_cb)(void *ctx);
#endif

typedef struct _music_queue {
    music_action_t  action;
    music_type_t    type;
    char            *data;
    uint8_t         is_big_data_analyse;
    action_type_t   action_type;
    music_user_cb   user_cb;
} music_queue_t;

extern xQueueHandle g_music_queue_handle;

int app_music_init(void);

void send_music_queue(music_type_t type, void *pdata);

#ifdef __cplusplus
}
#endif

#endif
