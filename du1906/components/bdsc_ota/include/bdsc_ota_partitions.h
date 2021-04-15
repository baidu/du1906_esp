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
#ifndef _BDSC_OTA_PART_H__
#define _BDSC_OTA_PART_H__

#ifdef __cplusplus
extern "C" {
#endif


#define NVS_USER_PART_LABEL     "nvs_user"
#define OTA_BACKUP_PART_NUM     (3)

typedef enum {
    TOGGLE_OTA_BOOT_TONE,
    TOGGLE_OTA_BOOT_DSP,
    TOGGLE_OTA_BOOT_APP,
} toggle_ota_boot_type_t;


typedef struct {
    char                    label[16];
    char                    checksum[32 + 1];
    toggle_ota_boot_type_t  type;
} bdsc_partition_info_t;


typedef struct {
    int boot_toggle[OTA_BACKUP_PART_NUM];
    bdsc_partition_info_t ota_backup_partition_info[OTA_BACKUP_PART_NUM];
} bdsc_partitions_t;


extern bdsc_partitions_t *g_bdsc_ota_partitions;

int bdsc_partitions_init();

bdsc_partition_info_t* bdsc_partitions_get_partition_info(const char *label);

int bdsc_partitions_clean_partition_info(const char *label);

int bdsc_partitions_update_partition_info(bdsc_partition_info_t info);

int bdsc_partitions_bootable_chain_update();

const char* bdsc_partitions_get_bootable_flash_tone_label();

const char* bdsc_partitions_get_bootable_dsp_label();

const char* bdsc_partitions_get_bootable_app_label();

bdsc_partition_info_t* bdsc_partitions_get_next_ota_part_info(toggle_ota_boot_type_t type);

#ifdef __cplusplus
}
#endif

#endif
