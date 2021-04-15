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
#ifndef __OTA_PROC_DEFAULT__
#define __OTA_PROC_DEFAULT__

#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "audio_element.h"
#include "ota_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief     get the custom process of `app partition` upgrade
  *
  * @param[in]  handle          pointer to `ota_upgrade_ops_t` structure
  *
  * @return
  *    - void
  */
void ota_app_get_custom_proc(ota_upgrade_ops_t *ops);

/**
  * @brief     get the custom process of `data partition` upgrade
  *
  * @param[in]  handle          pointer to `ota_upgrade_ops_t` structure
  *
  * @return
  *    - void
  */
void ota_data_get_custom_proc(ota_upgrade_ops_t *ops);

/**
  * @brief     read from the stream of upgrading
  *
  * @param[in]  handle          pointer to upgrade handle
  * @param[in]  buf             pointer to receive buffer
  * @param[in]  wanted_size     bytes to read
  *
  * @return
  *    - ESP_OK:  Success
  *    - Others:  Failed
  */
esp_err_t ota_data_image_stream_read(void *handle, char *buf, int wanted_size);

/**
  * @brief     write to the data partition under upgrading
  *
  * @param[in]  handle          pointer to upgrade handle
  * @param[in]  buf             pointer to data buffer
  * @param[in]  size            bytes to write
  *
  * @return
  *    - ESP_OK:  Success
  *    - Others:  Failed
  */
esp_err_t ota_data_partition_write(void *handle, char *buf, int size);

#ifdef __cplusplus
}
#endif

#endif /*__OTA_PROC_DEFAULT__*/