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

#define TAG "=========="

#define EVENT_ENGINE_QUEUE_LEN  256
#define DEFAULT_WAKEUP_BACKTIME 200
#define ENGINE_TASK_STACK_SZ    (1024 * 4)
#define WAKEUP_TASK_STACK_SZ    (1024 * 4)
#define SOP_TASK_STACK_SZ       (1024 * 4)

#define MIGU_ORIGIN "1079888"
#define HIMALAYA_ORIGIN "1030970"
#define HIMALAYA_OLD_ORIGIN "1030330"     //delete it later,but use it now
#define QING_ORIGIN  "1059717"

extern display_service_handle_t g_disp_serv;
bool g_pre_player_need_resume = false;
static bool stop_play = false;

typedef struct _engine_elem_t {
    int     event;
    uint8_t *data;
    size_t  data_len;
} engine_elem_t;

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

    ret_header->err = err->valueint;
    ret_header->idx = idx->valueint;
    cJSON_Delete(json);
    return 0;
}

/*
 *  | 2B | json data | 4B | raw mp3 data |
 */
int handle_tts_data(char* buffer, int length)
{
    char* ptr = buffer;
    short header_len = covert_to_short(ptr);
    ptr += 2;

    char* begin = ptr;
    ptr += header_len;
    char* end = ptr;

    int binary_len = covert_to_int(ptr);
    ptr += 4;

    if (length - (ptr - buffer) != binary_len) {
        ESP_LOGE(TAG, "tts binary_len check fail? %d %d %d", length, (ptr - buffer), binary_len);
        //return -1;
    }

    tts_header_t header;
    int ret = parse_tts_header(&header, begin, end);
    if (ret) {
        return -1;
    }

    if (header.err) {
        ESP_LOGE(TAG, "tts stream error, err: %d", header.err);
        return -1;
    }

    ESP_LOGI(TAG, "play... length %d.", binary_len);
    handle_play_cmd(CMD_RAW_PLAY_FEED_DATA, (uint8_t *)ptr, binary_len);

    return 0;
}

