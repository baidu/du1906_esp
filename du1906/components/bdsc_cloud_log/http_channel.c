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

int http_channel_init(void)
{
    return 0;
}

int http_send_log(char *msg,uint32_t len)
{
    cJSON *http_data_json = NULL;
    char *http_data_json_string = NULL;
    if (!(http_data_json = cJSON_CreateObject())) {
        return -1;
    }
    cJSON_AddStringToObject(http_data_json, "log",msg);
    if (!(http_data_json_string = cJSON_PrintUnformatted(http_data_json))) {
        ESP_LOGE(TAG, "cJSON_PrintUnformatted fail");
        cJSON_Delete(http_data_json);
        return -1;
    }
    cJSON_Delete(http_data_json);
    bdsc_send_http_post("http://xx.xx.xx.xx:8081", (char *)http_data_json_string, strlen(http_data_json_string));
    free(http_data_json_string);
    http_data_json_string = NULL;
    return 0;
}
