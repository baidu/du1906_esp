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

#define TAG "PLAY_POLICY"

extern TimerHandle_t next_music_timer_handle;

void play_tone_by_id(bdsc_hint_type_t id)
{
    switch (id) {
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

void app_music_play_policy(music_queue_t pQueue_data)
{
    int ret = 0;
    raw_data_t *raw = NULL;
    music_t *current_music = pls_get_current_music(g_pls_handle);
    music_t *second_music = pls_get_second_music(g_pls_handle);

    ESP_LOGE(TAG, "++++++++++++++++++++++++++++ app_music_play_policy");
    pls_set_current_music_player_state(g_pls_handle, RUNNING_STATE);

    // policy 0: delete  interrupted speech music
    if (second_music && second_music->type == SPEECH_MUSIC) {
        ESP_LOGE(TAG, "policy 0: delete  interrupted speech music");
        pls_delete_second_music(g_pls_handle);
    }

    // policy 1: tts + url case,  wait  tts playing done
    if (pQueue_data.type == URL_MUSIC) {
        ESP_LOGE(TAG, "policy 1: tts + url case");
        if (pQueue_data.action == PLAY_MUSIC || pQueue_data.action == CHANGE_TO_NEXT_MUSIC) {
            if (pQueue_data.action_type == RAW_TTS || pQueue_data.action_type == TTS_URL) {
                handle_play_cmd(CMD_RAW_PLAY_START, NULL, 0);
                return;
            }
        }
    }

    // policy 2: resume case
    if (pQueue_data.type == ALL_TYPE) {
        ESP_LOGE(TAG, "policy 2: resume case");
        if (pQueue_data.action == NEXT_MUSIC) {    // 播放列表，一首播放完，自动播放下一首
            // dont delet head music, because we have delete before
        } else if (pQueue_data.action == PLAY_MUSIC) { // 需要续播
            handle_play_cmd(CMD_HTTP_PLAY_RESUME, NULL, 0);
            return;
        }
    }

    // policy 3: speech music playing, raw_play_start if necessary
    if (pQueue_data.type == SPEECH_MUSIC) {
        ESP_LOGE(TAG, "policy 3: speech music playing, raw_play_start if necessary");
        if (pQueue_data.action_type == RAW_TTS ||
            pQueue_data.action_type == RAW_MIX ||
            pQueue_data.action_type == TTS_URL) {

            handle_play_cmd(CMD_RAW_PLAY_START, NULL, 0);
            return;
        }
        // else if (pQueue_data.action_type == ONLY_URL) {
        //     if (pQueue_data.data) {
        //         handle_play_cmd(CMD_HTTP_PLAY_START, (uint8_t *)pQueue_data.data, 0);
        //         return;
        //     }
        // }
    }

    // policy 4: device control case, '好的' need raw_play
    if ((pQueue_data.type == MUSIC_CTL_CONTINUE ||
        pQueue_data.type == MUSIC_CTL_PAUSE ||
        pQueue_data.type == MUSIC_CTL_STOP) &&
        pQueue_data.action_type == RAW_TTS) {
        
        ESP_LOGE(TAG, "policy 4: device control case, '好的' need raw_play");
        handle_play_cmd(CMD_RAW_PLAY_START, NULL, 0);
        return;
    }

    /* policy 5: raw tts case
     *        We must gurantee that the current music type is what we want.
     *        And last raw pkt need special handling.
     *        What's more, play url if necessary.
     *        ....
     */
    if (pQueue_data.type == RAW_TTS_DATA) {
        ESP_LOGE(TAG, "policy 5: raw tts case");
        if (!current_music) {
            ESP_LOGE(TAG, "no current music, bug!!!");
            return;
        }
        if (!(raw = (raw_data_t*)pQueue_data.data) ||
            !(raw->raw_data)) {
            ESP_LOGE(TAG, "raw data is empty");
            return;
        }
        if (current_music->action_type == RAW_TTS ||
            current_music->action_type == RAW_MIX ||
            current_music->action_type == TTS_URL) {
            
            handle_play_cmd(CMD_RAW_PLAY_FEED_DATA, (uint8_t *)raw->raw_data, raw->raw_data_len);
            if (raw->is_end) {
                ret = audio_player_raw_feed_finish();
                ESP_LOGE(TAG, "audio_player_raw_feed_finish ret: %d", ret);
                ret = audio_player_raw_waiting_finished();
                ESP_LOGE(TAG, "audio_player_raw_waiting_finished ret: %d", ret);
                
                if (current_music->action_type == RAW_MIX) {
                    ESP_LOGE(TAG, "++111++");
                    // '南京' case, no need resume, remove music anyway
                    if (g_bdsc_engine->cur_in_asr_session) {
                        // note: if current in an asr session, skip http
                        ESP_LOGE(TAG, "In asr session, skip mix HTTP!!!!");
                    } else {
                        handle_play_cmd(CMD_HTTP_PLAY_START, (uint8_t *)current_music->data, 0);
                    }
                    pls_delete_head_music(g_pls_handle);
                } else if (current_music->action_type == TTS_URL) {
                    ESP_LOGE(TAG, "++222++");
                    // '宝宝巴士' case, need resume, keep music
                    //handle_play_cmd(CMD_HTTP_PLAY_START, (uint8_t *)current_music->data, 0);
                    audio_free(raw->raw_data);
                    audio_free(raw);

                    while (g_bdsc_engine->cur_in_asr_session) {
                        ESP_LOGE(TAG, "In asr session, delay HTTP Play!!!!");
                        vTaskDelay(300 / portTICK_PERIOD_MS);
                        if (g_bdsc_engine->need_skip_current_pending_http_part) {
                            ESP_LOGE(TAG, "need_skip_current_pending_http_part true, skip HTTP Play!!!!");
                            g_bdsc_engine->need_skip_current_pending_http_part = false;
                            return;
                        }
                    }
                    
                    goto URL_MUSIC_PLAY;
                } else if (current_music->action_type == RAW_TTS && current_music->type == SPEECH_MUSIC) {
                    ESP_LOGE(TAG, "++333++");
                    // '今天天气' case, no need resume, remove music anyway
                    pls_delete_head_music(g_pls_handle);
                    // resume if necessary
                    current_music = pls_get_current_music(g_pls_handle);
                    if (current_music && current_music->play_state == PAUSE_STATE) {
                        // note: if current in an asr session, need delay resume. refer to issue 381
                        if (g_bdsc_engine->cur_in_asr_session) {
                            ESP_LOGE(TAG, "In asr session, delay HTTP Resume!!!!");
                        } else {
                            handle_play_cmd(CMD_HTTP_PLAY_RESUME, NULL, 0);
                            current_music->play_state = RUNNING_STATE;
                        }
                    }
                } else if (current_music->type == URL_MUSIC) {
                    ESP_LOGE(TAG, "++444++");
                    // '播放周杰伦的歌' case, need resume, keep music
                    ESP_LOGE(TAG, "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<");
                    EventBits_t res_bit = xEventGroupWaitBits(g_get_url_evt_group, 
                        GET_URL_SUCCESS | GET_URL_FAIL, true, false, (5 * 1000));
                    ESP_LOGE(TAG, ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
                    if (res_bit & GET_URL_SUCCESS) {
                        ESP_LOGE(TAG, "async url success: %s", g_migu_music_url_out);
                        current_music->data = audio_strdup(g_migu_music_url_out);
                        xEventGroupClearBits(g_get_url_evt_group, GET_URL_REQUEST | GET_URL_SUCCESS | GET_URL_FAIL);
                        audio_free(raw->raw_data);
                        audio_free(raw);

                        while (g_bdsc_engine->cur_in_asr_session) {
                            ESP_LOGE(TAG, "In asr session, delay HTTP Play!!!!");
                            vTaskDelay(300 / portTICK_PERIOD_MS);
                            if (g_bdsc_engine->need_skip_current_pending_http_part) {
                                ESP_LOGE(TAG, "need_skip_current_pending_http_part true, skip HTTP Play!!!!");
                                g_bdsc_engine->need_skip_current_pending_http_part = false;
                                return;
                            }
                        }

                        goto URL_MUSIC_PLAY;
                    } else if (res_bit & GET_URL_FAIL) {
                        ESP_LOGE(TAG, "async url fail!!");
                        xEventGroupClearBits(g_get_url_evt_group, GET_URL_REQUEST | GET_URL_SUCCESS | GET_URL_FAIL);
                        current_music->data = NULL;
                    } else {
                        ESP_LOGE(TAG, "xEventGroupWaitBits timeout?????");
                        current_music->data = NULL;
                        xEventGroupClearBits(g_get_url_evt_group, GET_URL_REQUEST | GET_URL_SUCCESS | GET_URL_FAIL);
                    }

                    pls_delete_head_music(g_pls_handle); // fetch url fail, delete music
                    audio_free(raw->raw_data);
                    audio_free(raw);
                    return;
                } else if (current_music->type == ACTIVE_TTS) {
                    ESP_LOGE(TAG, "++555++");
                    // '您所播放的歌曲版权已过期' case, no need resume, remove music anyway
                    pls_delete_head_music(g_pls_handle);
                } else if (current_music->type == MUSIC_CTL_CONTINUE) {
                    ESP_LOGE(TAG, "++666++");
                    // '继续' case, no need resume, remove music anyway
                    pls_delete_head_music(g_pls_handle);
                    // resume if necessary
                    current_music = pls_get_current_music(g_pls_handle);
                    if (current_music && (current_music->play_state == PAUSE_STATE || 
                        current_music->play_state == FORCE_PAUSE_STATE)) {

                        handle_play_cmd(CMD_HTTP_PLAY_RESUME, NULL, 0);
                        current_music->play_state = RUNNING_STATE;
                    }
                    audio_free(raw->raw_data);
                    audio_free(raw);
                    return;
                } else if (current_music->type == MUSIC_CTL_PAUSE) {
                    ESP_LOGE(TAG, "++777++");
                    // '暂停' case, no need resume, remove music anyway
                    pls_delete_head_music(g_pls_handle);
                    // pause if necessary
                    current_music = pls_get_current_music(g_pls_handle);
                    if (current_music) {
                        handle_play_cmd(CMD_HTTP_PLAY_PAUSE, NULL, 0);
                        current_music->play_state = FORCE_PAUSE_STATE;
                    }
                    audio_free(raw->raw_data);
                    audio_free(raw);
                    return;
                } else if (current_music->type == MUSIC_CTL_STOP) {
                    ESP_LOGE(TAG, "++888++");
                    // '停止' case, no need resume, remove music anyway
                    pls_delete_head_music(g_pls_handle);
                    // delete music
                    pls_delete_head_music(g_pls_handle);
                    audio_player_clear_audio_info();
                    audio_free(raw->raw_data);
                    audio_free(raw);
                    return;
                } else {
                    ESP_LOGE(TAG, "++999++, %d, %d", current_music->action_type, current_music->type);
                }
            }
        }
        audio_free(raw->raw_data);
        audio_free(raw);
        return;
    }

    /* policy 6: tone play case
     *          note: '-3005 not find effective speech' case, dont play
     */
    if (pQueue_data.type == TONE_MUSIC) {
        ESP_LOGE(TAG, "policy 6: tone play case");
        bdsc_hint_type_t *id = (bdsc_hint_type_t*)pQueue_data.data;
        if (*id == BDSC_HINT_NOT_FIND) {
            if (second_music && second_music->play_state == PAUSE_STATE) {
                handle_play_cmd(CMD_HTTP_PLAY_RESUME, NULL, 0);
                pls_delete_head_music(g_pls_handle);
                pls_set_current_music_player_state(g_pls_handle, RUNNING_STATE);
                return;
            }
        }

        play_tone_by_id(*id);
        pls_delete_head_music(g_pls_handle); // remove tone music
        return;
    }

    // policy 7: mqtt url play case
    if (pQueue_data.type == MQTT_URL) {
        ESP_LOGE(TAG, "policy 7: mqtt url play case");
        handle_play_cmd(CMD_HTTP_PLAY_START, (uint8_t *)pQueue_data.data, 0);
        return;
    }

    // policy 8: bt play
    if (pQueue_data.type == A2DP_PLAY) {
        ESP_LOGE(TAG, "policy 8: bt play case");
        handle_play_cmd(CMD_A2DP_PLAY_START, (uint8_t *)pQueue_data.data, 0);
        return;
    }

    // policy 9: active tts
    if (pQueue_data.type == ACTIVE_TTS) {
        ESP_LOGE(TAG, "policy 9: active tts case");
        handle_play_cmd(CMD_HTTP_PLAY_START, (uint8_t *)pQueue_data.data, 0);
        return;
    }


URL_MUSIC_PLAY:
    // policy 10: play default music and cache next song (if necessary)
    current_music = pls_get_current_music(g_pls_handle);
    if (current_music && current_music->data) {
        ESP_LOGE(TAG, "policy 10: play default music case");
        handle_play_cmd(CMD_HTTP_PLAY_START, (uint8_t *)current_music->data, strlen(current_music->data) + 1);
        current_music->play_state = RUNNING_STATE;
        if (current_music->user_cb) {
            current_music->user_cb(current_music);
        }
    }
}
