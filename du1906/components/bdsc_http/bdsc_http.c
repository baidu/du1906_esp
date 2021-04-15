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
#include "esp_http_client.h"
#include "esp_tls.h"
#include "bdsc_engine.h"

#include "mbedtls/platform.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/esp_debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"
#include "mbedtls/debug.h"

#define    TAG     "HTTP_TASK"

static esp_err_t _http_event_handle(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER");
            printf("%.*s", evt->data_len, (char*)evt->data);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d, %s", evt->data_len, (char*)evt->data);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                printf("%.*s", evt->data_len, (char*)evt->data);
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

int bdsc_send_http_post(char *url, char *post_data, size_t data_len)
{
    if (!url || !post_data || data_len <= 0)  {
        return -1;
    }
    
    esp_http_client_config_t config = {
    .url = url,
    .method = HTTP_METHOD_POST,
    .event_handler = _http_event_handle,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_post_field(client, post_data, data_len);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
    ESP_LOGI(TAG, "Status = %d, content_length = %d",
            esp_http_client_get_status_code(client),
            esp_http_client_get_content_length(client));
    }
    esp_http_client_cleanup(client);

    return 0;
}

int bdsc_send_https_post_sync(char *server, int port, 
                                char *cacert_pem_buf, size_t cacert_pem_bytes, 
                                char *post_data_in, size_t data_in_len, 
                                char **ret_data_out, size_t *data_out_len)
{
    char buf[512];
    int ret = 0, len = 0;
    uint8_t *ret_buf = NULL;
    uint8_t *tmp_buf = NULL;
    int tmp_buf_len = 1024;
    int cnt = 0;
    
    tmp_buf = audio_malloc(tmp_buf_len);
    assert(tmp_buf != NULL);
    bzero(tmp_buf, tmp_buf_len);

    esp_tls_cfg_t cfg = {
        .cacert_pem_buf  = (const unsigned char*)cacert_pem_buf,
        .cacert_pem_bytes = (unsigned int)cacert_pem_bytes,
    };
    
    struct esp_tls *tls = esp_tls_conn_new(server, strlen(server), port, &cfg);
    
    if(tls != NULL) {
        ESP_LOGI(TAG, "Connection established...");
    } else {
        ERR_OUT(exit, "Connection failed...");
    }
    
    size_t written_bytes = 0;
    do {
        ret = esp_tls_conn_write(tls, 
                                    post_data_in + written_bytes, 
                                    data_in_len - written_bytes);
        if (ret >= 0) {
            ESP_LOGI(TAG, "%d bytes written", ret);
            written_bytes += ret;
        } else if (ret != MBEDTLS_ERR_SSL_WANT_READ  && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            ERR_OUT(exit, "esp_tls_conn_write  returned 0x%x", ret);
        }
    } while(written_bytes < data_in_len);

    ESP_LOGI(TAG, "Reading HTTP response...");

    
    do
    {
        len = sizeof(buf) - 1;
        bzero(buf, sizeof(buf));
        ret = esp_tls_conn_read(tls, (char *)buf, len);
        
        if(ret == MBEDTLS_ERR_SSL_WANT_WRITE  || ret == MBEDTLS_ERR_SSL_WANT_READ)
            continue;
        
        if(ret < 0)
        {
            ESP_LOGE(TAG, "esp_tls_conn_read  returned -0x%x", -ret);
            break;
        }

        if(ret == 0)
        {
            ESP_LOGI(TAG, "connection closed");
            break;
        }

        len = ret;
        ESP_LOGI(TAG, "%d bytes read", len);
        // Print response directly to stdout as it is read 
        buf[len] = '\0';
        ESP_LOGI(TAG, "%s",buf);

        if (cnt + len > tmp_buf_len) {
            ESP_LOGE(TAG, "data too long");
            break;
        }
        memcpy(tmp_buf + cnt, buf, len);
        cnt += len;
    } while(1);

exit:
    esp_tls_conn_delete(tls);
    
    char *body;
    // find body
    if (!(body = bdsc_strnstr((const char *)tmp_buf, "\r\n\r\n", tmp_buf_len))) {
        ESP_LOGE(TAG, "not find body");
        return -1;
    }
    body += 4;
    ret_buf = audio_calloc(1, cnt + 1);
    assert(ret_buf != NULL);
    memcpy(ret_buf, body, strlen(body));
    *ret_data_out = (char*)ret_buf;
    *data_out_len = strlen(body);
    free(tmp_buf);

    return ret;
}

int net_would_block(const mbedtls_net_context *ctx, int *errout);

#define LOG_UPLOAD_TIMEOUT          (10*1000)
#define LOG_UPLOAD_SELECT_TIMEOUT   (5*1000)
int64_t g_log_upload_start_timestamp = 0;
int64_t sys_get_time_ms(void);

/*
 * Read at most 'len' characters, blocking for at most 'timeout' ms
 */
int my_mbedtls_net_recv_timeout(void *ctx, unsigned char *buf, size_t len)
{
    printf("<==s %llu\n", sys_get_time_ms());
    int ret = -1;
    struct timeval tv;
    fd_set read_fds;
    uint32_t timeout = LOG_UPLOAD_SELECT_TIMEOUT;
    int fd = ((mbedtls_net_context *) ctx)->fd;

    if (fd < 0) {
        return (MBEDTLS_ERR_NET_INVALID_CONTEXT);
    }

    if (sys_get_time_ms() - g_log_upload_start_timestamp > LOG_UPLOAD_TIMEOUT) {
        printf("++++++rcv timeout! quit!\n");
        return (MBEDTLS_ERR_SSL_TIMEOUT);
    }
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);

    tv.tv_sec  = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;

    ret = select(fd + 1, &read_fds, NULL, NULL, timeout == 0 ? NULL : &tv);

    /* Zero fds ready means we timed out */
    if (ret == 0) {
        return (MBEDTLS_ERR_SSL_TIMEOUT);
    }

    if (ret < 0) {
        if (errno == EINTR) {
            return (MBEDTLS_ERR_SSL_WANT_READ);
        }

        return (MBEDTLS_ERR_NET_RECV_FAILED);
    }
    printf("<==e %llu\n", sys_get_time_ms());
    /* This call will not block */
    return (mbedtls_net_recv(ctx, buf, len));
}

