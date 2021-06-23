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
#ifndef __PLAY_LIST__H
#define __PLAY_LIST__H

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
#include "app_music.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_PLS_LENGTH  (3)

typedef enum {
    RUNNING_STATE,
    STOP_STATE,
    PAUSE_STATE,
    FORCE_PAUSE_STATE,
    UNKNOW_STATE,
} music_play_state_t;

typedef int (*music_user_cb)(void *ctx);

typedef struct _music {
    music_type_t        type;
    void                *data;
    int                 duration;
    uint8_t             is_big_data_analyse;
    music_play_state_t  play_state;
    action_type_t       action_type;
    music_user_cb       user_cb;
    struct _music       *next;
} music_t;

typedef struct {
    music_t *pls_head;
} pls_handle_t;

extern pls_handle_t* g_pls_handle;

pls_handle_t* pls_create();

int pls_destroy(pls_handle_t *handle);

int pls_get_length(pls_handle_t *handle);

int pls_cache_music(pls_handle_t *handle, music_queue_t pQueue_data);

music_t* pls_change_to_next_music(pls_handle_t *handle);

void pls_clean_list(pls_handle_t *handle);

int pls_add_music_to_tail(pls_handle_t *handle, music_queue_t pQueue_data);

int pls_add_music_to_head(pls_handle_t *handle, music_queue_t pQueue_data);

music_t* pls_get_current_music(pls_handle_t *handle);

music_t* pls_get_second_music(pls_handle_t *handle);

int pls_delete_second_music(pls_handle_t *handle);

int pls_delete_head_music(pls_handle_t *handle);

int pls_set_current_music_player_state(pls_handle_t *handle, music_play_state_t state);

music_play_state_t pls_get_current_music_player_state(pls_handle_t *handle);

void pls_dump(pls_handle_t *handle);
#ifdef __cplusplus
}
#endif

#endif