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
#include <string.h>

#include "audio_mem.h"
#include "esp_https_ota.h"
#include "esp_fs_ota.h"
#include "esp_log.h"
#include "fatfs_stream.h"
#include "http_raw_stream.h"

#include "ota_proc_iot.h"
#include "cJSON.h"
#include "mbedtls/md5.h"
#include "bdsc_tools.h"
#include "app_ota_upgrade.h"
#include "bdsc_engine.h"

#define READER_BUF_LEN (1024 * 2)


typedef struct {
    audio_element_handle_t  r_stream;
    const esp_partition_t   *partition;

    int                     write_offset;
    char                    read_buf[READER_BUF_LEN];
} ota_data_upgrade_ctx_t;

typedef struct {
    void *ota_handle;
    esp_err_t (*get_img_desc)(void *handle, esp_app_desc_t *new_app_info);
    esp_err_t (*perform)(void *handle);
    int (*get_image_len_read)(void *handle);
    bool (*all_read)(void *handle);
    esp_err_t (*finish)(void *handle);
} ota_app_upgrade_ctx_t;


static const char *TAG = "OTA_CUSTOM";

static esp_err_t validate_image_header(esp_app_desc_t *new_app_info)
{
    if (new_app_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s, the incoming firmware version %s", running_app_info.version, new_app_info->version);
    }

    if (memcmp(new_app_info->version, running_app_info.version, sizeof(new_app_info->version)) == 0) {
        ESP_LOGW(TAG, "Current running version is the same as a new. We will not continue the update.");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t ota_app_partition_prepare(void **handle, ota_node_attr_t *node)
{
    ota_app_upgrade_ctx_t *context = audio_calloc(1, sizeof(ota_app_upgrade_ctx_t));
    AUDIO_NULL_CHECK(TAG, context, return ESP_FAIL);
    *handle = context;

    if (strstr(node->uri, "https://") || strstr(node->uri, "http://")) {
        context->get_img_desc = esp_https_ota_get_img_desc;
        context->perform = esp_https_ota_perform;
        context->get_image_len_read = esp_https_ota_get_image_len_read;
        //context->all_read = esp_https_ota_is_complete_data_received;
        context->finish = esp_https_ota_finish;
    } else {
        return ESP_FAIL;
    }


    return ESP_OK;
}

static esp_err_t ota_app_partition_exec_upgrade(void *handle, ota_node_attr_t *node)
{
    ota_app_upgrade_ctx_t *context = (ota_app_upgrade_ctx_t *)handle;
    AUDIO_NULL_CHECK(TAG, context, goto err_out);
    esp_err_t err = ESP_FAIL;

    // skip offset bytes
   esp_http_client_config_t config = {
        .url = node->uri,
        .cert_pem = node->cert_pem,
        .timeout_ms = 5 * 1000,
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };
    err = esp_https_ota_begin_with_pos(&ota_config, &context->ota_handle, node->cus_offset);
    if (err != ESP_OK) {
        ERR_OUT(err_ota_finish, "ESP HTTPS OTA Begin failed");
    }

    // read desc
    esp_app_desc_t app_desc;
    err = context->get_img_desc(context->ota_handle, &app_desc);
    if (err != ESP_OK) {
        ERR_OUT(err_ota_finish, "get_img_desc failed");
    }
    err = validate_image_header(&app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "image header verification failed");
        //return false;
    }

    int last_len = 0, current_len = 0;
    int read_len_origin = g_bdsc_engine->ota_read_bytes;
    while (1) {
        err = context->perform(context->ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS && err != ESP_ERR_FS_OTA_IN_PROGRESS && err != ESP_ERR_HTTPS_OTA_RECONNECT) {
            break;
        }
        ESP_LOGI(TAG, "Image bytes read: %d", (current_len = context->get_image_len_read(context->ota_handle)));
        g_bdsc_engine->ota_read_bytes = read_len_origin + current_len;
        if (current_len - last_len >= 64 * 1024) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            last_len = current_len;
        }
    }

    if (err == ESP_OK) {
        err = context->finish(context->ota_handle);
        if (err != ESP_OK) {
            if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
                ESP_LOGE(TAG, "Image validation failed, image is corrupted");
            }
            ESP_LOGE(TAG, "upgrade failed %d", err);
        }

        ESP_LOGI(TAG, "app upgrade success");
        return err;
    }
    

