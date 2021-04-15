
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
#include "app_cloud_log.h"

#define TAG "cloud_LOG_TASK"

static char* pMqtt_topic = NULL;

int mqtt_channel_init(void)
{
    int ret = 0;
    pMqtt_topic = audio_calloc(1, 256);
    if(pMqtt_topic == NULL) {
        ret = -1;
    }
    sprintf(pMqtt_topic, "$iot/%s/user/%s/%s/%s/log", g_bdsc_engine->g_vendor_info->mqtt_cid,\
            g_bdsc_engine->g_vendor_info->fc,\
            g_bdsc_engine->g_vendor_info->pk,\
            g_bdsc_engine->g_vendor_info->ak);
    return ret;
}

int mqtt_send_log(char *msg,uint32_t len)
{
    if (g_bdsc_engine->g_mqtt_client && pMqtt_topic) {
        esp_mqtt_client_publish(g_bdsc_engine->g_mqtt_client, pMqtt_topic, (const char *)msg, len, 0, 0);
        return 0;
    }
    return -1;
}