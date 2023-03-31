/**
 * @file active_tts.cpp
 * @author wangmeng (wangmeng43@baidu.com)
 * @brief 
 * @version 0.1
 * @date 2022-03-22
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "bdvs_active_tts_handler.h"

#include <string>

#include "cJSON.h"
#include "bdsc_tools.h"
#include "log.h"
#include "asr_event_send.h"
#include "bdsc_engine.h"

#include "generate_pam.h"
#include "receive_data_filter.h"
#include "bdvs_protocol_helper_c_wrapper.h"

#define TAG "ACTIVE_TTS"

void active_tts_send_data(char *in_text)
{
    if (in_text == NULL) {
        bds_hh2_loge(TAG, "please input the text");
        return;
    }

    char* full_str = bdvs_active_tts_request_build_c_wrapper(in_text);
    bds_hh2_loge(TAG, "the active tts request is %s", full_str);
    if(start_event_send_data(g_bdsc_engine->g_client_handle, full_str) < 0) {
        bds_hh2_loge(TAG, "send tts data failed");
    }
    free(full_str);
}

static int tts_stream_receive_handle(cJSON *dir_payload)
{
    if (!dir_payload) {
        return -1;
    }

    cJSON *pay_con = cJSON_GetObjectItem(dir_payload, "content");
    if (pay_con) {
        bds_hh2_loge(TAG, "tts header info is %s", pay_con->valuestring);
    }
    SET_CMD_BITS(g_bdvs_cmd_bit_set, BDVS_CMD_BIT_VOICE_OUTPUT_SPEAK);

    return 0;
}

void active_tts_handle_init()
{
    // add_new_handle_to_map((char *)"bdvs.capability.voice_output", (char *)"Speak", active_tts_receive_handle);
    bds_hh2_logi(TAG, "active tts init ===>");
    add_new_action_handle("tts", tts_stream_receive_handle);
}
