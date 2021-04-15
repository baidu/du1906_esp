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
#include "migu_music.h"
#include "app_music.h"
#include "bdsc_tools.h"
#include "bdsc_engine.h"
#include "bdsc_http.h"
#include "math.h"
#include "app_voice_control.h"
#include "bds_private.h"
#include "app_task_register.h"

#define TAG "MUSIC_TASK"
#define SILENT_NEXT_TIME_EARLY  (15*1000)
extern const uint8_t server_root_cert_pem_start[] asm("_binary_server_root_cert_pem_start");
extern const uint8_t server_root_cert_pem_end[]   asm("_binary_server_root_cert_pem_end");

xQueueHandle g_music_queue_handle = NULL;
static TimerHandle_t next_music_timer_handle = NULL;
extern bool g_pre_player_need_resume;
typedef struct _music {
    struct _music       *next;
    music_type_t        type;
    char                *url;
    int                 duration;
    music_play_state_t  play_state;
} music_t;

static music_t *current_music = NULL;
audio_thread_t next_task_handle = NULL;

void set_music_player_state(music_play_state_t state)
{
    if (!current_music) {
        return;
    }
    current_music->play_state = state;
}

music_play_state_t get_music_player_state()
{
    if (!current_music) {
        return UNKNOW_STATE;
    }
    return current_music->play_state;
}

music_t* create_music(music_type_t type, char *url)
{
    music_t *node = NULL;
    char *url_value = NULL;

    if (!(node = audio_calloc(1,sizeof(music_t)))) {
        ERR_OUT(ERR_RET, "calloc fail");
    }

    if (!(url_value = audio_strdup(url))) {
        ERR_OUT(ERR_FREE_NODE, "strdup fail");
    }

    node->type = type;
    node->url = url_value;
    node->next = NULL;
    return node;

ERR_FREE_NODE:
    audio_free(node);
ERR_RET:
    return NULL;
}

void delete_music(music_t *node)
{
    if (node == NULL) {
        return;
    }
    if (node->url) {
        audio_free(node->url);
    }
    audio_free(node);
    node = NULL;
}

static music_t* get_music_by_id(char *id)
{
    int ret = 0;
    char recvd_url[1024] = {0};
    
    if (!id) {
        ERR_OUT(ERR_RET, "id is null");
    }
    ret = get_migu_url(id, recvd_url, sizeof(recvd_url));
    if (ret) {
        ERR_OUT(ERR_RET, "get_migu_url fail");
    }

    return create_music(ID_MUSIC, recvd_url);

ERR_RET:
    return NULL;
}

static int generate_unit_post_string_need_free(char **post_buff, const char *uri, const char *private_body_str)
{
    int ts = -1;
    char *tmp_str = "";
    char *sig = NULL;
    int content_length = -1;
    int post_length = -1;
    int cnt = -1;
    
    ts = get_current_valid_ts() / 60;
    if (ts <= 0) {
        ERR_OUT(get_current_time_fail, "get time fail!!!");
    }

    if (private_body_str != NULL) {
        tmp_str = (char *)private_body_str;
    }
    sig = generate_auth_sig_needfree(g_bdsc_engine->g_vendor_info->ak,\
                        ts, g_bdsc_engine->g_vendor_info->sk);
    content_length = strlen("{\"fc\":")+strlen(g_bdsc_engine->g_vendor_info->fc) +strlen("\"\"") +\
                        strlen(",\"pk\":")+strlen(g_bdsc_engine->g_vendor_info->pk) +strlen("\"\"") +\
                        strlen(",\"ak\":")+strlen(g_bdsc_engine->g_vendor_info->ak) +strlen("\"\"") +\
                        strlen(",\"time_stamp\":")+ (int)log10(ts) + 1 +\
                        strlen(",\"signature\":")+strlen(sig) + strlen("\"\"") +\
                        strlen(tmp_str);
                        strlen("}");
    post_length = content_length + 256;
    *post_buff = audio_calloc(1, post_length);
    if (*post_buff == NULL) {
        ERR_OUT(audio_calloc_fail, "audio calloc fail!!!");
    }
    cnt = snprintf(*post_buff, post_length,\
                "POST %s HTTP/1.0\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: %d\r\n\r\n"
                "{\"fc\":\"%s\",\"pk\":\"%s\",\"ak\":\"%s\",\"time_stamp\":%d,\"signature\":\"%s\"%s}",\
                    uri,\
                    content_length + 1,\
                    g_bdsc_engine->g_vendor_info->fc,\
                    g_bdsc_engine->g_vendor_info->pk,\
                    g_bdsc_engine->g_vendor_info->ak,\
                    ts,\
                    sig,\
                    tmp_str);
    if (cnt < 0 || cnt >= post_length) {
        ERR_OUT(snprintf_fail, "snprintf fail!!!");
    }
    free((void*)(sig));
    return 0;

snprintf_fail:
    audio_free(*post_buff);
    *post_buff = NULL;
audio_calloc_fail:
    free((void*)(sig));
get_current_time_fail:
    return -1;
}

