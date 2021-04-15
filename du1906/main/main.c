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

#include "esp_log.h"
#include "nvs_flash.h"
#include "bdsc_engine.h"
#include "audio_tone_uri.h"
#include "audio_player.h"
#include "bdsc_event_dispatcher.h"
#include "app_control.h"
#include "app_sys_tools.h"
#include "app_voice_control.h"
#include "display_service.h"
#include "bdsc_json.h"
#include "audio_mem.h"
#include "bdsc_tools.h"
#include "cupid_device_manager.h"
#include "app_bt_init.h"
#ifdef CONFIG_CLOUD_LOG
#include "app_cloud_log.h"
#endif
#include "bdsc_ota_partitions.h"

#define TAG "MAIN"
cupid_device_manager_t g_dm;

cJSON* create_dm_upload_msg(int trans_num, const char *type, cJSON *body, bool is_active);
static char* create_asr_block_word_response_string(const char *words)
{
    cJSON *rsp = NULL, *wordsJ = NULL;

    if (!(rsp = create_dm_upload_msg(get_trannum_up(), "asrresult", NULL, true))) {
        ESP_LOGE(TAG, "create_dm_response error");
        return NULL;
    }
    wordsJ = BdsJsonObjectNew();
    BdsJsonObjectAddString(wordsJ, "words", words);
    BdsJsonObjectAdd(rsp, "body", wordsJ);
    char *rsp_str = BdsJsonPrintUnformatted(rsp);
    if (!rsp_str) {
        ESP_LOGE(TAG, "cJSON_PrintUnformatted fail");
        BdsJsonPut(rsp);
        return NULL;
    }

    BdsJsonPut(rsp);
    return rsp_str;
}

static char* create_duplex_mode_status_string(int in_or_out)
{
    cJSON *rsp = NULL;

    if (!(rsp = create_dm_upload_msg(get_trannum_up(), "TVMode", NULL, true))) {
        ESP_LOGE(TAG, "create_dm_response error");
        return NULL;
    }
    BdsJsonObjectAddInt(rsp, "code", in_or_out);
    char *rsp_str = BdsJsonPrintUnformatted(rsp);
    if (!rsp_str) {
        ESP_LOGE(TAG, "cJSON_PrintUnformatted fail");
        BdsJsonPut(rsp);
        return NULL;
    }

    BdsJsonPut(rsp);
    return rsp_str;
}


bool need_skip_current_playing()
{
    /* 用户在某些特定应用场景下需要跳过当前session的 asr tts 处理 */
    /* 情况一：当前拦截到 “asr”拦截词，则需要退出当前的session */
    if (g_bdsc_engine->current_asr_words &&
        g_bdsc_engine->asr_block_words &&
        strstr(g_bdsc_engine->current_asr_words, g_bdsc_engine->asr_block_words)) {
        
        ESP_LOGI(TAG, "skip asr_block tts..");
        return true;
    }
    // case 2: app want skip current asr-tts session
    /* 情况二：app 希望跳过当前一次的 asr-tts session，比如“全双工”最后一次的非 TV_ACTION query */
    if (g_bdsc_engine->skip_tts_playing_once) {
        return true;
    }
    return false;
}

