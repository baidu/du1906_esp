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

#include "app_voice_control.h"
#include "app_bt_init.h"
#include "audio_player.h"
#include "audio_mem.h"
#include <string.h>
#include <bdsc_engine.h>
#include "freertos/queue.h"
#include "app_music.h"

#define TAG "APP_UNIT"

#define CMP_STR_LEN     1024                        //the buff length of comparing

extern esp_bd_addr_t g_bd_addr;
extern bool g_pre_player_need_resume;
extern void unit_cmd_handle(unit_data_t *data, uint32_t code);
extern unit_data_t g_user_unit_data[];
extern int g_user_unit_array_num;
int __attribute__((weak)) g_user_unit_array_num = 0;    //number of user unit data array element
unit_data_t __attribute__((weak)) g_user_unit_data[0];  //it's defined by user in xxx_ui.c
void __attribute__((weak))  user_unit_cmd_handle(unit_data_t *pdata, uint32_t code)
{
    ESP_LOGE(TAG, " add your handle function on xxx_ui.c");
}

/************************** add myself unit skill usage ****************************
* 1. define your g_unit_data
* 2. append your g_unit_data to array
* 3. add your control function on unit_cmd_handle
************************************* end ****************************************/
static unit_data_t g_unit_data[] = {
    /* unit_code                 intend        origin    action_type     { custom_reply  }   slot number {slots   (flexible array)            }  */
    {VOICE_CTL_OPEN_BT,         "DEV_ACTION", "1045734", NO_CMP_STR, {NO_CMP_STR, NO_CMP_STR}, 2, {{"user_action", "ON"}, {"user_func_bluetooth", "BLUETOOTH"}}},
    {VOICE_CTL_CLOSE_BT,        "DEV_ACTION", "1045734", NO_CMP_STR, {NO_CMP_STR, NO_CMP_STR}, 2, {{"user_action", "OFF"}, {"user_func_bluetooth",  "BLUETOOTH"}}},
    {VOICE_CTL_MUSIC_CONTINUE,  "DEV_ACTION", "1045734", NO_CMP_STR, {NO_CMP_STR, NO_CMP_STR}, 1, {{"user_action", "CONTINUE"}}},
    {VOICE_CTL_MUSIC_PAUSE,     "DEV_ACTION", "1045734", NO_CMP_STR, {NO_CMP_STR, NO_CMP_STR}, 1, {{"user_action", "PAUSE"}}},
    {VOICE_CTL_MUSIC_STOP,      "DEV_ACTION", "1045734", NO_CMP_STR, {NO_CMP_STR, NO_CMP_STR}, 1, {{"user_action", "STOP"}}},
    {VOICE_CTL_VOL_UP,          "DEV_ACTION", "1045734", NO_CMP_STR, {NO_CMP_STR, NO_CMP_STR}, 2, {{"user_action", "SET_UP"}, {"user_volume", "VOLUME"}}},
    {VOICE_CTL_VOL_UP,          "DEV_ACTION", "1045734", NO_CMP_STR, {NO_CMP_STR, NO_CMP_STR}, 2, {{"user_action", "SET_VOLUME_UP"}, {"user_volume", "VOLUME"}}},
    {VOICE_CTL_VOL_TO,          "DEV_ACTION", "1045734", NO_CMP_STR, {NO_CMP_STR, NO_CMP_STR}, 3, {{"user_action","SET_TO"}, {"user_volume", "VOLUME"},{"user_attr_volume",NO_CMP_STR}}},
    {VOICE_CTL_VOL_DOWN,        "DEV_ACTION", "1045734", NO_CMP_STR, {NO_CMP_STR, NO_CMP_STR}, 2, {{"user_action", "SET_DOWN"}, {"user_volume", "VOLUME"}}},
    {VOICE_CTL_VOL_DOWN,        "DEV_ACTION", "1045734", NO_CMP_STR, {NO_CMP_STR, NO_CMP_STR}, 2, {{"user_action", "SET_VOLUME_DOWN"}, {"user_volume", "VOLUME"}}},
    {VOICE_CTL_VOL_ON,          "DEV_ACTION", "1045734", NO_CMP_STR, {NO_CMP_STR, NO_CMP_STR}, 2, {{"user_action", "ON"}, { "user_volume", "VOLUME"}}},
    {VOICE_CTL_VOL_OFF,         "DEV_ACTION", "1045734", NO_CMP_STR, {NO_CMP_STR, NO_CMP_STR}, 2, {{"user_action", "OFF"},{"user_volume", "VOLUME"}}},
    {VOICE_CTL_VOL_MUTE,        "DEV_ACTION", "1045734", NO_CMP_STR, {NO_CMP_STR, NO_CMP_STR}, 1, {{"user_func_mute", "MUTE"}}},
    {VOICE_CTL_MUSIC_ID_PALY,   NO_CMP_STR,   "1079888", NO_CMP_STR, {"music_id", NO_CMP_STR}, 0 /* Be careful, 0 means we dont care about it, not 0 slots!! */},
    {VOICE_CTL_URL_PALY,        NO_CMP_STR,   "1059717", NO_CMP_STR, {"url",      NO_CMP_STR}, 0},
    {VOICE_CTL_URL_PALY,        NO_CMP_STR,   "1030330", NO_CMP_STR, {"url",      NO_CMP_STR}, 0},
    {VOICE_CTL_URL_PALY,        NO_CMP_STR,   "1030970", NO_CMP_STR, {"url",      NO_CMP_STR}, 0},
};

