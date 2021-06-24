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
#include "play_list.h"

#define TAG "APP_UNIT"

#define CMP_STR_LEN     1024                        //the buff length of comparing

extern esp_bd_addr_t g_bd_addr;
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
    {VOICE_CTL_NOT_FOUND,       NO_CMP_STR,   "-1",      NO_CMP_STR, {"tts",      "对不起没有您想听的内容"}, 0},
    {IAQ_QUERY,                 "BUILT_IAQ",  "92134",   NO_CMP_STR, {"url",      NO_CMP_STR}, 0},
    {UNKNOW_QUERY,              NO_CMP_STR,   NO_CMP_STR,NO_CMP_STR, {NO_CMP_STR, NO_CMP_STR}, 0},
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
        ESP_LOGI(TAG, "++++ dumping unit data: ");
        if (p_unit_data->intent) {
            ESP_LOGI(TAG,"intent: %s", p_unit_data->intent);
        }
        if (p_unit_data->origin){
            ESP_LOGI(TAG,"origin: %s", p_unit_data->origin);
        }
        ESP_LOGI(TAG,"action_type: %s", (p_unit_data->action_type ? p_unit_data->action_type : "NULL"));
        for (i = 0; i < p_unit_data->slots_num; i++) {
            if (p_unit_data->slots_key_value[i].name){
                ESP_LOGI(TAG,"name %d: %s", i, p_unit_data->slots_key_value[i].name);
            }
            if (p_unit_data->slots_key_value[i].value) {
                ESP_LOGI(TAG,"value %d: %s", i, p_unit_data->slots_key_value[i].value);
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
        ESP_LOGD(TAG, "break 1");
        return -1;
    }

    // origin
    if (unit_strcmp(data->origin, user_data->origin)) {
        ESP_LOGD(TAG, "break 2");
        return -1;
    }

    // custom reply: type
    if (unit_strcmp(data->custom_reply.type, user_data->custom_reply.type)) {
        ESP_LOGD(TAG, "break 3");
        return -1;
    }

    // custom reply: value
    if (unit_strcmp(data->custom_reply.value, user_data->custom_reply.value)) {
        ESP_LOGD(TAG, "break 4");
        return -1;
    }

    // slots number
    // 0 means we dont care about it, pass
    if (user_data->slots_num != 0) {
        if (data->slots_num != user_data->slots_num) {
            ESP_LOGD(TAG, "break 5");
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
                ESP_LOGD(TAG, "break 6");
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
    int error_code = 0;
    unit_data_t *p_unit_data = audio_calloc(1, sizeof(unit_data_t));
    if (p_unit_data == NULL) {
        return;
    }
    BdsJsonObjectGetInt(data_json, "error_code", &error_code);
    if(error_code) {
        ESP_LOGE(TAG, "receive error code %d ...", error_code);
        audio_free(p_unit_data);
        return;
    }

    memset(p_unit_data, 0, sizeof(unit_data_t));
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

    // match custom unit list fist
    for (index = 0; index < g_user_unit_array_num; index++) {
        ESP_LOGD(TAG, "user unit data matching %d ...", index);
        if (!unit_data_strcmp(p_unit_data, &g_user_unit_data[index])) {
            user_unit_cmd_handle(p_unit_data, g_user_unit_data[index].unit_code);
            audio_free(p_unit_data);
            return;
        }
    }

    // search common unit list if not find
    if(index >= sizeof(g_user_unit_data) / sizeof(g_user_unit_data[0])) {
        for (index = 0; index < sizeof(g_unit_data) / sizeof(g_unit_data[0]); index++) {
            ESP_LOGD(TAG, "unit data matching %d ...", index);
            if (!unit_data_strcmp(p_unit_data, &g_unit_data[index])) {
                unit_cmd_handle(p_unit_data, g_unit_data[index].unit_code);
                audio_free(p_unit_data);
                return;
            }
        }
    }

    if (index >= sizeof(g_unit_data) / sizeof(g_unit_data[0])) {
        ESP_LOGE(TAG, "not found matching unit skill!!!!");
    }

    audio_free(p_unit_data);
}

void unit_cmd_handle(unit_data_t *pdata, uint32_t code)
{
    int player_volume = 0;
    static int pre_player_volume = 0;

    switch (code) {
    case VOICE_CTL_OPEN_BT:
        ESP_LOGW(TAG, "APP_VOICE_CTL_CMD_OPEN_BT");
        app_bt_start();
        send_music_queue(SPEECH_MUSIC, pdata);
        break;
    case VOICE_CTL_CLOSE_BT:
        ESP_LOGW(TAG, "APP_VOICE_CTL_CMD_CLOSE_BT");
        bdsc_engine_close_bt();
        app_bt_stop(g_bd_addr);
        send_music_queue(SPEECH_MUSIC, pdata);
        break;
    case VOICE_CTL_MUSIC_CONTINUE:
        ESP_LOGW(TAG, "VOICE_CTL_MUSIC_CONTINUE");
        send_music_queue(MUSIC_CTL_CONTINUE, pdata);
        break;
    case VOICE_CTL_MUSIC_PAUSE:
        ESP_LOGW(TAG, "VOICE_CTL_MUSIC_PAUSE");
        send_music_queue(MUSIC_CTL_PAUSE, pdata);
        break;
    case VOICE_CTL_MUSIC_STOP:
        ESP_LOGW(TAG, "VOICE_CTL_MUSIC_STOP");
        send_music_queue(MUSIC_CTL_STOP, pdata);
        break;
    case VOICE_CTL_VOL_UP: {
        audio_player_vol_get(&player_volume);
        player_volume = (player_volume < 90)?player_volume +10:100;
        audio_player_vol_set(player_volume);
        ESP_LOGW(TAG, "VOICE_CTL_VOL_UP");
        send_music_queue(SPEECH_MUSIC, pdata);
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
        send_music_queue(SPEECH_MUSIC, pdata);
        break;
    }
    case VOICE_CTL_VOL_DOWN: {
        audio_player_vol_get(&player_volume);
        player_volume = (player_volume >10) ? player_volume -10 : 0;
        audio_player_vol_set(player_volume);
        ESP_LOGW(TAG, "VOICE_CTL_VOL_DOWN");
        send_music_queue(SPEECH_MUSIC, pdata);
        break;
    }
    case VOICE_CTL_VOL_ON:
        if(!pre_player_volume)
            audio_player_vol_set(40);
        else
            audio_player_vol_set(pre_player_volume);
        ESP_LOGW(TAG, "VOICE_CTL_VOL_ON");
        send_music_queue(SPEECH_MUSIC, pdata);
        break;
    case VOICE_CTL_VOL_OFF:
    case VOICE_CTL_VOL_MUTE:
        audio_player_vol_get(&pre_player_volume);
        audio_player_vol_set(0);
        ESP_LOGW(TAG, "VOICE_CTL_VOL_MUTE");
        send_music_queue(SPEECH_MUSIC, pdata);
        break;
    case VOICE_CTL_URL_PALY:
        g_bdsc_engine->need_skip_current_pending_http_part = true;
        send_music_queue(URL_MUSIC, pdata);
        ESP_LOGW(TAG, "VOICE_CTL_URL_PALY");
        break;
    case VOICE_CTL_MUSIC_ID_PALY:
        g_bdsc_engine->need_skip_current_pending_http_part = true;
        ESP_LOGW(TAG, "VOICE_CTL_MUSIC_ID_PALY");
#ifdef CONFIG_MIGU_MUSIC
        send_music_queue(ID_MUSIC, pdata);
#endif
        break;
    case VOICE_CTL_NOT_FOUND:
        ESP_LOGW(TAG, "VOICE_CTL_NOT_FOUND");
        // cache next faild, do nothing, only play the current music
        break;
    case UNKNOW_QUERY:
        ESP_LOGW(TAG, "UNKNOW_QUERY");
        send_music_queue(SPEECH_MUSIC, pdata);
        break;
    case IAQ_QUERY:
        ESP_LOGW(TAG, "IAQ_QUERY");
        send_music_queue(SPEECH_MUSIC, pdata);
        break;
    default:
        ESP_LOGW(TAG, "UNKOWN CMD");
        break;
    }
}