esp_err_t my_bdsc_engine_event_handler(bdsc_engine_event_t *evt)
{
    cJSON *json = NULL;
    char *resp_str = NULL;
    
    switch (evt->event_id) {
    case BDSC_EVENT_ERROR:
        ESP_LOGI(TAG, "==> Got BDSC_EVENT_ERROR");
        /*
         * 语音全链路过程中遇到任何错误，都会通过该回调通知用户。
         * 返回的 evt 类型如下：
         *
         * evt->data     为 bdsc_event_error_t 结构体指针
         * evt->data_len 为 bdsc_event_error_t 结构体大小
         * evt->client   为 全局 bdsc_engine_handle_t 实例对象
         * 结构体定义如下：
         *
        typedef struct {
            char sn[SN_LENGTH];    // 在语音链路中，每个request都对应一个sn码，可以用来定位失败原因
            int32_t code;          // 错误码
            uint16_t info_length;  // 错误msg长度
            char info[];           // 错误msg
        } bdsc_event_error_t;
         *
         *
         * TIPS: 若有问题需百度技术支持，可提供上述的 “错误码” 以及 “sn号码”，协助排查问题。
         */
        bdsc_event_error_t *error = (bdsc_event_error_t*)evt->data;
        if (error) {
            if (ASR_TTS_ENGINE_GOT_ASR_BEGIN == bdsc_engine_get_internal_state(evt->client)) {
                if (error->code == -2002) {
                    bdsc_play_hint(BDSC_HINT_DISCONNECTED);
                }
                else {
                    bdsc_play_hint(BDSC_HINT_NOT_FIND);
                }
            }
            else {
                bdsc_play_hint(BDSC_HINT_NOT_FIND);
            }
            return BDSC_CUSTOM_DESIRE_SKIP_DEFAULT;
        }
        return BDSC_CUSTOM_DESIRE_DEFAULT;
    case BDSC_EVENT_ON_WAKEUP:
        /* 每次唤醒，退出全双工 */
        if (g_bdsc_engine->in_duplex_mode) {
            ESP_LOGI(TAG, "early EXIT duplex mode!");
            if (xTimerStop(g_bdsc_engine->duplex_timer, 10 / portTICK_PERIOD_MS) != pdPASS) {
                ESP_LOGE(TAG, "Can not delete duplex timer, must do something!");
            }
            g_bdsc_engine->in_duplex_mode = 0;

            if ((resp_str = create_duplex_mode_status_string(0))) {
                bdsc_engine_channel_data_upload((uint8_t *)resp_str, strlen(resp_str) + 1);
                free(resp_str);
                display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_MUTE_OFF, 0);
            }
        }
        return BDSC_CUSTOM_DESIRE_DEFAULT;
    case BDSC_EVENT_ON_ASR_RESULT:
        ESP_LOGI(TAG, "==> Got BDSC_EVENT_ON_ASR_RESULT");
        /*
         * 用户通过 “唤醒词 + 命令” 进行交互时，云端返回的ASR结果。
         * 返回的 evt 类型如下：
         * evt->data     为 bdsc_event_data_t 结构体指针
         * evt->data_len 为 bdsc_event_data_t 结构体大小
         * evt->client   为 全局 bdsc_engine_handle_t 实例对象
         * 结构体定义如下：
         *
        typedef struct {
            char sn[SN_LENGTH];      // 在语音链路中，每个request都对应一个sn码，方便追溯问题。
            int16_t idx;             // 序号
            uint16_t buffer_length;  // 数据包长度
            uint8_t buffer[];        // 数据
        } bdsc_event_data_t;
         *
         * 返回的 buffer 数据为 JSON 格式。格式如下：
         * 以“问：今天天气”为例，返回：
         *  {"corpus_no":6816175353451016467,"err_no":0,"raf":35,"result":{"word":["今天天气","今天天泣","今天天汽","今天天器","今天天弃"]},"sn":"015a3402-1723-4da7-a705-fd2b79dc2e70"}
         *
         * 各字段格式如下：
         * - corpus_no  ： 内部编号
         * - err_no     ： 错误码，正确返回0
         * - raf        ： raf
         * - result     ： ASR结果，为一 JSON 对象
         * - word       ： 候选ASR结果数组，第一项为置信度最高项
         * - sn         ： 每个request对应的sn码
         *
         * TIPS: 随 ASR 一起下发的TTS语音流，由SDK自动播放，暂时不对用户开放。
         */
        bdsc_event_data_t *asr_result = (bdsc_event_data_t *)evt->data;
        if (!asr_result->buffer) {
            ESP_LOGE(TAG, "BUG!!!\n");
            return BDSC_CUSTOM_DESIRE_DEFAULT;
        }
        ESP_LOGI(TAG, "========= asr result %s", (char *)asr_result->buffer);

        if (!(json = BdsJsonParse((const char *)asr_result->buffer))) {
            ESP_LOGE(TAG, "json format error");
            return BDSC_CUSTOM_DESIRE_SKIP_DEFAULT;
        }
        int err_value;
        if (-1 == BdsJsonObjectGetInt(json, "err_no", &err_value)) {
            ESP_LOGE(TAG, "json format error");
            BdsJsonPut(json);
            return BDSC_CUSTOM_DESIRE_SKIP_DEFAULT;
        }
        /* 如果处在全双工模式，禁止播放 3005 的提示音 */
        if (err_value == -3005 || err_value == -3003) {
            if (!g_bdsc_engine->in_duplex_mode) {
                // in duplex mode, dont play hint
                // bdsc_play_hint(BDSC_HINT_NOT_FIND);
                BdsJsonPut(json);
                return BDSC_CUSTOM_DESIRE_RESUME;
            }
            BdsJsonPut(json);
            return BDSC_CUSTOM_DESIRE_DEFAULT;
        }

        /* 把当前的asr识别结果保存起来，后面要用 */
        if (g_bdsc_engine->current_asr_words) {
            free(g_bdsc_engine->current_asr_words);
            g_bdsc_engine->current_asr_words = NULL;
        }
        g_bdsc_engine->current_asr_words = audio_strdup((char *)asr_result->buffer);
        BdsJsonPut(json);
#if CONFIG_CUPID_BOARD_V2
        /* asr拦截：在当前的 asr 结果中查询是否包含拦截词，如果包含，就mqtt推送一条消息 */
        if (g_bdsc_engine->asr_block_words && strstr((char *)g_bdsc_engine->current_asr_words, g_bdsc_engine->asr_block_words)) {
            ESP_LOGI(TAG, "catch asr block word!!");
            if ((resp_str = create_asr_block_word_response_string(g_bdsc_engine->asr_block_words))) {
                bdsc_engine_channel_data_upload((uint8_t *)resp_str, strlen(resp_str) + 1);
                free(resp_str);
            }
        }
#endif
#ifdef CONFIG_CLOUD_LOG
        if(strstr((char *)g_bdsc_engine->current_asr_words,"打开云端调试日志") != NULL)
        {
            ESP_LOGI(TAG, "set level 打开云端调试日志");
            set_cloud_log_level(ESP_LOG_DEBUG);
        }
        else if(strstr((char *)g_bdsc_engine->current_asr_words,"打开云端信息日志") != NULL)
        {
            ESP_LOGI(TAG, "set level 打开云端信息日志");
            set_cloud_log_level(ESP_LOG_INFO);
        }
        else if(strstr((char *)g_bdsc_engine->current_asr_words,"打开云端警告日志") != NULL)
        {
            ESP_LOGI(TAG, "set level 打开云端警告日志");
            set_cloud_log_level(ESP_LOG_WARN);
        }
        else if(strstr((char *)g_bdsc_engine->current_asr_words,"打开云端错误日志") != NULL)
        {
            ESP_LOGI(TAG, "set level 打开云端错误日志");
            set_cloud_log_level(ESP_LOG_ERROR);
        }
        else if(strstr((char *)g_bdsc_engine->current_asr_words,"关闭云端日志") != NULL)
        {
            ESP_LOGI(TAG, "set level 关闭云端日志");
            set_cloud_log_level(ESP_LOG_NONE);
        }
#endif
        return BDSC_CUSTOM_DESIRE_DEFAULT;
    case BDSC_EVENT_ON_NLP_RESULT:
        ESP_LOGI(TAG, "==> Got BDSC_EVENT_ON_NLP_RESULT");
        /*
         * 用户通过 “唤醒词 + 命令” 进行交互时，云端返回的NLP结果。默认情况下，该结果会随着ASR结果一起下发。
         * 返回的 evt 类型如下：
         * evt->data     为 nlp 结果 json 字符串
         * evt->data_len 为 nlp 结果 json 字符串长度
         * evt->client   为 全局 bdsc_engine_handle_t 实例对象
         *
         * 返回的 nlp 数据为 JSON 格式。格式如下：
         * 以下以电视控制技能为例（比如，“小度小度，湖南卫视”），返回：
         *
         {
            "error_code":0,
            "tts":{
                "text":"好的~",
                "param":"{\"pdt\":1,\"key\":\"com.baidu.asrkey\",\"lan\":\"ZH\",\"aue\":3}"
                },
            "content":"{\"action_type\":\"nlp_tts\",\"query\":[\"湖南卫视\"],\"intent\":\"TV_ACTION\",\"slots\":[{\"name\":\"user_channel\",\"value\":\"000032\"}],\"custom_reply\":[]}"
        }
         * 各字段格式如下：
         * - error_code ： 错误码，正确返回0
         * - tts        ： tts音频文本数据，流格式参数
         * - content    ： NLP结果字符串，json格式
         * content字符串字段格式：
         * - action_type ： action_type
         * - query       ： query 文本
         * - intent      ： NLP intent类型，这里是电视控制动作
         *                  关于如何配置NLP机器人所有用的技能，请参考相关文档。
         * - slots       ： NLP 词槽，这里包含了对应电视频道相关的信息，端上可以根据该信息进行电视切换动作。
         * - custom_reply： 空
         *
         *
         * TIPS: 随 NLP 一起下发的TTS语音流，由SDK自动播放，暂时不对用户开放。
         */

        
        cJSON *j_content = NULL;
        char *origin_str = NULL;
        /* 某些情况下，需要跳过当前会话 */
        if (need_skip_current_playing()) {
            ESP_LOGI(TAG, "skip playing");
            return BDSC_CUSTOM_DESIRE_SKIP_DEFAULT;
        }
        if (!(j_content = BdsJsonParse((const char *)evt->data))) {
            ESP_LOGE(TAG, "json format error");
            return BDSC_CUSTOM_DESIRE_SKIP_DEFAULT;
        }
#if CONFIG_CUPID_BOARD_V2
        /* 过滤 tv_action 字段，进入全双工模式 */
        if ((origin_str = BdsJsonObjectGetString(j_content, "origin")) &&
            (!strncmp(origin_str, "1016074", strlen("1016074"))) &&
            g_bdsc_engine->duplex_mode_function_enable) {
            if (g_bdsc_engine->in_duplex_mode == 0) {
                ESP_LOGI(TAG, "catch tv_action! ENTER duplex mode!");
                g_bdsc_engine->in_duplex_mode = 1;
                if (xTimerStart(g_bdsc_engine->duplex_timer, 10 / portTICK_PERIOD_MS) != pdPASS) {
                    ESP_LOGE(TAG, "DUPLEX timer can not start and we must do something!");
                }
                display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_VOLUMN_UP, 0);
                if ((resp_str = create_duplex_mode_status_string(1))) {
                    bdsc_engine_channel_data_upload((uint8_t *)resp_str, strlen(resp_str) + 1);
                    free(resp_str);
                    display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_MUTE_ON, 0);
                }
            } else {
                ESP_LOGI(TAG, "already in duplex mode!");
                if (xTimerStop(g_bdsc_engine->duplex_timer, 10 / portTICK_PERIOD_MS) != pdPASS) {
                    ESP_LOGE(TAG, "Can not delete duplex timer, must do something!");
                }
                if (xTimerStart(g_bdsc_engine->duplex_timer, 10 / portTICK_PERIOD_MS) != pdPASS) {
                    ESP_LOGE(TAG, "DUPLEX timer can not start and we must do something!");
                }
                display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_VOLUMN_UP, 0);
            }
            BdsJsonPut(j_content);
            return BDSC_CUSTOM_DESIRE_DEFAULT;
        } 
		else 