static int unit_strcmp(const char *str1, const char *str2)
{
    if (!strcmp(str2, NO_CMP_STR)) {
        return 0;
    } else if(str1 != NULL) {
        return strcmp(str1, str2);
    } else {
        return -1;
    }
}

static void dump_unit_data(unit_data_t *p_unit_data)
{
    int i = 0;
    if (p_unit_data) {
        ESP_LOGI(TAG,"++++ dumping unit data: ");
        if (p_unit_data->intent) {
            ESP_LOGI(TAG,"intent: %s",p_unit_data->intent);
        }
        if (p_unit_data->origin){
            ESP_LOGI(TAG,"origin: %s",p_unit_data->origin);
        }
        if (p_unit_data->action_type){
            ESP_LOGI(TAG,"action_type: %s",p_unit_data->action_type);
        }
        for (i = 0; i < p_unit_data->slots_num; i++) {
            if (p_unit_data->slots_key_value[i].name){
                ESP_LOGI(TAG,"name %d: %s", i, p_unit_data->slots_key_value[i].name);
            }
            if (p_unit_data->slots_key_value[i].value) {
                ESP_LOGI(TAG,"value %d: %s",i, p_unit_data->slots_key_value[i].value);
            }
        }
        if (p_unit_data->custom_reply.type) {
            ESP_LOGI(TAG,"custom_reply_type: %s", p_unit_data->custom_reply.type);
        }
    }
}

static int unit_data_strcmp(const unit_data_t *data, const unit_data_t *user_data)
{
    int i = 0,  j = 0, found = 0;

    // intent
    if (unit_strcmp(data->intent, user_data->intent)) {
        ESP_LOGE(TAG, "break 1");
        return -1;
    }

    // origin
    if (unit_strcmp(data->origin, user_data->origin)) {
        ESP_LOGE(TAG, "break 2");
        return -1;
    }

    // custom reply: type
    if (unit_strcmp(data->custom_reply.type, user_data->custom_reply.type)) {
        ESP_LOGE(TAG, "break 3");
        return -1;
    }

    // custom reply: value
    if (unit_strcmp(data->custom_reply.value, user_data->custom_reply.value)) {
        ESP_LOGE(TAG, "break 4");
        return -1;
    }

    // slots number
    // 0 means we dont care about it, pass
    if (user_data->slots_num != 0) {
        if (data->slots_num != user_data->slots_num) {
            ESP_LOGE(TAG, "break 5");
            return -1;
        }

        for (i = 0; i < data->slots_num; i++) {
            slots_key_value_t tmp = data->slots_key_value[i];
            found = 0;
            for (j = 0; j < data->slots_num; j++) {
                if (!unit_strcmp(tmp.name, user_data->slots_key_value[j].name) &&
                    !unit_strcmp(tmp.value, user_data->slots_key_value[j].value)) {    
                    found = 1;
                    break;
                }
            }
            if (!found) {
                ESP_LOGE(TAG, "break 6");
                return -1;
            }
        }
    }
    
    // match pattern found
    return 0;
}