err_ota_finish:
    err = context->finish(context->ota_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        }
        ESP_LOGE(TAG, "upgrade failed %d", err);
    }
err_out:
    return ESP_FAIL;
}

static esp_err_t ota_app_partition_finish(void *handle, ota_node_attr_t *node, esp_err_t result)
{
    audio_free(handle);
    return ESP_OK;
}

void ota_app_get_custom_proc(ota_upgrade_ops_t *ops)
{
    ops->prepare         = ota_app_partition_prepare;
    ops->need_upgrade    = NULL;
    ops->execute_upgrade = ota_app_partition_exec_upgrade;
    ops->finished_check  = ota_app_partition_finish;
}



static esp_err_t ota_data_partition_prepare(void **handle, ota_node_attr_t *node)
{
    ota_data_upgrade_ctx_t *context = audio_calloc(1, sizeof(ota_data_upgrade_ctx_t));
    AUDIO_NULL_CHECK(TAG, context, return ESP_FAIL);
    *handle = context;

    context->partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, node->label);
    if (context->partition == NULL) {
        ESP_LOGE(TAG, "partition [%s] not found", node->label);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "data upgrade uri %s", node->uri);
    return ESP_OK;
}


static esp_err_t ota_data_partition_exec_upgrade(void *handle, ota_node_attr_t *node)
{
    int r_size = 0;
    esp_err_t err = ESP_OK;
    char sig[64];
    mbedtls_md5_context md5_ctx;
    unsigned char md5_cur[16];
    mbedtls_md5_init(&md5_ctx);
    ota_data_upgrade_ctx_t *context = (ota_data_upgrade_ctx_t *)handle;
    AUDIO_NULL_CHECK(TAG, context, return ESP_FAIL);

    if (strstr(node->uri, "https://") || strstr(node->uri, "http://")) {
        http_raw_stream_cfg_t http_cfg = HTTP_RAW_STREAM_CFG_DEFAULT();
        http_cfg.type = AUDIO_STREAM_READER;

        context->r_stream = http_raw_stream_init(&http_cfg);
    } else {
        ERR_OUT(err_md5_free, "not support uri");
    }
    audio_element_set_uri(context->r_stream, node->uri);

    // skip offset bytes
    audio_element_info_t info = {0};
    audio_element_getinfo(context->r_stream, &info);
    info.byte_pos = node->cus_offset;
    audio_element_setinfo(context->r_stream, &info);

    if (audio_element_process_init(context->r_stream) != ESP_OK) {
        ERR_OUT(err_deinit_el, "reader stream init failed");
    }

    if ((err = esp_partition_erase_range(context->partition, 0, context->partition->size)) != ESP_OK) {
        ERR_OUT(err_deinit_el_process, "Erase [%s] failed and return %d", node->label, err);
    }
    if (mbedtls_md5_starts_ret(&md5_ctx)) {
        ERR_OUT(err_deinit_el_process, "mbedtls_md5_starts_ret() error");
    }

    int last_len = 0, current_len = 0;
    // start reading
    while ((r_size = audio_element_input(context->r_stream, context->read_buf, READER_BUF_LEN)) > 0) {
        g_bdsc_engine->ota_read_bytes += r_size;
        ESP_LOGI(TAG, "write_offset %d, r_size %d, %x %x %x %x", context->write_offset, r_size, 
                        context->read_buf[0], context->read_buf[1],
                        context->read_buf[2], context->read_buf[3]);
        if (context->write_offset + r_size > node->cus_bin_len) {
            // checksum
            if (mbedtls_md5_update_ret(&md5_ctx, (const unsigned char *)context->read_buf, node->cus_bin_len - context->write_offset)) {
                ERR_OUT(err_deinit_el_process, "mbedtls_md5_update_ret() failed");
            }
            ESP_LOGI(TAG, "reach the end, node->cus_bin_len %d, write len: %d", node->cus_bin_len, node->cus_bin_len - context->write_offset);
            if (esp_partition_write(context->partition, context->write_offset, context->read_buf, node->cus_bin_len - context->write_offset) == ESP_OK) {
                // checksum
                if (mbedtls_md5_finish_ret(&md5_ctx, md5_cur)) {
                    ERR_OUT(err_deinit_el_process, "mbedtls_md5_finish_ret() error");
                }
                decimal_to_hex(md5_cur, sizeof(md5_cur), sig);
                ESP_LOGI(TAG, "ok sig: %s, tmp sig: %s", node->cus_checksum, sig);
                mbedtls_md5_free(&md5_ctx);
                if (strcmp(sig, node->cus_checksum)) {
                    ERR_OUT(err_deinit_el_process, "md5 signature check fail!");
                }
                ESP_LOGE(TAG, "md5 signature check ok!");
                context->write_offset = node->cus_bin_len;
                audio_element_process_deinit(context->r_stream);
                audio_element_deinit(context->r_stream);
                mbedtls_md5_free(&md5_ctx);
                return ESP_OK;
            } else {
                ERR_OUT(err_deinit_el_process, "esp_partition_write fail!");
            }
        }
        
        // checksum
        if (mbedtls_md5_update_ret(&md5_ctx, (const unsigned char *)context->read_buf, r_size)) {
            ERR_OUT(err_deinit_el_process, "mbedtls_md5_update_ret() failed");
        }
        if (esp_partition_write(context->partition, context->write_offset, context->read_buf, r_size) == ESP_OK) {
            context->write_offset += r_size;
            current_len = context->write_offset;
            if (current_len - last_len >= 64 * 1024) {
                vTaskDelay(100 / portTICK_PERIOD_MS);
                last_len = current_len;
            }
        } else {
            ERR_OUT(err_deinit_el_process, "esp_partition_write fail!");
        }
    }
    ESP_LOGE(TAG, "partition %s upgrade quit abnormally", node->label);

err_deinit_el_process:
    audio_element_process_deinit(context->r_stream);
err_deinit_el:
    audio_element_deinit(context->r_stream);
    context->write_offset = 0;
err_md5_free:
    mbedtls_md5_free(&md5_ctx);
    return ESP_FAIL;
}


