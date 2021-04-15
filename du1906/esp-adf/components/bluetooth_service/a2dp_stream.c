/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2019 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_peripherals.h"
#include "audio_common.h"
#include "sdkconfig.h"

#include "a2dp_stream.h"
#include "esp_avrc_api.h"

#if CONFIG_BT_ENABLED

#define APP_RC_CT_TL_GET_CAPS            (0)
#define APP_RC_CT_TL_GET_META_DATA       (1)
#define APP_RC_CT_TL_RN_TRACK_CHANGE     (2)
#define APP_RC_CT_TL_RN_PLAYBACK_CHANGE  (3)
#define APP_RC_CT_TL_RN_PLAY_POS_CHANGE  (4)

static const char *TAG = "A2DP_STREAM";

static audio_element_handle_t a2dp_sink_stream_handle = NULL;
static audio_element_handle_t a2dp_source_stream_handle = NULL;
static a2dp_stream_user_callback_t a2dp_user_callback;
static esp_periph_handle_t bt_avrc_periph = NULL;
static bool avrcp_conn_state = false;
static audio_stream_type_t a2d_stream_type = 0;
static uint8_t trans_label = 0;
static esp_avrc_rn_evt_cap_mask_t s_avrc_peer_rn_cap;

static const char *audio_state_str[] = { "Suspended", "Stopped", "Started" };

static void bt_a2d_sink_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    if (a2dp_user_callback.user_a2d_cb) {
        a2dp_user_callback.user_a2d_cb(event, param);
    }
    esp_a2d_cb_param_t *a2d = NULL;
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
            a2d = (esp_a2d_cb_param_t *)(param);
            uint8_t *bda = a2d->conn_stat.remote_bda;
            ESP_LOGI(TAG, "A2DP bd address:, [%02x:%02x:%02x:%02x:%02x:%02x]",
                     bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
            if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
                ESP_LOGI(TAG, "A2DP connection state =  DISCONNECTED");
                if (a2dp_sink_stream_handle) {
                    audio_element_report_status(a2dp_sink_stream_handle, AEL_STATUS_INPUT_DONE);
                }
                if (bt_avrc_periph) {
                    esp_periph_send_event(bt_avrc_periph, PERIPH_BLUETOOTH_DISCONNECTED, NULL, 0);
                }
            } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
                ESP_LOGI(TAG, "A2DP connection state =  CONNECTED");
                if (bt_avrc_periph) {
                    esp_periph_send_event(bt_avrc_periph, PERIPH_BLUETOOTH_CONNECTED, NULL, 0);
                }
            }
            break;

        case ESP_A2D_AUDIO_STATE_EVT:
            a2d = (esp_a2d_cb_param_t *)(param);
            ESP_LOGD(TAG, "A2DP audio state: %s", audio_state_str[a2d->audio_stat.state]);
            if (bt_avrc_periph == NULL) {
                break;
            }

            if (ESP_A2D_AUDIO_STATE_STARTED == a2d->audio_stat.state) {
                esp_periph_send_event(bt_avrc_periph, PERIPH_BLUETOOTH_AUDIO_STARTED, NULL, 0);
            } else if (ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND == a2d->audio_stat.state) {
                esp_periph_send_event(bt_avrc_periph, PERIPH_BLUETOOTH_AUDIO_SUSPENDED, NULL, 0);
            } else if (ESP_A2D_AUDIO_STATE_STOPPED == a2d->audio_stat.state) {
                esp_periph_send_event(bt_avrc_periph, PERIPH_BLUETOOTH_AUDIO_STOPPED, NULL, 0);
            }
            break;

        case ESP_A2D_AUDIO_CFG_EVT:
            ESP_LOGI(TAG, "A2DP audio stream configuration, codec type %d", param->audio_cfg.mcc.type);
            if (param->audio_cfg.mcc.type == ESP_A2D_MCT_SBC) {
                int sample_rate = 16000;
                char oct0 = param->audio_cfg.mcc.cie.sbc[0];
                if (oct0 & (0x01 << 6)) {
                    sample_rate = 32000;
                } else if (oct0 & (0x01 << 5)) {
                    sample_rate = 44100;
                } else if (oct0 & (0x01 << 4)) {
                    sample_rate = 48000;
                }
                ESP_LOGI(TAG, "Bluetooth configured, sample rate=%d", sample_rate);
                if (a2dp_sink_stream_handle == NULL) {
                    break;
                }
                audio_element_set_music_info(a2dp_sink_stream_handle, sample_rate, 2, 16);
                audio_element_report_info(a2dp_sink_stream_handle);
            }
            break;
        default:
            ESP_LOGI(TAG, "Unhandled A2DP event: %d", event);
            break;
    }
}

