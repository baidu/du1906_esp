/**
 * @file active_device.cpp
 * @author wangmeng (wangmeng43@baidu.com)
 * @brief 
 * @version 0.1
 * @date 2022-03-15
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "generate_pam.h"
#include "bdsc_tools.h"
#include "bdsc_profile.h"
#include "log.h"
#include "bdsc_http.h"
#include "receive_data_filter.h"
#include "bdvs_protocol_helper_c_wrapper.h"
#include "bdvs_protocol_helper.hpp"
#include "bdvs_active_device_handler.h"

#include "bdsc_engine.h"
#define TAG "ACTIVE_DEV"

extern const uint8_t server_root_cert_pem_start[] asm("_binary_server_root_cert_pem_start");
extern const uint8_t server_root_cert_pem_end[]   asm("_binary_server_root_cert_pem_end");

static const std::string m_host = "smarthome-test.baidubce.com";
static const std::string m_url = "/v1/device/active";

static void active_device_update_asr_url(char* url)
{
    profile_key_set(PROFILE_KEY_TYPE_ASR_URL, url);
}

static void active_device_update_event_url(char* url)
{
    profile_key_set(PROFILE_KEY_TYPE_EVENT_URL, url);
}

static void active_device_update_bdvsid(char* bdvsid)
{
    profile_key_set(PROFILE_KEY_TYPE_BDVSID, bdvsid);
}

static void active_device_update_dictionary(bool is_dic)
{
    profile_key_set(PROFILE_KEY_TYPE_BDVS_DICTIONARY, &is_dic);
}

static void active_device_update_key(char* key)
{
    profile_key_set(PROFILE_KEY_TYPE_BDVS_KEY, key);
}

static void active_device_update_pid(char* pid)
{
    profile_key_set(PROFILE_KEY_TYPE_BDVS_PID, pid);
}

static void active_device_update_token_alpha(char *token_alpha)
{
    profile_key_set(PROFILE_KEY_TYPE_BDVS_TOKEN_ALPHA, token_alpha);
}
extern "C" void config_sdk(bds_client_handle_t handle);
static int payload_data_handle(cJSON *action)
{
    bds_hh2_logi(TAG, "action handle callback enter ==>");
    if (action == nullptr) {
        bds_hh2_loge(TAG, "nlp value is empty");
        return -1;
    }

    cJSON *act_arg = cJSON_GetObjectItem(action, "arg");
    if (!act_arg) {
        return -1;
    }

    // check error code and error message first
    cJSON *error_no = cJSON_GetObjectItem(act_arg, "errCode");
    if (error_no && error_no->valueint != 0) {
        cJSON *error_msg = cJSON_GetObjectItem(act_arg, "errMsg");
        if (error_msg) {
            bds_hh2_loge(TAG, "active device failed, error message is %s", error_msg->valuestring);
        }
        return -1;
    }

    cJSON *asr_param = cJSON_GetObjectItem(act_arg, "asrParam");
    if (asr_param == nullptr) {
        return -1;
    }

    cJSON *pid = cJSON_GetObjectItem(asr_param, "pid");
    if (pid) {
        // g_bdsc_engine->g_vendor_info->bdvs_asr_pid = pid->valuestring;
        // g_bdsc_engine->g_vendor_info->bdvs_evt_pid = pid->valuestring;

        // asr_pid, evt_pid use the same
        active_device_update_pid(pid->valuestring);
    }

    cJSON *key = cJSON_GetObjectItem(asr_param, "key");
    if (key) {
        // g_bdsc_engine->g_vendor_info->bdvs_evt_key = key->valuestring;
        // g_bdsc_engine->g_vendor_info->bdvs_asr_key = key->valuestring;

        // asr_key, evt_key use the same
        active_device_update_key(key->valuestring);
    }

    cJSON *bdvsid = cJSON_GetObjectItem(asr_param, "bdvsid");
    if (bdvsid) {
        // g_bdsc_engine->g_vendor_info->bdvsid = bdvsid->valuestring;

        active_device_update_bdvsid(bdvsid->valuestring);
    }

    cJSON *asrurl = cJSON_GetObjectItem(asr_param, "asrUrl");
    if (asrurl) {
        // g_bdsc_engine->g_vendor_info->bdvs_asr_url = asrurl->valuestring;

        active_device_update_asr_url(asrurl->valuestring);
    }

    cJSON *eventurl = cJSON_GetObjectItem(asr_param, "eventUrl");
    if (eventurl) {
        // g_bdsc_engine->g_vendor_info->bdvs_evt_url = eventurl->valuestring;

        active_device_update_event_url(eventurl->valuestring);
    }

    cJSON *extend_param = cJSON_GetObjectItem(act_arg, "extendParam");
    if (extend_param) {
        cJSON *is_dynamic = cJSON_GetObjectItem(extend_param, "isDynamic");
        if (is_dynamic) {
            // g_bdsc_engine->g_vendor_info->bdvs_dictionary = is_dynamic->valueint;
            active_device_update_dictionary(is_dynamic->valueint);
        }

        cJSON *token = cJSON_GetObjectItem(extend_param, "tokenAlpha");
        if (token) {
            // g_bdsc_engine->g_vendor_info->bdvs_token_alpha = token->valuestring;

            active_device_update_token_alpha(token->valuestring);
        }
    }
    if (g_bdsc_engine) {
        config_sdk(g_bdsc_engine->g_client_handle);
        bdsc_link_start();
    }
    return 0;
}

/**
 * @brief generate the active send body
 * 
 * @return std::string 
 */
