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

#include "audio_error.h"
#include "audio_mem.h"

#include "bds_client_command.h"
#include "bds_client_context.h"
#include "bds_client_event.h"
#include "bds_client_params.h"
#include "bds_client.h"
#include "bds_common_utility.h"
#include "bdsc_cmd.h"
#include "bdsc_event_dispatcher.h"
#include "bdsc_profile.h"
#include "generate_pam.h"
#include "bdsc_tools.h"
#include "bdsc_engine.h"
#include "bds_private.h"

void bdsc_start_asr(int back_time)
{
    char *_uuid;
    char *pam_data;
    int param_max_len = 4096;


    _uuid = audio_malloc(BDSC_MAX_UUID_LEN);
    AUDIO_MEM_CHECK("start_asr", _uuid, return);
    bds_generate_uuid(_uuid);
    pam_data = audio_malloc(param_max_len);
    AUDIO_MEM_CHECK("start_asr", pam_data, {
        free(_uuid);
        return;
    });
    if (generate_asr_thirdparty_pam(pam_data, param_max_len, 0) < 0) {
        free(_uuid);
        free(pam_data);
        return;
    }
    if (g_bdsc_engine->cuid[0] == '\0') {
        free(_uuid);
        free(pam_data);
        ESP_LOGE("BDSC_CMD", "cuid is null ,should not be here");
        return;
    }
    bdsc_asr_params_t *asr_params = bdsc_asr_params_create_wrapper(bdsc_asr_params_create, _uuid, 16000, g_bdsc_engine->cuid, back_time,
                                                           strlen(pam_data) + 1, pam_data);
    bds_client_command_t asr_start = {
            .key = CMD_ASR_START,
            .content = asr_params,
            .content_length = sizeof(bdsc_asr_params_t) + strlen(pam_data) + 1
    };
    bds_client_send(g_bdsc_engine->g_client_handle, &asr_start);
    bdsc_asr_params_destroy(asr_params);

    free(pam_data);
    free(_uuid);
}

void bdsc_stop_asr()
{
    bds_client_command_t asr_cancel = {
            .key = CMD_ASR_CANCEL
    };
    bds_client_send(g_bdsc_engine->g_client_handle, &asr_cancel);
}

static int s_wp_num = WP_NUM_DEFAULT;
void bdsc_start_wakeup()
{
    if (g_bdsc_engine->dsp_detect_error) {
        ESP_LOGE("BDSC_CMD", "dsp load failed, skip wakeup functionality");
        return;
    }
    bdsc_wp_params_t params = {
        .wakeup_num =  s_wp_num
    };
    if (s_wp_num >= WP_NUM_TWO) {
        s_wp_num = WP_NUM_DEFAULT;
    } else {
        s_wp_num++;
    }
    bds_client_command_t wakeup_start = {
            .key = CMD_WAKEUP_START,
            .content = &params,
            .content_length = sizeof(bdsc_wp_params_t)
    };
    bds_client_send(g_bdsc_engine->g_client_handle, &wakeup_start);
}

void bdsc_stop_wakeup()
{
    bds_client_command_t wakeup_stop = {
            .key = CMD_WAKEUP_STOP
    };
    bds_client_send(g_bdsc_engine->g_client_handle, &wakeup_stop);
}

void bdsc_event_cancel()
{
    bds_client_command_t event_cancel = {
            .key = CMD_EVENTUPLOAD_CANCEL,
            .content = NULL,
            .content_length = 0
    };
    bds_client_send(g_bdsc_engine->g_client_handle, &event_cancel);
}

void bdsc_start_recorder()
{
    int type = RECORDER_TYPE_PCM0;
    bds_client_command_t command = {
            .key = CMD_RECORDER_START,
            .content = &type,
            .content_length = sizeof(RECORDER_TYPE_PCM0)
    };
    bds_client_send(g_bdsc_engine->g_client_handle, &command);
}

void bdsc_stop_recorder()
{
    int type = RECORDER_TYPE_PCM0;
    bds_client_command_t recorder_stop = {
            .key = CMD_RECORDER_STOP,
            .content = &type,
            .content_length = sizeof(RECORDER_TYPE_PCM0)
    };
    bds_client_send(g_bdsc_engine->g_client_handle, &recorder_stop);
}

void bdsc_link_start()
{
    bds_client_command_t link_start = {
            .key = CMD_LINK_START,
            .content = NULL,
            .content_length = 0
    };
    bds_client_send(g_bdsc_engine->g_client_handle, &link_start);
}

void bdsc_link_stop()
{
    bds_client_command_t link_stop = {
            .key = CMD_LINK_STOP,
            .content = NULL,
            .content_length = 0
    };
    bds_client_send(g_bdsc_engine->g_client_handle, &link_stop);
}


void bdsc_dynamic_config(char *config)
{
    if (g_bdsc_engine->g_client_handle == NULL || config == NULL) {
        return;
    }
    bds_client_command_t dynamic_config = {
            .key = CMD_DYNAMIC_CONFIG,
            .content = config,
            .content_length = strlen(config) + 1
    };
    bds_client_send(g_bdsc_engine->g_client_handle, &dynamic_config);
}

