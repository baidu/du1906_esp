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

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>
#include "esp_sntp.h"
#include "esp_log.h"
#include <nvs.h>
#include "bdsc_tools.h"
#include "mbedtls/md5.h"
#include "audio_mem.h"
#include "bdsc_profile.h"

#define     TAG     "BDSC_TOOLS"


/*
 * 需要获取时间戳前，必须进行sntp初始化
 */
int SNTP_init(void)
{
    if (sntp_enabled()){
        sntp_stop();
    }
    ESP_LOGI(TAG, "------------Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "ntp01.baidu.com");	//设置访问服务器	中国提供商

    sntp_init();
    setenv("TZ", "GMT-8", 1);
    tzset();
	return 0;
}

/*
 * stop sntp
 */
void SNTP_stop(void)
{
    return sntp_stop();
}

/*
 * 返回当前时间戳 us
 */
unsigned long long get_current_time()
{
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    unsigned long long time = current_time.tv_sec;
    time = time * 1000000 + current_time.tv_usec;
    return time;
}

/*
 * 生成随机uuid
 */
int generate_uuid(char* uuid_out)
{
    const char *c = "89ab";
    char buf[37];
    char *p = buf;
    int n = 0;
    srand((unsigned)get_current_time() * 16);
    for (n = 0; n < 16; ++n)
    {
        int b = rand() % 255;
        switch(n)
        {
            case 6:
                sprintf(p, "4%x", b % 15);
                break;
            case 8:
                sprintf(p, "%c%x", c[rand() % strlen(c)], b % 15);
                break;
            default:
                sprintf(p, "%02x", b);
                break;
        }

        p += 2;
        switch(n)
        {
            case 3:
            case 5:
            case 7:
            case 9:
                *p++ = '-';
                break;
        }
    }
    *p = 0;

    strcpy(uuid_out, buf);
    return strlen(buf);
}


/*
 * hexString(小写) 转 十进制数组
 */
void hex_to_decimal(const char *hex_string, unsigned char *arr, size_t arr_len)
{
    const char *pos = hex_string;
    int count;

    for (count = 0; count < arr_len; count++) {
        sscanf(pos, "%2hhx", &arr[count]);
        pos += 2;
    }
}

/*
 * 十进制数组 转 hexString (小写)
 */
void decimal_to_hex(unsigned char *arr, size_t arr_len, char *hex_string)
{
    char *pos = hex_string;
    int count;

    for (count = 0; count < arr_len; count++) {
        sprintf(pos, "%02hhx", arr[count]);
        pos += 2;
    }
    *pos = '\0';
}


char* bdsc_strnstr(const char *buffer, const char *token, size_t n)
{
    const char *p;
    size_t tokenlen = strlen(token);
    if (tokenlen == 0) {
        return NULL;
    }
    for (p = buffer; *p && (p + tokenlen <= buffer + n); p++) {
        if ((*p == *token) && (strncmp(p, token, tokenlen) == 0)) {
            return (char*)p;
        }
    }
    return NULL;
}


esp_err_t bdsc_get_sn(char* outSn, size_t* length) 
{
    nvs_handle deviceNvsHandle;

    esp_err_t ret = nvs_open(NVS_DEVICE_SN_NAMESPACE, NVS_READONLY, &deviceNvsHandle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_get_str(deviceNvsHandle, NVS_DEVICE_SN_KEY, outSn, length);

    nvs_close(deviceNvsHandle);

    return ret;
}

char* generate_md5_checksum_needfree(uint8_t *buf, size_t buf_len)
{
    char sig[64];
    mbedtls_md5_context md5_ctx;
    unsigned char md5_cur[16];
    mbedtls_md5_init(&md5_ctx);

    if (mbedtls_md5_starts_ret(&md5_ctx)) {
        ESP_LOGE(TAG, "mbedtls_md5_starts_ret() error");
        mbedtls_md5_free(&md5_ctx);
        return NULL;
    }
    if (mbedtls_md5_update_ret(&md5_ctx, (const unsigned char *)buf, buf_len)) {
        ESP_LOGE(TAG, "mbedtls_md5_update_ret() failed");
        mbedtls_md5_free(&md5_ctx);
        return NULL;
    }
    if (mbedtls_md5_finish_ret(&md5_ctx, md5_cur)) {
        ESP_LOGE(TAG, "mbedtls_md5_finish_ret() error");
        mbedtls_md5_free(&md5_ctx);
        return NULL;
    }
    decimal_to_hex(md5_cur, sizeof(md5_cur), sig);
    mbedtls_md5_free(&md5_ctx);

    return audio_strdup(sig);
}

int get_current_valid_ts()
{
    static int utc_ts = 1597825065;
    struct timeval current_time;
    
    gettimeofday(&current_time, NULL);
    ESP_LOGI(TAG, "current ts: %ld\n", current_time.tv_sec);
    if (current_time.tv_sec < utc_ts) {
        ESP_LOGE(TAG, "NTP server not synced yet!!!");
        SNTP_init();
        return -1;
    }
    utc_ts = current_time.tv_sec;
    return utc_ts;
}

int get_trannum_up()
{
    static int ts = 0;
    int cts = 0;

    if ((cts = get_current_valid_ts()) < 0 || cts == ts) {
        srand((unsigned)get_current_time() * 16);
        return rand();
    }
    ts = cts;
    return ts;
}