/*
 * Write at most 'len' characters
 */
int my_mbedtls_net_send_timeout(void *ctx, const unsigned char *buf, size_t len)
{
    printf("==>s %llu\n", sys_get_time_ms());
    int ret = -1;
    struct timeval tv;
    fd_set write_fds;
    uint32_t timeout = LOG_UPLOAD_SELECT_TIMEOUT;
    int fd = ((mbedtls_net_context *) ctx)->fd;

    int error = 0;

    if (fd < 0) {
        return (MBEDTLS_ERR_NET_INVALID_CONTEXT);
    }

    if (sys_get_time_ms() - g_log_upload_start_timestamp > LOG_UPLOAD_TIMEOUT) {
        printf("++++++snd timeout! quit!\n");
        return (MBEDTLS_ERR_SSL_TIMEOUT);
    }
    FD_ZERO(&write_fds);
    FD_SET(fd, &write_fds);

    tv.tv_sec  = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;

    ret = select(fd + 1, NULL, &write_fds, NULL, timeout == 0 ? NULL : &tv);

    /* Zero fds ready means we timed out */
    if (ret == 0) {
        return (MBEDTLS_ERR_SSL_TIMEOUT);
    }

    if (ret < 0) {
        if (errno == EINTR) {
            return (MBEDTLS_ERR_SSL_WANT_READ);
        }

        return (MBEDTLS_ERR_NET_RECV_FAILED);
    }

    ret = (int) write(fd, buf, len);
    printf("==>e %llu\n", sys_get_time_ms());
    if (ret < 0) {
        if (net_would_block(ctx, &error) != 0) {
            ((mbedtls_net_context *) ctx)->err_num = error;
            return (MBEDTLS_ERR_SSL_WANT_WRITE);
        }
        ((mbedtls_net_context *) ctx)->err_num = error;

        if (error == EPIPE || error == ECONNRESET) {
            return (MBEDTLS_ERR_NET_CONN_RESET);
        }

        if (error == EINTR) {
            return (MBEDTLS_ERR_SSL_WANT_WRITE);
        }

        return (MBEDTLS_ERR_NET_SEND_FAILED);
    }

    return (ret);
}

