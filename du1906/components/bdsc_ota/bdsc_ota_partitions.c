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
#include "app_ota_upgrade.h"
#include "bdsc_ota_partitions.h"
#include "bdsc_profile.h"

#define     TAG     "OTA_PART"

bdsc_partitions_t *g_bdsc_ota_partitions;

static const char* bdsc_partitions_get_ota_flash_tone_label()
{
    return (g_bdsc_ota_partitions->boot_toggle[TOGGLE_OTA_BOOT_TONE] ? "flash_tone_0" : "flash_tone_1");
}

static const char* bdsc_partitions_get_ota_dsp_label()
{
    return (g_bdsc_ota_partitions->boot_toggle[TOGGLE_OTA_BOOT_DSP] ? "dsp_bin_0" : "dsp_bin_1");
}

static const char* bdsc_partitions_get_ota_app_label()
{
    return (g_bdsc_ota_partitions->boot_toggle[TOGGLE_OTA_BOOT_APP] ? "ota_0" : "ota_1");
}

static void ota_partitions_label_update()
{
    ESP_LOGI(TAG, "==> ota_partitions_label_update");
    int sz = sizeof(g_bdsc_ota_partitions->ota_backup_partition_info[0].label);
    strncpy(g_bdsc_ota_partitions->ota_backup_partition_info[TOGGLE_OTA_BOOT_TONE].label,
            bdsc_partitions_get_ota_flash_tone_label(), sz);
    g_bdsc_ota_partitions->ota_backup_partition_info[TOGGLE_OTA_BOOT_TONE].type = TOGGLE_OTA_BOOT_TONE;
    strncpy(g_bdsc_ota_partitions->ota_backup_partition_info[TOGGLE_OTA_BOOT_DSP].label,
            bdsc_partitions_get_ota_dsp_label(), sz);
    g_bdsc_ota_partitions->ota_backup_partition_info[TOGGLE_OTA_BOOT_DSP].type = TOGGLE_OTA_BOOT_DSP;
    strncpy(g_bdsc_ota_partitions->ota_backup_partition_info[TOGGLE_OTA_BOOT_APP].label,
            bdsc_partitions_get_ota_app_label(), sz);
    g_bdsc_ota_partitions->ota_backup_partition_info[TOGGLE_OTA_BOOT_APP].type = TOGGLE_OTA_BOOT_APP;
}

int bdsc_partitions_init()
{
    int err = -1;
    nvs_handle deviceNvsHandle;

    // init nvs flash
    err = nvs_flash_init_partition(NVS_USER_PART_LABEL);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "nvs_flash_init_partition failed, err: %x", err);
        return -1;
    }

    err = nvs_open_from_partition(NVS_USER_PART_LABEL, NVS_DEVICE_SYS_NAMESPACE, NVS_READWRITE, &deviceNvsHandle);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "nvs_open_from_partition fail, err: %x", err);
        return -1;
    }

    // load blob data from nvs
    if (g_bdsc_ota_partitions) {
        audio_free(g_bdsc_ota_partitions);
    }
    if (!(g_bdsc_ota_partitions = audio_calloc(1, sizeof(bdsc_partitions_t)))) {
        ESP_LOGE(TAG, "audio_calloc fail");
        nvs_close(deviceNvsHandle);
        return -1;;
    }
    size_t sz = sizeof(*g_bdsc_ota_partitions);
    err = nvs_get_blob(deviceNvsHandle, "ota_parts", g_bdsc_ota_partitions, &sz);
    if (ESP_OK != err && ESP_ERR_NVS_NOT_FOUND != err) {
        ESP_LOGE(TAG, "nvs_get_blob fail, err: %x", err);
        nvs_close(deviceNvsHandle);
        return -1;;
    }
    if (ESP_ERR_NVS_NOT_FOUND == err) {
        ESP_LOGI(TAG, "Empty nvs? intialize ota_parts...");
        ota_partitions_label_update();
    }

    ESP_LOGI(TAG, "++++++ dump ota partitions info: ");
    for (int i = 0; i < 3; i++) {
        ESP_LOGI(TAG, "%s, %s, %d", g_bdsc_ota_partitions->ota_backup_partition_info[i].label,
        g_bdsc_ota_partitions->ota_backup_partition_info[i].checksum,
        g_bdsc_ota_partitions->ota_backup_partition_info[i].type);
    }

    nvs_close(deviceNvsHandle);
    return 0;
}

