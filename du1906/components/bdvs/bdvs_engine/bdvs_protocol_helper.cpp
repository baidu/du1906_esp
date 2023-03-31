#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "bdsc_profile.h"
#include "log.h"
#include "bdsc_http.h"
#include "receive_data_filter.h"
#include "bdsc_engine.h"
#include "bdvs_utils.h"
#include "dev_status.h"
#include <map>
#include "bdvs_protocol_helper.hpp"

#define TAG "BDVS_PROT"

//////////////////////   Active Device Message Constructor    //////////////////
static cJSON* device_active_create_auth()
{
    bds_hh2_logi(TAG, "enter %s", __func__);
    vendor_info_t *g_vendor_info = g_bdsc_engine->g_vendor_info;
    if (!g_vendor_info) {
        return NULL;
    }
    int64_t ts_ms = 0;
    int ts = 0;
    if ((ts_ms = get_current_time_ms()) < 0) {
        ts_ms = 0; // or return?
    }
    ts = ts_ms / 60000; // minute
    std::stringstream stream;
    stream << ts;
    std::string time_str = stream.str();
    //std::string time_str = std::to_string(ts);
    std::string sig = get_signature(ts, g_vendor_info->ak, g_vendor_info->sk);

    cJSON *user = cJSON_CreateObject();

    cJSON_AddStringToObject(user, "ak", g_vendor_info->ak);
    cJSON_AddStringToObject(user, "pk", g_vendor_info->pk);
    cJSON_AddStringToObject(user, "fc", g_vendor_info->fc);
    cJSON_AddStringToObject(user, "bdvsid", "ee05ba01a9f211eafe05e4b1930ffa46");
    cJSON_AddStringToObject(user, "signature", sig.c_str());
    cJSON_AddStringToObject(user, "time_stamp", time_str.c_str());

    cJSON *method_list = cJSON_CreateArray();
    cJSON_AddItemToObject(user, "methods", method_list);

    cJSON_AddItemToArray(method_list, cJSON_CreateString("ASR"));
    cJSON_AddItemToArray(method_list, cJSON_CreateString("DIALOG"));
    cJSON_AddItemToArray(method_list, cJSON_CreateString("TTS"));

    return user;
}

static cJSON* device_active_create_event()
{
    bds_hh2_logi(TAG, "enter %s", __func__);
    std::stringstream stream;
    stream << get_current_time_ms() << create_rand_number(9, 0);
    std::string message_id = stream.str();
    //std::string message_id = std::to_string(get_current_time_ms()) + std::to_string(create_rand_number(9, 0));
    cJSON *event = cJSON_CreateObject();
    cJSON *header_value = cJSON_CreateObject();

    cJSON_AddStringToObject(header_value, "messageId", message_id.c_str());
    cJSON_AddStringToObject(header_value, "name", "SetResource");
    cJSON_AddStringToObject(header_value, "namespace", "bdvs.capability.extensions");

    // std:string tmp_str = "{" + payloadstring + "}";

    cJSON_AddNullToObject(event, "payload");
    cJSON_AddItemToObject(event, "header", header_value);

    return event;
}

static cJSON *get_tts_param()
{
    bds_hh2_logi(TAG, "enter %s", __func__);
    cJSON *pay_tts = cJSON_CreateObject();
    if (!strcmp(g_bdsc_engine->g_tts_param.pronounce, "0")) {
        return pay_tts;
    }
    cJSON_AddNumberToObject(pay_tts, "spd", g_bdsc_engine->g_tts_param.speed);
    cJSON_AddNumberToObject(pay_tts, "pit", g_bdsc_engine->g_tts_param.pit);
    cJSON_AddNumberToObject(pay_tts, "vol", g_bdsc_engine->g_tts_param.volume);
    cJSON_AddStringToObject(pay_tts, "pronounce", g_bdsc_engine->g_tts_param.pronounce);
    cJSON_AddNumberToObject(pay_tts, "rate", g_bdsc_engine->g_tts_param.rate);
    cJSON_AddNumberToObject(pay_tts, "reverb", g_bdsc_engine->g_tts_param.reverb);
    return pay_tts;
}