int64_t sys_get_time_ms(void)
{
    struct timeval te;
    gettimeofday(&te, NULL);
    int64_t milliseconds = te.tv_sec * 1000LL + te.tv_usec / 1000;
    return milliseconds;
}

mbedtls_entropy_context entropy;
mbedtls_ctr_drbg_context ctr_drbg;
mbedtls_ssl_context ssl;
mbedtls_x509_crt cacert;
mbedtls_ssl_config conf;
mbedtls_net_context server_fd;
mbedtls_ssl_session saved_session;
int has_saved_session = 0;
static bool g_tls_init_once = true;

int bdsc_send_https_log_sync(char *server, int port, 
                                char *cacert_pem_buf, size_t cacert_pem_bytes, 
                                char *post_data_in, size_t data_in_len, 
                                char **ret_data_out, size_t *data_out_len, int timeout)
{
    char buf[512] = {0};
    int ret = 0, flags = 0, len = 0;
    char port_str[16] = {0};

    g_log_upload_start_timestamp = sys_get_time_ms();
    if (g_tls_init_once) {
        memset(&saved_session, 0, sizeof(mbedtls_ssl_session));
        mbedtls_ssl_init(&ssl);
        mbedtls_x509_crt_init(&cacert);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        ESP_LOGI(TAG, "Seeding the random number generator");

        mbedtls_ssl_conf_session_tickets(&conf, MBEDTLS_SSL_SESSION_TICKETS_ENABLED);
        mbedtls_ssl_conf_renegotiation(&conf, MBEDTLS_SSL_RENEGOTIATION_ENABLED);
        mbedtls_ssl_config_init(&conf);

        mbedtls_entropy_init(&entropy);
        if((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                        NULL, 0)) != 0) {
            ERR_OUT(err_free_ssl, "mbedtls_ctr_drbg_seed returned %d", ret);
        }

        ESP_LOGI(TAG, "Loading the CA root certificate...");
        ret = mbedtls_x509_crt_parse(&cacert, (const unsigned char *)cacert_pem_buf,
                                    cacert_pem_bytes);

        if(ret < 0) {
            ERR_OUT(err_free_ssl, "mbedtls_x509_crt_parse returned -0x%x\n\n", -ret);
        }

        ESP_LOGI(TAG, "Setting hostname for TLS session...");

        /* Hostname set here should match CN in server certificate */
        if((ret = mbedtls_ssl_set_hostname(&ssl, server)) != 0) {
            ERR_OUT(err_free_crt, "mbedtls_ssl_set_hostname returned -0x%x", -ret);
        }

        ESP_LOGI(TAG, "Setting up the SSL/TLS structure...");

        if((ret = mbedtls_ssl_config_defaults(&conf,
                                            MBEDTLS_SSL_IS_CLIENT,
                                            MBEDTLS_SSL_TRANSPORT_STREAM,
                                            MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
            ERR_OUT(err_free_crt, "mbedtls_ssl_config_defaults returned %d", ret);
        }

        /* MBEDTLS_SSL_VERIFY_OPTIONAL is bad for security, in this example it will print
        a warning if CA verification fails but it will continue to connect.

        You should consider using MBEDTLS_SSL_VERIFY_REQUIRED in your own code.
        */
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
        mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
        mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
#ifdef CONFIG_MBEDTLS_DEBUG
        mbedtls_esp_enable_debug_log(&conf, 4);
#endif

        if ((ret = mbedtls_ssl_setup(&ssl, &conf)) != 0) {
            ERR_OUT(err_free_crt, "mbedtls_ssl_setup returned -0x%x\n\n", -ret);
        }

        g_tls_init_once = false;
    }

    mbedtls_net_init(&server_fd);

    snprintf(port_str, sizeof(port_str), "%d", port);
    ESP_LOGI(TAG, "Connecting to %s:%s...", server, port_str);

    if ((ret = mbedtls_net_connect(&server_fd, server,
                                    port_str, MBEDTLS_NET_PROTO_TCP)) != 0) {
        ERR_OUT(err_free_fd, "mbedtls_net_connect returned -%x", -ret);
    }

    ESP_LOGI(TAG, "Connected.");

    mbedtls_net_set_nonblock(&server_fd);

    mbedtls_ssl_set_bio(&ssl, &server_fd, my_mbedtls_net_send_timeout, my_mbedtls_net_recv_timeout, NULL);

    ESP_LOGI(TAG, "Performing the SSL/TLS handshake...");

    if (has_saved_session) {
            if ((ret = mbedtls_ssl_set_session(&ssl, &saved_session)) != 0) {
            ESP_LOGE(TAG, " failed\n  ! mbedtls_ssl_conf_session returned %d", ret);
            }
    }
    while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
        if (sys_get_time_ms() - g_log_upload_start_timestamp > timeout) {
            ERR_OUT(err_free_fd, "++++++blocking1! quit now!\n");
        }
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            ERR_OUT(err_free_fd, "mbedtls_ssl_handshake returned -0x%x", -ret);
        }
    }
    ESP_LOGI(TAG, "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");
    ESP_LOGI(TAG, "+++++++++++++++ handshake elapsed: %llu ms +++++++++++++++++++", \
        sys_get_time_ms() - g_log_upload_start_timestamp);
    ESP_LOGI(TAG, "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");
    mbedtls_ssl_get_session(&ssl, &saved_session);
    has_saved_session = 1;

    ESP_LOGI(TAG, "Verifying peer X.509 certificate...");

    if ((flags = mbedtls_ssl_get_verify_result(&ssl)) != 0) {
        /* In real life, we probably want to close connection if ret != 0 */
        ESP_LOGW(TAG, "Failed to verify peer certificate!");
        bzero(buf, sizeof(buf));
        mbedtls_x509_crt_verify_info(buf, sizeof(buf), "  ! ", flags);
        ESP_LOGW(TAG, "verification info: %s", buf);
    }
    else {
        ESP_LOGI(TAG, "Certificate verified.");
    }

    ESP_LOGI(TAG, "Cipher suite is %s", mbedtls_ssl_get_ciphersuite(&ssl));

    ESP_LOGI(TAG, "Writing HTTP request...");

    size_t written_bytes = 0;
    do {
        if (sys_get_time_ms() - g_log_upload_start_timestamp > timeout) {
            ERR_OUT(err_free_fd, "++++++blocking2! quit now!\n");
        }
        ret = mbedtls_ssl_write(&ssl,
                                (const unsigned char *)post_data_in + written_bytes,
                                strlen(post_data_in) - written_bytes);
        if (ret >= 0) {
            ESP_LOGI(TAG, "%d bytes written", ret);
            written_bytes += ret;
        } else if (ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != MBEDTLS_ERR_SSL_WANT_READ) {
            ERR_OUT(err_free_fd, "mbedtls_ssl_write returned -0x%x", -ret);
        }
    } while(written_bytes < strlen(post_data_in));

    ret = 0;
    vTaskDelay(500 / portTICK_PERIOD_MS);

    mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_session_reset(&ssl);
    mbedtls_net_free(&server_fd);

    if(ret != 0) {
        mbedtls_strerror(ret, buf, 100);
        ESP_LOGE(TAG, "Last error was: -0x%x - %s", -ret, buf);
    }

    
    *ret_data_out = NULL;
    *data_out_len = 0;
    return 0;


err_free_fd:
    mbedtls_net_free(&server_fd);
err_free_crt:
    mbedtls_x509_crt_free(&cacert);
err_free_ssl:
    mbedtls_entropy_free(&entropy);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_ssl_free(&ssl);
    g_tls_init_once = true;
    return -1;
}