#endif
		{
            if (g_bdsc_engine->in_duplex_mode) {
                /* 在duplex_mode中，过滤掉所有“非电视控制技能”的tts语音，以及url(如果有的话) */
                bdsc_engine_skip_current_session_playing_once_flag_set(evt->client);
            }

            app_voice_control_feed_data(j_content, NULL);
        }

        BdsJsonPut(j_content);
        return BDSC_CUSTOM_DESIRE_DEFAULT;

    case BDSC_EVENT_ON_CHANNEL_DATA:
        ESP_LOGI(TAG, "==> Got BDSC_EVENT_ON_CHANNEL_DATA");
        /*
         * 除了语音链路提供 “唤醒词 + 命令”进行交互外。还提供了用户第三方数据通道。用户可以推送任意文本数据到设备。
         * 设备从该回调中获取数据。返回的 evt 类型如下：
         *
         * evt->data     为 本数据指针
         * evt->data_len 为 本数据长度
         * evt->client   为 全局 bdsc_engine_handle_t 实例对象
         *
         * 关于 用户数据推送 的使用，请参考相关文档。
         */
        cupid_device_manager_feed_data(&g_dm, evt->data, evt->data_len, NULL);
        return BDSC_CUSTOM_DESIRE_DEFAULT;
    default:
        return BDSC_CUSTOM_DESIRE_DEFAULT;
    }

    return BDSC_CUSTOM_DESIRE_DEFAULT;
}

