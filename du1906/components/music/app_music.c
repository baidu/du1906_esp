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
#include "string.h"
#include "audio_mem.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio_thread.h"
#include "migu.h"
#include "raw_play_task.h"
#include "audio_player.h"
#include "audio_player_type.h"
#include "cJSON.h"
#include "audio_player.h"
#include "freertos/queue.h"
#include "bdsc_json.h"
#include "audio_def.h"
#include "freertos/timers.h"
#include "bdsc_tools.h"
#include "bdsc_engine.h"
#include "bdsc_http.h"
#include "math.h"
#include "app_voice_control.h"
#include "bds_private.h"
#include "audio_tone_uri.h"
#include "app_task_register.h"
#include "app_music.h"
#include "play_list.h"
#include "migu_music_service.h"
#include "migu_sdk_helper.h"

#define TAG "MUSIC_TASK"
static audio_thread_t next_song_task_handle = NULL;
static TimerHandle_t next_song_timer_handle = NULL;
#define SILENT_NEXT_TIME_EARLY      (15 * 1000)
#define MUSIC_QUEUE_ITEM_NUM        256
bool g_app_music_init_finish_flag = false;
xQueueHandle g_music_queue_handle = NULL;

pls_handle_t* g_pls_handle = NULL;

int active_music_license()
{
    int ret = 0;
#ifdef CONFIG_MIGU_MUSIC
    ret = migu_active_music_license();
#endif
    return ret;
}

void app_music_play_policy(music_queue_t pQueue_data);

void next_song_callback(TimerHandle_t xTimer)
{
    music_t* current_music = pls_get_current_music(g_pls_handle);
    if (current_music == NULL) {
        return;
    }
    ESP_LOGI(TAG, "send next_task_handle notify");
/*
* why not request for the next song directly
* beacause it's going to pause 1&2 second when play url music, so request next song task run core 1, play task run core 2
*/
    xTaskNotifyGive(next_song_task_handle);
}

int next_song_user_cb(void *ctx)
{
    music_t* current_music = (music_t*)ctx;
    if(current_music && current_music->is_big_data_analyse) {       //migu music need big data analysis
        migu_big_data_event();
    }
    if (ESP_ERR_AUDIO_NO_ERROR == audio_player_duration_get(&current_music->duration)) {
        ESP_LOGE(TAG, "%s|%d: audio_player_duration_get = %d!!!", __func__, __LINE__,current_music->duration);
    } else {
        ESP_LOGE(TAG, "%s|%d: audio_player_duration_get no player instance!!!", __func__, __LINE__);
    }
    if (next_song_timer_handle != NULL) {
        if(current_music->duration > SILENT_NEXT_TIME_EARLY) {
            xTimerChangePeriod(next_song_timer_handle, (current_music->duration - SILENT_NEXT_TIME_EARLY) / portTICK_PERIOD_MS, 0);
        } else {
            xTimerChangePeriod(next_song_timer_handle, 1 + current_music->duration / portTICK_PERIOD_MS, 0);
        }
        xTimerStart(next_song_timer_handle,0);
    } else {
        ESP_LOGE(TAG, "%s|%d: next_music_timer_handle create fail!!!", __func__, __LINE__);
    }
    pls_set_current_music_player_state(g_pls_handle, RUNNING_STATE);

    return 0;
}

void next_song_task(void *pvParameters)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGI(TAG, "next song request");
        migu_request_next_music();  //request next songs by migu_request_next_music for historical reasons at the moment
        vTaskDelay(100);
    }
    vTaskDelete(NULL);
}

static music_type_t get_action_type(char *type)
{
    if (!type) {
        return ERR_TYPE;
    }
    if (!strcmp(type, "asrnlp_tts")) {
        return RAW_TTS;
    // } else if (!strcmp(type, "asrnlp_url")) {
    //     return ONLY_URL;
    } else if (!strcmp(type, "asrnlp_mix")) {
        return RAW_MIX;
    } else if (!strcmp(type, "asrnlp_ttsurl")) {
        return TTS_URL;
    }
    return ERR_TYPE;
}

static void pause_current_http_music_if_needed()
{
    audio_player_state_t st = {0};
    audio_player_state_get(&st);
    ESP_LOGI(TAG, "Playing media is 0x%x, status is %d", st.media_src, st.status);
    if ((st.media_src == MEDIA_SRC_TYPE_MUSIC_HTTP) &&
        (st.status == AUDIO_PLAYER_STATUS_RUNNING)) {
        handle_play_cmd(CMD_HTTP_PLAY_PAUSE, NULL, 0);
        pls_set_current_music_player_state(g_pls_handle, PAUSE_STATE);
    }
}