static cJSON *active_dev_status()
{
    uint64_t ts = get_current_time_ms();
    cJSON *con_header = cJSON_CreateObject();
    cJSON_AddStringToObject(con_header, "namespace", "bdvs.capability.extensions");
    cJSON_AddStringToObject(con_header, "name", "RequestParam");

    // // player status
    cJSON *player_status = cJSON_CreateObject();
    cJSON_AddStringToObject(player_status, "state", g_dev_handle.play_state);
    cJSON_AddStringToObject(player_status, "domain", g_dev_handle.domain);


    cJSON_AddNumberToObject(player_status, "timestamp", ts);

    cJSON_AddNumberToObject(player_status, "PlayProgress", g_dev_handle.progress);
    cJSON_AddNumberToObject(player_status, "TotalLength", g_dev_handle.total_length);

    // map status
    cJSON *pay_map = cJSON_CreateObject();
    cJSON_AddStringToObject(pay_map, "state", "Yes");

    // tts param: how to get these infomation???
    cJSON *pay_tts = get_tts_param();

    cJSON *con_exten = cJSON_CreateObject();
    cJSON_AddItemToObject(con_exten, "playerStatus", player_status);
    cJSON_AddItemToObject(con_exten, "mapStatus", pay_map);
    cJSON_AddItemToObject(con_exten, "ttsParam", pay_tts);

    char *in_extern = cJSON_PrintUnformatted(con_exten);
    std::string exten_pam = in_extern;
    free(in_extern);
    cJSON_Delete(con_exten);

    cJSON *con_payload = cJSON_CreateObject();
    if (!exten_pam.empty()) {
        cJSON_AddStringToObject(con_payload, "extension", exten_pam.c_str());
    }

    cJSON *con_user = cJSON_CreateObject();
    cJSON_AddItemToObject(con_user, "header", con_header);
    cJSON_AddItemToObject(con_user, "payload", con_payload);

    return con_user;
}

std::string BdvsProtoHelper::bdvs_device_active_request_build()
{
    std::string result;
    int64_t ts_ms = get_current_time_ms();
    if (ts_ms == -1) {
        return result;
    }
    int ts_min = ts_ms / 60000; // minutes
    cJSON *user = cJSON_CreateObject(); // create root data object

    cJSON_AddItemToObject(user, "authorization", device_active_create_auth());

    cJSON_AddStringToObject(user, "bdvs-device-id", g_bdsc_engine->cuid);
    cJSON_AddNumberToObject(user, "timestamp", ts_min);
    cJSON_AddStringToObject(user, "bdvs-version", "2.0.0");

    cJSON_AddItemToObject(user, "event", device_active_create_event());

    cJSON *con_arry = cJSON_CreateArray(); // array is null
    cJSON_AddItemToArray(con_arry, active_dev_status()); // dev status

    cJSON_AddItemToObject(user, "contexts", con_arry);

    char *out = cJSON_Print(user);
    result = out;
    free(out);
    cJSON_Delete(user);

    return result;
}