void app_voice_control_feed_data(BdsJson *data_json, void *private_data)
{
    int index = 0;
    unit_data_t *p_unit_data = audio_calloc(1, sizeof(unit_data_t) + sizeof(slots_key_value_t)*SLOTS_MAX_NUM);
    if (p_unit_data == NULL) {
        return;
    }
    p_unit_data->intent = BdsJsonObjectGetString(data_json, "intent");
    p_unit_data->origin = BdsJsonObjectGetString(data_json, "origin");
    p_unit_data->action_type = BdsJsonObjectGetString(data_json, "action_type");
    BdsJson* slots_Json = BdsJsonObjectGet(data_json,"slots");
    if (slots_Json) {
        BdsJsonArrayForeach(slots_Json, elem_j) {
            p_unit_data->slots_key_value[p_unit_data->slots_num].name = BdsJsonObjectGetString(elem_j, "name");
            p_unit_data->slots_key_value[p_unit_data->slots_num].value = BdsJsonObjectGetString(elem_j, "value");
            p_unit_data->slots_num++;
            if (p_unit_data->slots_num >= SLOTS_MAX_NUM) {
                break;
            }
        }
    }
    BdsJson* custom_reply_Json = BdsJsonObjectGet(data_json,"custom_reply");
    if (NULL != custom_reply_Json) {
        BdsJsonArrayForeach(custom_reply_Json, elem_j) {
            p_unit_data->custom_reply.type = BdsJsonObjectGetString(elem_j, "type");
            if ((!unit_strcmp(p_unit_data->custom_reply.type, "url")) || (!unit_strcmp(p_unit_data->custom_reply.type, "music_id"))) {
                p_unit_data->custom_reply.value = BdsJsonObjectGetString(elem_j, "value");
                break;
            }
        }
    }
    dump_unit_data(p_unit_data);
    for (index = 0; index < sizeof(g_unit_data) / sizeof(g_unit_data[0]); index++) {
        ESP_LOGI(TAG, "unit data matching %d ...", index);
        if (!unit_data_strcmp(p_unit_data, &g_unit_data[index])) {
            unit_cmd_handle(p_unit_data, g_unit_data[index].unit_code);
            break;
        }
    }

    if(index >= sizeof(g_unit_data) / sizeof(g_unit_data[0])) {    // matching fail on common unit data
        for (index = 0; index < g_user_unit_array_num; index++) {
            ESP_LOGI(TAG, "user unit data matching %d ...", index);
            if (!unit_data_strcmp(p_unit_data, &g_user_unit_data[index])) {
                user_unit_cmd_handle(p_unit_data, g_user_unit_data[index].unit_code);
                break;
            }
        }
    }
    audio_free(p_unit_data);
}

