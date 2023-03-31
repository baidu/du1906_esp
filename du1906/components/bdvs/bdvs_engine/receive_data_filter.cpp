/***
 * filename: receive_data_parse
 * function: parse the received message
 */

#include <iostream>
#include <map> // use c++ map
#include <string>
#include <cstring>

#include "log.h"
#include "bdsc_engine.h"
#include "bdvs_protocol_helper_c_wrapper.h"
#include "bdvs_protocol_helper.hpp"
#include "receive_data_filter.h"
#include "app_music.h"

#define TAG "DATA_PARSE"

using namespace std;
int g_bdvs_cmd_bit_set = BDVS_CMD_BIT_RESET;
char *g_bdvs_cmd_url;
char *g_bdvs_cmd_id;

std::map<std::string, CLASS_HANDLE_FUNC> class_handle; // origin, action, custom
std::map<std::string, ACTION_HANDLE_FUNC> action_handle; // action handle
std::map<std::string, INTENT_HANDLE_FUNC> intent_handle; // intent handle

static int action_class_handle(cJSON *in_value)
{
    bds_hh2_logi(TAG, "enter %s", __func__);
    if (in_value == nullptr) {
        return -1;
    }

    cJSON *act_name = cJSON_GetObjectItem(in_value, "name");
    if (act_name) {
        // check if handle map exists
        std::string actname = act_name->valuestring;
        if (action_handle.count(actname) == 0) { // have handle fucntion
            bds_hh2_loge(TAG, "no %s handle action function", act_name->valuestring);
            return -1;
        }
bds_hh2_logi(TAG, "enter %s", __func__);
        int ret = action_handle[actname](in_value);
        if (ret < 0) {
            bds_hh2_loge(TAG, "handle action failed");
            return -1;
        }
    }

    return 0;
}

// origin handle
static int origin_class_handle(cJSON *in_value)
{
    if (in_value == nullptr) {
        return -1;
    }

    cJSON *query = cJSON_GetObjectItem(in_value, "query");
    if (query) {
        bds_hh2_loge(TAG, "the query string is %s", query->valuestring);
    }

    cJSON *result = cJSON_GetObjectItem(in_value, "results");
    if (!result) {
        return -1;
    }

    int result_no = cJSON_GetArraySize(result);
    if (result_no <= 0) {
        bds_hh2_loge(TAG, "result number is invalid");
        return -1;        
    }

    // bds_hh2_logi(TAG, "get the result %d", result_no);
    for (int k = 0; k < result_no; k++) {
        cJSON *result_item = cJSON_GetArrayItem(result, k);
        if (!result_item) {
            continue;
        }

        cJSON *result_domain = cJSON_GetObjectItem(result_item, "domain");
        if (!result_domain) {
            continue;
        }

        cJSON *result_intent = cJSON_GetObjectItem(result_item, "intent");
        if (!result_intent) {
            continue;
        }

        // cJSON *result_funcset = cJSON_GetObjectItem(result_item, "funcset");
        // if (!result_funcset) {
        //     continue;
        // }

        char *out = cJSON_PrintUnformatted(result_item);
        std::string item_value = out;
        free(out);

        std::string intent_value = result_domain->valuestring; // domain + intent as key
        intent_value += result_intent->valuestring;

        // check if have such handle function
        if (intent_handle.count(intent_value) == 0) {
            bds_hh2_loge(TAG, "no %s  handle function", intent_value.c_str());
            continue;
        }

        // handle the intent
        int ret = intent_handle[intent_value](item_value);
        if (ret < 0) {
            bds_hh2_logi(TAG, "handle the %s failed", intent_value.c_str());
        }
    }

    return 0;
}

static int custom_class_handle(cJSON *in_value)
{
    if (in_value == nullptr) {
        return -1;
    }

    return 0;
}

static int add_class_handle()
{
    class_handle.insert(std::pair<std::string, CLASS_HANDLE_FUNC>("action", action_class_handle));
    class_handle.insert(std::pair<std::string, CLASS_HANDLE_FUNC>("origin", origin_class_handle));
    class_handle.insert(std::pair<std::string, CLASS_HANDLE_FUNC>("custom", custom_class_handle));

    return 0;
}

int receive_data_handle_init()
{
    bds_hh2_logi(TAG, "receive data handle init ==>");
    add_class_handle();
    return 0;
}

