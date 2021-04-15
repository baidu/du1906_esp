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
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include "sdkconfig.h"
#include "esp_log.h"

#include "esp_partition.h"
#include "cJSON.h"
#include "bdsc_engine.h"
#include "bdsc_profile.h"
#include "audio_mem.h"
#include "esp_delegate.h"
#include "bdsc_ota_partitions.h"
#include "version.h"
#include "rom/rtc.h"

#define TAG "PROFILE"

static esp_err_t bdsc_nvs_get_str(nvs_handle deviceNvsHandle, const char* key, char** out_str)
{
    size_t required_size;
    int ret;
    
    if (!key) {
        return ESP_FAIL;
    }

    // get required length
    if (ESP_OK == (ret = nvs_get_str(deviceNvsHandle, key, NULL, &required_size))) {
        char* tmp_buf = audio_malloc(required_size);
        // get value
        if (ESP_OK == (ret = nvs_get_str(deviceNvsHandle, key, tmp_buf, &required_size))) {
            *out_str = tmp_buf;
            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "nvs_get_str [%s] fail: %d", key, ret);
    return ESP_FAIL;
}


/**
* @brief Generate vendor profiles: fc, pk, ak, sk
*
* @param vendor_info vendor profiles are saved in vendor_info_t structure
* @return return the vendor profiles are from profile file or not
*   @retval ESP_ERR_NOT_FOUND profile file is not in flash
*   @retval ESP_OK profile file is in flash
*/
static int generate_profile(vendor_info_t *vendor_info)
{
    esp_partition_t *partition = NULL;
    char *buf = NULL;
    nvs_handle deviceNvsHandle;
    int tmp_int;
    
    esp_err_t ret = nvs_open_from_partition(NVS_USER_PART_LABEL, NVS_DEVICE_SYS_NAMESPACE, NVS_READWRITE, &deviceNvsHandle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open_from_partition fail#1: %d", ret);
        return -1;
    }

    partition = (esp_partition_t *)esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
            0x29, NULL);
    if (partition == NULL) {
        ESP_LOGW(TAG, "Can not find profile partition");
        nvs_close(deviceNvsHandle);
        return -1;
    }
    ESP_LOGI(TAG, "%d: type[0x%x]", __LINE__, partition->type);
    ESP_LOGI(TAG, "%d: subtype[0x%x]", __LINE__, partition->subtype);
    ESP_LOGI(TAG, "%d: address:0x%x", __LINE__, partition->address);
    ESP_LOGI(TAG, "%d: size:0x%x", __LINE__, partition->size);
    ESP_LOGI(TAG, "%d: label:%s", __LINE__,  partition->label);

    buf = audio_malloc(partition->size);
    if (esp_partition_read(partition, 0, buf, partition->size) != 0) {
        ESP_LOGW(TAG, "Read profile failed and use default profile.");
        free(buf);
        nvs_close(deviceNvsHandle);
        return -1;
    }

    // bellow is mandatory
    char *fc, *pk, *ak, *sk;
    cJSON *json, *j_fc, *j_pk, *j_ak, *j_sk;
    fc = pk = ak = sk = "DEADBEEF";

    if ((json = cJSON_Parse(buf))) {
        if ((j_fc = cJSON_GetObjectItem(json, "fc"))) {
            fc = j_fc->valuestring;
        }
        if ((j_pk = cJSON_GetObjectItem(json, "pk"))) {
            pk = j_pk->valuestring;
        }
        if ((j_ak = cJSON_GetObjectItem(json, "ak"))) {
            ak = j_ak->valuestring;
        }
        if ((j_sk = cJSON_GetObjectItem(json, "sk"))) {
            sk = j_sk->valuestring;
        }
    }
    vendor_info->fc = audio_strdup(fc);
    vendor_info->pk = audio_strdup(pk);
    vendor_info->ak = audio_strdup(ak);
    vendor_info->sk = audio_strdup(sk);
    cJSON_Delete(json);
    free(buf);

    // bellow is optional
    if (ESP_FAIL == (ret = bdsc_nvs_get_str(deviceNvsHandle, PROFILE_NVS_KEY_BROKER, &vendor_info->mqtt_broker))) {
        vendor_info->mqtt_broker = audio_strdup("");
    }
    if (ESP_FAIL == (ret = bdsc_nvs_get_str(deviceNvsHandle, PROFILE_NVS_KEY_MQTT_USERNAME, &vendor_info->mqtt_username))) {
        vendor_info->mqtt_username = audio_strdup("");
    }
    if (ESP_FAIL == (ret = bdsc_nvs_get_str(deviceNvsHandle, PROFILE_NVS_KEY_MQTT_PASSWD, &vendor_info->mqtt_password))) {
        vendor_info->mqtt_password = audio_strdup("");
    }
    if (ESP_FAIL == (ret = bdsc_nvs_get_str(deviceNvsHandle, PROFILE_NVS_KEY_MQTT_CID, &vendor_info->mqtt_cid))) {
        vendor_info->mqtt_cid = audio_strdup("");
    }
    if (ESP_OK == (ret = nvs_get_i32(deviceNvsHandle, PROFILE_NVS_KEY_VER_NUM, &tmp_int))) {
        vendor_info->cur_version_num = tmp_int;
    } else {
        ESP_LOGE(TAG, "nvs_get_i32 fail#5: %d", ret);
        vendor_info->cur_version_num = FW_VERSION_NUM;
        ret = nvs_set_i32(deviceNvsHandle, PROFILE_NVS_KEY_VER_NUM, FW_VERSION_NUM);
        if (ESP_OK != ret) {
            ESP_LOGE(TAG, "nvs_set_i32 error#5.1: %d", ret);
        }

    }

    if (ESP_FAIL == (ret = bdsc_nvs_get_str(deviceNvsHandle, PROFILE_NVS_KEY_TONE_SUB, &vendor_info->tone_sub_ver))) {
        vendor_info->tone_sub_ver = audio_strdup(TONE_SUB_VERSION);
        ret = nvs_set_str(deviceNvsHandle, PROFILE_NVS_KEY_TONE_SUB, TONE_SUB_VERSION);
        if (ESP_OK != ret) {
            ESP_LOGE(TAG, "nvs_set_str error#6: %d", ret);
        }
    }

    if (ESP_FAIL == (ret = bdsc_nvs_get_str(deviceNvsHandle, PROFILE_NVS_KEY_DSP_SUB, &vendor_info->dsp_sub_ver))) {
        vendor_info->dsp_sub_ver = audio_strdup(DSP_SUB_VERSION);
        ret = nvs_set_str(deviceNvsHandle, PROFILE_NVS_KEY_DSP_SUB, DSP_SUB_VERSION);
        if (ESP_OK != ret) {
            ESP_LOGE(TAG, "nvs_set_str error#7: %d", ret);
        }
    }

    if (ESP_FAIL == (ret = bdsc_nvs_get_str(deviceNvsHandle, PROFILE_NVS_KEY_APP_SUB, &vendor_info->app_sub_ver))) {
        vendor_info->app_sub_ver = audio_strdup(APP_SUB_VERSION);
        ret = nvs_set_str(deviceNvsHandle, PROFILE_NVS_KEY_APP_SUB, APP_SUB_VERSION);
        if (ESP_OK != ret) {
            ESP_LOGE(TAG, "nvs_set_str error#8: %d", ret);
        }
    }

    if (ESP_FAIL == (ret = bdsc_nvs_get_str(deviceNvsHandle, PROFILE_NVS_KEY_LAST_OTA_URL, &vendor_info->last_ota_url))) {
        vendor_info->last_ota_url = NULL;
    }
    if (ESP_FAIL == (ret = nvs_get_i32(deviceNvsHandle, PROFILE_NVS_KEY_SLIENT_MODE, &g_bdsc_engine->silent_mode))) {
        g_bdsc_engine->silent_mode = 0;
    }
    // reset slient_mode in nvs
    if (g_bdsc_engine->silent_mode) {
        if (ESP_OK != (ret = nvs_set_i32(deviceNvsHandle, PROFILE_NVS_KEY_SLIENT_MODE, 0))) {
            ESP_LOGE(TAG, "nvs_set_i32 error: %d", ret);
        }
    }
    // flag indicate first boot since ota
    if (g_bdsc_engine->silent_mode) {
        g_bdsc_engine->boot_from_ota = true;
    }
    // if user manually press key to boot, exit silent mode
    if (rtc_get_reset_reason(0) == POWERON_RESET) {
        g_bdsc_engine->silent_mode = 0;
    }

    if (ESP_OK == (ret = nvs_get_i32(deviceNvsHandle, PROFILE_NVS_KEY_IS_ACTIVE_MUSIC_LICENSE, &tmp_int))) {
        vendor_info->is_active_music_license = tmp_int;
    } else {
        ESP_LOGE(TAG, "nvs_get_i32 fail#10: %d", ret);
        vendor_info->is_active_music_license = 0;
    }

    ESP_LOGI(TAG, "fc            = %s", vendor_info->fc);
    ESP_LOGI(TAG, "pk            = %s", vendor_info->pk);
    ESP_LOGI(TAG, "ak            = %s", vendor_info->ak);
    ESP_LOGD(TAG, "sk            = %s", vendor_info->sk);
    ESP_LOGI(TAG, "mqtt_broker   = %s", vendor_info->mqtt_broker);
    ESP_LOGI(TAG, "mqtt_username = %s", vendor_info->mqtt_username);
    ESP_LOGI(TAG, "mqtt_password = %s", vendor_info->mqtt_password);
    ESP_LOGI(TAG, "mqtt_cid      = %s", vendor_info->mqtt_cid);
    ESP_LOGI(TAG, "cur_version_num = %d", vendor_info->cur_version_num);
    ESP_LOGI(TAG, "tone_sub_ver    = %s", vendor_info->tone_sub_ver);
    ESP_LOGI(TAG, "dsp_sub_ver     = %s", vendor_info->dsp_sub_ver);
    ESP_LOGI(TAG, "app_sub_ver     = %s", vendor_info->app_sub_ver);
    ESP_LOGI(TAG, "silent          = %d", g_bdsc_engine->silent_mode);

    nvs_close(deviceNvsHandle);
    return 0;
}