static void bt_a2d_sink_data_cb(const uint8_t *data, uint32_t len)
{
    if (a2dp_user_callback.user_a2d_sink_data_cb) {
        a2dp_user_callback.user_a2d_sink_data_cb(data, len);
    }
    if (a2dp_sink_stream_handle) {
        if (audio_element_get_state(a2dp_sink_stream_handle) == AEL_STATE_RUNNING) {
            audio_element_output(a2dp_sink_stream_handle, (char *)data, len);
        }
    }
}

static void bt_a2d_source_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    if (a2dp_user_callback.user_a2d_cb) {
        a2dp_user_callback.user_a2d_cb(event, param);
    }
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
            if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
                ESP_LOGI(TAG, "a2dp source connected");
                ESP_LOGI(TAG, "a2dp media ready checking ...");
                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
                if (bt_avrc_periph) {
                    esp_periph_send_event(bt_avrc_periph, PERIPH_BLUETOOTH_CONNECTED, NULL, 0);
                }
            } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
                ESP_LOGI(TAG, "a2dp source disconnected");
                esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
                if (bt_avrc_periph) {
                    esp_periph_send_event(bt_avrc_periph, PERIPH_BLUETOOTH_DISCONNECTED, NULL, 0);
                }
            }
            break;
        case ESP_A2D_MEDIA_CTRL_ACK_EVT:
            if (param->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY &&
                param->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
            }
            break;
        default:
            break;
    }
}

static int32_t bt_a2d_source_data_cb(uint8_t *data, int32_t len)
{
    if (a2dp_source_stream_handle) {
        if (audio_element_get_state(a2dp_source_stream_handle) == AEL_STATE_RUNNING) {
            if (len < 0 || data == NULL) {
                return 0;
            }
            len = audio_element_input(a2dp_source_stream_handle, (char *)data, len);
            if (len == AEL_IO_DONE) {
                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_STOP);
                if (bt_avrc_periph) {
                    esp_periph_send_event(bt_avrc_periph, PERIPH_BLUETOOTH_AUDIO_SUSPENDED, NULL, 0);
                }
            }
            return len;
        }
    }
    return 0;
}

static esp_err_t a2dp_sink_destory(audio_element_handle_t self)
{
    ESP_LOGI(TAG, "a2dp_sink_destory");
    a2dp_sink_stream_handle = NULL;
    memset(&a2dp_user_callback, 0, sizeof(a2dp_stream_user_callback_t));
    return ESP_OK;
}

static esp_err_t a2dp_source_destory(audio_element_handle_t self)
{
    a2dp_source_stream_handle = NULL;
    memset(&a2dp_user_callback, 0, sizeof(a2dp_stream_user_callback_t));
    return ESP_OK;
}

audio_element_handle_t a2dp_stream_init(a2dp_stream_config_t *config)
{
    audio_element_handle_t el = NULL;
    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();

    if (a2dp_sink_stream_handle || a2dp_source_stream_handle) {
        ESP_LOGE(TAG, "a2dp stream already created. please terminate before create.");
        return NULL;
    }

    cfg.task_stack = -1; // No need task
    cfg.tag = "aadp";

    if (config->type == AUDIO_STREAM_READER) {
        // A2DP sink
        a2d_stream_type = AUDIO_STREAM_READER;
        cfg.destroy = a2dp_sink_destory;
        el = a2dp_sink_stream_handle = audio_element_init(&cfg);

        esp_a2d_sink_register_data_callback(bt_a2d_sink_data_cb);
        esp_a2d_register_callback(bt_a2d_sink_cb);
        esp_a2d_sink_init();
    } else {
        // A2DP source
        a2d_stream_type = AUDIO_STREAM_WRITER;
        cfg.destroy = a2dp_source_destory;
        el = a2dp_source_stream_handle = audio_element_init(&cfg);

        esp_a2d_register_callback(bt_a2d_source_cb);
        esp_a2d_source_register_data_callback(bt_a2d_source_data_cb);
        esp_a2d_source_init();
    }

    AUDIO_MEM_CHECK(TAG, el, return NULL);

    memcpy(&a2dp_user_callback, &config->user_callback, sizeof(a2dp_stream_user_callback_t));

    return el;
}

esp_err_t a2dp_destroy()
{
    if (a2d_stream_type == AUDIO_STREAM_READER) {
        esp_a2d_sink_deinit();
    } else if (a2d_stream_type == AUDIO_STREAM_WRITER) {
        esp_a2d_source_deinit();
    }
    return ESP_OK;
}