int https_post_to_unit_server(const char *uri, const char *private_body_str, char **ret_data_out, size_t *data_out_len)
{
    char *post_buff = NULL;

    if (generate_unit_post_string_need_free(&post_buff, uri, private_body_str)) {
        ERR_OUT(ERR_RET, "%s|%d:generate_post_string_need_free fail", __func__, __LINE__);
    }

    ESP_LOGI(TAG, "%s", post_buff);
    bdsc_send_https_post_sync((char *)g_bdsc_engine->cfg->auth_server, g_bdsc_engine->cfg->auth_port,
                            (char *)server_root_cert_pem_start, server_root_cert_pem_end - server_root_cert_pem_start,
                            post_buff, strlen(post_buff) + 1,
                            ret_data_out, data_out_len);

    audio_free(post_buff);
    post_buff = NULL;
    return 0;

ERR_RET:
    return -1;
}

int request_next_music(void)
{
    int ret = 0;
#ifdef CONFIG_MIGU_MUSIC
    ret = migu_request_next_music();
#endif
    return ret;
}

int active_music_license()
{
    int ret = 0;
#ifdef CONFIG_MIGU_MUSIC
    ret = migu_active_music_license();
#endif
    return ret;
}

static void next_music_callback( TimerHandle_t xTimer )
{
    if (current_music == NULL) {
        return;
    }
    ESP_LOGI(TAG, "send next_task_handle notify");
/* 
* why not request for the next song directly
* beacause it's going to pause 1&2 second when play url music, so request next song task run core 1, play task run core 2
*/
    xTaskNotifyGive(next_task_handle);
}

static void _clean_music_queue_data(music_queue_t pQueue_data)
{
    if (pQueue_data.data) {
        audio_free(pQueue_data.data);
        pQueue_data.data = NULL;
    }
    if (pQueue_data.action_type) {
        audio_free(pQueue_data.action_type);
        pQueue_data.action_type = NULL;
    }
}

