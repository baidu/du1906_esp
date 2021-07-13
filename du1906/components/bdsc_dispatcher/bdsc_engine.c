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

#include "audio_mem.h"
#include "bds_client_command.h"
#include "bds_client_context.h"
#include "bds_client_event.h"
#include "bds_client_params.h"
#include "bds_client.h"
#include "bdsc_tools.h"
#include "bds_common_utility.h"
#include "bdsc_engine.h"
#include "audio_player_helper.h"
#include "audio_player_type.h"
#include "audio_player.h"
#include "audio_tone_uri.h"
#include "display_service.h"
#include "esp_wifi_setting.h"
#include "blufi_config.h"
#include "raw_play_task.h"
#include "bdsc_ota_partitions.h"
#include "bds_private.h"
#include "app_music.h"

#define  MAIN_TAG  "BDSC_ENGINE"
#define TTS_URI_BUFFER_LEN 4096

bdsc_engine_handle_t g_bdsc_engine;
extern display_service_handle_t g_disp_serv;
extern esp_wifi_setting_handle_t g_wifi_setting;

static int32_t bdsc_event_callback(bds_client_event_t *event, void *custom)
{
    if (event != NULL) {
        switch (event->key) {
        case EVENT_ASR_ERROR:
        {
            bdsc_event_error_t *error = (bdsc_event_error_t*)event->content;
            event_engine_elem_EnQueque(EVENT_ASR_ERROR, error, sizeof(bdsc_event_error_t) + error->info_length);
            break;
        }
        case EVENT_ASR_CANCEL:
        {
            event_engine_elem_EnQueque(EVENT_ASR_CANCEL, event->content, sizeof(bdsc_event_process_t));
            break;
        }
        case EVENT_ASR_BEGIN:
        {
            event_engine_elem_EnQueque(EVENT_ASR_BEGIN, event->content, sizeof(bdsc_event_process_t));
            break;
        }
        case EVENT_ASR_RESULT:
        {
            bdsc_event_data_t * asr_result = (bdsc_event_data_t*)event->content;
            event_engine_elem_EnQueque(EVENT_ASR_RESULT, event->content, sizeof(bdsc_event_data_t) + asr_result->buffer_length);
            break;
        }
        case EVENT_ASR_EXTERN_DATA:
        {
            bdsc_event_data_t * extern_data = (bdsc_event_data_t*)event->content;
            event_engine_elem_EnQueque(EVENT_ASR_EXTERN_DATA, event->content, sizeof(bdsc_event_data_t) + extern_data->buffer_length);
            break;
        }
        case EVENT_ASR_TTS_DATA:
        {
            bdsc_event_data_t * tts_data = (bdsc_event_data_t*)event->content;
            event_engine_elem_EnQueque(EVENT_ASR_TTS_DATA, event->content, sizeof(bdsc_event_data_t) + tts_data->buffer_length);
            break;
        }
        case EVENT_ASR_END:
        {
            event_engine_elem_EnQueque(EVENT_ASR_END, event->content, sizeof(bdsc_event_process_t));
            break;
        }
        case EVENT_WAKEUP_TRIGGER:
        {
            display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_WAKEUP_ON, 0);
            event_engine_elem_EnQueque(EVENT_WAKEUP_TRIGGER, event->content, sizeof(bdsc_event_wakeup_t));
            break;
        }
        case EVENT_WAKEUP_OFFLINE_DIRECTIVE:
        {
            display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_WAKEUP_ON, 0);
            bdsc_event_data_t *offline_data = (bdsc_event_data_t*)event->content;
            event_engine_elem_EnQueque(EVENT_WAKEUP_OFFLINE_DIRECTIVE, offline_data, sizeof(bdsc_event_wakeup_t)\
                                       + offline_data->buffer_length);
            break;
        }
        case EVENT_WAKEUP_ERROR:
        {
            event_engine_elem_EnQueque(EVENT_WAKEUP_ERROR, event->content, sizeof(bdsc_event_error_t));
            break;
        }
        case EVENT_LINK_CONNECTED:
        {
            bdsc_event_data_t *connect_data = (bdsc_event_data_t*)event->content;
            event_engine_elem_EnQueque(EVENT_LINK_CONNECTED, connect_data, sizeof(bdsc_event_data_t)\
                                      + connect_data->buffer_length);
            break;
        }
        case EVENT_LINK_DISCONNECTED:
        {
            bdsc_event_data_t *dis_data = (bdsc_event_data_t*)event->content;
            event_engine_elem_EnQueque(EVENT_LINK_DISCONNECTED, dis_data, sizeof(bdsc_event_data_t)\
                                       + dis_data->buffer_length);
            break;
        }
        case EVENT_LINK_ERROR:
        {
            bdsc_event_error_t *error = (bdsc_event_error_t*)event->content;
            event_engine_elem_EnQueque(EVENT_LINK_ERROR, error, sizeof(bdsc_event_error_t) + error->info_length);
            break;
        }
        case EVENT_RECORDER_DATA:
        {
            bdsc_event_data_t *pcm_data = (bdsc_event_data_t*)event->content;
            event_engine_elem_EnQueque(EVENT_RECORDER_DATA, pcm_data, sizeof(bdsc_event_data_t)\
                                      + pcm_data->buffer_length);
            break;
        }
        case EVENT_RECORDER_ERROR:
        {
            bdsc_event_error_t *error = (bdsc_event_error_t*)event->content;
            event_engine_elem_EnQueque(EVENT_RECORDER_ERROR, error, sizeof(bdsc_event_error_t) + error->info_length);
            break;
        }
        case EVENT_SDK_START_COMPLETED:
        {
            event_engine_elem_EnQueque(EVENT_SDK_START_COMPLETED, NULL, 0);
            break;
        }
        case EVENT_DSP_FATAL_ERROR:
        {
            bdsc_event_error_t *error = (bdsc_event_error_t*)event->content;
            event_engine_elem_EnQueque(EVENT_DSP_FATAL_ERROR, error, sizeof(bdsc_event_error_t) + error->info_length);
            break;
        }
        default:
            ESP_LOGE(MAIN_TAG, "!!! unknow event %d!!!", event->key);
            break;
        }
    }
    return 0;
}