//////////////////////   Active TTS Message Constructor    //////////////////
static cJSON *online_create_auth()
{
    long ts = get_current_time_ms() / 60000; // minutes
    std::stringstream stream;
    stream << ts;
    std::string time_str = stream.str();
    // std::string time_str = std::to_string(ts);
    std::string sig = get_signature(ts, g_bdsc_engine->g_vendor_info->ak, g_bdsc_engine->g_vendor_info->sk);

    cJSON *user = cJSON_CreateObject();

    cJSON_AddStringToObject(user, "ak", g_bdsc_engine->g_vendor_info->ak);
    cJSON_AddStringToObject(user, "pk", g_bdsc_engine->g_vendor_info->pk);
    cJSON_AddStringToObject(user, "fc", g_bdsc_engine->g_vendor_info->fc);
    cJSON_AddStringToObject(user, "bdvsid", g_bdsc_engine->g_vendor_info->bdvsid);
    // cJSON_AddStringToObject(user, "bdvsid", "f9873861dffd578129db9dccd1f77851");
    cJSON_AddStringToObject(user, "signature", sig.c_str());
    cJSON_AddStringToObject(user, "time_stamp", time_str.c_str());
    cJSON_AddStringToObject(user, "token_alpha", g_bdsc_engine->g_vendor_info->bdvs_token_alpha);

    cJSON *method_list = cJSON_CreateArray();
    cJSON_AddItemToObject(user, "methods", method_list);

    cJSON_AddItemToArray(method_list, cJSON_CreateString("ASR"));
    cJSON_AddItemToArray(method_list, cJSON_CreateString("DIALOG"));
    cJSON_AddItemToArray(method_list, cJSON_CreateString("TTS"));

    return user;
}

static cJSON *online_create_event(std::string name_value, std::string argvalue)
{
    std::stringstream stream;
    stream << get_current_time_ms() << create_rand_number(9, 0);
    std::string message_id = stream.str();
    //std::string message_id = std::to_string(get_current_time_ms()) + std::to_string(create_rand_number(9, 0));
    cJSON *event = cJSON_CreateObject();
    cJSON *header_value = cJSON_CreateObject();

    cJSON_AddStringToObject(header_value, "messageId", message_id.c_str());
    cJSON_AddStringToObject(header_value, "name", "SetResource");
    cJSON_AddStringToObject(header_value, "namespace", "bdvs.capability.extensions");

    // std:string tmp_str = "{" + payloadstring + "}";

    cJSON *exten = cJSON_CreateObject();
    cJSON *action = cJSON_CreateObject();
    cJSON *act_val = cJSON_CreateObject();

    cJSON_AddStringToObject(act_val, "name", name_value.c_str());
    if (argvalue.empty()) {
        cJSON_AddNullToObject(act_val, "arg"); // add null
    } else {
        cJSON *arg = cJSON_Parse(argvalue.c_str());
        if (arg) {
            cJSON_AddItemToObject(act_val, "arg", arg);
        } else {
            cJSON_AddNullToObject(act_val, "arg"); // add null
        }
    }

    cJSON_AddItemToObject(action, "action", act_val);
    char *out = cJSON_PrintUnformatted(action);
    if (out) {
        cJSON_AddStringToObject(exten, "extension", out);
        free(out);
    }
    cJSON_Delete(action);

    cJSON_AddItemToObject(event, "header", header_value);
    cJSON_AddItemToObject(event, "payload", exten);
    // cJSON_AddStringToObject(event, "payload", payloadstring.c_str());

    return event;
}

static cJSON *online_event_create_context()
{
    uint64_t ts = get_current_time_ms();
    cJSON *con_header = cJSON_CreateObject();
    cJSON_AddStringToObject(con_header, "namespace", "bdvs.capability.extensions");
    cJSON_AddStringToObject(con_header, "name", "RequestParam");

    // // player status
    cJSON *player_status = cJSON_CreateObject();
    cJSON_AddStringToObject(player_status, "state", g_dev_handle.play_state);
    cJSON_AddStringToObject(player_status, "domain", g_dev_handle.domain);

    cJSON_AddNumberToObject(player_status, "timestamp", ts);

    cJSON_AddNumberToObject(player_status, "PlayProgress", g_dev_handle.progress);
    cJSON_AddNumberToObject(player_status, "TotalLength", g_dev_handle.total_length);

    // map status
    cJSON *pay_map = cJSON_CreateObject();
    cJSON_AddStringToObject(pay_map, "state", "Yes");

    // tts param
    cJSON *pay_tts = get_tts_param();

    cJSON *con_exten = cJSON_CreateObject();
    cJSON_AddItemToObject(con_exten, "playerStatus", player_status);
    cJSON_AddItemToObject(con_exten, "mapStatus", pay_map);
    cJSON_AddItemToObject(con_exten, "ttsParam", pay_tts);

    char *in_extern = cJSON_PrintUnformatted(con_exten);
    std::string exten_pam = in_extern;
    free(in_extern);
    cJSON_Delete(con_exten);

    cJSON *con_payload = cJSON_CreateObject();
    if (!exten_pam.empty()) {
        cJSON_AddStringToObject(con_payload, "extension", exten_pam.c_str());
    }

    cJSON *con_user = cJSON_CreateObject();
    cJSON_AddItemToObject(con_user, "header", con_header);
    cJSON_AddItemToObject(con_user, "payload", con_payload);

    return con_user;    
}

