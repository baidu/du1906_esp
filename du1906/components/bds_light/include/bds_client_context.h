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

#ifndef MAIN_CLIENT_BDS_CLIENT_CONTEXT_H_
#define MAIN_CLIENT_BDS_CLIENT_CONTEXT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Define interface error code.
#define ERROR_BDSC_INVALID_RESOURCE             -1000
#define ERROR_BDSC_ASR_START_FAILED             -2000
#define ERROR_BDSC_ASR_CANCEL_FAILED            -2001
#define ERROR_BDSC_ASR_NET_ERROR                -2002  
#define ERROR_BDSC_ASR_HD_SERVER_ERROR          -2003
#define ERROR_BDSC_ASR_TYPE_NOT_RSP             -2004
#define ERROR_BDSC_RECORDER_START_FAILED        -3000
#define ERROR_BDSC_RECORDER_READ_FAILED         -3001
#define ERROR_BDSC_EVENTUPLOAD_START_FAILED     -4000
#define ERROR_BDSC_EVENTUPLOAD_CANCEL_FAILED    -4001
#define ERROR_BDSC_EVENTUPLOAD_ENGINE_BUSY      -4002
#define ERROR_BDSC_EVENTUPLOAD_NET_ERROR        -4003  
#define ERROR_BDSC_EVENTUPLOAD_HD_SERVER_ERROR  -4004
#define ERROR_BDSC_EVENTUPLOAD_TYPE_NOT_RSP     -4005
#define ERROR_BDSC_PUSH_NET_ERROR               -4006
#define ERROR_BDSC_WAKEUP_START_FAILED          -5000
#define ERROR_BDSC_WAKEUP_STOP_FAILED           -5001
#define ERROR_BDSC_LINK_START_INVALID           -6000
#define ERROR_BDSC_LINK_STOP_INVALID            -6001
#define ERROR_BDSC_LINK_CONNECT_FAILED          -6002
#define ERROR_BDSC_DSP_RUNTIME_ERROR            -7004
#define ERROR_BDSC_DSP_LOAD_FLASH               -8000
#define ERROR_BDSC_DSP_LOAD_TRANSPORT           -8001
#define ERROR_BDSC_DSP_LOAD_MD5                 -8002
#define ERROR_BDSC_DSP_LOAD_SETUP               -8003
#define ERROR_BDSC_DSP_LOAD_UNKNOWN             -8004

#define SN_LENGTH                               37
#define KEY_LENGTH                              32
#define CUID_LENGTH                             37
#define HOST_LENGTH                             64
#define APP_LENGTH                              64
#define WP_WORDS_LENGTH                         32

#define FLAG_DEFAULT                            0
#define FLAG_TAIL                               1

#define KEY_ASR_MODE_STATE                      "asr_mode_state"
#define KEY_NQE_MODE                            "nqe_mode"
#define KEY_LC_HOST                             "lc_host"
#define KEY_SELF_WAKEUP_RESTRAIN                "self_wakeup_restrain"
#define KEY_IDX                                 "idx"
#define KEY_TTS_HOLD_WAKE                       "tts_hold_wake"
#define KEY_TTS_KEY_SP                          "tts_key_sp"
#define KEY_SN                                  "sn"
#define KEY_PER                                 "per"
#define KEY_TTS_TOTAL_TIME                      "tts_total_time"
#define KEY_SPK                                 "spk"
#define KEY_SADDR                               "saddr"
#define KEY_LENGTH_STR                          "length"
#define KEY_NIGHT_MODE                          "night_mode"
#define KEY_DBG_FIRST_WP                        "dbg_first_wp"
#define KEY_10S_SUPPORT                         "10s_support"
#define KEY_DBG_10S_PERIOD_S                    "dbg_10s_period"
typedef struct {
    int32_t flag;
    uint16_t buffer_length;
    uint16_t real_length;
    uint8_t buffer[];
} bdsc_audio_t;

/**
 * @brief      Bdsc Error type
 */
typedef struct {
    int32_t code;
    uint16_t info_length;
    char info[];
} bdsc_error_t;

/**
 * @brief      Bdsc client context type
 */
typedef struct {

} bds_client_context_t;
#define MIC_ENERGY_COUNT 8
#define MAX_MIC_NUM      6
typedef struct __attribute__((packed)) Detect_Result {
    unsigned mic_detect_status;
    float result_mic_energy_mean;
    float result_mic_energy_std;
    float result_mic_energy_max;
    float result_mic_energy[MIC_ENERGY_COUNT];
    float result_inconsistent_mean[MAX_MIC_NUM];
    float result_inconsistent_std[MAX_MIC_NUM];
    float result_inconsistent_ratio[MAX_MIC_NUM * MIC_ENERGY_COUNT];
} Detect_Result;
/**
 * @brief      Create bdsc audio data type
 *
 * @param[in]  flag             bdsc audio flag
 * @param[in]  buffer_length    bdsc audio buffer length
 * @param[in]  buffer           bdsc audio buffer
 *
 * @return
 *     - `bdsc_audio_t`
 *     - NULL if any errors
 */
bdsc_audio_t* bdsc_audio_create(int32_t flag, uint16_t buffer_length, const uint8_t *buffer);
/**
 * @brief      Destroy bdsc audio data type
 *
 * @param[in]  audio             bdsc_audio_t handle
 *
 * @return
 */
void bdsc_audio_destroy(bdsc_audio_t *audio);

/**
 * @brief      Create bdsc error data type
 *
 * @param[in]  code           bdsc error code
 * @param[in]  info_length    bdsc error info length
 * @param[in]  info           bdsc error info
 *
 * @return
 *     - `bdsc_error_t`
 *     - NULL if any errors
 */
bdsc_error_t* bdsc_error_create(int32_t code, uint16_t info_length, char *info);

/**
 * @brief      Destroy bdsc error data type
 *
 * @param[in]  error           bdsc_error_t handle
 *
 * @return
 */
void bdsc_error_destroy(bdsc_error_t *error);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_CLIENT_BDS_CLIENT_CONTEXT_H_ */