static void app_music_task(void *pvParameters)
{
    music_queue_t pQueue_data;

    vTaskDelay(2000);
    if (!g_bdsc_engine->g_vendor_info->is_active_music_license) {
        ESP_LOGW(TAG, "start active music license");
        if (!active_music_license()) {
            g_bdsc_engine->g_vendor_info->is_active_music_license = 1;
            if (custom_key_op_safe(CUSTOM_KEY_SET, CUSTOM_KEY_TYPE_INT32, \
                NVS_DEVICE_SYS_NAMESPACE, PROFILE_NVS_KEY_IS_ACTIVE_MUSIC_LICENSE, \
                &g_bdsc_engine->g_vendor_info->is_active_music_license, NULL) < 0) {
                ESP_LOGE(TAG, "active music license profile save fail!!");
            }
        }
    }
    while (1) {
        if (g_music_queue_handle && xQueueReceive(g_music_queue_handle, &pQueue_data, portMAX_DELAY) == pdPASS) {
            switch (pQueue_data.type) {
            case URL_MUSIC:
                ESP_LOGE(TAG, "===================> recv ID/URL music");
                if (pQueue_data.action == CACHE_MUSIC) {       // 缓存下一首
                    pls_cache_music(g_pls_handle, pQueue_data);
                    continue;
                } else if (pQueue_data.action == CHANGE_TO_NEXT_MUSIC ||  // 播放 migu 的过程中说 “下一首”
                            pQueue_data.action == PLAY_MUSIC) {           // 一开始说“播放周杰伦的哥”
                    pls_clean_list(g_pls_handle);
                    pls_add_music_to_tail(g_pls_handle, pQueue_data);
                }
                break;
            case ALL_TYPE:
                ESP_LOGE(TAG, "===================> recv ALL_TYPE");
                /*
                 * ALL_TYPE 有两种 action:
                 * 1.  NEXT_MUSIC: 表示 播放结束了的
                 * 2.  PLAY_MUSIC: 表示 需要续播的
                 */
                if (pQueue_data.action == NEXT_MUSIC) {    // 播放列表，一首播放完，自动播放下一首
                    ESP_LOGI(TAG, "delete played music");
                    pls_delete_head_music(g_pls_handle);
                }
                break;
            case SPEECH_MUSIC:
                ESP_LOGE(TAG, "===================> recv SPEECH_MUSIC");
                pls_add_music_to_head(g_pls_handle, pQueue_data);
                break;
            case RAW_TTS_DATA:
                ESP_LOGE(TAG, "===================> recv RAW_TTS_DATA");
                break;
            case TONE_MUSIC:
                ESP_LOGE(TAG, "===================> recv TONE_MUSIC");
                /* some randonly poped out music, such as bt_connected.
                 * should pause previous http play if necessary
                 */
                pause_current_http_music_if_needed();
                pls_add_music_to_head(g_pls_handle, pQueue_data);
                break;
            case MQTT_URL:
                ESP_LOGE(TAG, "===================> recv MQTT_URL");
                pls_clean_list(g_pls_handle);
                pls_add_music_to_head(g_pls_handle, pQueue_data);
                break;
            case A2DP_PLAY:
                ESP_LOGE(TAG, "===================> recv A2DP_PLAY");
                pls_clean_list(g_pls_handle);
                pls_add_music_to_head(g_pls_handle, pQueue_data);
                break;
            case ACTIVE_TTS:
                ESP_LOGE(TAG, "===================> recv ACTIVE_TTS");
                pls_add_music_to_head(g_pls_handle, pQueue_data);
                break;
            case MUSIC_CTL_CONTINUE:
                ESP_LOGE(TAG, "===================> recv MUSIC_CTL_CONTINUE");
                pls_add_music_to_head(g_pls_handle, pQueue_data);
                break;
            case MUSIC_CTL_PAUSE:
                ESP_LOGE(TAG, "===================> recv MUSIC_CTL_PAUSE");
                pls_add_music_to_head(g_pls_handle, pQueue_data);
                break;
            case MUSIC_CTL_STOP:
                ESP_LOGE(TAG, "===================> recv MUSIC_CTL_STOP");
                pls_add_music_to_head(g_pls_handle, pQueue_data);
                break;
            default:
                break;
            }
            
            app_music_play_policy(pQueue_data);

        }
        vTaskDelay(100);
    }
    vTaskDelete(NULL);
}

