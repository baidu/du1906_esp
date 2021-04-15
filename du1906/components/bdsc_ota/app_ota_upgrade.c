/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2020 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <string.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "audio_error.h"
#include "esp_partition.h"
#include "ota_service.h"
#include "ota_proc_iot.h"
#include "bdsc_event_dispatcher.h"
#include "bdsc_json.h"
#include "bdsc_engine.h"
#include "audio_player.h"
#include "display_service.h"
#include "audio_mem.h"
#include "board.h"
#include "bdsc_tools.h"
#include "app_ota_upgrade.h"
#include "bdsc_ota_partitions.h"
#include "app_ota_policy.h"
#include "app_task_register.h"

static const char *TAG = "APP_OTA_UPGRADE";

static EventGroupHandle_t OTA_FLAG;
extern bool g_is_mute;
#define OTA_FINISH (BIT0)
#define OTA_SERVICE_STACK_SIZE  (4 * 1024)

#define DUL1906_OTA_BIN_DEF_URL             "https://xxx.com/xxx.bin"
#define DUL1906_NEED_OTA_PARTITION_NUM      3

enum {
    DUL1906_OTA_PARTITION_ID_FLASH_TONE = 0,
    DUL1906_OTA_PARTITION_ID_DSP_BIN    = 1,
    DUL1906_OTA_PARTITION_ID_APP        = 2,
};

extern display_service_handle_t g_disp_serv;

typedef struct {
    char *url;
} bdsc_ota_info_t;


custom_ota_bin_desc_t *g_custom_ota_bin_desc = NULL;

static char *generate_otaack_errcode(int err_code)
{
    cJSON *ota_ackJ;

    ota_ackJ = cJSON_CreateObject();
    cJSON_AddNumberToObject(ota_ackJ, "trannum_up", get_trannum_up());
    cJSON_AddStringToObject(ota_ackJ, "type", "OTAACK");
    cJSON_AddNumberToObject(ota_ackJ, "errcode", err_code);
    
    char *rsp_str = cJSON_PrintUnformatted(ota_ackJ);
    if (!rsp_str) {
        ESP_LOGE(TAG, "cJSON_PrintUnformatted fail");
        cJSON_Delete(ota_ackJ);
        return NULL;
    }
    cJSON_Delete(ota_ackJ);
    return rsp_str;
}

static void custom_ota_bin_desc_cleanup()
{
    int i = 0;
    if (g_custom_ota_bin_desc) {
        for (i = 0; i < OTA_BACKUP_PART_NUM; i++) {
            if (g_custom_ota_bin_desc->custom_ota_bin_desc_parts[i].sub_version) {
                free(g_custom_ota_bin_desc->custom_ota_bin_desc_parts[i].sub_version);
            }
            if (g_custom_ota_bin_desc->custom_ota_bin_desc_parts[i].checksum) {
                free(g_custom_ota_bin_desc->custom_ota_bin_desc_parts[i].checksum);
            }
        }
        
        free(g_custom_ota_bin_desc);
        g_custom_ota_bin_desc = NULL;
    }
}

int _return_ota_error(int errcode)
{
    char *ota_rsp_str = generate_otaack_errcode(errcode);
    if (ota_rsp_str) {
        bdsc_engine_channel_data_upload((uint8_t *)ota_rsp_str, strlen(ota_rsp_str) + 1);
        free(ota_rsp_str);
    }

    if (errcode == 0) {
        //bdsc_play_hint(BDSC_HINT_OTA_COMPLETE);
    } else if (errcode == -1) {
        bdsc_play_hint(BDSC_HINT_OTA_FAIL);
    } else if (errcode == -2) {
        bdsc_play_hint(BDSC_HINT_OTA_ALREADY_NEWEST);
    }
    
    return 0;
}

cJSON* create_dm_upload_msg(int trans_num, const char *type, cJSON *body, bool is_active);

char* create_ota_download_status_string(int code)
{
    cJSON *rsp = NULL;

    if (!(rsp = create_dm_upload_msg(get_trannum_up(), "OTADL", NULL, true))) {
        ESP_LOGE(TAG, "create_dm_response error");
        return NULL;
    }
    BdsJsonObjectAddInt(rsp, "code", code);
    char *rsp_str = BdsJsonPrintUnformatted(rsp);
    if (!rsp_str) {
        ESP_LOGE(TAG, "cJSON_PrintUnformatted fail");
        BdsJsonPut(rsp);
        return NULL;
    }

    BdsJsonPut(rsp);
    return rsp_str;
}

