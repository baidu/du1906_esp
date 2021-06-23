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
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "audio_player_helper.h"
#include "audio_player_type.h"
#include "audio_player.h"
#include "display_service.h"

#include "raw_play_task.h"
#include "audio_tone_uri.h"
#include "audio_player_pipeline_int_tone.h"
#include "bdsc_engine.h"

#define TAG "raw_play"

extern display_service_handle_t g_disp_serv;

void handle_play_cmd(int cmd, uint8_t *buffer, size_t len)
{
    esp_err_t ret = ESP_OK;
    
    audio_player_wait_tone();
    audio_player_waiting_stop();
    
    switch (cmd) {
        case CMD_RAW_PLAY_START:
            //audio_player_stop();
            ESP_LOGW(TAG, "==> CMD_RAW_PLAY_START");
            audio_player_tone_play("raw://sdcard/ut/test.mp3", false, false, MEDIA_SRC_TYPE_MUSIC_RAW);
            //audio_player_music_play("raw://sdcard/ut/test.mp3", 0, MEDIA_SRC_TYPE_MUSIC_RAW);
            break;
        case CMD_RAW_PLAY_FEED_DATA:
            ESP_LOGW(TAG, "==> CMD_RAW_PLAY_FEED_DATA");
            // Sometime need about 1000ms return from function.
            // Can asynchronously call stop function to stop it immediately.
            audio_player_raw_feed_data(buffer, len);

            break;
        case CMD_RAW_PLAY_FEED_FINISH:
            ESP_LOGW(TAG, "==> CMD_RAW_PLAY_FEED_FINISH");
            audio_player_raw_feed_finish();

            break;
        case CMD_RAW_PLAY_STOP:
            ESP_LOGW(TAG, "==> CMD_RAW_PLAY_STOP");
            audio_player_stop();

            break;
        case CMD_HTTP_PLAY_START:
            ESP_LOGW(TAG, "==> CMD_HTTP_PLAY_START");
            if (g_bdsc_engine->in_ota_process_flag) {
                ESP_LOGE(TAG, "in ota process, skip http play");
                break;
            }
            ret = audio_player_music_play((const char *)buffer, 0, MEDIA_SRC_TYPE_MUSIC_HTTP);
            if (ret != ESP_OK && ret != ESP_ERR_AUDIO_STOP_BY_USER) {
                //audio_player_tone_play(tone_uri[TONE_TYPE_UNSTEADY], false, false, MEDIA_SRC_TYPE_TONE_FLASH);
            }
            break;
        case CMD_A2DP_PLAY_START:
            ESP_LOGW(TAG, "==> CMD_A2DP_PLAY_START");
            // Do not check return value cause always can play
            audio_player_music_play((const char *)buffer, 0, MEDIA_SRC_TYPE_MUSIC_A2DP);
            display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_BT_CONNECTED, 0);
            break;
        case CMD_HTTP_PLAY_PAUSE:
            ESP_LOGW(TAG, "==> CMD_HTTP_PLAY_PAUSE");
            audio_player_pause();
            break;
        case CMD_HTTP_PLAY_RESUME:
            ESP_LOGW(TAG, "==> CMD_HTTP_PLAY_RESUME");
            audio_player_resume();
            break;
        case CMD_HTTP_PLAY_MQTT:
            ESP_LOGW(TAG, "==> CMD_HTTP_PLAY_MQTT");
            if (g_bdsc_engine->silent_mode) {
                ESP_LOGI(TAG, "in silent mode, skip play");
                return;
            }
            // MQTT url play is not user's conscious stuff.
            // So we do not care about its result.
            audio_player_music_play((const char *)buffer, 0, MEDIA_SRC_TYPE_MUSIC_HTTP);
            break;
        default:
            break;
    }
}