static std::string online_event_send_string(std::string name_value, std::string argvalue)
{
    std::string result;
    int64_t ts_ms = get_current_time_ms();
    if (ts_ms == -1) {
        return result;
    }
    int ts_min = ts_ms / 60000; // minutes

    cJSON *user = cJSON_CreateObject(); // create root data object

    cJSON_AddItemToObject(user, "authorization", online_create_auth());

    cJSON_AddStringToObject(user, "bdvs-device-id", g_bdsc_engine->cuid);
    cJSON_AddNumberToObject(user, "timestamp", ts_min);
    cJSON_AddStringToObject(user, "bdvs-version", "2.0.0");

    cJSON_AddItemToObject(user, "event", online_create_event(name_value, argvalue));

    cJSON *con_arry = cJSON_CreateArray(); // array is null
    cJSON_AddItemToArray(con_arry, online_event_create_context());
    cJSON_AddItemToObject(user, "contexts", con_arry);

    char *out = cJSON_PrintUnformatted(user);
    result = out;
    if (out) {
        free(out);
    }
    cJSON_Delete(user);

    return result;
}

std::string BdvsProtoHelper::bdvs_active_tts_request_build(std::string in_text)
{
    std::string name_value = "tts";

    cJSON *event_payload = cJSON_CreateObject();

    cJSON_AddStringToObject(event_payload, "encoding", "utf8");
    cJSON_AddStringToObject(event_payload, "content", in_text.c_str());

    char *out = cJSON_PrintUnformatted(event_payload);
    std::string tts_payload = out;
    if (out) {
        free(out);
    }

    cJSON_Delete(event_payload);

    bds_hh2_logi(TAG, "the input tts_payload is %s", tts_payload.c_str());

    return online_event_send_string(name_value, tts_payload);
}

//////////////////////   Online pre/next CMD Constructor    //////////////////
std::string BdvsProtoHelper::bdvs_event_media_control_request_build(std::string cmd)
{
    return online_event_send_string(cmd, "");
}

//////////////////////   ASR IOT Parameter Constructor    //////////////////