int profile_init()
{
    vendor_info_t* info = audio_calloc(1, sizeof(vendor_info_t));
    if (!info) {
        ESP_LOGE(TAG, "audio_calloc fail");
        return -1;
    }
    if (generate_profile(info)) {
        ESP_LOGE(TAG, "generate_profile fail");
        return -1;
    }
    g_bdsc_engine->g_vendor_info = info;
    return 0;
}

int profile_key_set(bdsc_profile_key_type_t key_type, void *value)
{
    int err;
    nvs_handle deviceNvsHandle;
    vendor_info_t *vendor_info = g_bdsc_engine->g_vendor_info;
    if (!vendor_info) {
        return -1;
    }
    esp_err_t ret = nvs_open_from_partition(NVS_USER_PART_LABEL, NVS_DEVICE_SYS_NAMESPACE, NVS_READWRITE, &deviceNvsHandle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open_from_partition fail#2 %d", ret);
        return -1;
    }
    switch (key_type) {
    case PROFILE_KEY_TYPE_FC:
    case PROFILE_KEY_TYPE_PK:
    case PROFILE_KEY_TYPE_AK:
    case PROFILE_KEY_TYPE_SK:
        ESP_LOGE(TAG, "Readonly key, profile_key_set fail");
        break;
    case PROFILE_KEY_TYPE_MQTT_BROKER:
        err = nvs_set_str(deviceNvsHandle, PROFILE_NVS_KEY_BROKER, (const char*)value);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "nvs_set_str error#1: %d", err);
        }
        if (vendor_info->mqtt_broker) {
            free(vendor_info->mqtt_broker);
        }
        vendor_info->mqtt_broker = audio_strdup((const char*)value);
        break;
    case PROFILE_KEY_TYPE_MQTT_USERNAME:
        err = nvs_set_str(deviceNvsHandle, PROFILE_NVS_KEY_MQTT_USERNAME, (const char*)value);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "nvs_set_str error#2: %d", err);
        }
        
        if (vendor_info->mqtt_username) {
            free(vendor_info->mqtt_username);
        }
        vendor_info->mqtt_username = audio_strdup((const char*)value);
        //check_username();
        break;
    case PROFILE_KEY_TYPE_MQTT_PASSWD:
        err = nvs_set_str(deviceNvsHandle, PROFILE_NVS_KEY_MQTT_PASSWD, (const char*)value);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "nvs_set_str error#3: %d", err);
        }
        if (vendor_info->mqtt_password) {
            free(vendor_info->mqtt_password);
        }
        vendor_info->mqtt_password = audio_strdup((const char*)value);
        break;
    case PROFILE_KEY_TYPE_MQTT_CID:
        err = nvs_set_str(deviceNvsHandle, PROFILE_NVS_KEY_MQTT_CID, (const char*)value);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "nvs_set_str error#4: %d", err);
        }
        if (vendor_info->mqtt_cid) {
            free(vendor_info->mqtt_cid);
        }
        vendor_info->mqtt_cid = audio_strdup((const char*)value);
        break;
    case PROFILE_KEY_TYPE_VER_NUM:
        err = nvs_set_i32(deviceNvsHandle, PROFILE_NVS_KEY_VER_NUM, *((int*)value));
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "nvs_set_i32 error#5: %d", err);
        }
        vendor_info->cur_version_num = *((int*)value);
        break;
    case PROFILE_KEY_TYPE_TONE_SUB:
        err = nvs_set_str(deviceNvsHandle, PROFILE_NVS_KEY_TONE_SUB, (const char*)value);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "nvs_set_str error#6: %d", err);
        }
        if (vendor_info->tone_sub_ver) {
            free(vendor_info->tone_sub_ver);
        }
        vendor_info->tone_sub_ver = audio_strdup((const char*)value);
        break;
    case PROFILE_KEY_TYPE_DSP_SUB:
        err = nvs_set_str(deviceNvsHandle, PROFILE_NVS_KEY_DSP_SUB, (const char*)value);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "nvs_set_str error#7: %d", err);
        }
        if (vendor_info->dsp_sub_ver) {
            free(vendor_info->dsp_sub_ver);
        }
        vendor_info->dsp_sub_ver = audio_strdup((const char*)value);
        break;
    case PROFILE_KEY_TYPE_APP_SUB:
        err = nvs_set_str(deviceNvsHandle, PROFILE_NVS_KEY_APP_SUB, (const char*)value);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "nvs_set_str error#8: %d", err);
        }
        if (vendor_info->app_sub_ver) {
            free(vendor_info->app_sub_ver);
        }
        vendor_info->app_sub_ver = audio_strdup((const char*)value);
        break;
    case PROFILE_KEY_TYPE_SLIENT_MODE:
        err = nvs_set_i32(deviceNvsHandle, PROFILE_NVS_KEY_SLIENT_MODE, *((int*)value));
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "nvs_set_i32 error#9: %d", err);
        }
        break;
    case PROFILE_KEY_TYPE_LAST_OTA_URL:
        err = nvs_set_str(deviceNvsHandle, PROFILE_NVS_KEY_LAST_OTA_URL, (const char*)value);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "nvs_set_str error#10: %d", err);
        }
        if (vendor_info->last_ota_url) {
            free(vendor_info->last_ota_url);
        }
        vendor_info->last_ota_url = audio_strdup((const char*)value);
        break;
    case PROFILE_KEY_TYPE_IS_ACTIVE_MUSIC_LICENSE:
        err = nvs_set_i32(deviceNvsHandle, PROFILE_NVS_KEY_IS_ACTIVE_MUSIC_LICENSE, *((int*)value));
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "nvs_set_i32 error#11: %d", err);
        }
        vendor_info->is_active_music_license = *((int*)value);
        break;
    default:
        ESP_LOGE(TAG, "unknown key_type");
        break;
    }
    nvs_commit(deviceNvsHandle);
    nvs_close(deviceNvsHandle);
    return 0;
}


