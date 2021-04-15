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

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    URL_MUSIC,
    ID_MUSIC,
    ALL_TYPE,
} music_type_t;

typedef enum {
    PLAY_MUSIC,
    NEXT_MUSIC,
    CACHE_MUSIC,
    UNKNOW_ACTION,
} music_action_t;

typedef enum _music_play_state {
    RUNNING_STATE,
    STOP_STATE,
    PAUSE_STATE,
    UNKNOW_STATE,
} music_play_state_t;

typedef struct _music_queue {
    music_action_t  action;
    music_type_t    type;
    char            *data;
    char            *action_type;
} music_queue_t;

extern xQueueHandle g_music_queue_handle;
void set_music_player_state(music_play_state_t state);
music_play_state_t get_music_player_state();
int https_post_to_unit_server(const char *uri, const char *private_body_str, char **ret_data_out, size_t *data_out_len);
#ifdef __cplusplus
}
#endif

#endif