static void _ota_st_reset();
static esp_err_t ota_service_cb(periph_service_handle_t handle, periph_service_event_t *evt, void *ctx)
{
    char *dl_ok = NULL;
    if (evt->type == OTA_SERV_EVENT_TYPE_RESULT) {
        ota_result_t *result_data = evt->data;
        if (result_data->result == ESP_OTA_FAIL) {
            ESP_LOGE(TAG, "List id: %d, OTA failed", result_data->id);
            switch (result_data->id) {
            case DUL1906_OTA_PARTITION_ID_FLASH_TONE:
                //ota_fail_rsn_set(OTA_FAIL_RSN_TRACE_TONE);
                break;
            case DUL1906_OTA_PARTITION_ID_DSP_BIN:
                //ota_fail_rsn_set(OTA_FAIL_RSN_TRACE_DSP);
                break;
            case DUL1906_OTA_PARTITION_ID_APP:
                //ota_fail_rsn_set(OTA_FAIL_RSN_TRACE_APP);
                break;
            default:
                ESP_LOGE(TAG, "invalid partiton type!");
            }
            g_bdsc_engine->ota_fail_flag = true;
            ESP_LOGE(TAG, "ota fail!");
            _return_ota_error(-1);
        } else if (result_data->result == ESP_OTA_OK) {
            ESP_LOGI(TAG, "List id: %d, OTA sucessed", result_data->id);
            if (result_data->id != DUL1906_OTA_PARTITION_ID_FLASH_TONE &&
                result_data->id != DUL1906_OTA_PARTITION_ID_DSP_BIN &&
                result_data->id != DUL1906_OTA_PARTITION_ID_APP) {
                    ESP_LOGE(TAG, "invalid partiton type!");
                    return ESP_OK;
            }

            char *checksum = g_custom_ota_bin_desc->custom_ota_bin_desc_parts[result_data->id].checksum;
            bdsc_partition_info_t *info = bdsc_partitions_get_next_ota_part_info(result_data->id);
            strncpy(info->checksum, checksum, sizeof(info->checksum));
            bdsc_partitions_update_partition_info(*info);
        } else if (result_data->result == ESP_OTA_PASS) {
            g_bdsc_engine->ota_read_bytes += g_custom_ota_bin_desc->custom_ota_bin_desc_parts[result_data->id].len;
            ESP_LOGI(TAG, "List id: %d, OTA sucessed, bypassed", result_data->id);
        } else {
            ESP_LOGE(TAG, "unknown partition ota result! %d", result_data->result);
        }
    } else if (evt->type == OTA_SERV_EVENT_TYPE_OK_FINISH) {
        profile_key_set(PROFILE_KEY_TYPE_VER_NUM, &g_custom_ota_bin_desc->version);
        profile_key_set(PROFILE_KEY_TYPE_TONE_SUB, g_custom_ota_bin_desc->custom_ota_bin_desc_parts[TOGGLE_OTA_BOOT_TONE].sub_version);
        profile_key_set(PROFILE_KEY_TYPE_DSP_SUB, g_custom_ota_bin_desc->custom_ota_bin_desc_parts[TOGGLE_OTA_BOOT_DSP].sub_version);
        profile_key_set(PROFILE_KEY_TYPE_APP_SUB, g_custom_ota_bin_desc->custom_ota_bin_desc_parts[TOGGLE_OTA_BOOT_APP].sub_version);

        bdsc_partitions_bootable_chain_update();
        
        if ((dl_ok = create_ota_download_status_string(100))) {
            bdsc_engine_channel_data_upload((uint8_t*)dl_ok, strlen(dl_ok) + 1);
            free(dl_ok);
        }

        if (!g_bdsc_engine->silent_mode) {
            //_return_ota_error(0);
            bdsc_play_hint(BDSC_HINT_OTA_COMPLETE);
            // FIXME: Delay here to wait for hint playing done
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            ESP_LOGW(TAG, "restart!");
            esp_restart();
        }

        xEventGroupSetBits(OTA_FLAG, OTA_FINISH);
        g_bdsc_engine->is_force_ota = false;
        _ota_st_reset();
    } else if (evt->type == OTA_SERV_EVENT_TYPE_FAIL_FINISH) {
        xEventGroupSetBits(OTA_FLAG, OTA_FINISH);
        g_bdsc_engine->is_force_ota = false;
        _ota_st_reset();
    }

    return ESP_OK;
}


