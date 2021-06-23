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

#include "bds_client_command.h"
#include "bds_client_context.h"
#include "bds_client_event.h"
#include "bds_client_params.h"
#include "bds_client.h"
#include "bds_common_utility.h"
#include "raw_play_task.h"
#include "audio_mem.h"
#include "audio_error.h"
#include "bdsc_event_dispatcher.h"
#include "bdsc_cmd.h"
#include "display_service.h"
#include "bdsc_engine.h"

#include "audio_player_helper.h"
#include "audio_player_type.h"
#include "audio_player.h"
#include "audio_player_pipeline_int_tone.h"
#include "audio_tone_uri.h"
#include "esp_http_client.h"
#include "bdsc_tools.h"
#include "bdsc_json.h"
#include "audio_thread.h"
#include "bdsc_ota_partitions.h"
#include "app_task_register.h"
#include "app_music.h"
#include "play_list.h"

#define TAG "=========="

#define EVENT_ENGINE_QUEUE_LEN  256
#define DEFAULT_WAKEUP_BACKTIME 200
extern display_service_handle_t g_disp_serv;

typedef struct _engine_elem_t {
    int     event;
    uint8_t *data;
    size_t  data_len;
} engine_elem_t;

typedef struct _tts_pkg_t {
    uint16_t  json_len;
    uint8_t   *json_data;
    uint32_t  raw_len;
    uint8_t   *raw_data;
} tts_pkg_t;
static short covert_to_short(char *buffer)
{
    short len = buffer[1] << 8;
    len |=  buffer[0];
    return len;
}

static int covert_to_int(char *buffer)
{
    int len = buffer[3] << 24;
    len |= buffer[2] << 16;
    len |= buffer[1] << 8;
    len |=  buffer[0];
    return len;
}

typedef struct tts_header {
    int16_t err;
    int16_t idx;
} tts_header_t;

static int parse_tts_header(tts_header_t *ret_header, char *begin, char *end)
{
    cJSON *json = cJSON_Parse(begin);
    if (!json) {
        ESP_LOGI(TAG, "tts json format error");
        return -1;
    }
    char *content = cJSON_Print(json);
    ESP_LOGI(TAG, "tts header json: %s", content);
    free(content);
    cJSON *err = cJSON_GetObjectItem(json, "err");
    cJSON *idx = cJSON_GetObjectItem(json, "idx");

    ret_header->err = (err ? err->valueint : -1);
    ret_header->idx = (idx ? idx->valueint : -1);
    cJSON_Delete(json);
    return 0;
}

/*
 *  origin package: | 2B | json data | 4B | raw mp3 data |
 *  if origin package over than 2048 byte,it will be Unpacked multiple mini packets
 *  idx ：the index of mini package
 *  if idx < 0, the end of mini package
 */
int handle_tts_package_data(bdsc_event_data_t *tts_data)
{
    static tts_header_t header = {0};
    tts_pkg_t tts_pkg = {0};
    if(tts_data->idx == 1 || tts_data->idx == -1) {   //first mini package
        tts_pkg.json_len = covert_to_short((char*)tts_data->buffer);
        tts_pkg.raw_len = tts_data->buffer_length - sizeof(tts_pkg.json_len)\
                          - sizeof(tts_pkg.raw_len) - tts_pkg.json_len;
        if (tts_pkg.raw_len < 0) {
            ERR_OUT(ERROR_RAW, "raw length is invalid");
        }
        tts_pkg.json_data = (uint8_t*)tts_data->buffer + sizeof(tts_pkg.json_len);
        tts_pkg.raw_data = (uint8_t*)tts_data->buffer + sizeof(tts_pkg.json_len)\
                           + sizeof(tts_pkg.raw_len) + tts_pkg.json_len;
        int ret = parse_tts_header(&header, (char *)tts_pkg.json_data, NULL);
        if (ret) {
            ERR_OUT(ERROR_JSON, "parse json fail");
        }
        if (header.err) {
            ERR_OUT(ERROR_JSON, "tts stream error, err: %d", header.err);
        }
    } else {
        tts_pkg.raw_data = (uint8_t*)tts_data->buffer;
        tts_pkg.raw_len = tts_data->buffer_length;
    }
    raw_data_t *raw = (raw_data_t*)audio_calloc(1, sizeof(raw_data_t));
    raw->raw_data = (uint8_t*)audio_calloc(1, tts_pkg.raw_len);
    memcpy(raw->raw_data, tts_pkg.raw_data, tts_pkg.raw_len);
    raw->raw_data_len = tts_pkg.raw_len;
    if((header.idx < 0) && (tts_data->idx < 0)) {
        raw->is_end = true;
    } else {
        raw->is_end = false;
    }
    send_music_queue(RAW_TTS_DATA, raw);
    return 0;

ERROR_JSON:
ERROR_RAW:
    return -1;
}