static cJSON *event_create_auth(std::string ak, std::string sk, std::string pk, std::string fc, int pid)
{
    long ts = get_current_time_ms() / 60000;
    std::stringstream stream;
    stream << ts;
    std::string time_str = stream.str();
    //std::string time_str = std::to_string(ts);
    std::string sig = get_signature(ts, ak, sk);

    cJSON *user = cJSON_CreateObject();

    cJSON_AddStringToObject(user, "ak", ak.c_str());
    cJSON_AddStringToObject(user, "pk", pk.c_str());
    cJSON_AddStringToObject(user, "fc", fc.c_str());
    cJSON_AddStringToObject(user, "bdvsid", g_bdsc_engine->g_vendor_info->bdvsid);
    cJSON_AddStringToObject(user, "signature", sig.c_str());
    cJSON_AddStringToObject(user, "time_stamp", time_str.c_str());
    cJSON_AddStringToObject(user, "token_alpha", g_bdsc_engine->g_vendor_info->bdvs_token_alpha);
    // TODO: how to get these version info??
    cJSON_AddStringToObject(user, "sdkVersion", "0.0.0");
    cJSON_AddStringToObject(user, "dspVersion", "0.0.0");

    cJSON *method_list = cJSON_CreateArray();
    cJSON_AddItemToObject(user, "methods", method_list);

    cJSON_AddItemToArray(method_list, cJSON_CreateString("ASR"));
    cJSON_AddItemToArray(method_list, cJSON_CreateString("NLP"));
    cJSON_AddItemToArray(method_list, cJSON_CreateString("TTS"));

    cJSON *dynamic_list = cJSON_CreateArray();
    cJSON *dynamic_value1 = cJSON_CreateObject();
    cJSON *dynamic_value2 = cJSON_CreateObject();
    cJSON_AddNumberToObject(dynamic_value1, "mode", 0);
    cJSON_AddNumberToObject(dynamic_value1, "uploadPid", pid);
    cJSON_AddStringToObject(dynamic_value1, "dynamicDictType", "DYNAMIC_DICT_TYPE_COMMON");

    cJSON_AddNumberToObject(dynamic_value2, "mode", 1);
    cJSON_AddNumberToObject(dynamic_value2, "uploadPid", pid);
    cJSON_AddStringToObject(dynamic_value2, "dynamicDictType", "DYNAMIC_DICT_TYPE_USER");

    cJSON_AddItemToArray(dynamic_list, dynamic_value1);
    cJSON_AddItemToArray(dynamic_list, dynamic_value2);

    cJSON_AddItemToObject(user, "dynamicDictArray", dynamic_list);

    return user;
}

static cJSON* asr_query_create_event()
{
    std::stringstream stream;
    stream << get_current_time_ms() << create_rand_number(9, 0);
    std::string message_id = stream.str();
    //std::string message_id = std::to_string(get_current_time_ms()) + std::to_string(create_rand_number(9, 0));
    cJSON *event = cJSON_CreateObject();
    cJSON *header_value = cJSON_CreateObject();
    cJSON *payload_value = cJSON_CreateObject();

    cJSON_AddStringToObject(header_value, "messageId", message_id.c_str());
    cJSON_AddStringToObject(header_value, "name", "TextInput");
    cJSON_AddStringToObject(header_value, "namespace", "bdvs.capability.text_input");
    cJSON_AddStringToObject(header_value, "dialogRequestId", message_id.c_str()); // 取和message相同的值
    // std:string tmp_str = "{" + payloadstring + "}";

    cJSON_AddNullToObject(payload_value, "query");
    cJSON_AddNullToObject(payload_value, "sn");
    cJSON_AddNumberToObject(payload_value, "idx", -1);
    cJSON_AddNullToObject(payload_value, "asrState");

    cJSON_AddItemToObject(event, "payload", payload_value);
    cJSON_AddItemToObject(event, "header", header_value);

    return event;
}

static cJSON *asr_dev_status()
{
    uint64_t ts = get_current_time_ms();
    cJSON *con_header = cJSON_CreateObject();
    cJSON_AddStringToObject(con_header, "namespace", "bdvs.capability.extensions");
    cJSON_AddStringToObject(con_header, "name", "RequestParam");

    // // player status
    cJSON *player_status = cJSON_CreateObject();
    cJSON_AddStringToObject(player_status, "state", g_dev_handle.play_state);
    cJSON_AddStringToObject(player_status, "domain", g_dev_handle.domain);

    cJSON_AddNumberToObject(player_status, "timestamp", ts);

    cJSON_AddNumberToObject(player_status, "PlayProgress", g_dev_handle.progress);
    cJSON_AddNumberToObject(player_status, "TotalLength", g_dev_handle.total_length);

    bds_hh2_logi(TAG, "the player status set OK");

    // map status
    cJSON *pay_map = cJSON_CreateObject();
    cJSON_AddStringToObject(pay_map, "state", "Yes");
    bds_hh2_logi(TAG, "map state is ");

    // tts param
    cJSON *pay_tts = get_tts_param();

    bds_hh2_logi(TAG, "tts param ");

    cJSON *con_exten = cJSON_CreateObject();
    cJSON_AddItemToObject(con_exten, "playerStatus", player_status);
    cJSON_AddItemToObject(con_exten, "mapStatus", pay_map);
    cJSON_AddItemToObject(con_exten, "ttsParam", pay_tts);

    char *in_extern = cJSON_PrintUnformatted(con_exten);
    std::string exten_pam = in_extern;
    free(in_extern);
    cJSON_Delete(con_exten);

    cJSON *con_payload = cJSON_CreateObject();
    if (!exten_pam.empty()) {
        cJSON_AddStringToObject(con_payload, "extension", exten_pam.c_str());
    }

    cJSON *con_user = cJSON_CreateObject();
    cJSON_AddItemToObject(con_user, "header", con_header);
    cJSON_AddItemToObject(con_user, "payload", con_payload);

    return con_user;
}