void bdsc_play_hint(bdsc_hint_type_t type)
{
    if (g_bdsc_engine->silent_mode) {
        ESP_LOGI(TAG, "in silent mode, skip play");
        return;
    }
    int ret;
    switch (type) {
        case BDSC_HINT_BOOT:
            audio_player_tone_play(tone_uri[TONE_TYPE_BOOT], false, false, MEDIA_SRC_TYPE_TONE_FLASH);
            break;
        case BDSC_HINT_SHUTDOWN:
            audio_player_tone_play(tone_uri[TONE_TYPE_SHUT_DOWN], true, false, MEDIA_SRC_TYPE_TONE_FLASH);
            break;
        case BDSC_HINT_CONNECTED:
            audio_player_tone_play(tone_uri[TONE_TYPE_LINKED], false, false, MEDIA_SRC_TYPE_TONE_FLASH);
            break;
        case BDSC_HINT_DISCONNECTED:
            audio_player_tone_play(tone_uri[TONE_TYPE_UNLINKED], false, false, MEDIA_SRC_TYPE_TONE_FLASH);
            break;
        case BDSC_HINT_WAKEUP:
            audio_player_tone_play(tone_uri[TONE_TYPE_WAKEUP], false, false, MEDIA_SRC_TYPE_TONE_FLASH);
            break;
        case BDSC_HINT_SC:
            audio_player_tone_play(tone_uri[TONE_TYPE_SMART_CONFIG], true, false, MEDIA_SRC_TYPE_TONE_FLASH);
            break;
        case BDSC_HINT_NOT_FIND:
            audio_player_tone_play(tone_uri[TONE_TYPE_NOT_FIND], false, false, MEDIA_SRC_TYPE_TONE_FLASH);
            break;
        case BDSC_HINT_OTA_START:
            audio_player_tone_play(tone_uri[TONE_TYPE_OTA_START], true, false, MEDIA_SRC_TYPE_TONE_FLASH);
            break;
        case BDSC_HINT_OTA_COMPLETE:
            audio_player_tone_play(tone_uri[TONE_TYPE_DOWNLOADED], true, false, MEDIA_SRC_TYPE_TONE_FLASH);
            break;
        case BDSC_HINT_OTA_FAIL:
            audio_player_tone_play(tone_uri[TONE_TYPE_OTA_FAIL], true, false, MEDIA_SRC_TYPE_TONE_FLASH);
            break;
        case BDSC_HINT_BT_CONNECTED:
            audio_player_tone_play(tone_uri[TONE_TYPE_BT_CONNECT], false, false, MEDIA_SRC_TYPE_TONE_FLASH);
            break;
        case BDSC_HINT_BT_DISCONNECTED:
            audio_player_tone_play(tone_uri[TONE_TYPE_BT_DISCONNECT], false, false, MEDIA_SRC_TYPE_TONE_FLASH);
            break;
        case BDSC_HINT_OTA_ALREADY_NEWEST:
            audio_player_tone_play(tone_uri[TONE_TYPE_ALREADY_NEW], false, false, MEDIA_SRC_TYPE_TONE_FLASH);
            break;
        case BDSC_HINT_OTA_BAD_NET_REPORT:
            audio_player_tone_play(tone_uri[TONE_TYPE_BAD_NET_REPORT], false, false, MEDIA_SRC_TYPE_TONE_FLASH);
            break;
        case BDSC_HINT_DSP_LOAD_FAIL:
            audio_player_tone_play(tone_uri[TONE_TYPE_DSP_LOAD_FAIL], false, false, MEDIA_SRC_TYPE_TONE_FLASH);
            break;
        case BDSC_HINT_GREET:
#ifdef CONFIG_CUPID_BOARD_V2
            audio_player_tone_play(tone_uri[TONE_TYPE_GREET], false, false, MEDIA_SRC_TYPE_TONE_FLASH);
#else
            audio_player_tone_play(tone_uri[TONE_TYPE_DUHOME_GREET], false, false, MEDIA_SRC_TYPE_TONE_FLASH);
#endif
            break;
        case BDSC_HINT_WIFI_CONFIGUING:
            audio_player_tone_play(tone_uri[TONE_TYPE_WIFI_CONFIGUING], false, false, MEDIA_SRC_TYPE_TONE_FLASH);
            break;
        case BDSC_HINT_WIFI_CONFIG_OK:
            audio_player_tone_play(tone_uri[TONE_TYPE_WIFI_CONFIG_OK], false, false, MEDIA_SRC_TYPE_TONE_FLASH);
            break;
        case BDSC_HINT_WIFI_CONFIG_FAIL:
            audio_player_tone_play(tone_uri[TONE_TYPE_WIFI_CONFIG_FAIL], false, false, MEDIA_SRC_TYPE_TONE_FLASH);
            break;
        default:
            ESP_LOGE(TAG, "invalid hint type");
            break;
    }
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

void player_reset()
{
    if (g_bdsc_engine->asrnlp_ttsurl){
        free(g_bdsc_engine->asrnlp_ttsurl);
        g_bdsc_engine->asrnlp_ttsurl = NULL;
    }
}



bool need_skip_current_playing();
void dsp_fatal_handle(void);
extern audio_err_t ap_helper_raw_play_abort_outbf();
#define WK_FINISH (BIT0)


static void do_sop(int event)
{
    audio_player_state_t st = {0};
    audio_player_state_get(&st);
    ESP_LOGI(TAG, "Playing media is 0x%x, status is %d", st.media_src, st.status);
    if ((st.media_src == MEDIA_SRC_TYPE_MUSIC_HTTP) &&
        (st.status == AUDIO_PLAYER_STATUS_RUNNING)) {
        handle_play_cmd(CMD_HTTP_PLAY_PAUSE, NULL, 0);
        g_pre_player_need_resume = true;
    } else {
        // multiple wakeup/tts resume feature removed
        g_pre_player_need_resume = false;
        // Variables changed must be ahead of stop cmd
        // because stop cmd is blocking.
        stop_play = true;
        if (event == EVENT_RECV_A2DP_START_PLAY) {
            if (st.media_src != MEDIA_SRC_TYPE_MUSIC_A2DP) {
                handle_play_cmd(CMD_RAW_PLAY_STOP, NULL, 0);
            }
        }
        else {
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
    
    do_sop(EVENT_WAKEUP_TRIGGER);
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

    do_sop(event);
    
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

static char str_origin[16];
int32_t handle_bdsc_event(engine_elem_t elem)
{
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

                        player_reset();
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
                            if (desire == BDSC_CUSTOM_DESIRE_RESUME) {
                                if (g_pre_player_need_resume) {
                                    g_pre_player_need_resume = false;
                                    handle_play_cmd(CMD_HTTP_PLAY_RESUME, NULL, 0);
                                }
                                else {
                                    // Do not need stop cause player will
                                    // play new media to stop previous one.
                                    bdsc_play_hint(BDSC_HINT_NOT_FIND);
                                }
                            }
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
                cJSON *j_content, *j_custom_reply, *j_item;
                char *action_type_str = NULL, *type_str = NULL, *url = NULL, *origin = NULL;
                int i;
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
                            char *json_buf = NULL;
                            json_buf = audio_calloc(1, extern_result->buffer_length + 1);
                            memcpy(json_buf, extern_result->buffer + 12, extern_result->buffer_length);
                            desire = notify_bdsc_engine_event_to_user(BDSC_EVENT_ON_NLP_RESULT, (uint8_t *)json_buf, strlen((const char *)json_buf) + 1);
                            if (BDSC_CUSTOM_DESIRE_SKIP_DEFAULT == desire) {
                                free(json_buf);
                                json_buf = NULL;
                                break;
                            }
                            if (BDSC_CUSTOM_DESIRE_DEFAULT == desire) {
                                if (!(j_content = cJSON_Parse((const char *)json_buf))) {
                                    ESP_LOGE(TAG, "json format error");
                                    free(json_buf);
                                    json_buf = NULL;
                                    break;
                                }
                                free(json_buf);
                                json_buf = NULL;
                                /* 过滤 action_type 字段，决定 语音流的下发模式 */
                                if (!(action_type_str = BdsJsonObjectGetString(j_content, "action_type"))) {
                                    ESP_LOGE(TAG, "can not find action_type");
                                    BdsJsonPut(j_content);
                                    break;
                                }
                                origin = BdsJsonObjectGetString(j_content, "origin");
                                if(origin != NULL && strlen(origin)<sizeof(str_origin)) {
                                    strcpy(str_origin,origin);
                                } else {
                                    memset(str_origin,0,sizeof(str_origin));
                                }

                                if (!strncmp(action_type_str, "asrnlp_tts", strlen(action_type_str))) {
                                    /* 1. 如果是 asrnlp_tts 模式，直接 raw play 即可 */
                                    ESP_LOGI(TAG, "found tts type");
                                    handle_play_cmd(CMD_RAW_PLAY_START, NULL, 0);
                                } else if (!strncmp(action_type_str, "asrnlp_url", strlen(action_type_str)) ||
                                            !strncmp(action_type_str, "asrnlp_mix", strlen(action_type_str)) ||
                                            !strncmp(action_type_str, "asrnlp_ttsurl", strlen(action_type_str))) {
                                    ESP_LOGI(TAG, "found url type or mix type");

                                    if (g_bdsc_engine->in_duplex_mode) {
                                        ESP_LOGI(TAG, "we don't process this kind message In duplex mode");
                                        BdsJsonPut(j_content);
                                        break;
                                    }
                                    if (!(j_custom_reply = BdsJsonObjectGet(j_content, "custom_reply"))) {
                                        ESP_LOGE(TAG, "can not find action_type");
                                        BdsJsonPut(j_content);
                                        break;
                                    }
                                    if (cJSON_Array != BdsJsonGetType(j_custom_reply)) {
                                        ESP_LOGE(TAG, "custom_reply format error");
                                        BdsJsonPut(j_content);
                                        break;
                                    }
                                    for (i = 0; i < BdsJsonArrayLen(j_custom_reply); i++) {
                                        j_item = BdsJsonArrayGet(j_custom_reply, i);
                                        if ((type_str = BdsJsonObjectGetString(j_item, "type")) &&
                                                (!strncmp(type_str, "url", strlen(type_str))) &&
                                                (url = BdsJsonObjectGetString(j_item, "value"))) {
                                            ESP_LOGI(TAG, "found url: %s", url);
                                            break;
                                        }
                                    }
                                    if (!url) {
                                        ESP_LOGE(TAG, "can not find url");
                                        BdsJsonPut(j_content);
                                        break;
                                    }

                                    if (!strncmp(action_type_str, "asrnlp_url", strlen(action_type_str))) {
                                        if (!stop_play) {
                                            // Only handle http play because it has much time penalty.
                                            handle_play_cmd(CMD_HTTP_PLAY_START, (uint8_t *)url, strlen(url) + 1);
                                        }
                                    } else if ((!strncmp(action_type_str, "asrnlp_mix", strlen(action_type_str))) ||
                                            (!strncmp(action_type_str, "asrnlp_ttsurl", strlen(action_type_str)))) {
                                        ESP_LOGI(TAG, "found asrnlp_ttsurl or asrnlp_mix type");
                                        handle_play_cmd(CMD_RAW_PLAY_START, NULL, 0);
                                        if(strncmp(str_origin, MIGU_ORIGIN, strlen(str_origin)) &&\
                                           strncmp(str_origin, HIMALAYA_ORIGIN, strlen(str_origin)) &&\
                                           strncmp(str_origin, HIMALAYA_OLD_ORIGIN, strlen(str_origin)) &&\
                                           strncmp(str_origin, QING_ORIGIN, strlen(str_origin))) {         //music source is handled in app_music.c
                                            if (g_bdsc_engine->asrnlp_ttsurl) {
                                                free(g_bdsc_engine->asrnlp_ttsurl);
                                                g_bdsc_engine->asrnlp_ttsurl = NULL;
                                            }
                                            g_bdsc_engine->asrnlp_ttsurl = audio_strdup(url);
                                        }

                                    }
                                }
                                else {
                                    if (!strncmp(action_type_str, "asr_none", strlen(action_type_str))) {
                                        ESP_LOGI(TAG, "got action_type: asr_none");
                                    } else if (!strncmp(action_type_str, "asrnlp_none", strlen(action_type_str))) {
                                        ESP_LOGI(TAG, "got action_type: asrnlp_none");
                                    } else {
                                        ESP_LOGE(TAG, "unknown action_type");
                                    }
                                    // tts only case handled in ASR_END event.
                                    // Other action type handled here.
                                    // if (g_pre_player_need_resume) {
                                    //     g_pre_player_need_resume = false;
                                    //     handle_play_cmd(CMD_HTTP_PLAY_RESUME, NULL, 0);
                                    // }
                                }
                                BdsJsonPut(j_content);
                            }
                            if (json_buf) {
                                free(json_buf);
                                json_buf = NULL;
                            }
                            
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
                                int ret = handle_tts_data((char*)tts_data->buffer, tts_data->buffer_length);
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
                    case ASR_TTS_ENGINE_GOT_TTS_DATA: {
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

                        handle_play_cmd(CMD_RAW_PLAY_FEED_FINISH, NULL, 0);
                        if (g_bdsc_engine->in_duplex_mode) {
                            // FIXME: in duplex mode, waiting for 'haode' palying done
                            vTaskDelay(300 / portTICK_PERIOD_MS);
                        }
                        if (g_bdsc_engine->asrnlp_ttsurl) {
                            audio_err_t ret;
                            ret = audio_player_raw_waiting_finished();
                            // Mixplay stopped by user we can not play url.
                            // Stop at the tail of tts play sometimes and return
                            // value is not STOP_BY_USER. So we use stop_play to avoid
                            // unnecessary http play.
                            // Only handle http play because it has much time penalty.
                            if ((ret == ESP_OK) && !stop_play) {
                               handle_play_cmd(CMD_HTTP_PLAY_START, (uint8_t *)g_bdsc_engine->asrnlp_ttsurl, strlen(g_bdsc_engine->asrnlp_ttsurl) + 1);
                            }
                            free(g_bdsc_engine->asrnlp_ttsurl);
                            g_bdsc_engine->asrnlp_ttsurl = NULL;
                        } else {
                            // only tts data so resume previous url play.
                            if (g_bdsc_engine->in_duplex_mode) {
                                g_pre_player_need_resume = false;
                            } else {
                                audio_player_raw_waiting_finished();
                                if (g_pre_player_need_resume) {
                                    g_pre_player_need_resume = false;
                                    if(strncmp(str_origin, MIGU_ORIGIN, strlen(str_origin)) &&\
                                           strncmp(str_origin, HIMALAYA_ORIGIN, strlen(str_origin)) &&\
                                           strncmp(str_origin, HIMALAYA_OLD_ORIGIN, strlen(str_origin)) &&\
                                           strncmp(str_origin, QING_ORIGIN, strlen(str_origin))) {
                                        handle_play_cmd(CMD_HTTP_PLAY_RESUME, NULL, 0);            //Don't resume when receive music query
                                    }
                                }
                            }
                        }
                        if (g_bdsc_engine->in_duplex_mode) {
                            //bdsc_stop_asr();
                            // 纯识别，需要设置 backtine == 0
                            bdsc_start_asr(0);
                            g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_STARTTED;
                        }
                        break;
                    }
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
                        player_reset();

                        ESP_LOGI(TAG, "==> triggered, start asr");
                        g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_STARTTED;
                        break;
                    default:
                        ESP_LOGI(TAG, "%d invalid state %d", __LINE__, g_bdsc_engine->g_asr_tts_state);
                        break;
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
                player_reset();
                g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_GOT_ASR_END;
                handle_play_cmd(CMD_HTTP_PLAY_MQTT, (uint8_t *)elem.data, elem.data_len);
                break;
            }
        case EVENT_RECV_A2DP_START_PLAY: {
                bdsc_stop_asr();
                player_reset();
                g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_GOT_ASR_END;
                handle_play_cmd(CMD_A2DP_PLAY_START, (uint8_t *)elem.data, elem.data_len);
                break;
            }
        case EVENT_RECV_ACTIVE_TTS_PLAY: {
                audio_player_stop();
                ap_helper_raw_play_abort_outbf();
                bdsc_stop_asr();
                player_reset();
                g_bdsc_engine->g_asr_tts_state = ASR_TTS_ENGINE_GOT_ASR_END;
                start_tts((char *)elem.data);
                break;
            }
        case EVENT_TTS_BEGIN:
            handle_play_cmd(CMD_RAW_PLAY_START, NULL, 0);
            break;
        case EVENT_TTS_RESULT:
            handle_play_cmd(CMD_RAW_PLAY_FEED_DATA, (uint8_t *)elem.data, elem.data_len);
            break;
        case EVENT_TTS_END:
            handle_play_cmd(CMD_RAW_PLAY_FEED_FINISH, NULL, 0);
            audio_player_raw_waiting_finished();
            break;
        case EVENT_DSP_LOAD_FAILED: {
            ESP_LOGW(TAG, "---> EVENT_DSP_LOAD_FAILED");
            if (!g_bdsc_engine->dsp_detect_error) {
                g_bdsc_engine->dsp_detect_error = true;
                bdsc_play_hint(BDSC_HINT_DSP_LOAD_FAIL);
                vTaskDelay(5000 / portTICK_PERIOD_MS); // make sure play done
            }
            break;
        }
        default:
            ESP_LOGE(TAG, "!!! unknow event !!!");
            break;
    }

    stop_play = false;
    
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

void event_engine_elem_EnQueque(int event, uint8_t *buffer, size_t len)
{
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
        ESP_LOGI(TAG, "Stack: %d", uxTaskGetStackHighWaterMark(NULL));
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

