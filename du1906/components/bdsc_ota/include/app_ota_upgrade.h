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

#ifndef __APP_OTA_UPGRADE_H__
#define __APP_OTA_UPGRADE_H__

#include "bdsc_ota_partitions.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char   *sub_version;
    size_t offset;
    size_t len;
    char   *checksum;
} custom_ota_bin_desc_part_t;

typedef struct {
    int version;
    custom_ota_bin_desc_part_t custom_ota_bin_desc_parts[OTA_BACKUP_PART_NUM];
} custom_ota_bin_desc_t;

/**
 * @brief Global ota bin descriptor
 *
 */
extern custom_ota_bin_desc_t *g_custom_ota_bin_desc;

/**
 * @brief Start the ota service that registered
 *
 */
int bdsc_start_ota_thread(char *ota_url);


#ifdef __cplusplus
}
#endif

#endif
