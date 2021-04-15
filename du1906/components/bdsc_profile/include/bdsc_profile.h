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
#ifndef __BDSC_PROFILE_H__
#define __BDSC_PROFILE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>
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


#define NVS_DEVICE_SN_NAMESPACE     "names_bd_device"
#define NVS_DEVICE_SYS_NAMESPACE    "names_bd_system"
#define NVS_DEVICE_CUSTOM_NAMESPACE "names_bd_custom"

typedef enum {
    PROFILE_KEY_TYPE_FC,
    PROFILE_KEY_TYPE_PK,
    PROFILE_KEY_TYPE_AK,
    PROFILE_KEY_TYPE_SK,

    PROFILE_KEY_TYPE_MQTT_BROKER,
    PROFILE_KEY_TYPE_MQTT_USERNAME,
    PROFILE_KEY_TYPE_MQTT_PASSWD,
    PROFILE_KEY_TYPE_MQTT_CID,

    PROFILE_KEY_TYPE_VER_NUM,
    PROFILE_KEY_TYPE_TONE_SUB,
    PROFILE_KEY_TYPE_DSP_SUB,
    PROFILE_KEY_TYPE_APP_SUB,
    PROFILE_KEY_TYPE_SLIENT_MODE,
    PROFILE_KEY_TYPE_LAST_OTA_URL,
    PROFILE_KEY_TYPE_IS_ACTIVE_MUSIC_LICENSE,

} bdsc_profile_key_type_t;

/* max key len 15 */
#define NVS_DEVICE_SN_KEY               "bd_device_sn"
#define PROFILE_NVS_KEY_BROKER          "bdd_broker"
#define PROFILE_NVS_KEY_MQTT_USERNAME   "bdd_mq_un"
#define PROFILE_NVS_KEY_MQTT_PASSWD     "bdd_mq_pwd"
#define PROFILE_NVS_KEY_MQTT_CID        "bdd_mq_cid"
#define PROFILE_NVS_KEY_VER_NUM         "bdd_ver"
#define PROFILE_NVS_KEY_TONE_SUB        "bdd_t"
#define PROFILE_NVS_KEY_DSP_SUB         "bdd_d"
#define PROFILE_NVS_KEY_APP_SUB         "bdd_a"
#define PROFILE_NVS_KEY_SLIENT_MODE     "bdd_slt"
#define PROFILE_NVS_KEY_LAST_OTA_URL    "bdd_l_url"
#define PROFILE_NVS_KEY_IS_ACTIVE_MUSIC_LICENSE    "bdd_license"


/**
 * @brief vendor_info_t structure to store user info
 */
typedef struct {
    char *fc;
    char *pk;
    char *ak;
    char *sk;

    char *mqtt_broker;
    char *mqtt_username;
    char *mqtt_password;
    char *mqtt_cid;

    int cur_version_num;
    char *tone_sub_ver;
    char *dsp_sub_ver;
    char *app_sub_ver;
    char *last_ota_url;
    int is_active_music_license;
} vendor_info_t;

typedef enum {
    CUSTOM_KEY_GET,
    CUSTOM_KEY_SET,
} custom_op_type_t;

typedef enum {
    CUSTOM_KEY_TYPE_INT32,
    CUSTOM_KEY_TYPE_STRING,
} custom_key_type_t;

typedef struct {
    custom_op_type_t    op;
    custom_key_type_t   type;
    char                *location;
    const char          *key;
    char                *value;
    int                 *len;
} custom_key_value_arg_t;

/**
 * @brief load vendor_info from profile partition
 */
int profile_init();

/**
 * @brief save key to nvs
 */
int profile_key_set(bdsc_profile_key_type_t key_type, void *value);

/**
 * @brief get key from nvs
 */
int profile_key_get(bdsc_profile_key_type_t key_type, void **value_ptr_holder);

/**
 * @brief get custom key from nvs
 */
int custom_key_op_safe(custom_op_type_t op, custom_key_type_t type, const char *location, const char *key, char *ret_value, int *ret_len);


#ifdef __cplusplus
}
#endif

#endif
