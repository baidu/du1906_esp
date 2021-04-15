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
#include <sys/unistd.h>
#include <sys/stat.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <strings.h>
#include "esp_log.h"
#include "errno.h"
#include "http_raw_stream.h"
#include "audio_mem.h"
#include "audio_element.h"
#include "esp_system.h"
#include "esp_http_client.h"
#include "bdsc_engine.h"
#include "bdsc_tools.h"
#include "app_ota_policy.h"

static const char *TAG = "HTTP_RAW_STREAM";
#define MAX_PLAYLIST_LINE_SIZE (512)
#define HTTP_STREAM_BUFFER_SIZE (2048)
#define HTTP_MAX_CONNECT_TIMES  (5)

typedef struct http_raw_stream {
    audio_stream_type_t             type;
    bool                            is_open;
    esp_http_client_handle_t        client;
    audio_stream_type_t             stream_type;
    void                            *user_data;
    int                             _errno;
} http_raw_stream_t;


static esp_err_t _http_event_handle(esp_http_client_event_t *evt)
{
    audio_element_handle_t el = (audio_element_handle_t)evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_HEADER) {
        return ESP_OK;
    }

    if (strcasecmp(evt->header_key, "Content-Type") == 0) {
        ESP_LOGD(TAG, "%s = %s", evt->header_key, evt->header_value);
    }

    return ESP_OK;
}

static esp_err_t _http_open(audio_element_handle_t self)
{
    ESP_LOGI(TAG, "===================== _http_open begin =====================");
    http_raw_stream_t *http = (http_raw_stream_t *)audio_element_getdata(self);
    esp_err_t err;
    char *uri = NULL;
    audio_element_info_t info;
    int delay = -1;

    http->_errno = 0;
    uri = audio_element_get_uri(self);
    if (uri == NULL) {
        ESP_LOGE(TAG, "Error open connection, uri = NULL");
        return ESP_FAIL;
    }
    audio_element_getinfo(self, &info);
    ESP_LOGD(TAG, "URI=%s", uri);
    // if not initialize http client, initial it
    if (http->client == NULL) {
        esp_http_client_config_t http_cfg = {
            .url = uri,
            .event_handler = _http_event_handle,
            .user_data = self,
            .timeout_ms = 5 * 1000,
            .buffer_size = HTTP_STREAM_BUFFER_SIZE,
        };
        http->client = esp_http_client_init(&http_cfg);
        AUDIO_MEM_CHECK(TAG, http->client, return ESP_ERR_NO_MEM);
    } else {
        esp_http_client_set_url(http->client, uri);
    }


    if (info.byte_pos) {
        char rang_header[32];
        snprintf(rang_header, 32, "bytes=%d-", (int)info.byte_pos);
        esp_http_client_set_header(http->client, "Range", rang_header);
    }

    ota_exponent_backoff_init(32);
try_again:
    delay = ota_exponent_backoff_get_next_delay();
    if (ESP_FAIL == ota_timeout_checkpoint(delay)) {
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "next delay: %d", delay);
    vTaskDelay(delay * 1000 / portTICK_PERIOD_MS);

    if (http->stream_type == AUDIO_STREAM_WRITER) {
        err = esp_http_client_open(http->client, -1);
        if (err == ESP_OK) {
            http->is_open = true;
        }
        goto try_again;
    }

    char *buffer = NULL;
    int post_len = esp_http_client_get_post_field(http->client, &buffer);
_stream_redirect:
    if ((err = esp_http_client_open(http->client, post_len)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open http stream");
        goto try_again;
    }

    int wrlen = 0;

    if (post_len && buffer && wrlen == 0) {
        if (esp_http_client_write(http->client, buffer, post_len) <= 0) {
            ESP_LOGE(TAG, "Failed to write data to http stream");
            goto try_again;
        }
        ESP_LOGD(TAG, "len=%d, data=%s", post_len, buffer);
    }

    /*
    * Due to the total byte of content has been changed after seek, set info.total_bytes at beginning only.
    */
    int64_t cur_pos = esp_http_client_fetch_headers(http->client);
    audio_element_getinfo(self, &info);
    if (info.byte_pos <= 0) {
        info.total_bytes = cur_pos;
    }

    ESP_LOGI(TAG, "total_bytes=%d", (int)info.total_bytes);
    int status_code = esp_http_client_get_status_code(http->client);
    if (status_code == 301 || status_code == 302) {
        esp_http_client_set_redirection(http->client);
        goto _stream_redirect;
    }
    if (status_code != 200
        && (esp_http_client_get_status_code(http->client) != 206)) {
        ESP_LOGE(TAG, "Invalid HTTP stream, status code = %d", status_code);
        goto try_again;
    }

    /**
     * `audio_element_setinfo` is risky affair.
     * It overwrites URI pointer as well. Pay attention to that!
     */
    audio_element_set_total_bytes(self, info.total_bytes);

    http->is_open = true;
    audio_element_report_codec_fmt(self);

    ESP_LOGI(TAG, "===================== _http_open end =====================");
    return ESP_OK;
}

static esp_err_t _http_close(audio_element_handle_t self)
{
    http_raw_stream_t *http = (http_raw_stream_t *)audio_element_getdata(self);
    ESP_LOGD(TAG, "_http_close");
    while (http->is_open) {
        http->is_open = false;
        if (http->stream_type != AUDIO_STREAM_WRITER) {
            break;
        }
        esp_http_client_fetch_headers(http->client);
    }
    if (http->client) {
        esp_http_client_close(http->client);
        esp_http_client_cleanup(http->client);
        http->client = NULL;
    }
    if (AEL_STATE_PAUSED != audio_element_get_state(self) && http->_errno == 0) {
        audio_element_report_pos(self);
        audio_element_set_byte_pos(self, 0);
    }
    return ESP_OK;
}

static int _http_read(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    http_raw_stream_t *http = (http_raw_stream_t *)audio_element_getdata(self);
    audio_element_info_t info;
    audio_element_getinfo(self, &info);
    int delay = -1;
    int rlen = 0;

    while (true) {
        rlen = esp_http_client_read(http->client, buffer, len);
        if (ESP_FAIL == ota_timeout_checkpoint(0)) {
            return ESP_FAIL;
        }

        if (rlen <= 0) {
            http->_errno = esp_http_client_get_errno(http->client);
            ESP_LOGW(TAG, "No more data,errno:%d, total_bytes:%llu, rlen = %d, xxxxxx", http->_errno, info.byte_pos, rlen);
            // Error occuered, reset connection
            if (http->_errno == 0) {
                ESP_LOGI(TAG, "server close the connection!!!");
            }
            ESP_LOGW(TAG, "Got %d errno(%s)", http->_errno, strerror(http->_errno));
            esp_err_t ret = ESP_OK;

            // close socket and reset http st
            esp_http_client_close(http->client);
            // _http_open will do loop connect until timeout
            ret = _http_open(self);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Fail to reset connection, ret = %d", ret);
                return ESP_FAIL;
            }
            continue;
        } else {
            audio_element_update_byte_pos(self, rlen);
        }
        ESP_LOGD(TAG, "req lengh=%d, read=%d, pos=%d/%d", len, rlen, (int)info.byte_pos, (int)info.total_bytes);
        return rlen;
    }
}

