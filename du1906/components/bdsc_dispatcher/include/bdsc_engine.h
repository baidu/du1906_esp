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
#ifndef __BDSC_ENGINE__H
#define __BDSC_ENGINE__H

#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include "esp_err.h"
#include "auth_task.h"
#include "esp_http_client.h"
#include "bdsc_cmd.h"
#include "generate_pam.h"
#include "bdsc_profile.h"
#include "auth_task.h"
#include "bdsc_event_dispatcher.h"
#include "mqtt_client.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_BDSC_ENGINE_BUF_SIZE (512)

typedef struct bdsc_engine *bdsc_engine_handle_t;
typedef struct bdsc_engine_event *bdsc_engine_event_handle_t;


/**
 * @brief   Bdsc Engine Supported Methods
 */
#define BDSC_METHODS_ASR        (1 << 0)
#define BDSC_METHODS_TTS        (1 << 1)
#define BDSC_METHODS_NLP        (1 << 2)
#define BDSC_METHODS_DEFAULT    (BDSC_METHODS_ASR | BDSC_METHODS_TTS | BDSC_METHODS_NLP)

/**
 * @brief   Bdsc Engine events id
 */
typedef enum {
    BDSC_EVENT_ERROR = 0,            /*!< Occurs when there are any errors */
    BDSC_EVENT_ON_WAKEUP,            /*!< Occurs when wakeup */
    BDSC_EVENT_ON_ASR_RESULT,        /*!< Occurs when receiving asr result from server */
    BDSC_EVENT_ON_NLP_RESULT,        /*!< Occurs when receiving nlp result from server */
    BDSC_EVENT_ON_CHANNEL_DATA,      /*!< Occurs when receiving mqtt data from server */
} bdsc_engine_event_id_t;

/**
 * @brief   Interrupt the default play logic
 */
typedef enum {
    BDSC_CUSTOM_DESIRE_SKIP_DEFAULT,
    BDSC_CUSTOM_DESIRE_DEFAULT,
} bdsc_custom_desire_action_t;

/**
 * @brief      Bdsc Engine  event type
 */
typedef struct bdsc_engine_event {
    bdsc_engine_event_id_t  event_id;    /*!< event_id, to know the cause of the event */
    bdsc_engine_handle_t    client;      /*!< bdsc_engine_handle_t context */
    void                    *data;       /*!< data of the event */
    int                     data_len;    /*!< data length of data */
} bdsc_engine_event_t;


/**
 * @brief      BDSC Client transport
 */
typedef enum {
    BDSC_TRANSPORT_UNKNOWN = 0x0,   /*!< Unknown */
    BDSC_TRANSPORT_OVER_TCP,        /*!< Transport over tcp */
    BDSC_TRANSPORT_OVER_SSL,        /*!< Transport over ssl */
    BDSC_TRANSPORT_OVER_WSS,        /*!< Transport over wss */
} bdsc_engine_transport_t;

typedef esp_err_t (*bdsc_engine_event_handle_cb)(bdsc_engine_event_t *evt);

/**
 * @brief bdsc engine configuration
 */
typedef struct {
    int                         log_level;         /*!< Set log level: 0:E  1:W  2:I 3:D 4:V */
    const char                  *bdsc_host;        /*!< BDSC server url */
    int                         bdsc_port;         /*!< BDSC server port, default depend on bdsc_engine_transport_t */
    const char                  *auth_server;      /*!< BDSC auth server */
    int                         auth_port;         /*!< BDSC auth port */
    bdsc_engine_event_handle_cb event_handler;     /*!< BDSC Event Handle */
    bdsc_engine_transport_t     transport_type;    /*!< BDSC transport type, see `bdsc_engine_transport_t` */
    int                         bdsc_methods;      /*!< BDSC configuable methods*/ 
} bdsc_engine_config_t;

typedef enum {
    ASR_TTS_ENGINE_WAKEUP_TRIGGER,
    ASR_TTS_ENGINE_STARTTED,
    ASR_TTS_ENGINE_GOT_ASR_BEGIN,
    ASR_TTS_ENGINE_GOT_ASR_RESULT,
    ASR_TTS_ENGINE_GOT_EXTERN_DATA,
    ASR_TTS_ENGINE_GOT_TTS_DATA,
    ASR_TTS_ENGINE_GOT_ASR_ERROR,
    ASR_TTS_ENGINE_GOT_ASR_END,
    ASR_TTS_ENGINE_WANT_RESTART_ASR,
} bdsc_engine_internal_st;

