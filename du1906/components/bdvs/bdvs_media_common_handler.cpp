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

void media_handle_init()
{
    add_new_action_handle("media.play", media_handle_callback);
}
