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
#include <inttypes.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "freertos/event_groups.h"
#include "esp_event_loop.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "bds_client_command.h"
#include "bds_client_context.h"
#include "bds_client_event.h"
#include "bds_client_params.h"
#include "bds_client.h"
#include "bdsc_tools.h"
#include "bds_common_utility.h"
#include "raw_play_task.h"
#include "bdsc_profile.h"
#include "audio_mem.h"
#include "audio_error.h"

#include "esp_http_client.h"

#include "esp_tls.h"
#include "bdsc_engine.h"
#include "bdsc_http.h"
#include "generate_pam.h"
#include "auth_task.h"
#include "bdsc_json.h"
#include "audio_thread.h"
#include "app_task_register.h"

#define    TAG     "AUTH_TASK"

extern const uint8_t server_root_cert_pem_start[] asm("_binary_server_root_cert_pem_start");
extern const uint8_t server_root_cert_pem_end[]   asm("_binary_server_root_cert_pem_end");

static char *generate_auth_body_needfree()
{
    char *buf;
    char *body;
    char cl[10] = {0};
    size_t buffer_len = 512;

    if (!(buf = audio_calloc(1, buffer_len))) {
        return NULL;
    }
    if (!(body = audio_calloc(1, buffer_len))) {
        free(buf);
        return NULL;
    }
    strcat(buf, "POST /v1/manage/mqtt HTTP/1.0\r\n"
                "Host: smarthome.baidubce.com\r\n"
                "User-Agent: esp32\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: ");
    if (generate_auth_pam(body, buffer_len)) {
        ESP_LOGE(TAG, "generate_auth_pam fail");
        free(buf);
        free(body);
        return NULL;
    }

    itoa(strlen(body), cl, 10);
    strcat(buf, cl);
    strcat(buf, "\r\n\r\n");
    if (strlen(buf) + strlen(body) >= buffer_len) {
        ESP_LOGE(TAG, "body too long");
        free(body);
        return NULL;
    }
    strcat(buf, body);
    free(body);

    ESP_LOGI(TAG, "test request: %s", buf);
    return buf;
}


static void auth_task(void *pvParameters)
{
    char *ret_data_out = NULL;
    size_t data_out_len = 0;
    int err = 0;
    char *request_str;

    while (1) {
        if (!(request_str = generate_auth_body_needfree())) {
            ESP_LOGE(TAG, "generate_auth_body_needfree fail");
            vTaskDelay(3000 / portTICK_PERIOD_MS);
            continue;
        }
        err = bdsc_send_https_post_sync((char *)g_bdsc_engine->cfg->auth_server, g_bdsc_engine->cfg->auth_port, 
                                    (char *)server_root_cert_pem_start, server_root_cert_pem_end - server_root_cert_pem_start, 
                                    request_str, strlen(request_str) + 1, 
                                    &ret_data_out, &data_out_len);
        free(request_str);
        if (err) {
            ESP_LOGE(TAG, "auth failed!!");
        } else {
            ESP_LOGI(TAG, "recv body: %s", ret_data_out);
            if (bdsc_strnstr(ret_data_out, "err_msg", data_out_len)) {
                ERR_OUT(retry, "auth failed, retry!");
            } else {
                BdsJson *json;
                if (!(json = BdsJsonParse((const char *)ret_data_out))) {
                    ERR_OUT(retry, "http auth json format error");
                }

                auth_result_t res;
                memset(&res, 0, sizeof(res));
                if ((res.broker = BdsJsonObjectGetString(json, "broker")) &&
                    (res.mqtt_username = BdsJsonObjectGetString(json, "user")) &&
                    (res.mqtt_password = BdsJsonObjectGetString(json, "pass")) &&
                    (res.client_id = BdsJsonObjectGetString(json, "clientID"))) {

                    res.err = ESP_OK;
                    res.user_data = g_bdsc_engine;

                    if (g_bdsc_engine->g_auth_cb) {
                        g_bdsc_engine->g_auth_cb(&res);
                    }
                    
                    free(ret_data_out);
                    ret_data_out = NULL;
                    data_out_len = 0;
                    BdsJsonPut(json);
                    break;
                } else {
                    BdsJsonPut(json);
                    ERR_OUT(retry, "http auth json format error, missing some key?");
                }
                
            }
        }
retry:
        free(ret_data_out);
        ret_data_out = NULL;
        data_out_len = 0;
        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

int start_auth_thread(bdsc_auth_cb auth_cb) 
{
    int ret = app_task_regist(APP_TASK_ID_AUTH, auth_task, NULL, NULL);
    if (ret != pdPASS) {
        ERR_OUT(ERR_RET, "fail to create auth_task");
    }
    g_bdsc_engine->g_auth_cb = auth_cb;
    return 0;

ERR_RET:
    return -1;
}