char *get_last_ota_url()
{
    char *url;
    if (profile_key_get(PROFILE_KEY_TYPE_LAST_OTA_URL, (void**)&url) < 0) {
        return NULL;
    }
    return url;
}

static int set_last_ota_url(char *url)
{
    return profile_key_set(PROFILE_KEY_TYPE_LAST_OTA_URL, url);
}

static int ota_custom_header_md5_check(char *ota_custom_header)
{
    char *checksum_ok;
    char *checksum_tmp;
    if (!ota_custom_header) {
        return -1;
    }
    checksum_ok = ota_custom_header + strlen(ota_custom_header) + 1;
    checksum_tmp = generate_md5_checksum_needfree((uint8_t *)ota_custom_header, strlen(ota_custom_header));
    if (!checksum_tmp) {
        ESP_LOGE(TAG, "generate_md5_checksum_needfree fail");
        return -1;
    }
    ESP_LOGI(TAG, "json checksum : %s, tmp checksum: %s", checksum_ok, checksum_tmp);
    if (!strcmp(checksum_ok, checksum_tmp)) {
        free(checksum_tmp);
        return 0;
    }

    free(checksum_tmp);
    return -1;
}

static custom_ota_bin_desc_t *fetch_custom_header_once(char * ota_url)
{
    custom_ota_bin_desc_t *bin_desc = NULL;
    cJSON *desc_j = NULL;

    esp_http_client_config_t config = {
        .url = ota_url,
        .cert_pem = NULL,
        .timeout_ms = 1000,
    };
    esp_http_client_handle_t http_client = esp_http_client_init(&config);
    if (!http_client) {
        ESP_LOGE(TAG, "Failed to initialise HTTP connection");
        return NULL;
    }
    int err;
    err = esp_http_client_open(http_client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(http_client);
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        return NULL;
    }
    esp_http_client_fetch_headers(http_client);
    int status_code = esp_http_client_get_status_code(http_client);
    ESP_LOGI(TAG, "status_code: %d", status_code);

    uint8_t *tmp_buf = NULL;
    size_t tmp_buf_len = 1024;
    size_t read_len;
    if (!(tmp_buf = audio_calloc(1, tmp_buf_len))) {
        ERR_OUT(err_out, "malloc error");
    }
    read_len = esp_http_client_read(http_client, (char *)tmp_buf, tmp_buf_len);
    if (read_len != tmp_buf_len) {
        ERR_OUT(err_out, "esp_http_client_read error");
    }
    const char *parse_end;
    cJSON_bool require_null_terminated = 1;
    printf("tmp_buf: %s\n", tmp_buf);
    if (ota_custom_header_md5_check((char *)tmp_buf)) {
        ERR_OUT(err_out, "ota_custom_header_md5_check fail");
    }
    ESP_LOGI(TAG, "json header md5 check ok!");
    desc_j = cJSON_ParseWithOpts((const char *)tmp_buf, &parse_end, require_null_terminated);
    if (!desc_j) {
        ERR_OUT(err_out, "cJSON_ParseWithOpts error");
    }
    bin_desc = (custom_ota_bin_desc_t*)audio_calloc(1, sizeof(custom_ota_bin_desc_t));
    if (!bin_desc) {
        ERR_OUT(err_out, "malloc error");
    }
    cJSON *sub_verJ, *offsetJ, *lengthJ, *checksumJ;
    cJSON *version_j = cJSON_GetObjectItem(desc_j, "version");
    bin_desc->version = version_j->valueint;
    cJSON *tone_j = cJSON_GetObjectItem(desc_j, "tone");
    if (tone_j) {
        sub_verJ  = cJSON_GetObjectItem(tone_j, "sub_ver");
        offsetJ   = cJSON_GetObjectItem(tone_j, "offset");
        lengthJ   = cJSON_GetObjectItem(tone_j, "length");
        checksumJ = cJSON_GetObjectItem(tone_j, "checksum");
        
        if (sub_verJ && offsetJ && lengthJ && checksumJ) {
            ESP_LOGI(TAG, "tone sub_ver: %s, %s", sub_verJ->valuestring, checksumJ->valuestring);
            bin_desc->custom_ota_bin_desc_parts[TOGGLE_OTA_BOOT_TONE].sub_version = audio_strdup(sub_verJ->valuestring);
            bin_desc->custom_ota_bin_desc_parts[TOGGLE_OTA_BOOT_TONE].offset = offsetJ->valueint;
            bin_desc->custom_ota_bin_desc_parts[TOGGLE_OTA_BOOT_TONE].len = lengthJ->valueint;
            bin_desc->custom_ota_bin_desc_parts[TOGGLE_OTA_BOOT_TONE].checksum = audio_strdup(checksumJ->valuestring);
            g_bdsc_engine->ota_total_bytes += lengthJ->valueint;
        } else {
            ESP_LOGE(TAG, "custom_ota_bin_desc json format error");
        }
    }
    cJSON *dsp_j = cJSON_GetObjectItem(desc_j, "dsp");
    if (dsp_j) {
        sub_verJ  = cJSON_GetObjectItem(dsp_j, "sub_ver");
        offsetJ   = cJSON_GetObjectItem(dsp_j, "offset");
        lengthJ   = cJSON_GetObjectItem(dsp_j, "length");
        checksumJ = cJSON_GetObjectItem(dsp_j, "checksum");
        
        if (sub_verJ && offsetJ && lengthJ && checksumJ) {
            ESP_LOGI(TAG, "dsp sub_ver: %s, %s", sub_verJ->valuestring, checksumJ->valuestring);
            bin_desc->custom_ota_bin_desc_parts[TOGGLE_OTA_BOOT_DSP].sub_version = audio_strdup(sub_verJ->valuestring);
            bin_desc->custom_ota_bin_desc_parts[TOGGLE_OTA_BOOT_DSP].offset = offsetJ->valueint;
            bin_desc->custom_ota_bin_desc_parts[TOGGLE_OTA_BOOT_DSP].len = lengthJ->valueint;
            bin_desc->custom_ota_bin_desc_parts[TOGGLE_OTA_BOOT_DSP].checksum = audio_strdup(checksumJ->valuestring);
            g_bdsc_engine->ota_total_bytes += lengthJ->valueint;
        } else {
            ESP_LOGE(TAG, "custom_ota_bin_desc json format error");
        }
    }
    cJSON *app_j = cJSON_GetObjectItem(desc_j, "app");
    if (app_j) {
        sub_verJ  = cJSON_GetObjectItem(app_j, "sub_ver");
        offsetJ   = cJSON_GetObjectItem(app_j, "offset");
        lengthJ   = cJSON_GetObjectItem(app_j, "length");
        checksumJ = cJSON_GetObjectItem(app_j, "checksum");
        
        if (sub_verJ && offsetJ && lengthJ && checksumJ) {
            ESP_LOGI(TAG, "app sub_ver: %s, %s", sub_verJ->valuestring, checksumJ->valuestring);
            bin_desc->custom_ota_bin_desc_parts[TOGGLE_OTA_BOOT_APP].sub_version = audio_strdup(sub_verJ->valuestring);
            bin_desc->custom_ota_bin_desc_parts[TOGGLE_OTA_BOOT_APP].offset = offsetJ->valueint;
            bin_desc->custom_ota_bin_desc_parts[TOGGLE_OTA_BOOT_APP].len = lengthJ->valueint;
            bin_desc->custom_ota_bin_desc_parts[TOGGLE_OTA_BOOT_APP].checksum = audio_strdup(checksumJ->valuestring);
            g_bdsc_engine->ota_total_bytes += lengthJ->valueint;
        } else {
            ESP_LOGE(TAG, "custom_ota_bin_desc json format error");
        }
    }

    ESP_LOGI(TAG, "parse custom_ota_bin_desc json success");
err_out:
    if (tmp_buf) {
        free(tmp_buf);
    }
    if (desc_j) {
        cJSON_Delete(desc_j);
    }
    esp_http_client_close(http_client);
    esp_http_client_cleanup(http_client);
    return bin_desc;
}