static void bt_av_new_track(void)
{
    // request metadata
    uint8_t attr_mask = ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST | ESP_AVRC_MD_ATTR_ALBUM | ESP_AVRC_MD_ATTR_GENRE;
    esp_avrc_ct_send_metadata_cmd(APP_RC_CT_TL_GET_META_DATA, attr_mask);

    // register notification if peer support the event_id
    if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_avrc_peer_rn_cap,
                                           ESP_AVRC_RN_TRACK_CHANGE)) {
        esp_avrc_ct_send_register_notification_cmd(APP_RC_CT_TL_RN_TRACK_CHANGE, ESP_AVRC_RN_TRACK_CHANGE, 0);
    }
}

static void bt_av_playback_changed(void)
{
    if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_avrc_peer_rn_cap,
                                           ESP_AVRC_RN_PLAY_STATUS_CHANGE)) {
        esp_avrc_ct_send_register_notification_cmd(APP_RC_CT_TL_RN_PLAYBACK_CHANGE, ESP_AVRC_RN_PLAY_STATUS_CHANGE, 0);
    }
}

static void bt_av_play_pos_changed(void)
{
    if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_avrc_peer_rn_cap,
                                           ESP_AVRC_RN_PLAY_POS_CHANGED)) {
        esp_avrc_ct_send_register_notification_cmd(APP_RC_CT_TL_RN_PLAY_POS_CHANGE, ESP_AVRC_RN_PLAY_POS_CHANGED, 10);
    }
}

extern esp_err_t bdsc_engine_open_bt();
void bt_av_notify_evt_handler(uint8_t event_id, esp_avrc_rn_param_t *event_parameter)
{
    switch (event_id) {
    case ESP_AVRC_RN_TRACK_CHANGE:
        bt_av_new_track();
        break;
    case ESP_AVRC_RN_PLAY_STATUS_CHANGE:
        ESP_LOGI(TAG, "Playback status changed: 0x%x", event_parameter->playback);
        if (event_parameter->playback == ESP_AVRC_PLAYBACK_PLAYING) {
            bdsc_engine_open_bt();
        }
        bt_av_playback_changed();
        break;
    case ESP_AVRC_RN_PLAY_POS_CHANGED:
        ESP_LOGI(TAG, "Play position changed: %d-ms", event_parameter->play_pos);
        bt_av_play_pos_changed();
        break;
    }
}