extern int g_is_getting_url;
int get_url_by_id(unit_data_t *unit_data, music_queue_t *music_data, bool is_async)
{
    char *url_value = NULL;
    char recvd_url[1024] = {0};
    char *id_str = NULL;
    char *custom_reply_value = NULL;

    music_data->type = URL_MUSIC;
    custom_reply_value = unit_data->custom_reply.value;
    if (!custom_reply_value || !custom_reply_value[0]) {
        ERR_OUT(ERR_RET, "custom_reply is null");
    }
    if (is_async) {
        if (g_is_getting_url) {
            ESP_LOGE(TAG, "get url task is running, wait...");
            vTaskDelay(3000);
            if (g_is_getting_url) {
                ESP_LOGE(TAG, "get url task is still running, something went wrong???");
                return -1;
            }
        }
        strncpy(g_migu_music_id_in, custom_reply_value, sizeof(g_migu_music_id_in));
        xEventGroupClearBits(g_get_url_evt_group, GET_URL_REQUEST | GET_URL_SUCCESS | GET_URL_FAIL);
        xEventGroupSetBits(g_get_url_evt_group, GET_URL_REQUEST);
        music_data->data = NULL;
    } else {
        id_str = (char*)custom_reply_value;
        if (migu_get_url_by_id(id_str, recvd_url, sizeof(recvd_url))) {
            ERR_OUT(ERR_RET, "migu_get_url_by_id fail");
        }
        if (!(url_value = audio_strdup(recvd_url))) {
            ERR_OUT(ERR_RET, "malloc fail");
        }
        music_data->data = url_value;
    }

    return 0;
ERR_RET:
    return -1;
}

void get_action_by_intent(unit_data_t *unit_data, music_queue_t *music_data)
{
    if (!strcmp(unit_data->intent, "CHANGE_TO_NEXT")) {
        music_data->action  = CHANGE_TO_NEXT_MUSIC;
    } else if (!strcmp(unit_data->intent, "CACHE_MUSIC")) {
        music_data->action  = CACHE_MUSIC;
    } else {
        music_data->action  = PLAY_MUSIC;
    }
}

void music_queue_policy_send(QueueHandle_t xQueue, const void *pvItemToQueue)
{
    music_t *current_music = pls_get_current_music(g_pls_handle);
    music_queue_t pQueue_data;
    music_queue_t *music_data = (music_queue_t*)pvItemToQueue;

    /*
     * policy 0: if cache music, bypass
     */
    if (current_music && music_data->action == CACHE_MUSIC) {
        ESP_LOGE(TAG, "cache music, bypass");
    }

    /*
     * policy 1: flush all tts packages if necessary
     */
    else if (current_music && music_data->type != RAW_TTS_DATA && 
            (current_music->action_type == RAW_TTS ||
            current_music->action_type == RAW_MIX)) {
        ESP_LOGE(TAG, "!!!! flush music queue !!!!");
        pls_dump(g_pls_handle);
        while (xQueueReceive(xQueue, &pQueue_data, 0) == pdTRUE) {
            raw_data_t *raw = NULL;
            if (pQueue_data.type == RAW_TTS_DATA) {
                raw = (raw_data_t*)pQueue_data.data;
                audio_free(raw->raw_data);
            }
            if (pQueue_data.data) {
                audio_free(pQueue_data.data);
            }
        }
    }

    /*
     * normal send
     */
    xQueueSend(xQueue, pvItemToQueue, 0);
}

