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
#ifndef _APP_OTA_POLICY_H_
#define _APP_OTA_POLICY_H_

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_DOWNLOAD_NORMAL_WAIT_TIME       (60)
#define OTA_DOWNLOAD_MID_WAIT_TIME          (1.5 * OTA_DOWNLOAD_NORMAL_WAIT_TIME)
#define OTA_DOWNLOAD_TOTAL_WAIT_TIME        (3 * OTA_DOWNLOAD_NORMAL_WAIT_TIME)

typedef enum {
    OTA_DOWNLOAD_STAGE_INIT,
    OTA_DOWNLOAD_STAGE_MID,
    OTA_DOWNLOAD_STAGE_END,
} ota_download_stage_t;

int ota_timeout_checkpoint(int delay);

void ota_exponent_backoff_init(int limit);

int ota_exponent_backoff_get_next_delay();

#ifdef __cplusplus
}
#endif
#endif