static void bt_avrc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *p_param)
{
    esp_avrc_ct_cb_param_t *rc = p_param;
    switch (event) {
        case ESP_AVRC_CT_CONNECTION_STATE_EVT: {
                uint8_t *bda = rc->conn_stat.remote_bda;
                avrcp_conn_state = rc->conn_stat.connected;
                if (rc->conn_stat.connected) {
                    ESP_LOGD(TAG, "ESP_AVRC_CT_CONNECTION_STATE_EVT");
                    bt_key_act_sm_init();
                    // get remote supported event_ids of peer AVRCP Target
                    esp_avrc_ct_send_get_rn_capabilities_cmd(APP_RC_CT_TL_GET_CAPS);
                } else if (0 == rc->conn_stat.connected) {
                    bt_key_act_sm_deinit();
                    // clear peer notification capability record
                    s_avrc_peer_rn_cap.bits = 0;
                }

                ESP_LOGI(TAG, "AVRC conn_state evt: state %d, [%02x:%02x:%02x:%02x:%02x:%02x]",
                         rc->conn_stat.connected, bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
                break;
            }
        case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT: {
                if (avrcp_conn_state) {
                    ESP_LOGI(TAG, "AVRC passthrough rsp: key_code 0x%x, key_state %d", rc->psth_rsp.key_code, rc->psth_rsp.key_state);
                    bt_key_act_param_t param;
                    memset(&param, 0, sizeof(bt_key_act_param_t));
                    param.evt = event;
                    param.tl = rc->psth_rsp.tl;
                    param.key_code = rc->psth_rsp.key_code;
                    param.key_state = rc->psth_rsp.key_state;
                    bt_key_act_state_machine(&param);
                }
                break;
            }
        case ESP_AVRC_CT_METADATA_RSP_EVT: {
                ESP_LOGI(TAG, "AVRC metadata rsp: attribute id 0x%x, %s", rc->meta_rsp.attr_id, rc->meta_rsp.attr_text);
                // free(rc->meta_rsp.attr_text);
                break;
            }
        case ESP_AVRC_CT_CHANGE_NOTIFY_EVT: {
                ESP_LOGI(TAG, "AVRC event notification: %d", rc->change_ntf.event_id);
                bt_av_notify_evt_handler(rc->change_ntf.event_id, &rc->change_ntf.event_parameter);
                break;
            }
        case ESP_AVRC_CT_REMOTE_FEATURES_EVT: {
                ESP_LOGI(TAG, "AVRC remote features %x", rc->rmt_feats.feat_mask);
                break;
            }
        case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT: {
            ESP_LOGI(TAG, "remote rn_cap: count %d, bitmask 0x%x", rc->get_rn_caps_rsp.cap_count,
                     rc->get_rn_caps_rsp.evt_set.bits);
            s_avrc_peer_rn_cap.bits = rc->get_rn_caps_rsp.evt_set.bits;
            bt_av_new_track();
            bt_av_playback_changed();
            bt_av_play_pos_changed();
            break;
        }
        default:
            ESP_LOGE(TAG, "%s unhandled evt %d", __func__, event);
            break;
    }
}

static esp_err_t _bt_avrc_periph_init(esp_periph_handle_t periph)
{
    esp_avrc_ct_init();
    esp_avrc_ct_register_callback(bt_avrc_ct_cb);
    return ESP_OK;
}

static esp_err_t _bt_avrc_periph_run(esp_periph_handle_t self, audio_event_iface_msg_t *msg)
{
    return ESP_OK;
}

static esp_err_t _bt_avrc_periph_destroy(esp_periph_handle_t periph)
{
    esp_avrc_ct_deinit();
    bt_avrc_periph = NULL;
    return ESP_OK;
}

esp_periph_handle_t bt_create_periph()
{
    if (bt_avrc_periph) {
        ESP_LOGE(TAG, "bt periph have been created");
        return NULL;
    }
    bt_avrc_periph = esp_periph_create(PERIPH_ID_BLUETOOTH, "periph_bt");
    esp_periph_set_function(bt_avrc_periph, _bt_avrc_periph_init, _bt_avrc_periph_run, _bt_avrc_periph_destroy);
    return bt_avrc_periph;
}

static esp_err_t periph_bt_avrc_passthrough_cmd(esp_periph_handle_t periph, uint8_t cmd)
{
    esp_err_t err = ESP_OK;

    if (avrcp_conn_state) {
        bt_key_act_param_t param;
        memset(&param, 0, sizeof(bt_key_act_param_t));
        param.evt = ESP_AVRC_CT_KEY_STATE_CHG_EVT;
        param.key_code = cmd;
        param.key_state = 0;
        param.tl = trans_label & 0x0F;
        trans_label = (trans_label + 2) & 0x0f;
        bt_key_act_state_machine(&param);
    }
    return err;
}

esp_err_t periph_bt_play(esp_periph_handle_t periph)
{
    esp_err_t err = ESP_OK;
    if (a2d_stream_type == AUDIO_STREAM_READER) {
        err = periph_bt_avrc_passthrough_cmd(periph, ESP_AVRC_PT_CMD_PLAY);
    } else {
        err = esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
    }
    return err;
}

esp_err_t periph_bt_pause(esp_periph_handle_t periph)
{
    esp_err_t err = ESP_OK;
    if (a2d_stream_type == AUDIO_STREAM_READER) {
        err = periph_bt_avrc_passthrough_cmd(periph, ESP_AVRC_PT_CMD_PAUSE);
    } else {
        err = esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
    }
    return err;
}

esp_err_t periph_bt_stop(esp_periph_handle_t periph)
{
    esp_err_t err = ESP_OK;
    if (a2d_stream_type == AUDIO_STREAM_READER) {
        err = periph_bt_avrc_passthrough_cmd(periph, ESP_AVRC_PT_CMD_STOP);
    } else {
        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_STOP);
    }
    return err;
}

esp_err_t periph_bt_avrc_next(esp_periph_handle_t periph)
{
    return periph_bt_avrc_passthrough_cmd(periph, ESP_AVRC_PT_CMD_FORWARD);
}

esp_err_t periph_bt_avrc_prev(esp_periph_handle_t periph)
{
    return periph_bt_avrc_passthrough_cmd(periph, ESP_AVRC_PT_CMD_BACKWARD);
}

esp_err_t periph_bt_avrc_rewind(esp_periph_handle_t periph)
{
    return periph_bt_avrc_passthrough_cmd(periph, ESP_AVRC_PT_CMD_REWIND);
}

esp_err_t periph_bt_avrc_fast_forward(esp_periph_handle_t periph)
{
    return periph_bt_avrc_passthrough_cmd(periph, ESP_AVRC_PT_CMD_FAST_FORWARD);
}

esp_err_t periph_bt_discover(esp_periph_handle_t periph)
{
    if (a2d_stream_type == AUDIO_STREAM_READER) {
        return esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
    }
    return ESP_OK;
}

#endif
