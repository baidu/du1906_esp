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
#include "generate_pam.h"
#include "bds_private.h"

#define TAG "cloud_LOG_TASK"

#define LOG_UPLOAD_TIMEOUT  (10*1000)

extern const char server_root_cert_pem_start[] asm("_binary_server_root_cert_pem_start");
extern const char server_root_cert_pem_end[]   asm("_binary_server_root_cert_pem_end");

typedef struct {
    char     *server;
    int      port;
    char     *request_buff; 
    uint32_t buff_len;
    uint32_t pIndex;
} https_data_t;

static https_data_t *pHttps_data=NULL;

/*cjson can't parse control ascii*/
void cjson_escape_character(char* data,int len)
{
    for(int i=0; i<len; i++) {
        if ((data[i] > 0x7E) || (data[i] < 32) || (data[i] == '\"') || (data[i] == '\\')) {
            switch(data[i]) {
            case '\n':
                data[i++] = '\\';
                if(i<len) {
                    data[i] = 'n';
                }
                break;
            case '\r':
                data[i++] = '\\';
                if(i<len) {
                    data[i] = 'r';
                }
                break;
            default:
                data[i] = ' ';
                break;
            }
        }
    }
    data[len-1] = '\0';
}

static char *generate_https_log_body(char* log_data,int log_len)
{
    int content_length = 0;
    const char* sig = NULL;
    int ts = get_current_valid_ts()/60;
    cjson_escape_character(log_data,log_len);
    if(g_bdsc_engine == NULL || g_bdsc_engine->g_vendor_info == NULL) {
        ESP_LOGE(TAG, "g_bdsc_engine is NULL\n");
        return NULL;
    }
    sig = generate_auth_sig_needfree(g_bdsc_engine->g_vendor_info->ak, ts, g_bdsc_engine->g_vendor_info->sk);
    if(sig == NULL) {
        ESP_LOGE(TAG, "==> generate_auth_sig_needfree fail\n");
        return NULL;
    }
    content_length = strlen("{\"fc\":")+strlen(g_bdsc_engine->g_vendor_info->fc) +strlen("\"\"") +\
                     strlen(",\"pk\":")+strlen(g_bdsc_engine->g_vendor_info->pk) +strlen("\"\"") +\
                     strlen(",\"ak\":")+strlen(g_bdsc_engine->g_vendor_info->ak) +strlen("\"\"") +\
                     strlen(",\"log\":")+strlen(log_data) +strlen("\"\"") +\
                     strlen(",\"time_stamp\":")+ (int)log10(ts) + 1 +\
                     strlen(",\"signature\":")+strlen(sig) + strlen("\"\"") +\
                     strlen("}");

    snprintf(pHttps_data->request_buff, pHttps_data->buff_len,\
                "POST /xxx/xxx/xxx HTTP/1.0\r\n"
                "Host: xxx.xxx.xxx\r\n"
                "User-Agent: esp32\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: %d\r\n\r\n"
                "{\"fc\":\"%s\",\"pk\":\"%s\",\"ak\":\"%s\",\"log\":\"%s\",\"time_stamp\":%d,\"signature\":\"%s\"}",\
                    content_length,
                    g_bdsc_engine->g_vendor_info->fc,\
                    g_bdsc_engine->g_vendor_info->pk,\
                    g_bdsc_engine->g_vendor_info->ak,\
                    log_data,\
                    ts,\
                    sig);
    free((void*)(sig));
    pHttps_data->pIndex = strlen(pHttps_data->request_buff);
    return pHttps_data->request_buff;
}

int https_channel_init(void)
{
    int ret = 0;
    pHttps_data = audio_calloc(1, sizeof(https_data_t));
    if(pHttps_data == NULL) {
        ret = -1;
        ERR_OUT(https_data_fail, "audio_calloc fail");
    }
    pHttps_data->server = "xxx.xxx.xxx";
    pHttps_data->port = 443;
    pHttps_data->pIndex = 0;
    pHttps_data->buff_len = (g_cloud_log_ring_buf_sz + 20 * 1024);
    pHttps_data->request_buff = audio_calloc(1, pHttps_data->buff_len);
    if(pHttps_data->request_buff == NULL) {
        ret = -2;
        ERR_OUT(request_buff_fail, "audio_calloc fail");
    }
    return ret;

request_buff_fail:
    audio_free(pHttps_data);
    pHttps_data = NULL;
https_data_fail:
    return ret;   
}

int https_send_log(char *msg, uint32_t len)
{
    char* ret_data_out = NULL;
    char* request_str=NULL;
    uint32_t data_out_len = 0;

    if (!(request_str = generate_https_log_body(msg,len))) {
        ESP_LOGE(TAG, "generate_https_log_body fail");
        return -1;
    }
    bdsc_send_https_log_sync(pHttps_data->server, pHttps_data->port,\
                             (char *)server_root_cert_pem_start, server_root_cert_pem_end - server_root_cert_pem_start, \
                             request_str, pHttps_data->pIndex + 1,\
                             &ret_data_out, &data_out_len, LOG_UPLOAD_TIMEOUT);
    
    if(ret_data_out != NULL) {
        free(ret_data_out);
        ret_data_out=NULL;
    }
    return 0;
}