int profile_key_get(bdsc_profile_key_type_t key_type, void **value_ptr_holder)
{
    vendor_info_t *vendor_info = g_bdsc_engine->g_vendor_info;
    if (!vendor_info) {
        return -1;
    }
    switch (key_type) {
    case PROFILE_KEY_TYPE_FC:
        if (!vendor_info->fc) {
            ESP_LOGE(TAG, "fc not set!");
            return -1;
        }
        *value_ptr_holder = vendor_info->fc;
        break;
    case PROFILE_KEY_TYPE_PK:
        if (!vendor_info->pk) {
            ESP_LOGE(TAG, "pk not set!");
            return -1;
        }
        *value_ptr_holder = vendor_info->pk;
        break;
    case PROFILE_KEY_TYPE_AK:
        if (!vendor_info->ak) {
            ESP_LOGE(TAG, "ak not set!");
            return -1;
        }
        *value_ptr_holder = vendor_info->ak;
        break;
    case PROFILE_KEY_TYPE_SK:
        if (!vendor_info->sk) {
            ESP_LOGE(TAG, "sk not set!");
            return -1;
        }
        *value_ptr_holder = vendor_info->sk;
        break;
    case PROFILE_KEY_TYPE_MQTT_BROKER:
        if (!vendor_info->mqtt_broker) {
            ESP_LOGE(TAG, "mqtt_broker not set!");
            return -1;
        }
        *value_ptr_holder = vendor_info->mqtt_broker;
        break;
    case PROFILE_KEY_TYPE_MQTT_USERNAME:
        if (!vendor_info->mqtt_username) {
            ESP_LOGE(TAG, "mqtt_username not set!");
            return -1;
        }
        *value_ptr_holder = vendor_info->mqtt_username;
        break;
    case PROFILE_KEY_TYPE_MQTT_PASSWD:
        if (!vendor_info->mqtt_password) {
            ESP_LOGE(TAG, "mqtt_password not set!");
            return -1;
        }
        *value_ptr_holder = vendor_info->mqtt_password;
        break;
    case PROFILE_KEY_TYPE_MQTT_CID:
        if (!vendor_info->mqtt_cid) {
            ESP_LOGE(TAG, "mqtt_cid not set!");
            return -1;
        }
        *value_ptr_holder = vendor_info->mqtt_cid;
        break;
    case PROFILE_KEY_TYPE_VER_NUM:
        *(int*)value_ptr_holder = vendor_info->cur_version_num;
        break;
    case PROFILE_KEY_TYPE_TONE_SUB:
        if (!vendor_info->tone_sub_ver) {
            ESP_LOGE(TAG, "tone_sub_ver not set!");
            return -1;
        }
        *value_ptr_holder = vendor_info->tone_sub_ver;
        break;
    case PROFILE_KEY_TYPE_DSP_SUB:
        if (!vendor_info->dsp_sub_ver) {
            ESP_LOGE(TAG, "dsp_sub_ver not set!");
            return -1;
        }
        *value_ptr_holder = vendor_info->dsp_sub_ver;
        break;
    case PROFILE_KEY_TYPE_APP_SUB:
        if (!vendor_info->app_sub_ver) {
            ESP_LOGE(TAG, "app_sub_ver not set!");
            return -1;
        }
        *value_ptr_holder = vendor_info->app_sub_ver;
        break;
    case PROFILE_KEY_TYPE_LAST_OTA_URL:
        if (!vendor_info->last_ota_url) {
            ESP_LOGE(TAG, "last_ota_url not set!");
            return -1;
        }
        *value_ptr_holder = vendor_info->last_ota_url;
        break;
    default:
        ESP_LOGE(TAG, "unknown key_type");
        return -1;
    }
    
    return 0;
}

