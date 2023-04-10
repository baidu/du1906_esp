/**
 * @file media_common.cpp
 * @author wangmeng (wangmeng43@baidu.com)
 * @brief 
 * @version 0.1
 * @date 2022-03-22
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "bdvs_media_common_handler.h"

#include <string>

#include "migu.h"
#include "cJSON.h"
#include "log.h"
#include "bdsc_tools.h"
#include "asr_event_send.h"
#include "generate_pam.h"
#include "bdsc_engine.h"
#include "app_voice_control.h"
#include "app_music.h"
#include "dev_status.h"
#include "bdvs_protocol_helper_c_wrapper.h"
#include "bdvs_protocol_helper.hpp"
#include "receive_data_filter.h"

#include "audio_player.h"

#define TAG "MEDIA_COMMON"

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
    vTaskDelay(500);
    bdvs_send_music_queue(BDVS_MUSIC_CTL_PAUSE, nullptr);
    return 0;
}

static int media_stop_handle_callback(std::string in_value)
{
    ESP_LOGI("media_handle","media stop handle callback enter ==>");
    bdsc_play_hint(BDSC_HINT_HAODE);
    vTaskDelay(500);
    bdvs_send_music_queue(BDVS_MUSIC_CTL_STOP, nullptr);
    return 0;
}

static int media_continue_handle_callback(cJSON *action)
{
    ESP_LOGI("media_handle","media continue handle callback enter ==>");
    bdsc_play_hint(BDSC_HINT_HAODE);
    vTaskDelay(500);
    bdvs_send_music_queue(BDVS_MUSIC_CTL_CONTINUE, nullptr);
    return 0;
}

static int volume_down_handle_callback(std::string in_value)
{
    ESP_LOGI("media_handle","volume down handle callback enter ==>");
    bdsc_play_hint(BDSC_HINT_HAODE);
    int player_volume = 0;
    audio_player_vol_get(&player_volume);
    player_volume = (player_volume >10) ? player_volume -10 : 0;
    audio_player_vol_set(player_volume);
    ESP_LOGW("media_handle", "VOICE_CTL_VOL_DOWN");
    return 0;
}

static int volume_up_handle_callback(std::string in_value)
{
    ESP_LOGI("media_handle","volume up handle callback enter ==>");
    bdsc_play_hint(BDSC_HINT_HAODE);
    int player_volume = 0;
    audio_player_vol_get(&player_volume);
    player_volume = (player_volume < 90)?player_volume +10:100;
    audio_player_vol_set(player_volume);
    ESP_LOGW("media_handle", "VOICE_CTL_VOL_UP");
    return 0;
}

void media_handle_init()
{
    add_new_action_handle("media.play", media_handle_callback);
    add_new_action_handle("media.pause", media_pause_handle_callback);
    add_new_action_handle("media.continue", media_continue_handle_callback);
    add_new_intent_handle("sys_command", "stop", media_stop_handle_callback);
    add_new_intent_handle("sys_command", "down_volume", volume_down_handle_callback);
    add_new_intent_handle("sys_command", "up_volume", volume_up_handle_callback);
}