extern bool g_app_music_init_finish_flag;
void play_tone_by_id(bdsc_hint_type_t id);
void bdsc_play_hint(bdsc_hint_type_t type)
{
    bdsc_hint_type_t *tone_id = NULL;

    if (g_bdsc_engine->silent_mode) {
        ESP_LOGI(TAG, "in silent mode, skip play");
        return;
    }

    // app_music_task uninitialized
    if(!g_app_music_init_finish_flag) {
        play_tone_by_id(type);
        return;
    }

    tone_id = audio_calloc(1, sizeof(bdsc_hint_type_t));
    *tone_id = type;
    send_music_queue(TONE_MUSIC, tone_id);

}

int notify_bdsc_engine_event_to_user(int event, uint8_t *data, size_t data_len)
{
    bdsc_engine_event_t evt;

    evt.event_id = event;
    evt.data = data;
    evt.data_len = data_len;
    evt.client = g_bdsc_engine;
    return g_bdsc_engine->cfg->event_handler(&evt);
}

bool need_skip_current_playing();
void dsp_fatal_handle(void);
extern audio_err_t ap_helper_raw_play_abort_outbf();
#define WK_FINISH (BIT0)

static void stop_player(int event)
{
    audio_player_state_t st = {0};
    audio_player_state_get(&st);
    ESP_LOGI(TAG, "Playing media is 0x%x, status is %d", st.media_src, st.status);
    if ((st.media_src == MEDIA_SRC_TYPE_MUSIC_HTTP) &&
        (st.status == AUDIO_PLAYER_STATUS_RUNNING)) {
        handle_play_cmd(CMD_HTTP_PLAY_PAUSE, NULL, 0);
        pls_set_current_music_player_state(g_pls_handle, PAUSE_STATE);
    } else {
        if (event == EVENT_RECV_A2DP_START_PLAY) {
            if (st.media_src != MEDIA_SRC_TYPE_MUSIC_A2DP) {
                handle_play_cmd(CMD_RAW_PLAY_STOP, NULL, 0);
            }
        } else {
            handle_play_cmd(CMD_RAW_PLAY_STOP, NULL, 0);
        }
    }
}


static void wk_task(void *pvParameters)
{
    g_bdsc_engine->in_wakeup = true;
    media_source_type_t src_type = -1;
    audio_err_t ret = audio_player_media_src_get(&src_type);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fail to get player media src");
    }
    if (src_type == MEDIA_SRC_TYPE_MUSIC_RAW) {
        ap_helper_raw_play_abort_outbf();
    }
    //display_service_set_pattern(disp_serv, DISPLAY_PATTERN_WAKEUP_ON, 0);
    audio_player_int_tone_play(tone_uri[TONE_TYPE_WAKEUP + (xTaskGetTickCount()%4)]);   //random play wakwup mp3
    
    stop_player(EVENT_WAKEUP_TRIGGER);
    ESP_LOGI(TAG, "+++++++++++++ set bit!!! 3333 ");
    xEventGroupSetBits(g_bdsc_engine->wk_group, WK_FINISH);
    g_bdsc_engine->in_wakeup = false;
    ESP_LOGI(TAG, "+++++++++++++ 444444 ");

    vTaskDelete(NULL);
}


void show_wakeup_async(void)
{
    int ret;
    // FIXME: should not put here
    bdsc_stop_asr();
    bdsc_start_asr(DEFAULT_WAKEUP_BACKTIME);

    if (g_bdsc_engine->in_wakeup) {
        return;
    }
    xEventGroupClearBits(g_bdsc_engine->wk_group, WK_FINISH);

    ret = app_task_regist(APP_TASK_ID_WK, wk_task, NULL, NULL);
    if (ret == ESP_FAIL) {
        ESP_LOGE(TAG, "Couldn't create wk_task");
        // If task create failed we must not set wakeup flag
        // to avoid engine_task waiting forever.
        g_bdsc_engine->in_wakeup = false;
    }
}


static void sop_task(void *pvParameters)
{
    int event = *(int*)pvParameters;

    stop_player(event);
    
    audio_free(pvParameters);
    vTaskDelete(NULL);
}

void stop_or_pause_async(int event)
{
    int *evt;
    int ret;

    evt = audio_malloc(sizeof(int));
    *evt = event;
    ret = app_task_regist(APP_TASK_ID_SOP, sop_task, evt, NULL);
    if (ret == ESP_FAIL) {
        ESP_LOGE(TAG, "Couldn't create sop_task");
    }
}