void send_music_queue(music_type_t type, unit_data_t *pdata)
{
    if(g_music_queue_handle != NULL) {
        music_queue_t music_data;

        memset(&music_data, 0, sizeof(music_queue_t));
        music_data.type = type;
        if (pdata != NULL) {
            // for URL_MUSIC & ID_MUSIC
            music_data.data = audio_strdup(pdata->custom_reply.value);
            if (!strcmp(pdata->intent, "CHANGE_TO_NEXT")) {
                music_data.action  = NEXT_MUSIC;
            } else if (!strcmp(pdata->intent, "CACHE_MUSIC")) {
                music_data.action  = CACHE_MUSIC;
            } else {
                music_data.action  = PLAY_MUSIC;
            }
            if (pdata->action_type) {
                music_data.action_type = audio_strdup(pdata->action_type);
            }
        } else {
            // for ALL_TYPE (auto change to next song)
            music_data.action  = NEXT_MUSIC;
            music_data.data    = NULL;
            music_data.action_type = NULL;
        }
        ESP_LOGI(TAG, "=====================> action: %d, type: %d", music_data.action, music_data.type);
        xQueueSend(g_music_queue_handle, (void*)&music_data, 0);
    }
}
void unit_cmd_handle(unit_data_t *pdata, uint32_t code)
{
    int player_volume = 0;
    static int pre_player_volume = 0;
    switch (code) {
    case VOICE_CTL_OPEN_BT:
        ESP_LOGW(TAG, "APP_VOICE_CTL_CMD_OPEN_BT");
        app_bt_start();
        break;
    case VOICE_CTL_CLOSE_BT:
        ESP_LOGW(TAG, "APP_VOICE_CTL_CMD_CLOSE_BT");
        bdsc_engine_close_bt();
        app_bt_stop(g_bd_addr);
        break;
    case VOICE_CTL_MUSIC_CONTINUE:
        set_music_player_state(RUNNING_STATE);
        g_pre_player_need_resume = true;
        ESP_LOGW(TAG, "VOICE_CTL_MUSIC_CONTINUE");
        break;
    case VOICE_CTL_MUSIC_PAUSE:
        set_music_player_state(PAUSE_STATE);
        g_pre_player_need_resume = false;
        ESP_LOGW(TAG, "VOICE_CTL_MUSIC_PAUSE");
        break;
    case VOICE_CTL_MUSIC_STOP:
        set_music_player_state(STOP_STATE);
        g_pre_player_need_resume = false;
        audio_player_clear_audio_info();
        ESP_LOGW(TAG, "VOICE_CTL_MUSIC_STOP");
        break;
    case VOICE_CTL_VOL_UP: {
        audio_player_vol_get(&player_volume);
        player_volume = (player_volume < 90)?player_volume +10:100;
        audio_player_vol_set(player_volume);
        ESP_LOGW(TAG, "VOICE_CTL_VOL_UP");
        break;
    }
    case VOICE_CTL_VOL_TO: {
        const char *user_att_value = pdata->slots_key_value[2].value;
        if(strrchr(user_att_value,'e')) {     //Scientific Notation value
            player_volume = atof(user_att_value) * 100;
        } else {
            player_volume = atoi(user_att_value);
        }
        if (player_volume >= 0 && player_volume <= 100) {
            audio_player_vol_set(player_volume);
        }
        ESP_LOGW(TAG, "VOICE_CTL_VOL_TO");
        break;
    }
    case VOICE_CTL_VOL_DOWN: {
        audio_player_vol_get(&player_volume);
        player_volume = (player_volume >10) ? player_volume -10 : 0;
        audio_player_vol_set(player_volume);
        ESP_LOGW(TAG, "VOICE_CTL_VOL_DOWN");
        break;
    }
    case VOICE_CTL_VOL_ON:
        if(!pre_player_volume)
            audio_player_vol_set(40);
        else
            audio_player_vol_set(pre_player_volume);
        ESP_LOGW(TAG, "VOICE_CTL_VOL_ON");
        break;
    case VOICE_CTL_VOL_OFF:
    case VOICE_CTL_VOL_MUTE:
        audio_player_vol_get(&pre_player_volume);
        audio_player_vol_set(0);
        ESP_LOGW(TAG, "VOICE_CTL_VOL_MUTE");
        break;
    case VOICE_CTL_URL_PALY:
        send_music_queue(URL_MUSIC, pdata);
        ESP_LOGW(TAG, "VOICE_CTL_URL_PALY");
        break;
    case VOICE_CTL_MUSIC_ID_PALY:
#ifdef CONFIG_MIGU_MUSIC
        send_music_queue(ID_MUSIC, pdata);
#endif
        ESP_LOGW(TAG, "VOICE_CTL_MUSIC_ID_PALY");
        break;
    default:
        ESP_LOGW(TAG, "UNKOWN CMD");
        break;
    }
}