static esp_err_t ota_data_partition_finish(void *handle, ota_node_attr_t *node, esp_err_t result)
{
    // ota_data_upgrade_ctx_t *context = (ota_data_upgrade_ctx_t *)handle;
    // AUDIO_NULL_CHECK(TAG, context->r_stream, return ESP_FAIL);
    // audio_element_process_deinit(context->r_stream);
    // audio_element_deinit(context->r_stream);

    audio_free(handle);
    return result;
}

void ota_data_get_custom_proc(ota_upgrade_ops_t *ops)
{
    ops->prepare         = ota_data_partition_prepare;
    ops->need_upgrade    = NULL;
    ops->execute_upgrade = ota_data_partition_exec_upgrade;
    ops->finished_check  = ota_data_partition_finish;
}

esp_err_t ota_data_image_stream_read(void *handle, char *buf, int wanted_size)
{
    ota_data_upgrade_ctx_t *context = (ota_data_upgrade_ctx_t *)handle;

    if (context == NULL) {
        ESP_LOGE(TAG, "run prepare first");
        return ESP_ERR_INVALID_STATE;
    }
    AUDIO_NULL_CHECK(TAG, context->r_stream, return ESP_FAIL);
    int r_size = 0;
    do {
        int ret = audio_element_input(context->r_stream, buf, wanted_size - r_size);
        if (ret > 0) {
            r_size += ret;
        } else {
            break;
        }
    } while (r_size < wanted_size);

    if (r_size == wanted_size) {
        return ESP_OK;
    } else {
        return ESP_FAIL;
    }
}

esp_err_t ota_data_partition_write(void *handle, char *buf, int size)
{
    ota_data_upgrade_ctx_t *context = (ota_data_upgrade_ctx_t *)handle;

    if (context == NULL) {
        ESP_LOGE(TAG, "run prepare first");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "write_offset %d, size %d", context->write_offset, size);
    if (esp_partition_write(context->partition, context->write_offset, buf, size) == ESP_OK) {
        context->write_offset += size;
        return ESP_OK;
    } else {
        return ESP_FAIL;
    }
}