bool check_duplex_mode_and_exit(char *extern_json)
{
    cJSON *j_content = NULL;
    char *action_type_str = NULL;

    if (extern_json &&
        (j_content = cJSON_Parse((const char *)extern_json)) &&
        (action_type_str = BdsJsonObjectGetString(j_content, "action_type")) &&
        (!strncmp(action_type_str, "asrnlp_url", strlen("asrnlp_url")) ||
        !strncmp(action_type_str, "asrnlp_mix", strlen("asrnlp_mix")) ||
        !strncmp(action_type_str, "asrnlp_ttsurl", strlen("asrnlp_ttsurl")))) {
        
        if (g_bdsc_engine->in_duplex_mode) {
            ESP_LOGI(TAG, "we don't process this kind message In duplex mode");
            BdsJsonPut(j_content);
            return true;
        }
    }

    if (j_content) {
        BdsJsonPut(j_content);
    }
    return false;
}

int32_t handle_bdsc_event(engine_elem_t elem)
{
    raw_data_t *raw = NULL;
    bdsc_custom_desire_action_t desire = BDSC_CUSTOM_DESIRE_DEFAULT;
    
    switch (elem.event) {
        case EVENT_ASR_ERROR: {
                bdsc_event_error_t *error = (bdsc_event_error_t *)elem.data;
                if (error) {
                    ESP_LOGE(TAG, "---> EVENT_ASR_ERROR sn=%s, code=%"PRId32"--info_length=%"PRIu16"--info=%s",
                             error->sn, error->code, error->info_length, error->info);
                    desire = notify_bdsc_engine_event_to_user(BDSC_EVENT_ERROR, (uint8_t *)error, sizeof(bdsc_event_error_t));
                } else {
                    ESP_LOGE(TAG, "---> EVENT_ASR_ERROR error null");
                }
                switch (g_bdsc_engine->g_asr_tts_state) {
                    case ASR_TTS_ENGINE_WAKEUP_TRIGGER:
                    case ASR_TTS_ENGINE_STARTTED:
                        ESP_LOGW(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                    case ASR_TTS_ENGINE_GOT_ASR_BEGIN:
                        ESP_LOGE(TAG, "%d get asr error %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                    case ASR_TTS_ENGINE_GOT_ASR_RESULT:
                    case ASR_TTS_ENGINE_GOT_EXTERN_DATA:
                    case ASR_TTS_ENGINE_GOT_TTS_DATA:
                        ESP_LOGW(TAG, "%d get asr error,maybe connection lost %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                    case ASR_TTS_ENGINE_GOT_ASR_ERROR:
                    case ASR_TTS_ENGINE_GOT_ASR_END:
                        ESP_LOGW(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                    case ASR_TTS_ENGINE_WANT_RESTART_ASR:
                        g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_STARTTED;
                        return 0;
                    default:
                        ESP_LOGW(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                }

                g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_GOT_ASR_ERROR;
                break;
            }
        case EVENT_ASR_CANCEL: {
                bdsc_event_process_t *process = (bdsc_event_process_t *)elem.data;
                if (process) {
                    ESP_LOGW(TAG, "---> EVENT_ASR_CANCEL sn=%s", process->sn);
                } else {
                    ESP_LOGE(TAG, "---> EVENT_ASR_CANCEL process null");
                }

                switch (g_bdsc_engine->g_asr_tts_state) {
                    case ASR_TTS_ENGINE_WAKEUP_TRIGGER:
                        ESP_LOGI(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                    case ASR_TTS_ENGINE_STARTTED:
                        // sometimes 2 'EVENT_ASR_CANCEL' fired at the same time
                        // why??
                        break;
                    case ASR_TTS_ENGINE_GOT_ASR_BEGIN:
                    case ASR_TTS_ENGINE_GOT_ASR_RESULT:
                    case ASR_TTS_ENGINE_GOT_EXTERN_DATA:
                    case ASR_TTS_ENGINE_GOT_TTS_DATA:
                    case ASR_TTS_ENGINE_GOT_ASR_ERROR:
                    case ASR_TTS_ENGINE_GOT_ASR_END:
                        ESP_LOGI(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                    case ASR_TTS_ENGINE_WANT_RESTART_ASR:
                        ESP_LOGI(TAG, "%d restart asr", __LINE__);
                        g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_STARTTED;

                        return 0;
                    default:
                        ESP_LOGI(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                }

                break;
            }
        case EVENT_ASR_BEGIN: {
                bdsc_event_process_t *process = (bdsc_event_process_t *)elem.data;
                if (process) {
                    ESP_LOGW(TAG, "---> EVENT_ASR_BEGIN sn=%s", process->sn);
                } else {
                    ESP_LOGE(TAG, "---> EVENT_ASR_BEGIN process null");
                }
                switch (g_bdsc_engine->g_asr_tts_state) {
                    case ASR_TTS_ENGINE_WAKEUP_TRIGGER:
                        ESP_LOGI(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                    case ASR_TTS_ENGINE_STARTTED:
                        g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_GOT_ASR_BEGIN;
                        break;
                    case ASR_TTS_ENGINE_GOT_ASR_BEGIN:
                    case ASR_TTS_ENGINE_GOT_ASR_RESULT:
                    case ASR_TTS_ENGINE_GOT_EXTERN_DATA:
                    case ASR_TTS_ENGINE_GOT_TTS_DATA:
                    case ASR_TTS_ENGINE_GOT_ASR_ERROR:
                    case ASR_TTS_ENGINE_GOT_ASR_END:
                        ESP_LOGI(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                    case ASR_TTS_ENGINE_WANT_RESTART_ASR:
                        return 0;
                    default:
                        ESP_LOGI(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                }

                g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_GOT_ASR_BEGIN;
                break;
            }
        case EVENT_ASR_RESULT: {
                bdsc_event_data_t *asr_result = (bdsc_event_data_t *)elem.data;
                switch (g_bdsc_engine->g_asr_tts_state) {
                    case ASR_TTS_ENGINE_WAKEUP_TRIGGER:
                    case ASR_TTS_ENGINE_STARTTED:
                        ESP_LOGI(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                    case ASR_TTS_ENGINE_GOT_ASR_BEGIN:
                        ESP_LOGI(TAG, "%d got asr result", __LINE__);
                        g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_GOT_ASR_RESULT;
                        if (asr_result) {
                            asr_result->buffer[asr_result->buffer_length - 1] = '\0';
                            ESP_LOGW(TAG, "---> EVENT_ASR_RESULT sn=%s, idx=%d, buffer_length=%d, buffer=%s",
                                     asr_result->sn, asr_result->idx, asr_result->buffer_length, asr_result->buffer);
                            desire = notify_bdsc_engine_event_to_user(BDSC_EVENT_ON_ASR_RESULT, (uint8_t*)asr_result, sizeof(bdsc_event_data_t));
                            
                        } else {
                            ESP_LOGW(TAG, "---> EVENT_ASR_RESULT result null");
                        }
                        return 0;
                    case ASR_TTS_ENGINE_GOT_ASR_RESULT:
                    case ASR_TTS_ENGINE_GOT_EXTERN_DATA:
                    case ASR_TTS_ENGINE_GOT_TTS_DATA:
                    case ASR_TTS_ENGINE_GOT_ASR_ERROR:
                    case ASR_TTS_ENGINE_GOT_ASR_END:
                        ESP_LOGI(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                    case ASR_TTS_ENGINE_WANT_RESTART_ASR:
                        return 0;
                    default:
                        ESP_LOGI(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                }
                g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_GOT_ASR_RESULT;
                break;
            }
        case EVENT_ASR_EXTERN_DATA: {
                bdsc_event_data_t *extern_result = (bdsc_event_data_t *)elem.data;
                switch (g_bdsc_engine->g_asr_tts_state) {
                    case ASR_TTS_ENGINE_WAKEUP_TRIGGER:
                    case ASR_TTS_ENGINE_STARTTED:
                    case ASR_TTS_ENGINE_GOT_ASR_BEGIN:
                        ESP_LOGI(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                    case ASR_TTS_ENGINE_GOT_ASR_RESULT:
                        ESP_LOGI(TAG, "got 1st extern data");
                        if (extern_result) {
                            /*
                            * buffer 格式：
                            *      | json string len (4B) | type (8B, "iot") | json stirng ....|
                            */
                            ESP_LOGW(TAG, "---> EVENT_ASR_EXTERN_DATA sn=%s, idx=%d, buffer_length=%d, buffer=%s",
                                     extern_result->sn, extern_result->idx,
                                     extern_result->buffer_length, extern_result->buffer + 12);
                            
                            g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_GOT_EXTERN_DATA;
                            if (check_duplex_mode_and_exit((char*)extern_result->buffer + 12)) {
                                bdsc_engine_skip_current_session_playing_once_flag_set(g_bdsc_engine); // skip tailing tts data
                                break;
                            }
                            char *json_buf = NULL;
                            json_buf = audio_calloc(1, extern_result->buffer_length + 1);
                            memcpy(json_buf, extern_result->buffer + 12, extern_result->buffer_length);
                            desire = notify_bdsc_engine_event_to_user(BDSC_EVENT_ON_NLP_RESULT, (uint8_t *)json_buf, strlen((const char *)json_buf) + 1);
                            free(json_buf);
                        }
                        break;
                    case ASR_TTS_ENGINE_GOT_EXTERN_DATA:
                        ESP_LOGI(TAG, "got 1+ extern data");
                        // EXTERN_DATA 长度如果大于2048字节，会分包发送
                        if (extern_result) {
                            ESP_LOGW(TAG, "---> EVENT_ASR_EXTERN_DATA sn=%s, idx=%d, buffer_length=%d, buffer=%s",
                                     extern_result->sn, extern_result->idx,
                                     extern_result->buffer_length, extern_result->buffer);
                            g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_GOT_EXTERN_DATA;
                        }
                        break;
                    case ASR_TTS_ENGINE_GOT_TTS_DATA:
                    case ASR_TTS_ENGINE_GOT_ASR_ERROR:
                    case ASR_TTS_ENGINE_GOT_ASR_END:
                        ESP_LOGI(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                    case ASR_TTS_ENGINE_WANT_RESTART_ASR:
                        return 0;
                    default:
                        ESP_LOGI(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                }
                g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_GOT_EXTERN_DATA;
                break;
            }
        case EVENT_ASR_TTS_DATA: {
                bdsc_event_data_t *tts_data = (bdsc_event_data_t *)elem.data;
                switch (g_bdsc_engine->g_asr_tts_state) {
                    case ASR_TTS_ENGINE_WAKEUP_TRIGGER:
                    case ASR_TTS_ENGINE_STARTTED:
                    case ASR_TTS_ENGINE_GOT_ASR_BEGIN:
                    case ASR_TTS_ENGINE_GOT_ASR_RESULT:
                        ESP_LOGI(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                    case ASR_TTS_ENGINE_GOT_EXTERN_DATA:
                    case ASR_TTS_ENGINE_GOT_TTS_DATA:
                        if (tts_data) {
                            /*
                            * Get rid of any log printing in tts playing !!!
                            */
                            ESP_LOGW(TAG, "---> EVENT_ASR_TTS_DATA sn=%s, idx=%d, buffer_length=%d, buffer=%p",
                                     tts_data->sn, tts_data->idx,
                                     tts_data->buffer_length, tts_data->buffer);

                            if (!need_skip_current_playing()) {
                                int ret = handle_tts_package_data(tts_data);
                                if (ret == -1 && tts_data->idx == 0) {
                                    // if the first tts data is error, skip current playing
                                    bdsc_engine_skip_current_session_playing_once_flag_set(g_bdsc_engine);
                                }

                            }
                        }
                        g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_GOT_TTS_DATA;
                        break;
                    case ASR_TTS_ENGINE_GOT_ASR_ERROR:
                    case ASR_TTS_ENGINE_GOT_ASR_END:
                        ESP_LOGI(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                    case ASR_TTS_ENGINE_WANT_RESTART_ASR:
                        ESP_LOGI(TAG, "%d WANT_RESTART_ASR quit tts playing %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                    default:
                        ESP_LOGI(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                }
                break;
            }
        case EVENT_ASR_END: {
                bdsc_event_process_t *process = (bdsc_event_process_t *)elem.data;
                if (process) {
                    ESP_LOGW(TAG, "---> EVENT_ASR_END sn=%s", process->sn);
                }
                //display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_WAKEUP_FINISHED, 0);
                bdsc_engine_skip_current_session_playing_once_flag_unset(g_bdsc_engine);
                switch (g_bdsc_engine->g_asr_tts_state) {
                    case ASR_TTS_ENGINE_WAKEUP_TRIGGER:
                    case ASR_TTS_ENGINE_STARTTED:
                    case ASR_TTS_ENGINE_GOT_ASR_BEGIN:
                        ESP_LOGI(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                    case ASR_TTS_ENGINE_GOT_ASR_RESULT:
                    case ASR_TTS_ENGINE_GOT_EXTERN_DATA:
                        ESP_LOGI(TAG, "got asr end, no tts data");
                        g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_GOT_ASR_END;
                        if (g_bdsc_engine->in_duplex_mode) {
                            ESP_LOGI(TAG, "in duplex mode1");
                            bdsc_start_asr(0);
                            g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_STARTTED;
                            break;
                        }
                        break;
                    case ASR_TTS_ENGINE_GOT_TTS_DATA:
                        ESP_LOGI(TAG, "got asr end, has tts data");
                        g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_GOT_ASR_END;

                        if (need_skip_current_playing()) {
                            if (g_bdsc_engine->in_duplex_mode) {
                                ESP_LOGI(TAG, "need_skip_current_playing in duplex mode2");
                                bdsc_start_asr(0);
                                g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_STARTTED;
                            }
                            break;
                        }
                        if (g_bdsc_engine->in_duplex_mode) {
                            // FIXME: in duplex mode, waiting for 'haode' palying done
                            vTaskDelay(600 / portTICK_PERIOD_MS);
                        }
                        if (g_bdsc_engine->in_duplex_mode) {
                            //bdsc_stop_asr();
                            // 纯识别，需要设置 backtine == 0
                            bdsc_start_asr(0);
                            g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_STARTTED;
                        }
                        break;
                    case ASR_TTS_ENGINE_GOT_ASR_ERROR:
                    case ASR_TTS_ENGINE_GOT_ASR_END:
                        ESP_LOGI(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                    case ASR_TTS_ENGINE_WANT_RESTART_ASR:
                        return 0;
                    default:
                        ESP_LOGI(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                }
                break;
            }
        case EVENT_WAKEUP_TRIGGER: {
                bdsc_event_wakeup_t *wakeup = (bdsc_event_wakeup_t *)elem.data;
                if (wakeup) {
                    ESP_LOGW(TAG, "---> EVENT_WAKEUP_TRIGGER status=%d", wakeup->status);
                } else {
                    ESP_LOGE(TAG, "---> EVENT_WAKEUP_TRIGGER wakeup null");
                }
                g_bdsc_engine->silent_mode = 0;
                desire = notify_bdsc_engine_event_to_user(BDSC_EVENT_ON_WAKEUP, NULL, 0);
                if (BDSC_CUSTOM_DESIRE_SKIP_DEFAULT == desire) {
                    break;
                }
                switch (g_bdsc_engine->g_asr_tts_state) {
                    case ASR_TTS_ENGINE_WAKEUP_TRIGGER:
                    case ASR_TTS_ENGINE_STARTTED:
                    case ASR_TTS_ENGINE_GOT_ASR_BEGIN:
                    case ASR_TTS_ENGINE_GOT_ASR_RESULT:
                    case ASR_TTS_ENGINE_GOT_EXTERN_DATA:
                    case ASR_TTS_ENGINE_GOT_TTS_DATA:
                    case ASR_TTS_ENGINE_GOT_ASR_ERROR:
                    case ASR_TTS_ENGINE_WANT_RESTART_ASR:
                        ESP_LOGI(TAG, "stop asr && need restart asr");
                        // stop will take a short time and 'CANCELED' event will be fired
                        if (ASR_TTS_ENGINE_GOT_TTS_DATA == g_bdsc_engine->g_asr_tts_state) {
                            ESP_LOGI(TAG, "stop playing and flush queue");
                        }
                        g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_WANT_RESTART_ASR;
                        break;
                    case ASR_TTS_ENGINE_GOT_ASR_END:

                        ESP_LOGI(TAG, "==> triggered, start asr");
                        g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_STARTTED;
                        break;
                    default:
                        ESP_LOGI(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
                }
                break;
            }
        case EVENT_WAKEUP_OFFLINE_DIRECTIVE: {
            bdsc_event_data_t *dir_data = (bdsc_event_data_t*)elem.data;
            if (dir_data) {
                ESP_LOGI(TAG, "---> EVENT_WAKEUP_OFFLINE_DIRECTIVE idx=%d, buffer_length=%d, buffer=%s",
                        dir_data->idx, dir_data->buffer_length, dir_data->buffer);
            } else {
                ESP_LOGE(TAG, "---> EVENT_WAKEUP_OFFLINE_DIRECTIVE data null");
            }
            break;
        }
        case EVENT_WAKEUP_ERROR: {
                bdsc_event_error_t *error = (bdsc_event_error_t *)elem.data;
                if (error) {
                    ESP_LOGW(TAG, "---> EVENT_WAKEUP_ERROR code=%"PRId32"--info=%s", error->code, error->info);
                } else {
                    ESP_LOGE(TAG, "---> EVENT_WAKEUP_ERROR error null");
                }
                break;
            }
        case EVENT_SDK_START_COMPLETED: {
                ESP_LOGW(TAG, "---> EVENT_SDK_START_COMPLETED");
                display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_TURN_OFF, 0);
                g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_GOT_ASR_END;
                break;
            }
        case EVENT_LINK_CONNECTED: {
                bdsc_event_data_t *push_data = (bdsc_event_data_t*)elem.data;
                if (push_data) {
                    ESP_LOGW(TAG, "---> ---> EVENT_LINK_CONNECTED buffer_length=%d, buffer=%s",
                            push_data->buffer_length, push_data->buffer);
                } else {
                    ESP_LOGE(TAG, "---> EVENT_LINK_CONNECTED data null");
                }
                break;
            }
        case EVENT_LINK_DISCONNECTED: {
                bdsc_event_data_t *push_data = (bdsc_event_data_t*)elem.data;
                if (push_data) {
                    ESP_LOGW(TAG, "---> ---> EVENT_LINK_DISCONNECTED buffer_length=%d, buffer=",
                            push_data->buffer_length);
                } else {
                    ESP_LOGE(TAG, "---> EVENT_LINK_CONNECTED data null");
                }
                break;
            }
        case EVENT_LINK_ERROR: {
                bdsc_event_error_t *error = (bdsc_event_error_t *)elem.data;
                if (error) {
                    ESP_LOGW(TAG, "---> EVENT_LINK_ERROR code=%"PRId32"--info=", error->code);
                } else {
                    ESP_LOGE(TAG, "---> EVENT_LINK_ERROR error null");
                }
                break;
            }
        case EVENT_RECORDER_DATA: {
                bdsc_event_data_t *pcm_data = (bdsc_event_data_t *)elem.data;
                if (pcm_data) {
                    ESP_LOGI(TAG, "---> EVENT_RECORDER_DATA idx=%d, buffer_length=%d, buffer=%p",
                             pcm_data->idx, pcm_data->buffer_length, pcm_data->buffer);
                } else {
                    ESP_LOGE(TAG, "---> EVENT_RECORDER_DATA data null");
                }
                break;
            }
        case EVENT_RECORDER_ERROR: {
                bdsc_event_error_t *error = (bdsc_event_error_t *)elem.data;
                if (error) {
                    ESP_LOGW(TAG, "---> EVENT_RECORDER_ERROR code=%"PRId32"--info=%s", error->code, error->info);
                } else {
                    ESP_LOGE(TAG, "---> EVENT_RECORDER_ERROR error null");
                }
                break;
            }
        case EVENT_DSP_FATAL_ERROR: {
                bdsc_event_error_t *error = (bdsc_event_error_t*)elem.data;
                if (error) {
                    ESP_LOGW(TAG, "---> EVENT_DSP_FATAL_ERROR code=%"PRId32"--info=%s", error->code, error->info);
                    if (error->code == -7004) {
                        // No dsp heart beat and reset sdk
                        ESP_LOGE(TAG, "Restart sdk!");
                        dsp_fatal_handle();
                    }
                } else {
                    ESP_LOGE(TAG, "---> EVENT_DSP_FATAL_ERROR error null");
                }
                break;
            }
        case EVENT_RECV_MQTT_PUSH_URL: {
                if (g_bdsc_engine->dsp_detect_error) {
                    ESP_LOGI(TAG, "dsp detect error, dont play");
                    break;
                }
                if (!g_bdsc_engine->in_duplex_mode) {
                    bdsc_stop_asr();
                }
                g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_GOT_ASR_END;
                char *mqtt_url = audio_strdup((const char*)elem.data);
                send_music_queue(MQTT_URL, mqtt_url);
                break;
            }
        case EVENT_RECV_A2DP_START_PLAY: {
                bdsc_stop_asr();
                g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_GOT_ASR_END;
                char *a2dp_url = audio_strdup((const char*)elem.data);
                send_music_queue(A2DP_PLAY, a2dp_url);
                break;
            }
        case EVENT_RECV_ACTIVE_TTS_PLAY: {
                audio_player_stop();
                ap_helper_raw_play_abort_outbf();
                bdsc_stop_asr();
                g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_GOT_ASR_END;
                break;
            }
        default:
            ESP_LOGE(TAG, "!!! unknow event !!!");
            break;
    }
    
    return 0;
}

void event_engine_elem_FlushQueque()
{
    int recv_cnt;

    ESP_LOGI(TAG, "==> event_engine_elem_FlushQueque");
    if (uxQueueSpacesAvailable(g_bdsc_engine->g_engine_queue) < EVENT_ENGINE_QUEUE_LEN) {

        recv_cnt =  EVENT_ENGINE_QUEUE_LEN - uxQueueSpacesAvailable(g_bdsc_engine->g_engine_queue);
        engine_elem_t elem;
        memset(&elem, 0, sizeof(engine_elem_t));
        int i;
        ESP_LOGI(TAG, "FLUSH QUEUE! recvd: %d\n", recv_cnt);

        for (i = 0; i < recv_cnt; i++) {
            if (xQueueReceive(g_bdsc_engine->g_engine_queue, &elem, 0) == pdPASS) {
                free(elem.data);
            }
        }
    } else {
        ESP_LOGI(TAG, "g_engine_queue is null\n");
    }

}

static void asr_session_st_update(int event)
{
    if (event == EVENT_WAKEUP_TRIGGER) {
        g_bdsc_engine->cur_in_asr_session = true;
        g_bdsc_engine->need_skip_current_pending_http_part = false;
    }
    if (event == EVENT_ASR_END) {
        g_bdsc_engine->cur_in_asr_session = false;
    }
}

void event_engine_elem_EnQueque(int event, uint8_t *buffer, size_t len)
{
    asr_session_st_update(event);
    if (!g_bdsc_engine->enqueue_mutex) {
        return;
    }
    xSemaphoreTake(g_bdsc_engine->enqueue_mutex, portMAX_DELAY);
    engine_elem_t elem;
    elem.event = event;
    elem.data_len = len;
    elem.data = audio_calloc(1, len);
    memcpy(elem.data, buffer, len);

    // EVENT_WAKEUP_TRIGGER high priotiry, FlushQueque!
    if (EVENT_WAKEUP_TRIGGER == event ||
        EVENT_RECV_MQTT_PUSH_URL == event ||
        EVENT_RECV_A2DP_START_PLAY == event ||
        EVENT_RECV_ACTIVE_TTS_PLAY == event) {
        event_engine_elem_FlushQueque();
	    if (EVENT_WAKEUP_TRIGGER == event) {
            show_wakeup_async();
        } else {
            stop_or_pause_async(event);
        }

    }
    if (pdTRUE != xQueueSend(g_bdsc_engine->g_engine_queue, (void *)&elem, 0)) {
        free(elem.data);
    }
    xSemaphoreGive(g_bdsc_engine->enqueue_mutex);
}

static void engine_task(void *pvParameters)
{
    engine_elem_t elem;

    while (1) {
        if (EVENT_ENGINE_QUEUE_LEN == uxQueueSpacesAvailable(g_bdsc_engine->g_engine_queue)) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }
        if (xQueueReceive(g_bdsc_engine->g_engine_queue, &elem, 0) == pdPASS) {
            if (g_bdsc_engine->in_ota_process_flag) {
                ESP_LOGI(TAG, "in ota, we skip all external event");
                free(elem.data);
                continue;
            }
            ESP_LOGI(TAG, "event loop ==>");
            if (g_bdsc_engine->in_wakeup) {
                EventBits_t bits = xEventGroupWaitBits(g_bdsc_engine->wk_group, WK_FINISH, true, false, portMAX_DELAY);
                if (bits & WK_FINISH) {
                    ESP_LOGW(TAG, "waiting wakeup finish");
                }
            }

            ESP_LOGW("EVENT_IN", "Handle sdk event start.");
            handle_bdsc_event(elem);
            free(elem.data);
            ESP_LOGW("EVENT_OUT", "Handle sdk event end.");
        }
        //ESP_LOGI(TAG, "Stack: %d", uxTaskGetStackHighWaterMark(NULL));
    }

    vTaskDelete(NULL);
}

int bdsc_asr_tts_engine_init()
{
    StaticQueue_t *engine_queue_buffer = audio_calloc(1, sizeof(StaticQueue_t));
    assert(engine_queue_buffer != NULL);
    uint8_t *engine_queue_storage = audio_calloc(1, (EVENT_ENGINE_QUEUE_LEN * sizeof(engine_elem_t)));
    assert(engine_queue_storage != NULL);
    g_bdsc_engine->g_engine_queue = xQueueCreateStatic(EVENT_ENGINE_QUEUE_LEN,
                                    sizeof(engine_elem_t),
                                    (uint8_t *)engine_queue_storage,
                                    engine_queue_buffer);
    assert(g_bdsc_engine->g_engine_queue != NULL);

    int ret = app_task_regist(APP_TASK_ID_BDSC_ENGINE, engine_task, NULL, NULL); 
    if (ret == ESP_FAIL) {
        ESP_LOGE(TAG, "Couldn't create engine_task");
    }

    g_bdsc_engine->enqueue_mutex = xSemaphoreCreateMutex();
    g_bdsc_engine->in_wakeup = false;
    g_bdsc_engine->wk_group = xEventGroupCreate();

    g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_GOT_ASR_END;

    return 0;
}

static void dsp_fatal_error_task(void *pvParameters)
{
    stop_sdk();
    start_sdk();
    bdsc_start_wakeup();
    vTaskDelete(NULL);
}

void dsp_fatal_handle(void)
{
    int ret = app_task_regist(APP_TASK_ID_DSP_FATAL_ERROR, dsp_fatal_error_task, NULL, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "fail to create dsp_fatal.");
    }
}