void start_sdk()
{
    if (g_bdsc_engine->cuid[0] == '\0') {
        if (g_bdsc_engine->g_vendor_info->fc &&
            g_bdsc_engine->g_vendor_info->pk &&
            g_bdsc_engine->g_vendor_info->ak) {

            snprintf(g_bdsc_engine->cuid, sizeof(g_bdsc_engine->cuid), "%s%s%s",
                                                    g_bdsc_engine->g_vendor_info->fc,
                                                    g_bdsc_engine->g_vendor_info->pk,
                                                    g_bdsc_engine->g_vendor_info->ak);
            ESP_LOGI(MAIN_TAG, "cuid: %s", g_bdsc_engine->cuid);
        } else {
            ESP_LOGE(MAIN_TAG, "generate cuid failed!");
            return;
        }
    }
    int err = bds_client_start(g_bdsc_engine->g_client_handle);
    if (err) {
        ESP_LOGE(MAIN_TAG, "bds_client_start failed");
        bdsc_play_hint(BDSC_HINT_DSP_LOAD_FAIL);
    }
}

void config_sdk(bds_client_handle_t handle)
{
    esp_partition_t *partition = NULL;

    ESP_LOGI(MAIN_TAG, "bootbale dsp lable: %s", bdsc_partitions_get_bootable_dsp_label());
    partition = (esp_partition_t *)esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
            ESP_PARTITION_SUBTYPE_ANY, bdsc_partitions_get_bootable_dsp_label());
    if (partition == NULL) {
        ESP_LOGE(MAIN_TAG, "Can not find dsp partition");
        return;
    }
    char sn[BDSC_MAX_UUID_LEN];
    generate_uuid(sn);
    char *pam_data = "";//iot don't authentication when connect server
    bdsc_engine_params_t *engine_params = bdsc_engine_params_create_wrapper(sn, "du1906_app", strlen(pam_data) + 1, pam_data);
    bds_client_params_t params;
    memset(&params, 0 , sizeof(bds_client_params_t));
    params.engine_params = engine_params;
    params.dsp_subtype = partition->subtype;
    bds_client_config(handle, &params);
    bdsc_engine_params_destroy(engine_params);
}

void stop_sdk()
{
    bds_client_stop(g_bdsc_engine->g_client_handle);
}

static void bdsc_sdk_init()
{
    bds_client_context_t context;
    g_bdsc_engine->g_client_handle = bds_client_create(&context);
    config_sdk(g_bdsc_engine->g_client_handle);
    bds_client_set_event_listener(g_bdsc_engine->g_client_handle, bdsc_event_callback, NULL);
    bds_set_log_level(g_bdsc_engine->cfg->log_level);
    start_sdk();
    bdsc_link_start();
    bdsc_start_wakeup();

    esp_log_level_set("wakeup_hal", ESP_LOG_WARN);
}

void mqtt_task_init();
void startup_system();

esp_err_t my_bdsc_auth_cb(auth_result_t *result)
{
    int err = 0;

    if (!result) {
        return ESP_FAIL;
    }
    if (ESP_OK != result->err) {
        return ESP_FAIL;
    }
    ESP_LOGI(MAIN_TAG, "auth restul: broker: %s, clientID: %s, username: %s, pwd: %s\n",
                                result->broker,
                                result->client_id,
                                result->mqtt_username,
                                result->mqtt_password);

    err |= profile_key_set(PROFILE_KEY_TYPE_MQTT_BROKER, (char*)result->broker);
    err |= profile_key_set(PROFILE_KEY_TYPE_MQTT_USERNAME, (char*)result->mqtt_username);
    err |= profile_key_set(PROFILE_KEY_TYPE_MQTT_PASSWD, (char*)result->mqtt_password);
    err |= profile_key_set(PROFILE_KEY_TYPE_MQTT_CID, (char*)result->client_id);
    if (err) {
        ESP_LOGE(MAIN_TAG, "profile save failed!!");
        return ESP_FAIL;
    }
    ESP_LOGI(MAIN_TAG, "profile save OK");
    startup_system();

    return ESP_OK;
}