static void app_music_task(void *pvParameters)
{
    music_queue_t pQueue_data;
    audio_player_state_t st = {0};

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
            case ID_MUSIC:
            case URL_MUSIC:
                if (pQueue_data.action == CACHE_MUSIC) {     //cache next music
                    if (current_music) {
                        delete_music(current_music->next);
                        if (pQueue_data.type == URL_MUSIC) {
                            current_music->next = create_music(pQueue_data.type, pQueue_data.data);
                        } else {
                            current_music->next = get_music_by_id(pQueue_data.data);
                        }

                        _clean_music_queue_data(pQueue_data);
                        continue;
                    }
                } else if (pQueue_data.action == NEXT_MUSIC && current_music && current_music->next) {   //play next music
                    music_t *tmp = current_music;
                    current_music = current_music->next;
                    delete_music(tmp);
                } else {              //play receive url music
                    if(current_music) {
                        delete_music(current_music->next);
                        delete_music(current_music);
                    }
                    if (pQueue_data.type == URL_MUSIC) {
                        current_music = create_music(pQueue_data.type, pQueue_data.data);
                    } else {
                        current_music = get_music_by_id(pQueue_data.data);
                    }
                }
                if(pQueue_data.type == URL_MUSIC) {
                    vTaskDelay(2000);      //wait for tts start play
                }
                break;
            case ALL_TYPE:
                if (g_pre_player_need_resume) {
                    g_pre_player_need_resume = false;
                    handle_play_cmd(CMD_HTTP_PLAY_RESUME, NULL, 0);
                    ESP_LOGI(TAG, "play CMD_HTTP_PLAY_RESUME");
                    _clean_music_queue_data(pQueue_data);
                    continue;
                } else if (pQueue_data.action == NEXT_MUSIC && current_music && current_music->next) {   //play cache next music
                    music_t *tmp = current_music;
                    current_music = current_music->next;
                    delete_music(tmp);
                    ESP_LOGI(TAG, "play next music");
                }  else {
                    _clean_music_queue_data(pQueue_data);
                    continue;
                }
                break;
            default:
                break;
            }

            _clean_music_queue_data(pQueue_data);

            if (current_music && current_music->url) {
                audio_player_state_get(&st);
                while ((st.media_src != MEDIA_SRC_TYPE_MUSIC_HTTP) && st.status == AUDIO_PLAYER_STATUS_RUNNING) {
                    ESP_LOGI(TAG, "%s|%d:It's AUDIO_STATUS_RUNNING", __func__,__LINE__);
                    vTaskDelay(200);
                    audio_player_state_get(&st);
                }
#ifdef CONFIG_MIGU_MUSIC
                /*The start time of the song is equal to the end time of the previous song*/
                send_migu_event_queue(END_EVENT);
                if(current_music->type == ID_MUSIC) {
                    send_migu_event_queue(MIGU_START_EVENT);
                }
#endif
                handle_play_cmd(CMD_HTTP_PLAY_START, (uint8_t *)current_music->url, strlen(current_music->url) + 1);
                set_music_player_state(RUNNING_STATE);
                vTaskDelay(100);
                if (ESP_ERR_AUDIO_NO_ERROR == audio_player_duration_get(&current_music->duration)) {
                    ESP_LOGE(TAG, "%s|%d: audio_player_duration_get = %d!!!", __func__, __LINE__,current_music->duration);
                } else {
                    ESP_LOGE(TAG, "%s|%d: audio_player_duration_get no player instance!!!", __func__, __LINE__);
                }
                if (next_music_timer_handle != NULL) {
                    if(current_music->duration > SILENT_NEXT_TIME_EARLY) {
                        xTimerChangePeriod(next_music_timer_handle, (current_music->duration - SILENT_NEXT_TIME_EARLY) /portTICK_PERIOD_MS, 0);
                    }
                    xTimerStart(next_music_timer_handle,0);
                } else {
                   ESP_LOGE(TAG, "%s|%d: next_music_timer_handle create fail!!!", __func__, __LINE__);
                }
            }
        }
        vTaskDelay(100);
    }
    vTaskDelete(NULL);
}


static void next_task(void *pvParameters)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGI(TAG, "next music request");
        request_next_music();
        vTaskDelay(100);
    }
    vTaskDelete(NULL);
}

int app_music_init(void)
{
    audio_thread_t app_music_task_handle = NULL;
    g_music_queue_handle = xQueueCreate(3, sizeof(music_queue_t));
    int ret = app_task_regist(APP_TASK_ID_APP_MUSIC, app_music_task, NULL, &app_music_task_handle);
    if (ret == ESP_FAIL) {
        ERR_OUT(thred_create_fail1, "Couldn't create app_music_task");
    }

    ret = app_task_regist(APP_TASK_ID_NEXT_MUSIC, next_task, NULL, &next_task_handle);
    if (ret == ESP_FAIL) {
        ERR_OUT(thred_create_fail2, "Couldn't create next_task");
    }
    next_music_timer_handle = xTimerCreate("next_music_timer", (10 * 1000 / portTICK_PERIOD_MS), pdFALSE, (void *)0, next_music_callback);
#ifdef CONFIG_MIGU_MUSIC
    migu_music_init();
#endif
    return 0;

thred_create_fail2:
    vTaskDelete(app_music_task_handle);
thred_create_fail1:
    vQueueDelete(g_music_queue_handle);
    return -1;
}