static custom_ota_bin_desc_t *fetch_custom_header_loop(char * ota_url)
{
    custom_ota_bin_desc_t *desc;

    ESP_LOGI(TAG, "fetch_custom_header_loop==>");
    ota_exponent_backoff_init(32);
    while (true) {
        int delay = ota_exponent_backoff_get_next_delay();
        if (ESP_FAIL == ota_timeout_checkpoint(delay)) {
            return NULL;
        }
        ESP_LOGI(TAG, "one retry, next delay time: %d", delay);
        vTaskDelay((delay * 1000) / portTICK_PERIOD_MS);
        desc = fetch_custom_header_once(ota_url);
        if (desc) {
            return desc;
        }
    }
    return NULL;
}

static int version_cmp(char *version1, char *version2) 
{
    unsigned major1 = 0, minor1 = 0, bugfix1 = 0;
    unsigned major2 = 0, minor2 = 0, bugfix2 = 0;
    sscanf(version1, "%u.%u.%u", &major1, &minor1, &bugfix1);
    sscanf(version2, "%u.%u.%u", &major2, &minor2, &bugfix2);
    if (major1 < major2) return -1;
    if (major1 > major2) return 1;
    if (minor1 < minor2) return -1;
    if (minor1 > minor2) return 1;
    if (bugfix1 < bugfix2) return -1;
    if (bugfix1 > bugfix2) return 1;
    return 0;
}