static std::string asr_query_param(std::string ak, std::string sk, std::string pk, std::string fc, int pid)
{
    std::string result;
    int64_t ts_ms = get_current_time_ms();
    if (ts_ms == -1) {
        return result;
    }
    int ts_min = ts_ms / 60000; // minutes
    std::string sig = get_signature(ts_min, ak, sk);
    cJSON *auth_value = event_create_auth(ak, sk, pk, fc, pid);
    if (auth_value == nullptr) {
        bds_hh2_loge(TAG, "create auth error");
        return "";
    }

    cJSON *user = cJSON_CreateObject(); // create root data object

    cJSON_AddItemToObject(user, "authorization", auth_value);

    cJSON_AddStringToObject(user, "bdvs-device-id", g_bdsc_engine->cuid);
    cJSON_AddNumberToObject(user, "timestamp", ts_min);
    cJSON_AddStringToObject(user, "bdvs-version", "2.0.0");

    cJSON_AddItemToObject(user, "event", asr_query_create_event());

    cJSON *con_arry = cJSON_CreateArray(); // array is null
    cJSON_AddItemToArray(con_arry, asr_dev_status()); // dev status

    cJSON_AddItemToObject(user, "contexts", con_arry);

    char *out = cJSON_PrintUnformatted(user);
    result = out;
    cJSON_Delete(user);
    if (out) {
        free(out);
    }

    bds_hh2_logi(TAG, "the request pam is %s", result.c_str());
    return result;
}

std::string BdvsProtoHelper::bdvs_asr_pam_build(std::string ak, std::string sk, std::string pk, std::string fc, int pid)
{
    return asr_query_param(ak, sk, pk, fc, pid);
}

//////////////////////   ASR Message Parser    //////////////////
std::string BdvsProtoHelper::bdvs_asr_data_parse(std::string in_str, std::string &sn)
{
    bds_hh2_loge(TAG, "enter %s", __func__);
    if(in_str.empty()) {
        bds_hh2_loge(TAG, "the input string is null");
        return "";
    }

    cJSON *root = cJSON_Parse(in_str.c_str());
    if (!root) {
        bds_hh2_loge(TAG, "the asr result is not right format");
        return "";
    }

    cJSON *error_code = cJSON_GetObjectItem(root, "err_no");
    if (error_code->valueint != 0) {
        bds_hh2_loge(TAG, "asr result error, error code is %d", error_code->valueint);
        cJSON_Delete(root);
        return "";
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!result) {
        bds_hh2_loge(TAG, "no result data in the result");
        cJSON_Delete(root);
        return "";
    }

    cJSON *word = cJSON_GetObjectItem(result, "word");
    if (!word) {
        bds_hh2_loge(TAG, "no word in the asr result");
        cJSON_Delete(root);
        return "";
    }

    int arr_size = cJSON_GetArraySize(word);
    if (arr_size <= 0) {
        bds_hh2_loge(TAG, "the word is null");
        cJSON_Delete(root);
        return "";
    }

    cJSON *value = cJSON_GetArrayItem(word, 0); // only get the first result
    bds_hh2_logi(TAG, "the word result is %s", value->valuestring);
    std::string tmp_value = value->valuestring;

    // only show not the key value
    cJSON *sn_no = cJSON_GetObjectItem(root, "sn");
    if (!sn_no) {
        bds_hh2_loge(TAG, "no sn_no");
        sn = "";
    } else {
        bds_hh2_logi(TAG, "the sn number is %s", sn_no->valuestring);
        sn = sn_no->valuestring;
    }

    cJSON_Delete(root);
    return tmp_value;
}