static void duplex_timer_cb(TimerHandle_t xTimer)
{
    char *resp_str = NULL;
    ESP_LOGW(TAG, "DUPLEX timeout expired! STOP timer");
    if (xTimerStop(g_bdsc_engine->duplex_timer, 10 / portTICK_PERIOD_MS) != pdPASS) {
        ESP_LOGE(TAG, "Can not delete duplex timer, must do something!");
    }
    g_bdsc_engine->in_duplex_mode = 0;

    if ((resp_str = create_duplex_mode_status_string(0))) {
        bdsc_engine_channel_data_upload((uint8_t *)resp_str, strlen(resp_str) + 1);
        free(resp_str);
        display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_MUTE_OFF, 0);
    }
    bdsc_engine_skip_current_session_playing_once_flag_set(g_bdsc_engine);
}

extern int app_music_init(void);
void app_main(void)
{
    esp_err_t ret  = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    tcpip_adapter_init();

    bdsc_partitions_init();
    app_init();

    bdsc_engine_handle_t client;
    bdsc_engine_config_t cfg = {
        .log_level = 2,
        .bdsc_host = "leetest.baidu.com",
        .bdsc_port = 443,
        .bdsc_methods = BDSC_METHODS_DEFAULT,
        .auth_server = "smarthome.baidubce.com",
        .auth_port = 443,
        .transport_type = BDSC_TRANSPORT_OVER_TCP,
        .event_handler = my_bdsc_engine_event_handler,
    };
    client = bdsc_engine_init(&cfg);
#if CONFIG_CUPID_BOARD_V2
    cupid_device_manager_init(&g_dm);
#endif
    // duplex mode init
    client->duplex_timer = xTimerCreate("DUPLEX_TIMER", (60000 / portTICK_PERIOD_MS),
                    pdFALSE, NULL, duplex_timer_cb);

    client->duplex_mode_function_enable = false;

    // load asr block words when statup
    char block_word[256];
    int buf_len = 256;
    if (custom_key_op_safe(CUSTOM_KEY_GET, CUSTOM_KEY_TYPE_STRING, NVS_DEVICE_CUSTOM_NAMESPACE, "asr_block_word", block_word, &buf_len)) {
        ESP_LOGE(TAG, "load asr_block_words fail");
    } else {
        client->asr_block_words = audio_strdup(block_word);
    }
#ifdef CONFIG_CLOUD_LOG
    cloud_log_cfg_t log_cfg = {
        .type  = LOG_CHANNEL_TYPE_HTTPS,
        .level = CLOUD_LOG_WARN,
    };
    app_cloud_log_task_init(&log_cfg);
#endif
#ifdef CONFIG_ENABLE_MUSIC_UNIT
    app_music_init();
#endif
    start_sys_monitor();
    //esp_log_level_set("*", ESP_LOG_NONE);   // Default print off
}
