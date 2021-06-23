#include "cupid_device_manager.h"
#include "board.h"
#include "audio_mem.h"
#include "audio_player.h"
#include "bdsc_tools.h"
#include "app_ota_upgrade.h"
#include "app_cloud_log.h"

#define TAG "CUPID_DM"

#define CUPID_DM_DOWN_TRANS_NUM             101
#define CUPID_DM_DOWN_TRANS_KEY             "trannum"
#define CUPID_DM_DOWN_TYPE_KEY              "type"
#define CUPID_DM_DOWN_BODY_KEY              "body"
#define CUPID_DM_DOWN_CMD_OTA               "ota"
#define CUPID_DM_DOWN_CMD_OTA_S             "silentota"
#define CUPID_DM_DOWN_CMD_URLPLAY           "urlplay"
#define CUPID_DM_DOWN_CMD_SET               "set"
#define CUPID_DM_DOWN_CMD_SET_ACTION_OC     "openclose"
#define CUPID_DM_DOWN_CMD_SET_ACTION_AB     "asrblock"
#define CUPID_DM_DOWN_CMD_SET_ACTION_CL     "cloud_log"
#define CUPID_DM_DOWN_CMD_GET               "get"
#define CUPID_DM_DOWN_CMD_GET_STATUS        "getstatus"
#define CUPID_DM_DOWN_CMD_RESTART           "restart"


int cupid_device_manager_init(cupid_device_manager_t *dm)
{
    return 0;
}

cJSON* create_dm_upload_msg(int trans_num, const char *type, cJSON *body, bool is_active)
{
    cJSON *rsp = cJSON_CreateObject();
    if (rsp == NULL) {
        ESP_LOGE(TAG, "cJSON_CreateObject error");
        return NULL;
    }

    if (is_active) {
        // active upload msg
        cJSON_AddNumberToObject(rsp, "trannum_up", get_trannum_up());
    } else {
        // normal request-response msg
        cJSON_AddNumberToObject(rsp, "trannum", trans_num);
    }
    
    cJSON_AddStringToObject(rsp, "type", type);
    if (body) {
        cJSON_AddItemToObject(rsp, "body", body);
    }
    return  rsp;
}

char *generate_iot_device_status_info(int err_num, bool is_active)
{
    cJSON *rsp, *bodyJ;
    char *ret_str = NULL;

    if (!(rsp = create_dm_upload_msg(err_num, "status", NULL, is_active))) {
        ESP_LOGE(TAG, "create_dm_upload_msg error");
        return NULL;
    }
    bodyJ = cJSON_CreateObject();
    cJSON_AddItemToObject(rsp, "body", bodyJ);
    // version number
    cJSON_AddNumberToObject(bodyJ, "ver", g_bdsc_engine->g_vendor_info->cur_version_num);
#if CONFIG_CUPID_BOARD_V2
    // battery info
    cJSON_AddNumberToObject(bodyJ, "bat", audio_board_get_battery_voltage());
#endif

    ret_str = cJSON_PrintUnformatted(rsp);
    cJSON_Delete(rsp);
    return ret_str;
}

static char *generate_response_answer_errcode(int tran_int, char *answer, int err_code)
{
    cJSON *rsp, *errJ;

    if (!(rsp = create_dm_upload_msg(tran_int, answer, NULL, false))) {
        ESP_LOGE(TAG, "create_dm_response error");
        return NULL;
    }
    errJ = cJSON_CreateObject();
    cJSON_AddNumberToObject(errJ, "code", err_code);
    cJSON_AddItemToObject(rsp, "body", errJ);
    char *rsp_str = cJSON_PrintUnformatted(rsp);
    if (!rsp_str) {
        ESP_LOGE(TAG, "cJSON_PrintUnformatted fail");
        cJSON_Delete(rsp);
        return NULL;
    }
    cJSON_Delete(rsp);
    
    return rsp_str;
}