static int _http_write(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    http_raw_stream_t *http = (http_raw_stream_t *)audio_element_getdata(self);
    int wrlen = 0;
    if ((wrlen = esp_http_client_write(http->client, buffer, len)) <= 0) {
        http->_errno = esp_http_client_get_errno(http->client);
        ESP_LOGE(TAG, "Failed to write data to http stream, wrlen=%d, errno=%d(%s)", wrlen, http->_errno, strerror(http->_errno));
    }
    return wrlen;
}

#define HTTP_RECONNECT_RESET    104
static int _http_process(audio_element_handle_t self, char *in_buffer, int in_len)
{
    int r_size = audio_element_input(self, in_buffer, in_len);
    int w_size = 0;
    if (r_size > 0) {
        http_raw_stream_t *http = (http_raw_stream_t *)audio_element_getdata(self);
        if (http->_errno != 0) {
            ESP_LOGW(TAG, "reconnect to peer successful");
            w_size = HTTP_RECONNECT_RESET;
        } else {
            w_size = audio_element_output(self, in_buffer, r_size);
            audio_element_multi_output(self, in_buffer, r_size, 0);
        }
    } else {
        w_size = r_size;
    }
    return w_size;
}

static esp_err_t _http_destroy(audio_element_handle_t self)
{
    http_raw_stream_t *http = (http_raw_stream_t *)audio_element_getdata(self);
    audio_free(http);
    return ESP_OK;
}

audio_element_handle_t http_raw_stream_init(http_raw_stream_cfg_t *config)
{
    audio_element_handle_t el;
    http_raw_stream_t *http = audio_calloc(1, sizeof(http_raw_stream_t));

    AUDIO_MEM_CHECK(TAG, http, return NULL);

    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    cfg.open = _http_open;
    cfg.close = _http_close;
    cfg.process = _http_process;
    cfg.destroy = _http_destroy;
    cfg.task_stack = config->task_stack;
    cfg.task_prio = config->task_prio;
    cfg.task_core = config->task_core;
    cfg.stack_in_ext = config->stack_in_ext;
    cfg.out_rb_size = config->out_rb_size;
    cfg.tag = "http";

    http->type = config->type;
    http->stream_type = config->type;
    http->user_data = config->user_data;

    if (config->type == AUDIO_STREAM_READER) {
        cfg.read = _http_read;
    } else if (config->type == AUDIO_STREAM_WRITER) {
        cfg.write = _http_write;
    }

    el = audio_element_init(&cfg);
    AUDIO_MEM_CHECK(TAG, el, {
        audio_free(http);
        return NULL;
    });
    audio_element_setdata(el, http);
    return el;
}


