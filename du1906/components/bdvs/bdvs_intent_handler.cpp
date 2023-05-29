/**
 * @file intent_handle.cpp
 * @author wangmeng (wangmeng43@baidu.com)
 * @brief 
 * @version 0.1
 * @date 2022-03-22
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "bdvs_intent_handler.h"

#include <string>

#include "cJSON.h"
#include "log.h"
#include "bdsc_event_dispatcher.h"
// #include "bt_init.h"
#include "audio_player.h"
#include "receive_data_filter.h"
#include "app_music.h"

#include "bdsc_engine.h"
#include "asr_event_send.h"
#include "bdvs_protocol_helper_c_wrapper.h"
#include "bdvs_protocol_helper.hpp"
#include "receive_data_filter.h"

#define TAG "INTENT_HDL"
// next media
void media_control_event_next(char *domain)
{
    std::string full_str = BdvsProtoHelper::bdvs_event_media_control_request_build("media.next");
    if(start_event_send_data(g_bdsc_engine->g_client_handle, (char *)full_str.c_str()) < 0) {
        bds_hh2_loge(TAG, "send media next failed");
    }
}

// pre meida
void media_control_event_pre(char *domain)
{
    std::string full_str = BdvsProtoHelper::bdvs_event_media_control_request_build("media.pre");
    if(start_event_send_data(g_bdsc_engine->g_client_handle, (char *)full_str.c_str()) < 0) {
        bds_hh2_loge(TAG, "send media next failed");
    }
}

static int media_handle_callback(cJSON *action)
{
    return BdvsProtoHelper::bdvs_action_media_play_parse(action);
}

static int media_pause_handle_callback(cJSON *action)
{
    ESP_LOGI("media_handle", "media pause handle callback enter ==>");
    bdsc_play_hint(BDSC_HINT_HAODE);
    bdvs_send_music_queue(BDVS_MUSIC_CTL_PAUSE, nullptr);
    return 0;
}

static int media_continue_handle_callback(cJSON *action)
{
    ESP_LOGI("media_handle","media continue handle callback enter ==>");
    bdsc_play_hint(BDSC_HINT_HAODE);
    bdvs_send_music_queue(BDVS_MUSIC_CTL_CONTINUE, nullptr);
    return 0;
}

static int player_pause_callback(std::string in_value)
{
    bds_hh2_loge(TAG, "common pause");
    // NOTE: bdvs dont have 'haode' tts, you should play on yourown
    bdsc_play_hint(BDSC_HINT_HAODE);
    bdvs_send_music_queue(BDVS_MUSIC_CTL_PAUSE, NULL);
    return 0;
}

static int player_continue_callback(std::string in_value)
{
    bds_hh2_loge(TAG, "common continue");
    bdsc_play_hint(BDSC_HINT_HAODE);
    bdvs_send_music_queue(BDVS_MUSIC_CTL_CONTINUE, NULL);
    return 0;
}

static int player_stop_callback(std::string in_value)
{
    bds_hh2_loge(TAG, "common stop");
    bdsc_play_hint(BDSC_HINT_HAODE);
    bdvs_send_music_queue(BDVS_MUSIC_CTL_STOP, NULL);
    return 0;
}

static int volume_down_handle_callback(std::string in_value)
{
    ESP_LOGI(TAG,"volume down handle callback enter ==>");
    bdsc_play_hint(BDSC_HINT_HAODE);
    int player_volume = 0;
    audio_player_vol_get(&player_volume);
    player_volume = VOLUME_VALID(player_volume - 10);
    audio_player_vol_set(player_volume);
    ESP_LOGW(TAG, "VOICE_CTL_VOL_DOWN");
    return 0;
}

static int volume_up_handle_callback(std::string in_value)
{
    ESP_LOGI(TAG,"volume up handle callback enter ==>");
    bdsc_play_hint(BDSC_HINT_HAODE);
    int player_volume = 0;
    audio_player_vol_get(&player_volume);
    player_volume = VOLUME_VALID(player_volume + 10);
    audio_player_vol_set(player_volume);
    ESP_LOGW(TAG, "VOICE_CTL_VOL_UP");
    return 0;
}

static int volume_to_handle_callback(std::string in_value)
{
    ESP_LOGI(TAG,"volume up handle callback enter ==>");
    bdsc_play_hint(BDSC_HINT_HAODE);

    cJSON* json = cJSON_Parse(in_value.c_str());
    cJSON* slots = cJSON_GetObjectItem(json, "slots");
    if (cJSON_IsObject(slots)) {
        cJSON* value = cJSON_GetObjectItem(slots, "value");
        if (!cJSON_IsArray(value)) {
            ESP_LOGI(TAG,"value is null, try percent_value");
            value = cJSON_GetObjectItem(slots, "percent_value");
        }
        if (cJSON_IsArray(value)) {
            cJSON* item = cJSON_GetArrayItem(value, 0);
            cJSON* value_num = cJSON_GetObjectItem(item, "value");
            if (cJSON_IsNumber(value_num)) {
                // vol等于0时为静音, 其他音量范围为: 5 ~ 100
                int volume = (value_num->valueint == 0) ? 0 : VOLUME_VALID(value_num->valueint);
                audio_player_vol_set(volume);
                ESP_LOGW(TAG, "set volume: %d\n", volume);
            }
        }
    }
    cJSON_Delete(json);
    ESP_LOGW(TAG, "VOICE_CTL_VOL_TO:%s", in_value.c_str());
    return 0;
}

static int volume_max_handle_callback(std::string in_value)
{
    ESP_LOGI(TAG,"volume max handle callback enter ==>");
    bdsc_play_hint(BDSC_HINT_HAODE);

    audio_player_vol_set(VOLUME_MAX);
    ESP_LOGW(TAG, "set volume: %d\n", VOLUME_MAX);

    return 0;
}

static int volume_min_handle_callback(std::string in_value)
{
    ESP_LOGI(TAG,"volume min handle callback enter ==>");
    bdsc_play_hint(BDSC_HINT_HAODE);

    audio_player_vol_set(VOLUME_MIN);
    ESP_LOGW(TAG, "set volume: %d\n", VOLUME_MIN);

    return 0;
}

static int volume_mid_handle_callback(std::string in_value)
{
    ESP_LOGI(TAG,"volume mid handle callback enter ==>");
    bdsc_play_hint(BDSC_HINT_HAODE);

    audio_player_vol_set(VOLUME_MID);
    ESP_LOGW(TAG, "set volume: %d\n", VOLUME_MID);

    return 0;
}

static int volume_mute_handle_callback(std::string in_value)
{
    ESP_LOGI(TAG,"volume mid handle callback enter ==>");
    bdsc_play_hint(BDSC_HINT_HAODE);

    // 静音时音量为0 下次唤醒时音量会恢复默认值
    audio_player_vol_set(VOLUME_MUTE);
    ESP_LOGW(TAG, "set volume: %d\n", VOLUME_MUTE);

    return 0;
}

void intent_handle_init()
{
    bds_hh2_logi(TAG, "intent handle init ==>");
    //播放音乐
    add_new_action_handle("media.play", media_handle_callback);
    add_new_action_handle("media.pause", media_pause_handle_callback);
    add_new_action_handle("media.continue", media_continue_handle_callback);

    // “暂停” “继续” “停止”
    add_new_intent_handle("sys_command", "pause", player_pause_callback);
    add_new_intent_handle("sys_command", "continue", player_continue_callback);
    add_new_intent_handle("sys_command", "stop", player_stop_callback);

    // “暂停播放” “继续播放” “停止播放”
    add_new_intent_handle("media_command", "player.pause", player_pause_callback);
    add_new_intent_handle("media_command", "player.continue", player_continue_callback);
    add_new_intent_handle("media_command", "stop", player_stop_callback);

    // “音乐暂停” “继续播放音乐” “音乐停止/退出播放”
    add_new_intent_handle("audio.music", "audio.music.pause", player_pause_callback);
    add_new_intent_handle("audio.music", "audio.music.continue", player_continue_callback);
    add_new_intent_handle("audio.music", "audio.music.stop", player_stop_callback);

    // “音量调小” “音量减小”
    add_new_intent_handle("sys_command", "down_volume", volume_down_handle_callback);
    // “音量调大” “音量减大”
    add_new_intent_handle("sys_command", "up_volume", volume_up_handle_callback);
    // “音量调到xxx”
    add_new_intent_handle("sys_command", "to_volume", volume_to_handle_callback);
    // “音量调到最大”
    add_new_intent_handle("sys_command", "max_volume", volume_max_handle_callback);
    // “音量调到最小”
    add_new_intent_handle("sys_command", "min_volume", volume_min_handle_callback);
    // “音量调到中等”
    add_new_intent_handle("sys_command", "mid_volume", volume_mid_handle_callback);
    // “静音”
    add_new_intent_handle("sys_command", "control.hardware.volume.mute", volume_mute_handle_callback);

}