static std::string active_device_generate_body()
{
    std::string send_str;
    std::string head = "POST " + m_url + " HTTP/1.0\r\n" +
                "Host: " + m_host + "\r\n" +
                "User-Agent: esp32\r\n" +
                "Content-Type: application/json\r\n" +
                "Content-Length: ";

    std::string body = BdvsProtoHelper::bdvs_device_active_request_build();
    if (body.empty()) {
        return send_str;
    }
    std::stringstream stream;
    stream << body.size();
    head += stream.str();
    //head += std::to_string(body.size());
    head += "\r\n\r\n";

    send_str = head + body;
    return send_str;
}

// init function ,can use list initialization
void active_device_handle_init()
{
    // register handle function
    bds_hh2_loge(TAG, "add the active device handle");
    //add_new_handle_to_map((char *)"bdvs.capability.extensions.iot.command", (char *)"Active", payload_data_handle);
    add_new_action_handle("device.active", payload_data_handle);
}

void active_device_send_task_callback(void *pvParameters)
{
    char *ret_data_out = NULL;
    size_t data_out_len = 0;
    int err = 0;
    std::string request_str;
    int retry_time = 1;

    while (1) {
        request_str = active_device_generate_body();
        if (request_str.empty()) {
            bds_hh2_loge(TAG, "generate the device active body fail");
            vTaskDelay(3000 / portTICK_PERIOD_MS);
            continue;
        }

        bds_hh2_loge(TAG, "begin to send the active device data: %s", request_str.c_str());
        err = bdsc_send_https_post_sync((char *)m_host.c_str(), 443, 
                                    (char *)server_root_cert_pem_start, server_root_cert_pem_end - server_root_cert_pem_start,
                                    (char *)request_str.c_str(), request_str.size() + 1, 
                                    &ret_data_out, &data_out_len);

        if (err) {
            retry_time *= 2;
            if (retry_time > 20) {
                retry_time = 20; // max delay time
            }
            bds_hh2_loge(TAG, "active device failed, will retry after %d seconds", retry_time);

            vTaskDelay(retry_time * 1000 / portTICK_PERIOD_MS);
            continue;
        }

        // handle received value
        // todo: handle received value
        bds_hh2_logi(TAG, "recv body: %s", ret_data_out);
        int ret = receive_nlp_data_handle(ret_data_out);
        if (ret != 0) { // retry receive data error
            retry_time *= 2;
            if (retry_time > 20) {
                retry_time = 20; // max delay time
            }
            bds_hh2_loge(TAG, "active device failed, will retry after %d seconds", retry_time);

            vTaskDelay(retry_time * 1000 / portTICK_PERIOD_MS);
            continue; 
        }

        bds_hh2_loge(TAG, "active success");
        break;
    }

    vTaskDelete(NULL); // destroy current task
}

int start_active_device_task()
{
    bds_hh2_loge(TAG, "start the active device task");
    active_device_handle_init();
    int ret = xTaskCreate(active_device_send_task_callback,
                        "active_device_task",
                        10 * 1024,
                        NULL,
                        11,
                        NULL);

    if (ret != pdPASS) {
        ERR_OUT(ERR_RET, "fail to create active device task");
    }

    return 0;

ERR_RET:
    return -1;
}