//////////////////////   NLP Message Parser    //////////////////
int BdvsProtoHelper::bdvs_nlp_data_parse(std::string in_str)
{
    bds_hh2_logi(TAG, "enter %s", __func__);
    if (in_str.empty()) {
        bds_hh2_loge(TAG, "input nlp result is null");
        return -1;
    }

    cJSON *root = cJSON_Parse(in_str.c_str());
    if (!root) {
        bds_hh2_loge(TAG, "get next song cache msg failed");
        return -1;
    }

    cJSON *err_value = cJSON_GetObjectItem(root, "error_code"); // if error situation
    if (err_value && err_value->valueint == -1) {
        bds_hh2_loge(TAG, "receive error , error num is %d", err_value->valueint);
        cJSON_Delete(root);
        return -1;
    }

    // printf("receive data %s\n", root->valuestring);
    cJSON *error_code = cJSON_GetObjectItem(root, "status");
    if (error_code && ((error_code->valueint != 200) && (error_code->valueint != 400) &&
                                                        (error_code->valueint != 410))) {
        bds_hh2_loge(TAG, "receive error , error num is %d", error_code->valueint);
        cJSON_Delete(root);
        return -1;
    }

    cJSON *directives_value = cJSON_GetObjectItem(root, "directives");
    if (!directives_value) { // directives empty
        bds_hh2_loge(TAG, "directives is empty");
        cJSON_Delete(root);
        return -1;
    }

    int dir_num = cJSON_GetArraySize(directives_value);
    if (dir_num <= 0) {
        bds_hh2_loge(TAG, "directives is invalid");
        cJSON_Delete(root);
        return -1;
    }

    for (int j = 0; j < dir_num; j++) {
        cJSON *dir_item = cJSON_GetArrayItem(directives_value, j);
        if (dir_item == nullptr) {
            continue;
        }

        cJSON *dir_header = cJSON_GetObjectItem(dir_item, "header");
        if (!dir_header) {
            continue;
        }

        cJSON *header_namespace = cJSON_GetObjectItem(dir_header, "namespace");
        if (!header_namespace) {
            continue;
        }

        // if namespace is voice output
        if (strcmp(header_namespace->valuestring, "bdvs.capability.voice_output") == 0) {
            cJSON *dir_payload = cJSON_GetObjectItem(dir_item, "payload");
            if (!dir_payload) {
                continue;
            }

            cJSON *pay_con = cJSON_GetObjectItem(dir_payload, "content");
            if (pay_con) {
                bds_hh2_loge(TAG, "tts header info is %s", pay_con->valuestring);
            }

            SET_CMD_BITS(g_bdvs_cmd_bit_set, BDVS_CMD_BIT_VOICE_OUTPUT_SPEAK);
        } else if (strcmp(header_namespace->valuestring, "bdvs.capability.extensions") == 0) { // other situation
            cJSON *dir_payload = cJSON_GetObjectItem(dir_item, "payload");
            if (!dir_payload) {
                continue;
            }

            cJSON *dir_ext = cJSON_GetObjectItem(dir_payload, "extension"); // externsion的value, string value
            if (!dir_ext) {
                continue;
            }

            bds_hh2_logi(TAG, "the extension value is %s", dir_ext->valuestring);
            cJSON * ext_root = cJSON_Parse(dir_ext->valuestring);
            if (!ext_root) {
                continue;
            }

            // extension handle, custom -> action -> origin
            cJSON *custom = cJSON_GetObjectItem(ext_root, "custom");
            if (custom) { // if have custom, handle first
                bds_hh2_loge(TAG, "custom");
                int ret = class_handle["custom"](custom); // handle custom
                if (ret < 0) {
                    bds_hh2_loge(TAG, "handle custom failed");
                }

                cJSON_Delete(ext_root);
                continue;
            }

            cJSON *action = cJSON_GetObjectItem(ext_root, "action");
            if (action) {
                int ret = class_handle["action"](action);
                if (ret < 0) {
                    bds_hh2_loge(TAG, "handle the action failed");
                }

                cJSON_Delete(ext_root);
                continue;
            } else {
                cJSON *action_list_arr = cJSON_GetObjectItem(ext_root, "actionList");
                if (action_list_arr) {
                    int action_list_size = cJSON_GetArraySize(action_list_arr);
                    if (action_list_size <= 0) {
                        continue;
                    }

                    for (int k = 0; k < action_list_size; k++) {
                        cJSON *list_item = cJSON_GetArrayItem(action_list_arr, k);
                        if (!list_item) {
                            continue;
                        }

                        int ret = class_handle["action"](list_item);
                        if (ret < 0) {
                            bds_hh2_loge(TAG, "handle the action failed");
                            continue;
                        }
                    }

                    cJSON_Delete(ext_root);
                    continue;
                }
            }

            cJSON *origin = cJSON_GetObjectItem(ext_root, "origin");
            if (origin) {
                bds_hh2_loge(TAG, "origin");
                int ret = class_handle["origin"](origin);
                if (ret < 0) {
                    bds_hh2_loge(TAG, "handle the origin failed");
                }

                cJSON_Delete(ext_root);
                continue;
            }
        }
    }

    if (root) {
        cJSON_Delete(root);
        root = nullptr;
    }
    
    return 0;
}

