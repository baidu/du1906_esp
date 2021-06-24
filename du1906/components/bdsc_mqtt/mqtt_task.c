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
#include <string.h>

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
#include "mqtt_client.h"
#include "bdsc_event_dispatcher.h"
#include "bdsc_engine.h"
#include "audio_thread.h"
#include "app_task_register.h"

#define TAG "MQTT_TASK"

#define  MQTT_SUB_TOPIC     "$iot/%s/user/%s/%s/%s/down"
#define  MQTT_PUB_TOPIC     "$iot/%s/user/%s/%s/%s/up"

#define  MQTT_CLIENT_STACK_SIZE (3 * 1024)
#define  IS_MQTT_WITH_TLS 0
extern const uint8_t mqtt_server_root_cert_pem_start[] asm("_binary_mqtt_server_root_cert_pem_start");
int notify_bdsc_engine_event_to_user(int event, uint8_t *data, size_t data_len);

int bdsc_engine_channel_data_upload(uint8_t *upload_data, size_t upload_data_len)
{
    if (g_bdsc_engine->g_mqtt_client) {
        int msg_id = esp_mqtt_client_publish(g_bdsc_engine->g_mqtt_client, g_bdsc_engine->g_pub_topic, (const char *)upload_data, upload_data_len, 1, 0);
        if (msg_id > 0) {
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        }
        return 0;
    }
    return -1;
}

char *generate_iot_device_status_info(int err_num, bool is_active);
int _return_ota_error(int errcode);

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    char online_msg[64];
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            if (!g_bdsc_engine->g_has_greeted) {
                g_bdsc_engine->g_has_greeted = true;
                snprintf(online_msg, sizeof(online_msg), "{\"trannum_up\":%d, \"type\": \"online\"}", get_trannum_up());
                msg_id = esp_mqtt_client_publish(client, g_bdsc_engine->g_pub_topic, (const char *)online_msg, strlen(online_msg) + 1, 1, 0);
                ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

                // first boot from ota need ack server
                if (g_bdsc_engine->boot_from_ota) {
                    _return_ota_error(0);
                }
            }

            msg_id = esp_mqtt_client_subscribe(client, g_bdsc_engine->g_sub_topic, 1);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            ESP_LOGI(TAG, "TOPIC=%.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "DATA=%.*s", event->data_len, event->data);
            notify_bdsc_engine_event_to_user(BDSC_EVENT_ON_CHANNEL_DATA, (uint8_t*)event->data, event->data_len);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

static void generate_mqtt_topics()
{
    vendor_info_t *g_vendor_info = g_bdsc_engine->g_vendor_info;
    if (strlen(g_vendor_info->mqtt_cid) + strlen(g_vendor_info->fc) +
        strlen(g_vendor_info->pk) + strlen(g_vendor_info->ak) + 18 >= sizeof(g_bdsc_engine->g_sub_topic)) {

        ESP_LOGE(TAG, "topics too long");
        return;
    }
    sprintf(g_bdsc_engine->g_sub_topic, MQTT_SUB_TOPIC, g_vendor_info->mqtt_cid, g_vendor_info->fc, g_vendor_info->pk, g_vendor_info->ak);
    sprintf(g_bdsc_engine->g_pub_topic, MQTT_PUB_TOPIC, g_vendor_info->mqtt_cid, g_vendor_info->fc, g_vendor_info->pk, g_vendor_info->ak);
}

static void mqtt_app_start(void)
{
    vendor_info_t *g_vendor_info = g_bdsc_engine->g_vendor_info;
    generate_mqtt_topics();
    ESP_LOGI(TAG, "mqtt_app_start ==> \n clientid: %s, sub: %s, pub: %s", 
                    g_vendor_info->mqtt_cid, g_bdsc_engine->g_sub_topic, g_bdsc_engine->g_pub_topic);
#if IS_MQTT_WITH_TLS       //add tls for mqtt
    esp_mqtt_client_config_t mqtt_cfg = {
        .host           = g_vendor_info->mqtt_broker,
        .port           = 1884,
        .cert_pem       = (const char *)mqtt_server_root_cert_pem_start,
        .transport      = MQTT_TRANSPORT_OVER_SSL,
        .username       = g_vendor_info->mqtt_username,
        .password       = g_vendor_info->mqtt_password,
        .event_handle   = mqtt_event_handler,
        .client_id      = g_vendor_info->mqtt_cid,
        .task_stack     = MQTT_CLIENT_STACK_SIZE
    };
#else
    esp_mqtt_client_config_t mqtt_cfg = {
        .host           = g_vendor_info->mqtt_broker,
        .port           = 1883,
        .transport      = MQTT_TRANSPORT_OVER_TCP,
        .username       = g_vendor_info->mqtt_username,
        .password       = g_vendor_info->mqtt_password,
        .event_handle   = mqtt_event_handler,
        // .user_context = (void *)your_context
        .client_id      = g_vendor_info->mqtt_cid,
        .task_stack     = MQTT_CLIENT_STACK_SIZE
    };
#endif

    g_bdsc_engine->g_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(g_bdsc_engine->g_mqtt_client);
}



static void mqtt_task(void *pvParameters)
{
    mqtt_app_start();

    while (1) {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "Stack: %d", uxTaskGetStackHighWaterMark(NULL));
    }

    vTaskDelete(NULL);
}

int mqtt_task_init() 
{
    audio_thread_t mqtt_task_handle = NULL;
    int ret = app_task_regist(APP_TASK_ID_IOT_MQTT, mqtt_task, NULL, NULL); 
    if (ret == ESP_FAIL) {
        ESP_LOGE(TAG, "Couldn't create mqtt_task");
    }

    return 0;
}