static int update_ota_parts_to_nvs()
{
    int err = -1;
    nvs_handle deviceNvsHandle;

    err = nvs_open_from_partition(NVS_USER_PART_LABEL, NVS_DEVICE_SYS_NAMESPACE, NVS_READWRITE, &deviceNvsHandle);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "nvs_open_from_partition fail, err: %x", err);
        return -1;
    }

    err = nvs_set_blob(deviceNvsHandle, "ota_parts", g_bdsc_ota_partitions, sizeof(*g_bdsc_ota_partitions));
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "nvs_set_blob fail, err: %x", err);
        nvs_close(deviceNvsHandle);
        return -1;;
    }
    nvs_commit(deviceNvsHandle);
    ESP_LOGI(TAG, "+++++++ save to nvs ok!");
    nvs_close(deviceNvsHandle);
    return 0;
}

bdsc_partition_info_t* bdsc_partitions_get_partition_info(const char *label)
{
    int i = 0;
    for (i = 0; i < OTA_BACKUP_PART_NUM; i++) {
        bdsc_partition_info_t info = g_bdsc_ota_partitions->ota_backup_partition_info[i];
        if (!strcmp(info.label, label)) {
            return &g_bdsc_ota_partitions->ota_backup_partition_info[i];
        }
    }
    return NULL;
}

int bdsc_partitions_clean_partition_info(const char *label)
{
    int i = 0;
    for (i = 0; i < OTA_BACKUP_PART_NUM; i++) {
        bdsc_partition_info_t info = g_bdsc_ota_partitions->ota_backup_partition_info[i];

        if (!strcmp(info.label, label)) {
            // reset patition info to nvs
            //memset(&g_bdsc_ota_partitions->ota_backup_partition_info[i], 0, sizeof(bdsc_partition_info_t));
            memset(&g_bdsc_ota_partitions->ota_backup_partition_info[i].checksum, 0, 32);
            update_ota_parts_to_nvs();
            return 0;
        }
    }
    return 0;
}

int bdsc_partitions_update_partition_info(bdsc_partition_info_t info)
{
    int idx = info.type;
    // save to nvs
    ESP_LOGI(TAG, "==> bdsc_partitions_update_partition_info");
    g_bdsc_ota_partitions->ota_backup_partition_info[idx] = info;
    update_ota_parts_to_nvs();
    return 0;
}

static void bdsc_partitions_ota_toggle_switch(toggle_ota_boot_type_t type)
{
    ESP_LOGI(TAG, "==> bdsc_partitions_ota_toggle_switch");
    g_bdsc_ota_partitions->boot_toggle[type] = 1 - g_bdsc_ota_partitions->boot_toggle[type];
}

int bdsc_partitions_bootable_chain_update()
{
    ESP_LOGI(TAG, "==> bdsc_partitions_bootable_chain_update");
    // update all version info to nvs
    int i = 0;
    for (i = 0; i < OTA_BACKUP_PART_NUM; i++) {
        bdsc_partition_info_t info = g_bdsc_ota_partitions->ota_backup_partition_info[i];

        if (info.checksum && g_custom_ota_bin_desc &&
            !strcmp(info.checksum, g_custom_ota_bin_desc->custom_ota_bin_desc_parts[i].checksum)) {
                bdsc_partitions_ota_toggle_switch(i);
        }
    }
    memset(g_bdsc_ota_partitions->ota_backup_partition_info, 0, sizeof(g_bdsc_ota_partitions->ota_backup_partition_info));
    ota_partitions_label_update();
    update_ota_parts_to_nvs();
    return 0;
}



const char* bdsc_partitions_get_bootable_flash_tone_label()
{
    return (g_bdsc_ota_partitions->boot_toggle[TOGGLE_OTA_BOOT_TONE] ? "flash_tone_1" : "flash_tone_0");
}


const char* bdsc_partitions_get_bootable_dsp_label()
{
    return (g_bdsc_ota_partitions->boot_toggle[TOGGLE_OTA_BOOT_DSP] ? "dsp_bin_1" : "dsp_bin_0");
}


const char* bdsc_partitions_get_bootable_app_label()
{
    return (g_bdsc_ota_partitions->boot_toggle[TOGGLE_OTA_BOOT_APP] ? "ota_1" : "ota_0");
}


bdsc_partition_info_t* bdsc_partitions_get_next_ota_part_info(toggle_ota_boot_type_t type)
{
    return (&g_bdsc_ota_partitions->ota_backup_partition_info[type]);
}