void startup_system()
{
    bdsc_asr_tts_engine_init();

    bdsc_sdk_init();

    mqtt_task_init();
}

void check_smartconfig_on_boot();

bdsc_engine_handle_t bdsc_engine_init(bdsc_engine_config_t *cfg)
{
    ESP_LOGI(MAIN_TAG, "APP version is %s", APP_VER);

    g_bdsc_engine = (bdsc_engine_handle_t)audio_calloc(1, sizeof(struct bdsc_engine));
    assert(g_bdsc_engine != NULL);
    g_bdsc_engine->userData = NULL;
    g_bdsc_engine->cfg = (bdsc_engine_config_t *)audio_calloc(1, sizeof(bdsc_engine_config_t));
    assert(g_bdsc_engine->cfg != NULL);
    memcpy(g_bdsc_engine->cfg, cfg, sizeof(bdsc_engine_config_t));
    g_bdsc_engine->cur_vol = 40;
    audio_player_vol_set(g_bdsc_engine->cur_vol);

    SNTP_init();
    profile_init();

    bdsc_play_hint(BDSC_HINT_BOOT);
    display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_BOOT, 0);

    vendor_info_t *g_vendor_info = g_bdsc_engine->g_vendor_info;
    char customer_data[256];
    if (strlen(g_vendor_info->fc) + strlen(g_vendor_info->pk) + 
        strlen(g_vendor_info->ak) + strlen(g_vendor_info->sk) + 3 >= sizeof(customer_data)) {

        ESP_LOGE(MAIN_TAG, "bad customer data, should not be here");
        sprintf(customer_data, "%s", "bad customer data");
    } else {
        sprintf(customer_data, "%s#%s#%s#%s", g_vendor_info->fc, 
                g_vendor_info->pk, g_vendor_info->ak, g_vendor_info->sk);
    }
    blufi_set_customized_data(g_wifi_setting, customer_data, strlen(customer_data));
    if (!g_bdsc_engine->sc_customer_data) {
        g_bdsc_engine->sc_customer_data = audio_strdup(customer_data);
    }
    check_smartconfig_on_boot();
    if (g_vendor_info->mqtt_broker[0] == '\0' ||
        g_vendor_info->mqtt_username[0] == '\0' ||
        g_vendor_info->mqtt_password[0] == '\0' ||
        g_vendor_info->mqtt_cid[0] == '\0') {
        start_auth_thread(my_bdsc_auth_cb);
    } else {
        startup_system();
    }

    return g_bdsc_engine;
}

esp_err_t bdsc_engine_open_bt()
{
    ESP_LOGI(MAIN_TAG, "[ * ] [Enter BT mode]");
    char *a2dp_url = "aadp://44100:2@bt/sink/stream.pcm";
    event_engine_elem_EnQueque(EVENT_RECV_A2DP_START_PLAY, (uint8_t *)a2dp_url, strlen(a2dp_url) + 1);
    return 0;
}

esp_err_t bdsc_engine_close_bt()
{
    audio_player_stop();
    ESP_LOGI(MAIN_TAG, "[ * ] [Exit BT mode]");
    display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_BT_DISCONNECTED, 0);
    return 0;
}


int bdsc_engine_get_internal_state(bdsc_engine_handle_t client)
{
    return  client->g_asr_tts_state;
}

void bdsc_engine_skip_current_session_playing_once_flag_set(bdsc_engine_handle_t client)
{
    client->skip_tts_playing_once = true;
}

void bdsc_engine_skip_current_session_playing_once_flag_unset(bdsc_engine_handle_t client)
{
    client->skip_tts_playing_once = false;
}

esp_err_t bdsc_engine_deinit(bdsc_engine_handle_t client)
{
    // todo
    return ESP_OK;
}

void bdsc_engine_start_tts(const char *tts_text)
{
    ESP_LOGI(MAIN_TAG, "==> bdsc_engine_start_tts");
    char *buffer = audio_calloc(1, TTS_URI_BUFFER_LEN);
    if(buffer == NULL) {
        ESP_LOGE(MAIN_TAG, "%s|%d: malloc fail!", __func__, __LINE__);
        return;
    }
    buffer[0] = '\0';
    strncat(buffer, ACTIVE_TTS_PREFIX, TTS_URI_BUFFER_LEN - strlen(buffer));
    strncat(buffer, tts_text, TTS_URI_BUFFER_LEN - strlen(buffer));
    send_music_queue(ACTIVE_TTS, buffer);
}