void send_music_queue(music_type_t type, void *pdata)
{
    unit_data_t *unit_data = (unit_data_t*)pdata;
    music_queue_t music_data;
    music_play_state_t current_state;
    bool need_async = true;
    
    //ESP_LOGI(TAG, "==> send_music_queue");
    current_state = pls_get_current_music_player_state(g_pls_handle);
    //ESP_LOGD(TAG, "cur state: %d", current_state);

    if (type != ALL_TYPE && !pdata) {
        ERR_OUT(ERR_RET, "pdata is null");
    }
    memset(&music_data, 0, sizeof(music_queue_t));
    music_data.type = type;
    switch (type) {
    case ALL_TYPE:
        if (PAUSE_STATE == current_state) { // paused, need resume
            music_data.action  = PLAY_MUSIC;
        } else {
            music_data.action  = NEXT_MUSIC;
        }
        break;
#ifdef CONFIG_MIGU_MUSIC
    case ID_MUSIC:
        get_action_by_intent(pdata, &music_data);
        if (music_data.action == CACHE_MUSIC) {
            // cache music dont need async mode
            need_async = false;
        }
        if (get_url_by_id(pdata, &music_data, need_async) < 0) {
            ERR_OUT(ERR_RET, "get_url_by_id fail");
        }
        music_data.user_cb = next_song_user_cb; // ID/URL MUSIC need auto cache next song logic
        music_data.is_big_data_analyse = true;  //enable big data analyse
        music_data.action_type = get_action_type(unit_data->action_type);
        break;
#endif
    case URL_MUSIC:
        get_action_by_intent(pdata, &music_data);
        music_data.user_cb = next_song_user_cb; // ID/URL MUSIC need auto cache next song logic
    case SPEECH_MUSIC:
        music_data.action_type = get_action_type(unit_data->action_type);
        if (!music_data.data && unit_data->custom_reply.value) {
            music_data.data = audio_strdup(unit_data->custom_reply.value);
        }
        break;
    case RAW_TTS_DATA:
        music_data.data = pdata;
        break;
    case TONE_MUSIC:
        music_data.data = pdata;
        break;
    case MQTT_URL:
        music_data.data = pdata;
        break;
    case A2DP_PLAY:
        music_data.data = pdata;
        break;
    case ACTIVE_TTS:
        music_data.data = pdata;
        break;
    case MUSIC_CTL_CONTINUE:
        music_data.data = NULL;
        break;
    case MUSIC_CTL_PAUSE:
        music_data.data = NULL;
        break;
    case MUSIC_CTL_STOP:
        music_data.data = NULL;
        break;
    default:
        break;
    }

    ESP_LOGI(TAG, "==> send_music_queue,  action: %d, type: %d, data: %s, action_type:%d", \
        music_data.action, music_data.type, \
        (music_data.data ? music_data.data : "NULL"), \
        music_data.action_type);
    //xQueueSend(g_music_queue_handle, (void*)&music_data, 0);
    music_queue_policy_send(g_music_queue_handle, (void*)&music_data);

ERR_RET:
    return;
}

int app_music_init(void)
{
    int ret = -1;
    audio_thread_t app_music_task_handle = NULL;
    QueueHandle_t *music_queue_buffer = audio_calloc(1, sizeof(StaticQueue_t));
    if (!music_queue_buffer) {
        ERR_OUT(ERR_CALLOC1, "calloc music_queue_buffer fail");
    }
    uint8_t *music_queue_storage = audio_calloc(1, MUSIC_QUEUE_ITEM_NUM * sizeof(music_queue_t));
    if (!music_queue_storage) {
        ERR_OUT(ERR_CALLOC2, "calloc music_queue_storage fail");
    }
    if (!(g_music_queue_handle = xQueueCreateStatic(MUSIC_QUEUE_ITEM_NUM, sizeof(music_queue_t), music_queue_storage, music_queue_buffer))) {
        ERR_OUT(ERR_CREAT, "xQueueCreateStatic fail\n");
    }
    
    ret = app_task_regist(APP_TASK_ID_APP_MUSIC, app_music_task, NULL, &app_music_task_handle);
    if (ret == ESP_FAIL) {
        ERR_OUT(DEL_QUE, "app_music_task create fail");
    }

    if (!(g_pls_handle = pls_create())) {
        ERR_OUT(DEL_MUSIC_TSK, "pls_create fail");
    }

    /* task to auto change next music */
    ret = app_task_regist(APP_TASK_ID_NEXT_MUSIC, next_song_task, NULL, &next_song_task_handle);
    if (ret == ESP_FAIL) {
        ERR_OUT(DEL_PLS, "Couldn't create next_task");
    }

    /* timer to issue next music request */
    next_song_timer_handle = xTimerCreate("next_music_timer", (10 * 1000 / portTICK_PERIOD_MS), pdFALSE, (void *)0, next_song_callback);
    if (!next_song_timer_handle) {
        ERR_OUT(DEL_NEXT_SONG, "Couldn't create next_song_timer");
    }

#ifdef CONFIG_MIGU_MUSIC
    ret = migu_music_service_init();
#endif

    g_app_music_init_finish_flag = true;
    return 0;

DEL_NEXT_SONG:
    vTaskDelete(next_song_task_handle);
DEL_PLS:
    pls_destroy(g_pls_handle);
DEL_MUSIC_TSK:
    vTaskDelete(app_music_task_handle);
DEL_QUE:
    vQueueDelete(g_music_queue_handle);
ERR_CREAT:
    audio_free(music_queue_storage);
ERR_CALLOC2:
    audio_free(music_queue_buffer);
ERR_CALLOC1:
    return -1;
}