static bool _need_upgrade(void *handle, ota_node_attr_t *node)
{
    bdsc_partition_info_t *info = NULL;
    char *sub_version = NULL;
    char *checksum = NULL;
    size_t len = -1, offset = -1;

    if (!g_custom_ota_bin_desc) {
        return false;
    }
    if (!(info = bdsc_partitions_get_partition_info(node->label))) {
        return false;
    }
    // 1. master version compare
    if (!g_bdsc_engine->is_force_ota && g_bdsc_engine->g_vendor_info->cur_version_num >= g_custom_ota_bin_desc->version) {
        ESP_LOGI(TAG, "version already newest, pass");
        return false;
    }
    // 2. sub version compare
    sub_version = g_custom_ota_bin_desc->custom_ota_bin_desc_parts[info->type].sub_version;
    checksum    = g_custom_ota_bin_desc->custom_ota_bin_desc_parts[info->type].checksum;
    len         = g_custom_ota_bin_desc->custom_ota_bin_desc_parts[info->type].len;
    offset      = g_custom_ota_bin_desc->custom_ota_bin_desc_parts[info->type].offset;
    if (!sub_version) {
        return false;
    }
    if (!g_bdsc_engine->is_force_ota && version_cmp(sub_version, node->current_version) <= 0) {
        ESP_LOGI(TAG, "sub version is lower, pass");
        return false;
    }
    // 3. checksum compare
    // bypass ota if checksum is same, even if in force_ota case!
    ESP_LOGI(TAG, "info->checksum: %s, checksum: %s", info->checksum, checksum);
    if (!strncmp(info->checksum, checksum, sizeof(info->checksum))) {
        ESP_LOGI(TAG, "md5 signature match, no need upgread");
        return false;
    }
    
    node->cus_offset   = offset;
    node->cus_bin_len  = len;
    node->cus_checksum = checksum;
    bdsc_partitions_clean_partition_info(node->label);

    return true;
}