static int bdsc_dup_msg_simple_filter(int trannum)
{
    static int cur_idx = 0;
    int i;
    int cached_msg_queue_len = 1000;

    if (!g_bdsc_engine->cached_msg_queue) {
        g_bdsc_engine->cached_msg_queue = (int*)audio_calloc(cached_msg_queue_len, sizeof(int));
        memset(g_bdsc_engine->cached_msg_queue, 0, sizeof(int) * cached_msg_queue_len);
    }
    for (i = 0; i < cached_msg_queue_len; i++) {
        if (trannum == g_bdsc_engine->cached_msg_queue[i]) {
            ESP_LOGE(TAG, "found a duplicate message!");
            return 1;
        }
    }
    g_bdsc_engine->cached_msg_queue[cur_idx++ % cached_msg_queue_len] = trannum;
    return 0;
    
}

int cupid_device_manager_feed_data(cupid_device_manager_t *dm, 
                                    uint8_t *data, size_t data_len, void *userdata)
{
    cJSON *json, *transNumJ, *typeJ, *bodyJ;
    cJSON *ota_urlJ, *ota_verJ, *play_urlJ, *actionJ, *wordsJ;
    char *rsp_str;

    if (!(json = cJSON_Parse((const char *)data))) {
        ESP_LOGE(TAG, "mqtt push json format error");
        return -1;
    }

    if ((transNumJ = cJSON_GetObjectItem(json, CUPID_DM_DOWN_TRANS_KEY)) &&
        (transNumJ->type == cJSON_Number)) {
        int tran_int = transNumJ->valueint;
        if (bdsc_dup_msg_simple_filter(tran_int)) {
            ERR_OUT(err_out, "bdsc_dup_msg_simple_filter fail");
        }
        if ((typeJ = cJSON_GetObjectItem(json, CUPID_DM_DOWN_TYPE_KEY)) &&
            (typeJ->type == cJSON_String)) {
            
            if (!strcmp(typeJ->valuestring ,CUPID_DM_DOWN_CMD_OTA) ||
                !strcmp(typeJ->valuestring, CUPID_DM_DOWN_CMD_OTA_S)) {
                /* case2: 开始OTA。
                * 
                * Request:
                * 参数：url地址，版本号
                * {
                *   “trannum”：101，
                *   "type": "ota",
                *   "body": {"url": "xxxxxx","ver": 101}
                *   }
                * 
                * Response:
                * 先是普通反馈：
                * {
                    “trannum”：101，
                    "type": "answer",
                    "body": {"code": 2}
                    }
                * 
                * 等待ota成功完成，再重启之前在发送成功的反馈：
                * OTA反馈
                * 
                *   {
                *   “trannum_up”：101，
                *   "type": "otaback",
                *   "body": {“errcode”：0，"ver": 1}
                *   }
                */
                ESP_LOGI(TAG, "Got ota cmd!");
                if (g_bdsc_engine->in_ota_process_flag) {
                    ERR_OUT(err_out, "already in ota, skip");
                }
                if ((bodyJ = cJSON_GetObjectItem(json, CUPID_DM_DOWN_BODY_KEY))) {
                    if ((ota_urlJ = cJSON_GetObjectItem(bodyJ, "url")) &&
                        (ota_verJ = cJSON_GetObjectItem(bodyJ, "ver"))) {
                        ESP_LOGI(TAG, "url: %s, ver: %d", ota_urlJ->valuestring, ota_verJ->valueint);

                        rsp_str = generate_response_answer_errcode(tran_int, "answer", 0);
                        if (rsp_str) {
                            bdsc_engine_channel_data_upload((uint8_t *)rsp_str, strlen(rsp_str) + 1);
                            free(rsp_str);
                        } else {
                            ESP_LOGE(TAG, "generate_response_answer_errcode fail");
                        }
                        // set silent flag
                        if (!strcmp(typeJ->valuestring, CUPID_DM_DOWN_CMD_OTA_S)) {
                            g_bdsc_engine->silent_mode = 1;
                        }
                        // shut down player
                        audio_player_state_t st = {0};
                        audio_player_state_get(&st);
                        if (((int)st.status == AUDIO_STATUS_RUNNING)) {
                            audio_player_stop();
                        }
                        // start ota thread, mqtt cb can not block
                        bdsc_start_ota_thread(ota_urlJ->valuestring);
                    }
                }
            } else if (!strcmp(typeJ->valuestring ,CUPID_DM_DOWN_CMD_URLPLAY)) {
                /* case3: 发送音频文件，合成音频文件
                * 
                * Request:
                * {
                    “trannum”：101，
                    "type": "urlplay",
                    "body": {"url": "xxxxxx"}
                    }
                * 
                * Response:
                * {
                    “trannum”：101，
                    "type": "answer",
                    "body": {"code": 2}
                    }
                */
                ESP_LOGI(TAG, "Got urlplay cmd!");
                if ((bodyJ = cJSON_GetObjectItem(json, CUPID_DM_DOWN_BODY_KEY))) {
                    if ((play_urlJ = cJSON_GetObjectItem(bodyJ, "url"))) {
                        ESP_LOGI(TAG, "url: %s", play_urlJ->valuestring);
                        // start playing url...
                        event_engine_elem_EnQueque(EVENT_RECV_MQTT_PUSH_URL, (uint8_t *)play_urlJ->valuestring, strlen(play_urlJ->valuestring) + 1);
                        rsp_str = generate_response_answer_errcode(tran_int, "answer", 0);
                        if (rsp_str) {
                            bdsc_engine_channel_data_upload((uint8_t *)rsp_str, strlen(rsp_str) + 1);
                            free(rsp_str);
                        } else {
                            ESP_LOGE(TAG, "generate_response_answer_errcode fail");
                        }
                    }
                }
            } else if (!strcmp(typeJ->valuestring ,CUPID_DM_DOWN_CMD_SET)) {
                /* case4: 设置参数，一次只设置一个参数。例如，设置 ASR 拦截
                * 
                * Request:
                * {
                    “trannum”：101，  
                    "type": "set",
                    "body": {“action”：“openclose”，"para":false}
                    }
                * 
                * Response:
                * {
                    “trannum”：101，
                    "type": "answer",
                    "body": {"code": 2}
                    }
                */
                ESP_LOGI(TAG, "Got set cmd!");
                if ((bodyJ = cJSON_GetObjectItem(json, CUPID_DM_DOWN_BODY_KEY))) {
                    if ((actionJ = cJSON_GetObjectItem(bodyJ, "action")) && 
                        (actionJ->type == cJSON_String)) {
                        if (!strcmp(actionJ->valuestring, CUPID_DM_DOWN_CMD_SET_ACTION_AB)) {
                            wordsJ = cJSON_GetObjectItem(bodyJ, "para");
                            if (!wordsJ) {
                                ERR_OUT(err_out, "can not find para");
                            }
                            ESP_LOGI(TAG, "got asr_block words: %s", wordsJ->valuestring);
                            // saving the asr block words...
                            if (custom_key_op_safe(CUSTOM_KEY_SET, CUSTOM_KEY_TYPE_STRING, NVS_DEVICE_CUSTOM_NAMESPACE, "asr_block_word", wordsJ->valuestring, NULL) < 0) {
                                ERR_OUT(err_out, "custom_key_op_safe fail");
                            }
                            if (g_bdsc_engine->asr_block_words) {
                                free(g_bdsc_engine->asr_block_words);
                            }
                            g_bdsc_engine->asr_block_words = audio_strdup(wordsJ->valuestring);
                            ESP_LOGI(TAG, "profile save success");
                            // feedback
                            rsp_str = generate_response_answer_errcode(tran_int, "answer", 0);
                            if (rsp_str) {
                                bdsc_engine_channel_data_upload((uint8_t *)rsp_str, strlen(rsp_str) + 1);
                                free(rsp_str);
                            } else {
                                ESP_LOGE(TAG, "generate_response_answer_errcode fail");
                            }
                        } else if (!strcmp(actionJ->valuestring, CUPID_DM_DOWN_CMD_SET_ACTION_OC)) {
                            ESP_LOGI(TAG, "got common set value");
                            // saving the asr key-values...
                        } else if(!strcmp(actionJ->valuestring, CUPID_DM_DOWN_CMD_SET_ACTION_CL)) {
                            cJSON *cJson_cloud_log_level = cJSON_GetObjectItem(bodyJ, "para");
                            if (!cJson_cloud_log_level) {
                                ERR_OUT(err_out, "can not find para");
                            }
                            if (!strcmp(cJson_cloud_log_level->valuestring,"D")) {
                                ESP_LOGI(TAG, "set level 打开云端调试日志");
                                set_cloud_log_level(ESP_LOG_DEBUG);
                            } else if (!strcmp(cJson_cloud_log_level->valuestring,"I")) {
                                ESP_LOGI(TAG, "set level 打开云端信息日志");
                                set_cloud_log_level(ESP_LOG_INFO);
                            } else if (!strcmp(cJson_cloud_log_level->valuestring,"W")) {
                                ESP_LOGI(TAG, "set level 打开云端警告日志");
                                set_cloud_log_level(ESP_LOG_WARN);
                            } else if (!strcmp(cJson_cloud_log_level->valuestring,"E")) {
                                ESP_LOGI(TAG, "set level 打开云端错误日志");
                                set_cloud_log_level(ESP_LOG_ERROR);
                            } else if (!strcmp(cJson_cloud_log_level->valuestring,"N")) {
                                ESP_LOGI(TAG, "set level 关闭云端日志");
                                set_cloud_log_level(ESP_LOG_NONE);
                            } else {
                                ESP_LOGE(TAG, "unkonw para:%s",cJson_cloud_log_level->valuestring);
                            }
                        } else {
                            ESP_LOGI(TAG, "unkown action");
                        }
                    }
                }
            } else if (!strcmp(typeJ->valuestring ,CUPID_DM_DOWN_CMD_GET_STATUS)) {
                /* case5: 查询设备状态。
                * 
                * Request:
                * {
                *   “trannum”：101，  
                *   "type": "getstatus",
                *   }
                * 
                * Response:
                * {
                *   “trannum”：101，
                *   "type": "status",
                *   "body": {"ver": 1,"para1":false,"para2":true,"para3":false}
                *   }
                */
                ESP_LOGI(TAG, "Got getstatus cmd!");
                char *device_status_info = generate_iot_device_status_info(tran_int, false);
                if (device_status_info) {
                    bdsc_engine_channel_data_upload((uint8_t *)device_status_info, strlen(device_status_info) + 1);
                    free(device_status_info);
                }
            }  else if (!strcmp(typeJ->valuestring ,CUPID_DM_DOWN_CMD_RESTART)) {
                ESP_LOGI(TAG, "Got restart cmd!");
                if ((rsp_str = generate_response_answer_errcode(tran_int, "answer", 0))) {
                    bdsc_engine_channel_data_upload((uint8_t *)rsp_str, strlen(rsp_str) + 1);
                    free(rsp_str);
                }
                if (g_bdsc_engine->silent_mode) {
                    int mode = 1;
                    if (custom_key_op_safe(CUSTOM_KEY_SET, CUSTOM_KEY_TYPE_INT32, NVS_DEVICE_SYS_NAMESPACE, PROFILE_NVS_KEY_SLIENT_MODE, &mode, NULL) < 0) {
                        ERR_OUT(err_out, "custom_key_op_safe fail");
                    }
                }
                vTaskDelay(500 / portTICK_PERIOD_MS);
                esp_restart();
            } else {
                ERR_OUT(err_out, "not supported key： %s  %d", typeJ->valuestring, strlen(typeJ->valuestring));
            }
        } else {
            ERR_OUT(err_out, "not find type key");
        }
    } else {
        ERR_OUT(err_out, "unsupported transNum");
    }
    
    cJSON_Delete(json);
    return 0;

err_out:
    cJSON_Delete(json);
    return -1;
    
}


int cupid_device_manager_deinit(cupid_device_manager_t *dm)
{
    return 0;
}
