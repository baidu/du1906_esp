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
#include "stdio.h"
#include "string.h"
#include "bds_client_context.h"
#include "bds_common_utility.h"
#include "cJSON.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "bdsc_tools.h"
#include "generate_pam.h"
#include "bdsc_profile.h"
#include "audio_mem.h"
#include "bdsc_engine.h"
#include "bds_private.h"

#define     TAG     "GEN_PAM"

static cJSON* build_bdsc_methods(int option)
{
    cJSON *a = NULL;
    cJSON *n = NULL;

    if (g_bdsc_engine && g_bdsc_engine->cfg && g_bdsc_engine->cfg->bdsc_methods) {
        a = cJSON_CreateArray();
        if (option == 0) {
            if (g_bdsc_engine->cfg->bdsc_methods & BDSC_METHODS_ASR) {
                n = cJSON_CreateString("ASR");
                cJSON_AddItemToArray(a, n);
            }
            if (g_bdsc_engine->cfg->bdsc_methods & BDSC_METHODS_TTS) {
                n = cJSON_CreateString("TTS");
                cJSON_AddItemToArray(a, n);
            }
            if (g_bdsc_engine->cfg->bdsc_methods & BDSC_METHODS_NLP) {
                n = cJSON_CreateString("UNIT");
                cJSON_AddItemToArray(a, n);
            }
        } else if (option == 1) {
            // 主动tts的鉴权字段里，method参数只写TTS就可以了
            n = cJSON_CreateString("TTS");
            cJSON_AddItemToArray(a, n);
        } else {
            ESP_LOGE(TAG, "unknown option!");
            cJSON_Delete(a);
            return NULL;
        }
    }

    return a;
}

static int _generate_common_pam_data(cJSON **pam_json, bool is_time_str)
{
    const char *sig = NULL;
    char ts_str[12];
    int ts = 0;
    vendor_info_t *g_vendor_info = g_bdsc_engine->g_vendor_info;
    if (!g_vendor_info) {
        return -1;
    }
    if (!(*pam_json = cJSON_CreateObject())) {
        return -1;
    }
    cJSON_AddStringToObject(*pam_json, "fc", g_vendor_info->fc);
    cJSON_AddStringToObject(*pam_json, "pk", g_vendor_info->pk);
    cJSON_AddStringToObject(*pam_json, "ak", g_vendor_info->ak);

    if ((ts = get_current_valid_ts()) < 0) {
        ts = 0; // or return?
    }
    ts = ts / 60;
    if(is_time_str) {
        itoa(ts, ts_str, 10);
        cJSON_AddStringToObject(*pam_json, "time_stamp", ts_str);
    } else {
        cJSON_AddNumberToObject(*pam_json, "time_stamp", ts);    
    }

    if (!(sig = generate_auth_sig_needfree(g_vendor_info->ak, ts, g_vendor_info->sk))) {
        ESP_LOGE(TAG, "generate_auth_sig_needfree fail");
        cJSON_Delete(*pam_json);
        return -1;
    }
    cJSON_AddStringToObject(*pam_json, "signature", sig);
    free((void*)sig);
    return 0;
}

int generate_asr_thirdparty_pam(char* pam_prama, size_t max_len, int option)
{
    ESP_LOGI(TAG, "==> generate_asr_thirdparty_pam\n");
    cJSON *pam_json = NULL, *methodJ = NULL;
    char *pam_string = NULL;
    vendor_info_t *g_vendor_info = g_bdsc_engine->g_vendor_info;

    if(_generate_common_pam_data(&pam_json, 1)) {
        return -1;
    }
    cJSON_AddBoolToObject(pam_json, "optimize", true); // mixed play flag
    cJSON_AddNumberToObject(pam_json, "aue", 3);       // 3：mp3， default is 0 (pcm)
    cJSON_AddNumberToObject(pam_json, "rate", 4);      // bitrate, 4: for mp3

    //methodJ = cJSON_CreateStringArray(methods, 3);
    methodJ = build_bdsc_methods(option);
    cJSON_AddItemToObject(pam_json, "methods", methodJ);

    if (!(pam_string = cJSON_PrintUnformatted(pam_json))) {
        ESP_LOGE(TAG, "cJSON_PrintUnformatted fail");
        cJSON_Delete(pam_json);
        return -1;
    }
    cJSON_Delete(pam_json);
    if (strlen(pam_string) >= max_len) {
        free((void*)pam_string);
        ESP_LOGE(TAG, "sig too long");
        return -1;
    }

    memcpy(pam_prama, pam_string, strlen(pam_string) + 1);
    ESP_LOGI(TAG, "pam_string: %s\n", pam_string);
    free((void*)pam_string);
    return 0;
}

int generate_auth_pam(char* pam_prama, size_t max_len)
{
    ESP_LOGI(TAG, "==> generate_auth_pam\n");
    cJSON *pam_json = NULL;
    char *pam_string = NULL;

    if(_generate_common_pam_data(&pam_json, 0)) {
        return -1;
    }
    if (!(pam_string = cJSON_PrintUnformatted(pam_json))) {
        ESP_LOGE(TAG, "cJSON_PrintUnformatted fail");
        cJSON_Delete(pam_json);
        return -1;
    }
    cJSON_Delete(pam_json);
    if (strlen(pam_string) >= max_len) {
        free((void*)pam_string);
        ESP_LOGE(TAG, "sig too long");
        return -1;
    }

    memcpy(pam_prama, pam_string, strlen(pam_string) + 1);
    ESP_LOGI(TAG, "pam_string: %s\n", pam_string);
    free((void*)pam_string);
    return 0;
}

int generate_active_tts_pam(char* tts_text, char* pam_prama, size_t max_len)
{
    ESP_LOGI(TAG, "==> generate_active_tts_pam\n");
    cJSON *pam_json = NULL;
    char *pam_string = NULL;
    if(_generate_common_pam_data(&pam_json, 0)) {
        return -1;
    }
    cJSON_AddStringToObject(pam_json, "text", tts_text);

    if (!(pam_string = cJSON_PrintUnformatted(pam_json))) {
        ESP_LOGE(TAG, "cJSON_PrintUnformatted fail");
        cJSON_Delete(pam_json);
        return -1;
    }
    cJSON_Delete(pam_json);
    if (strlen(pam_string) >= max_len) {
        free((void*)pam_string);
        ESP_LOGE(TAG, "sig too long");
        return -1;
    }

    memcpy(pam_prama, pam_string, strlen(pam_string) + 1);
    ESP_LOGI(TAG, "pam_string: %s\n", pam_string);
    free((void*)pam_string);
    return 0;
}