ota_upgrade_ops_t *generate_custom_upgrade_list(char *custom_url)
{
    bdsc_partition_info_t* info = NULL;
    ota_upgrade_ops_t *list = audio_calloc(DUL1906_NEED_OTA_PARTITION_NUM, sizeof(ota_upgrade_ops_t));
    if (!list) {
        return NULL;
    }
    if (!custom_url) {
        ESP_LOGE(TAG, "custom_url not set!!");
        return NULL;
    }

    /* flash tone ota config */
    list[DUL1906_OTA_PARTITION_ID_FLASH_TONE].node.type         = ESP_PARTITION_TYPE_DATA;
    list[DUL1906_OTA_PARTITION_ID_FLASH_TONE].node.uri          = custom_url;
    info = bdsc_partitions_get_next_ota_part_info(TOGGLE_OTA_BOOT_TONE);
    list[DUL1906_OTA_PARTITION_ID_FLASH_TONE].node.label        = (char*)info->label; //"flash_tone_x";
    ESP_LOGI(TAG, "+++++++ next tone ota part: %s", (char*)info->label);
    list[DUL1906_OTA_PARTITION_ID_FLASH_TONE].node.cert_pem     = NULL;
    list[DUL1906_OTA_PARTITION_ID_FLASH_TONE].node.current_version = g_bdsc_engine->g_vendor_info->tone_sub_ver;
    list[DUL1906_OTA_PARTITION_ID_FLASH_TONE].break_after_fail  = true;
    list[DUL1906_OTA_PARTITION_ID_FLASH_TONE].reboot_flag       = false;
    ota_data_get_custom_proc(&list[DUL1906_OTA_PARTITION_ID_FLASH_TONE]);
    list[DUL1906_OTA_PARTITION_ID_FLASH_TONE].need_upgrade = _need_upgrade;

    /* dsp_bin ota config */
    list[DUL1906_OTA_PARTITION_ID_DSP_BIN].node.type         = ESP_PARTITION_TYPE_DATA;
    list[DUL1906_OTA_PARTITION_ID_DSP_BIN].node.uri          = custom_url;
    info = bdsc_partitions_get_next_ota_part_info(TOGGLE_OTA_BOOT_DSP);
    list[DUL1906_OTA_PARTITION_ID_DSP_BIN].node.label        = (char*)info->label;  //"dsp_bin_x";
    ESP_LOGI(TAG, "+++++++ next dsp ota part: %s", (char*)info->label);
    list[DUL1906_OTA_PARTITION_ID_DSP_BIN].node.cert_pem     = NULL;
    list[DUL1906_OTA_PARTITION_ID_DSP_BIN].node.current_version = g_bdsc_engine->g_vendor_info->dsp_sub_ver;
    list[DUL1906_OTA_PARTITION_ID_DSP_BIN].break_after_fail  = true;
    list[DUL1906_OTA_PARTITION_ID_DSP_BIN].reboot_flag       = false;
    ota_data_get_custom_proc(&list[DUL1906_OTA_PARTITION_ID_DSP_BIN]);
    list[DUL1906_OTA_PARTITION_ID_DSP_BIN].need_upgrade = _need_upgrade;

    /* app ota config */
    list[DUL1906_OTA_PARTITION_ID_APP].node.type         = ESP_PARTITION_TYPE_APP;
    list[DUL1906_OTA_PARTITION_ID_APP].node.uri          = custom_url;
    info = bdsc_partitions_get_next_ota_part_info(TOGGLE_OTA_BOOT_APP);
    list[DUL1906_OTA_PARTITION_ID_APP].node.label        = (char*)info->label; //"app_x";
    list[DUL1906_OTA_PARTITION_ID_APP].node.cert_pem     = NULL;
    list[DUL1906_OTA_PARTITION_ID_APP].node.current_version = g_bdsc_engine->g_vendor_info->app_sub_ver;
    list[DUL1906_OTA_PARTITION_ID_APP].break_after_fail  = true;
    list[DUL1906_OTA_PARTITION_ID_APP].reboot_flag       = false;
    ota_app_get_custom_proc(&list[DUL1906_OTA_PARTITION_ID_APP]);
    list[DUL1906_OTA_PARTITION_ID_APP].need_upgrade = _need_upgrade;
    return list;
}

static void _ota_st_reset()
{
    g_bdsc_engine->ota_download_st = OTA_DOWNLOAD_STAGE_INIT;
    ota_exponent_backoff_init(32);
}