//////////////////////   Media Play Parser    //////////////////
int BdvsProtoHelper::bdvs_action_media_play_parse(cJSON *action)
{
    bds_hh2_logd(TAG, "media handle callback enter ==>");
    if (action == nullptr) {
        bds_hh2_loge(TAG, "no valid action list value");
        return -1;
    }

    cJSON *act_arg = cJSON_GetObjectItem(action, "arg");
    if (!act_arg) {
        return -1;
    }

    cJSON *arg_domain = cJSON_GetObjectItem(act_arg, "domain");
    if (arg_domain) {
        dev_status_set_domain(&g_dev_handle, arg_domain->valuestring);
    }
    cJSON *arg_type = cJSON_GetObjectItem(act_arg, "type");
    cJSON *arg_info = cJSON_GetObjectItem(act_arg, "info");
    if (!arg_info) {
        return -1;
    }

    cJSON *track_id = cJSON_GetObjectItem(arg_info, "trackId");
    cJSON *track_url = cJSON_GetObjectItem(arg_info, "trackUrl");

    if (track_url) {
        bds_hh2_loge(TAG, "recevie url %s", track_url->valuestring);
        
        dev_status_set_play_state(&g_dev_handle, "on");
        //dev_status_set_track_url(&g_dev_handle, track_url->valuestring);
        g_bdvs_cmd_url = track_url->valuestring;
        SET_CMD_BITS(g_bdvs_cmd_bit_set, BDVS_CMD_BIT_AUDIO_PLAYER_URL_PLAY);
    } else {
        bds_hh2_loge(TAG, "recevei track id");
    }

    return 0;
}

//////////////////////// C Wrapper APIs ///////////////////
extern "C" {
char* bdvs_device_active_request_build_c_wrapper()
{
    std::string request_str = BdvsProtoHelper::bdvs_device_active_request_build();
    return strdup(request_str.c_str());
}


char* bdvs_active_tts_request_build_c_wrapper(char* in_text)
{
    std::string request_str = BdvsProtoHelper::bdvs_active_tts_request_build(in_text);
    return strdup(request_str.c_str());
}


char* bdvs_asr_pam_build_c_wrapper(char* ak, char* sk, char* pk, char* fc, int pid)
{
    std::string result = BdvsProtoHelper::bdvs_asr_pam_build(ak, sk, pk, fc, pid);
    return strdup(result.c_str());
}
}