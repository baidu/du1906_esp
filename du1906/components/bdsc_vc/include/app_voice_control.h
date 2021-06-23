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
 
#ifndef __APP_VOICE_CONTROL_H__
#define __APP_VOICE_CONTROL_H__

#include <string.h>
#include "bdsc_json.h"
#include <stdint.h>
#include "app_music.h"

#define SLOTS_MAX_NUM   4         //the slot max numbers in json data
#define NO_CMP_STR      "the term is not compare"   //ignore this string in comparing

typedef enum {
    VOICE_CTL_OPEN_BT,
    VOICE_CTL_CLOSE_BT,
    VOICE_CTL_MUSIC_CONTINUE,
    VOICE_CTL_MUSIC_PAUSE,
    VOICE_CTL_MUSIC_STOP,
    VOICE_CTL_VOL_UP,
    VOICE_CTL_VOL_TO,
    VOICE_CTL_VOL_DOWN,
    VOICE_CTL_VOL_ON,
    VOICE_CTL_VOL_OFF,
    VOICE_CTL_VOL_MUTE,
    VOICE_CTL_URL_PALY,
    VOICE_CTL_NOT_FOUND,
    VOICE_CTL_MUSIC_ID_PALY,
    IAQ_QUERY,
    UNKNOW_QUERY,     //it is used to handle unknow query
} voice_ctr_cmd_t;    /*the voice control CODE ID*/

typedef struct {
    const char *name;
    const char *value;
} slots_key_value_t;

typedef struct {
    const char *type;
    const char *value;
} custom_reply_t;

typedef void (*cmd_handle_func)(const void *pdata, uint32_t code);
typedef struct _unit_data {
    const uint32_t  unit_code;    //code
    const char      *intent;      //intend
    const char      *origin;      //origin
    const char      *action_type;  //action_type
    custom_reply_t  custom_reply; //custom reply
    uint8_t         slots_num;    //slot number
    slots_key_value_t slots_key_value[SLOTS_MAX_NUM];   //slot array
} unit_data_t;

void app_voice_control_feed_data(BdsJson *data_json, void *private_data);
#endif
