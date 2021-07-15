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

const char *TAG = "BDSC_CMD";
void bdsc_start_asr(int back_time)
{
    char sn[BDSC_MAX_UUID_LEN] = {0};
    bds_generate_uuid(sn);
    char *pam_data = audio_calloc(1, 4096);
    if(pam_data == NULL) {
        ESP_LOGE(TAG, "audio_calloc fail!!!");
        return;
    }
    if (generate_asr_thirdparty_pam(pam_data, 4096, 0) < 0) {
        free(pam_data);
        return;
    }
    bdsc_asr_params_t *asr_params = bdsc_asr_params_create_wrapper(bdsc_asr_params_create, sn, 16000,\
                                                            g_bdsc_engine->cuid, back_time,\
                                                            0, strlen(pam_data) + 1, pam_data);
    bds_client_command_t asr_start = {
            .key = CMD_ASR_START,
            .content = asr_params,
            .content_length = sizeof(bdsc_asr_params_t) + strlen(pam_data) + 1
    };
    bds_client_send(g_bdsc_engine->g_client_handle, &asr_start);
    bdsc_asr_params_destroy(asr_params);
    free(pam_data);
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
    bdsc_wp_params_t params = {
        .wakeup_num =  s_wp_num,
#ifdef CONFIG_USE_OFFLINE_DIRECTIVE
        .offline_directive_duration = 3000 //the valid interval of wakeup, unit ms
#endif
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
    };
    bds_client_send(g_bdsc_engine->g_client_handle, &command);
}

void bdsc_stop_recorder()
{
    int type = RECORDER_TYPE_PCM0;
    bds_client_command_t recorder_stop = {
            .key = CMD_RECORDER_STOP,
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