static void app_ota_start(char *custom_url)
{
    char *last_url;

    if (g_custom_ota_bin_desc) {
        ESP_LOGE(TAG, "already in ota, pass");
        return;
    }

    /* loggin the ota time used */
    g_bdsc_engine->ota_time_begin = xTaskGetTickCount();
    _ota_st_reset();
    ESP_LOGI(TAG, "+++++++++ start check: %d", g_bdsc_engine->ota_time_begin);

    /* fetch custom header once */
    if (!custom_url) {
        // if has url, use it 
        // if no url, use last
        if ((last_url = get_last_ota_url())) {
            custom_url = last_url;
        } else {
            // if no last, use default
            custom_url = DUL1906_OTA_BIN_DEF_URL;
        }
        
    } else {
        set_last_ota_url(custom_url);
    }
    g_custom_ota_bin_desc = fetch_custom_header_loop(custom_url);
    if (!g_custom_ota_bin_desc) {
        _return_ota_error(-1);
        ESP_LOGE(TAG, "ota fail!");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        return;
    }
    ESP_LOGI(TAG, "server version: %d, local version: %d", g_custom_ota_bin_desc->version,
                                                           g_bdsc_engine->g_vendor_info->cur_version_num);
    if (!g_bdsc_engine->is_force_ota && g_custom_ota_bin_desc->version <= g_bdsc_engine->g_vendor_info->cur_version_num) {
        ESP_LOGW(TAG, "firmware alreay newest, pass");
        _return_ota_error(-2);
        custom_ota_bin_desc_cleanup();
        return;
    }

    ESP_LOGI(TAG, "Create OTA service");
    OTA_FLAG = xEventGroupCreate();
    ota_service_config_t ota_service_cfg = OTA_SERVICE_DEFAULT_CONFIG();
    ota_service_cfg.task_stack = OTA_SERVICE_STACK_SIZE;
    ota_service_cfg.evt_cb = ota_service_cb;
    ota_service_cfg.cb_ctx = NULL;
    periph_service_handle_t ota_service = ota_service_create(&ota_service_cfg);

    ota_upgrade_ops_t *upgrade_list = generate_custom_upgrade_list(custom_url);
    if (!upgrade_list) {
        periph_service_destroy(ota_service);
        custom_ota_bin_desc_cleanup();
        return;
    }
    
    ota_service_set_upgrade_param(ota_service, upgrade_list, DUL1906_NEED_OTA_PARTITION_NUM);
    ESP_LOGI(TAG, "Start OTA service");
    periph_service_start(ota_service);
    EventBits_t bits = xEventGroupWaitBits(OTA_FLAG, OTA_FINISH, true, false, portMAX_DELAY);
    if (bits & OTA_FINISH) {
        ESP_LOGW(TAG, "upgrade finished, Please check the result of OTA");
    }
    vEventGroupDelete(OTA_FLAG);
    periph_service_destroy(ota_service);
    custom_ota_bin_desc_cleanup();
}


static int bdsc_ota_start(char *url)
{
    bool tmp_mute;
    
    bdsc_stop_wakeup();
    if (!g_bdsc_engine->silent_mode) {
        display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_OTA, 0);
    }
    
    tmp_mute = g_is_mute;
    g_is_mute = false;    // force pa on
    bdsc_play_hint(BDSC_HINT_OTA_START);
    g_bdsc_engine->in_ota_process_flag = true;
    app_ota_start(url);
    g_bdsc_engine->in_ota_process_flag = false;
    g_bdsc_engine->ota_fail_flag = false;
    ESP_LOGW(TAG, "OTA Finish!");
    // led off
    display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_WIFI_CONNECTED, 0);
    g_is_mute = tmp_mute;

    if (g_is_mute) {
        display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_MUTE_ON, 0);
    }
    else {
        bdsc_start_wakeup();
    }

    return 0;
}

static void _ota_task(void *para)
{
    bdsc_ota_info_t *ota_info = (bdsc_ota_info_t *) para;
    bdsc_ota_start(ota_info->url);
    // Can not reach here if really upgrade successfully.
    ESP_LOGI(TAG, "_ota_task end");
    if (ota_info->url) {
        free(ota_info->url);
    }
    free(ota_info);
    vTaskDelete(NULL);
}

void start_ota_monitor(void);
int bdsc_start_ota_thread(char *ota_url)
{
    char *resp = NULL;

    ESP_LOGW(TAG, "==> start ota thread");
#if CONFIG_CUPID_BOARD_V2
    if (audio_board_get_battery_voltage() < 1233 && gpio_get_level(USB_DET_GPIO) != 1) {
        ESP_LOGW(TAG, "battery low, skip OTA!!");
        if ((resp = create_ota_download_status_string(-2))) {
            bdsc_engine_channel_data_upload((uint8_t*)resp, strlen(resp) + 1);
            free(resp);
        }
        return 0;
    }
#endif
    
    if (g_bdsc_engine->in_ota_process_flag) {
        ESP_LOGW(TAG, "already in ota process, pass");
        return -1;
    }
    bdsc_ota_info_t *info = audio_calloc(1, sizeof(bdsc_ota_info_t));
    if (ota_url) {
        info->url = audio_strdup(ota_url);
    }

    if (app_task_regist(APP_TASK_ID_OTA, _ota_task, info, NULL) != pdPASS) {
        ESP_LOGE(TAG, "ERROR creating _ota_task task! Out of memory?");
    }
    start_ota_monitor();
    g_bdsc_engine->in_ota_process_flag = true;
    return 0;
}