/**
 * @brief bdsc engine instance
 */
struct bdsc_engine {
    bdsc_engine_config_t    *cfg;
    vendor_info_t           *g_vendor_info;
    char                    cuid[256];
    bds_client_handle_t     g_client_handle;
    bdsc_auth_cb            g_auth_cb;
    QueueHandle_t           g_engine_queue;
    SemaphoreHandle_t       enqueue_mutex;
    bool                    in_wakeup;
    EventGroupHandle_t      wk_group;

    bdsc_engine_internal_st g_asr_tts_state;
    bool                    cur_in_asr_session;
    bool                    need_skip_current_pending_http_part;

    esp_mqtt_client_handle_t g_mqtt_client;
    char                     g_pub_topic[256];
    char                     g_sub_topic[256];

    int                     cur_vol;
    char                    *asrnlp_ttsurl;
    bool                    g_has_greeted;
    bool                    skip_tts_playing_once;
    bool                    in_ota_process_flag;
    char                    *sc_customer_data;
    uint32_t                ota_time_begin;
    xTaskHandle             ota_monitor_task_handle;
    int*                    cached_msg_queue;

    bool                    is_force_ota;
    int                     ota_download_st;
    int                     silent_mode;
    int                     boot_from_ota;
    int                     ota_read_bytes;
    int                     ota_total_bytes;
    bool                    ota_fail_flag;

	void                    *userData;
};

/**
 * @brief golbal bdsc engine reference
 */
extern bdsc_engine_handle_t g_bdsc_engine;

/**
 * @brief      Initialize the Bdsc Engine handler
 *             This function must be the first function to call
 *
 * @param[in]  cfg     config to use to initialize the engine
 *
 * @return
 *     - `bdsc_engine_handle_t`
 *     - NULL if any errors
 */
bdsc_engine_handle_t bdsc_engine_init(bdsc_engine_config_t *cfg);

esp_err_t bdsc_engine_open_bt();

esp_err_t bdsc_engine_close_bt();

/**
 * @brief      BT play
 *
 * @param[in]  ...
 *
 * @return
 *  - ESP_OK on successful
 *  - ESP_FAIL on error
 */
esp_err_t bdsc_engine_bt_play();

/**
 * @brief      Tone play
 *
 * @param[in]  ...
 *
 * @return
 *  - ESP_OK on successful
 *  - ESP_FAIL on error
 */
esp_err_t bdsc_engine_tone_play();

/**
 * @brief      HTTP play
 *
 * @param[in]  ...
 *
 * @return
 *  - ESP_OK on successful
 *  - ESP_FAIL on error
 */
esp_err_t bdsc_engine_http_play();


/**
 * @brief      Bdsc Engine net connected notifyer
 *
 * @param[in]  ...
 *
 * @note       This func MUST be called manually once net is connected
 *
 * @return
 */
void bdsc_engine_net_connected_cb();


int bdsc_engine_get_internal_state(bdsc_engine_handle_t client);

void bdsc_engine_skip_current_session_playing_once_flag_set(bdsc_engine_handle_t client);
void bdsc_engine_skip_current_session_playing_once_flag_unset(bdsc_engine_handle_t client);

/**
 * @brief      Bdsc Engine data channel upload
 *
 * @param[in]  upload_data          data to upload
 * @param[in]  upload_data_len      data length
 *
 * @return
 *  - 0 on successful
 *  - -1 on error
 */
int bdsc_engine_channel_data_upload(uint8_t *upload_data, size_t upload_data_len);

/**
 * @brief      De-init Bdsc Engine handler
 *
 * @param[in]  client  Bdsc Engine handler to be initialized
 *
 * @return
 *  - ESP_OK on successful
 *  - ESP_FAIL on error
 */
esp_err_t bdsc_engine_deinit(bdsc_engine_handle_t client);

void bdsc_engine_start_tts(const char *tts_text);

void start_sdk();
void stop_sdk();


#define ACTIVE_TTS_PREFIX   "http://activetts/"

#ifdef __cplusplus
}
#endif

#endif