int add_new_action_handle(std::string type_name, ACTION_HANDLE_FUNC in_func)
{
    action_handle.insert(std::pair<std::string, ACTION_HANDLE_FUNC>(type_name, in_func));
    return 0;
}

int add_new_intent_handle(std::string domain, std::string intent, INTENT_HANDLE_FUNC in_func)
{
    std::string key = domain + intent; // domain + intent as key value
    intent_handle.insert(std::pair<std::string, INTENT_HANDLE_FUNC>(key, in_func));
    return 0;
}

int receive_nlp_data_handle(char *in_str)
{
    bds_hh2_logi(TAG, "enter %s", __func__);
    g_bdvs_cmd_bit_set = BDVS_CMD_BIT_RESET;
    g_bdvs_cmd_url = NULL;

    int ret = BdvsProtoHelper::bdvs_nlp_data_parse(in_str);
    if (ret < 0) {
        bds_hh2_loge(TAG, "bdvs_nlp_data_parse fail");
        return -1;
    }
    if (CHECK_CMD_BITS(g_bdvs_cmd_bit_set, BDVS_CMD_BIT_VOICE_OUTPUT_SPEAK)) {
        g_bdsc_engine->need_skip_current_pending_http_part = true;
        g_music_ctl_sm = MUSIC_CTL_SM_ST_PLAYING;
        if (CHECK_CMD_BITS(g_bdvs_cmd_bit_set, BDVS_CMD_BIT_AUDIO_PLAYER_URL_PLAY)) { // 酷我 我想听 下一首 都是同一个逻辑
            // tts + url
            bds_hh2_loge(TAG, "tts+url case, url: %s", g_bdvs_cmd_url);
            bdvs_send_music_queue(BDVS_URL_MUSIC, g_bdvs_cmd_url);
        } else if (CHECK_CMD_BITS(g_bdvs_cmd_bit_set, BDVS_CMD_BIT_AUDIO_PLAYER_ID_PLAY)) {
            // tts + id
            bds_hh2_loge(TAG, "tts+id case, id: %s", g_bdvs_cmd_id);
            bdvs_send_music_queue(BDVS_ID_MUSIC, g_bdvs_cmd_id);
        } else {
            // only tts
            bds_hh2_loge(TAG, "raw tts case");
            bdvs_send_music_queue(BDVS_SPEECH_MUSIC, NULL);
        }
    } else {
        // if (CHECK_CMD_BITS(g_bdvs_cmd_bit_set, BDVS_CMD_BIT_AUDIO_PLAYER_ID_PLAY)) {
        //     // cache music?
        //     if (g_music_ctl_sm == MUSIC_CTL_SM_ST_CACHEING_NEXT) {
        //         bds_hh2_loge(TAG, "cache music case, id: %s", g_bdvs_cmd_id);
        //     } else {
        //         g_bdsc_engine->need_skip_current_pending_http_part = true;
        //         // we have no way to catch "下一首" case
        //         g_music_ctl_sm = MUSIC_CTL_SM_ST_REQESTING_NEXT;
        //         bds_hh2_loge(TAG, "g_music_ctl_sm => MUSIC_CTL_SM_ST_REQESTING_NEXT");
        //         bds_hh2_loge(TAG, "request for next music case, id: %s", g_bdvs_cmd_id);
        //     }
        //     bdvs_send_music_queue(BDVS_ID_MUSIC, g_bdvs_cmd_id);
        // } else 
        if (CHECK_CMD_BITS(g_bdvs_cmd_bit_set, BDVS_CMD_BIT_AUDIO_PLAYER_URL_PLAY)) { // 酷我 自动缓存 “下一首”
                bds_hh2_loge(TAG, "cache music case, url: %s", g_bdvs_cmd_url);
                // g_music_ctl_sm = MUSIC_CTL_SM_ST_REQESTING_NEXT;
                // bds_hh2_loge(TAG, "g_music_ctl_sm => MUSIC_CTL_SM_ST_REQESTING_NEXT");
                // bds_hh2_loge(TAG, "request for next music case, url: %s", g_bdvs_cmd_url);
                if (g_music_ctl_sm == MUSIC_CTL_SM_ST_CACHEING_NEXT) {
                    bdvs_send_music_queue(BDVS_URL_MUSIC, g_bdvs_cmd_url);
                } else {
                    bds_hh2_loge(TAG, "must be a bug!!!");
                }
             }
        // todo...
    }

    return 0;
}
