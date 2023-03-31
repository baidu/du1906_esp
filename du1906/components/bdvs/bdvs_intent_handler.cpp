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

#define TAG "INTENT_HDL"

static int player_pause_callback(std::string in_value)
{
    bds_hh2_loge(TAG, "common pause");
    // NOTE: bdvs dont have 'haode' tts, you should play on yourown
    // bdsc_play_hint(BDSC_HINT_HAODE);
    bdvs_send_music_queue(BDVS_MUSIC_CTL_PAUSE, NULL);
    return 0;
}

static int player_continue_callback(std::string in_value)
{
    bds_hh2_loge(TAG, "common continue");
    // bdsc_play_hint(BDSC_HINT_HAODE);
    bdvs_send_music_queue(BDVS_MUSIC_CTL_CONTINUE, NULL);
    return 0;
}

static int player_stop_callback(std::string in_value)
{
    bds_hh2_loge(TAG, "common stop");
    // bdsc_play_hint(BDSC_HINT_HAODE);
    bdvs_send_music_queue(BDVS_MUSIC_CTL_STOP, NULL);
    return 0;
}

void intent_handle_init()
{
    bds_hh2_logi(TAG, "intent handle init ==>");

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
}