static esp_err_t custom_key_op_invoker(void *instance, action_arg_t *arg, action_result_t *result)
{
    nvs_handle deviceNvsHandle;
    custom_key_value_arg_t *kv = NULL;
    const char *key = NULL;
    char *value = NULL;
    int *len = NULL;

    kv    = (custom_key_value_arg_t *)arg->data;
    key   = kv->key;
    value = kv->value;
    len   = kv->len;

    esp_err_t ret = nvs_open_from_partition(NVS_USER_PART_LABEL, kv->location, NVS_READWRITE, &deviceNvsHandle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open_from_partition fail#4 %d", ret);
        return -1;
    }
    switch (kv->op) {
    case CUSTOM_KEY_GET:
        switch (kv->type) {
        case CUSTOM_KEY_TYPE_INT32:
            ret = nvs_get_i32(deviceNvsHandle, key, (void*)value);
            if (ESP_OK != ret) {
                ESP_LOGE(TAG, "nvs get %s error: %d", key, ret);
            }
            break;
        case CUSTOM_KEY_TYPE_STRING:
            ret = nvs_get_str(deviceNvsHandle, key, value, (size_t *)len);
            if (ESP_OK != ret) {
                ESP_LOGE(TAG, "nvs get %s error: %d", key, ret);
            }
            break;
        default:
            ESP_LOGE(TAG, "unsupport type: %d", kv->type);
            break;
        }
        break;
    case CUSTOM_KEY_SET:
        switch (kv->type) {
        case CUSTOM_KEY_TYPE_INT32:
            ret = nvs_set_i32(deviceNvsHandle, key, *(int*)value);
            if (ESP_OK != ret) {
                ESP_LOGE(TAG, "nvs_set_i32 error: %d", ret);
            }
            nvs_commit(deviceNvsHandle);
            break;
        case CUSTOM_KEY_TYPE_STRING:
            /* For strings, the maximum length (including null character) is 4000 bytes */
            ret = nvs_set_str(deviceNvsHandle, key, (const char*)value);
            if (ESP_OK != ret) {
                ESP_LOGE(TAG, "nvs set %s error: %d", key, ret);
            }
            nvs_commit(deviceNvsHandle);
            break;
        default:
            break;
        }
        break;
    default:
        ESP_LOGE(TAG, "unsupport op: %d", kv->op);
        break;
    }
    
    nvs_close(deviceNvsHandle);
    return 0;
}

int custom_key_op_safe(custom_op_type_t op, custom_key_type_t type, const char *location, const char *key, char *value, int *len)
{
    // psram-task must use esp_dispatcher to manipulate flash 
    esp_dispatcher_handle_t dh = esp_dispatcher_get_delegate_handle();
    action_result_t ret;
    custom_key_value_arg_t kv = {
        .op     = op,
        .type   = type,
        .location = location,
        .key    = key,
        .value  = value,
        .len    = len,
    };

    action_arg_t arg = {
        .data = &kv,
        .len  = sizeof(custom_key_value_arg_t),
    };
    // this will block current task and waitting the invoker finished
    esp_dispatcher_execute_with_func(dh, custom_key_op_invoker, NULL, &arg, &ret);
    ESP_LOGI(TAG, "esp_dispatcher ret: %d", ret.err);
    return 0;
}
