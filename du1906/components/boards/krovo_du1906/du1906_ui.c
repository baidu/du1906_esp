#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "input_key_service.h"
#include "wifi_service.h"
#include "audio_player.h"
#include "bdsc_engine.h"
#include "audio_tone_uri.h"
#include "bdsc_cmd.h"
#include "wifi_ssid_manager.h"
#include "ble_gatts_module.h"
#include "app_control.h"
#include "app_ota_upgrade.h"
#include "blufi_config.h"
#include "wifi_service.h"
#include "board.h"
#include "bds_common_utility.h"
#include "bdsc_tools.h"
#include "esp_delegate.h"
#include "bdsc_engine.h"
#include "app_voice_control.h"
#include "tas5805m.h"
#include "app_music.h"
#define  TAG  "KROVO_DU1906_UI"

extern bool g_print_task_status;
extern bool g_pre_player_need_resume;
extern int get_last_wifi_disconnect_real_reason(int *main_rsn, int *assist_rsn);

esp_err_t wifi_service_cb(periph_service_handle_t handle, periph_service_event_t *evt, void *ctx)
{
    ESP_LOGD(TAG, "event type:%d,source:%p, data:%p,len:%d,ctx:%p",
             evt->type, evt->source, evt->data, evt->len, ctx);
    if (evt->type == WIFI_SERV_EVENT_CONNECTING) {
        ESP_LOGI(TAG, "PERIPH_WIFI_CONNECTING [%d]", __LINE__);
        bdsc_play_hint(BDSC_HINT_WIFI_CONFIGUING);
        vTaskDelay(5000 / portTICK_PERIOD_MS);

    } else if (evt->type == WIFI_SERV_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "PERIPH_WIFI_CONNECTED [%d]", __LINE__);
        if (g_wifi_setting_flag) {
            bdsc_play_hint(BDSC_HINT_WIFI_CONFIG_OK);
            g_wifi_setting_flag = false;
            if (!g_bdsc_engine->sc_customer_data) {
                ESP_LOGE(TAG, "Bug!!");
                return ESP_OK;
            }
            blufi_set_customized_data(g_wifi_setting, g_bdsc_engine->sc_customer_data, strlen(g_bdsc_engine->sc_customer_data));
            blufi_send_customized_data(g_wifi_setting);
            blufi_set_sta_connected_flag(g_wifi_setting, true);
            ble_gatts_module_stop_adv();
            if (g_wifi_config_event) {
                xEventGroupSetBits(g_wifi_config_event, 1);
            }
            display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_WIFI_CONNECTED, 0);
        }
    } else if (evt->type == WIFI_SERV_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "PERIPH_WIFI_DISCONNECTED [%d]", __LINE__);
        blufi_set_sta_connected_flag(g_wifi_setting, false);
        display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_WIFI_DISCONNECTED, 0);
    } else if (evt->type == WIFI_SERV_EVENT_SETTING_TIMEOUT) {
        ESP_LOGE(TAG, "WIFI_SERV_EVENT_SETTING_TIMEOUT [%d]", __LINE__);
        ble_gatts_module_stop_adv();
        display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_WIFI_DISCONNECTED, 0);
    } else if (evt->type == WIFI_SERV_EVENT_SETTING_FAILED) {
        ESP_LOGE(TAG, "WIFI_SERV_EVENT_SETTING_FAILED [%d]", __LINE__);
        char customer_data[32] = {0};
        int main_rsn = 0;
        int assist_rsn = 0;
        get_last_wifi_disconnect_real_reason(&main_rsn, &assist_rsn);
        if (assist_rsn > 0xff) {
            ESP_LOGE(TAG, "assist rsn: %d, out of range!", assist_rsn);
            assist_rsn = 0xff;
        }
        bdsc_play_hint(BDSC_HINT_WIFI_CONFIG_FAIL);
        switch (main_rsn) {
        case WIFI_SERV_STA_AUTH_ERROR:
            sprintf(customer_data, "##00%02x", assist_rsn);
            break;
        case WIFI_SERV_STA_AP_NOT_FOUND:
            sprintf(customer_data, "##01%02x", assist_rsn);
            break;
        case WIFI_SERV_STA_COM_ERROR:
        default:
            sprintf(customer_data, "##02%02x", assist_rsn);
            break;
        }
        ESP_LOGI(TAG, "main rsn: %d, assist rsn: %d, cus: %s", main_rsn, assist_rsn, customer_data);
        blufi_set_customized_data(g_wifi_setting, customer_data, strlen(customer_data));
        blufi_send_customized_data(g_wifi_setting);
        ble_gatts_module_stop_adv();
        display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_WIFI_DISCONNECTED, 0);
    } else {
        ESP_LOGE(TAG, "unknown WIFI_SERV_EVENT [%d]", __LINE__);
    }

    return ESP_OK;
}

esp_err_t input_key_service_cb(periph_service_handle_t handle, periph_service_event_t *evt, void *ctx)
{
    ESP_LOGD(TAG, "type=%d, source=%d, data=%d, len=%d", evt->type, (int)evt->source, (int)evt->data, evt->len);
    audio_player_state_t st = {0};
    switch ((int)evt->data) {
        case INPUT_KEY_USER_ID_MUTE:
            if (evt->type == INPUT_KEY_SERVICE_ACTION_PRESS) {
                audio_player_state_get(&st);
                if (st.media_src == MEDIA_SRC_TYPE_MUSIC_A2DP) {
                    if (((int)st.status == AUDIO_STATUS_RUNNING)) {
                        audio_player_stop();
                        ESP_LOGE(TAG, "[ * ] [Exit BT mode]");
                        display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_BT_DISCONNECTED, 0);
                    } else {
                        ESP_LOGE(TAG, "[ * ] [Enter BT mode by no running]");
                        if (audio_player_music_play("aadp://44100:2@bt/sink/stream.pcm", 0, MEDIA_SRC_TYPE_MUSIC_A2DP) == ESP_ERR_AUDIO_NO_ERROR) {
                            display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_BT_CONNECTED, 0);
                        } else {
                            display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_BT_DISCONNECTED, 0);
                        }
                    }
                } else {
                    ESP_LOGE(TAG, "[ * ] [Enter BT mode]");
                    char *a2dp_url = "aadp://44100:2@bt/sink/stream.pcm";
                    event_engine_elem_EnQueque(EVENT_RECV_A2DP_START_PLAY, (uint8_t *)a2dp_url, strlen(a2dp_url) + 1);
                }
            }
            break;
        case INPUT_KEY_USER_ID_SET:
            ESP_LOGI(TAG, "[ * ] [Set] Setting Wi-Fi");
#ifdef CONFIG_ESP_BLUFI
            ble_gatts_module_start_adv();
            blufi_set_sta_connected_flag(g_wifi_setting, false);
#endif
            if (evt->type == INPUT_KEY_SERVICE_ACTION_PRESS) {
                audio_player_state_get(&st);
                if (((int)st.status == AUDIO_STATUS_RUNNING)) {
                    audio_player_stop();
                }
                bdsc_play_hint(BDSC_HINT_SC);
                wifi_service_setting_start(g_wifi_serv, 0);
                g_wifi_setting_flag = true;
                ESP_LOGI(TAG, "AUDIO_USER_KEY_WIFI_SET, WiFi setting started.");
                display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_TURN_OFF, 0);
                display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_WIFI_SETTING, 0);
            }
            break;
        case INPUT_KEY_USER_ID_VOLDOWN:
            if (evt->type == INPUT_KEY_SERVICE_ACTION_CLICK_RELEASE) {
                ESP_LOGI(TAG, "[ * ] [Vol-] input key event");
                int player_volume = 0;
                audio_player_vol_get(&player_volume);
                player_volume -= 10;
                if (player_volume < 0) {
                    player_volume = 0;
                }
                audio_player_vol_set(player_volume);
                ESP_LOGI(TAG, "Now volume is %d", player_volume);
            }
            break;
        case INPUT_KEY_USER_ID_VOLUP:
            if (evt->type == INPUT_KEY_SERVICE_ACTION_CLICK_RELEASE) {
                ESP_LOGI(TAG, "[ * ] [Vol+] input key event");
                int player_volume = 0;
                audio_player_vol_get(&player_volume);
                player_volume += 10;
                if (player_volume > 100) {
                    player_volume = 100;
                }
                audio_player_vol_set(player_volume);
                ESP_LOGI(TAG, "Now volume is %d", player_volume);
            } else if (evt->type == INPUT_KEY_SERVICE_ACTION_PRESS) {
                ESP_LOGI(TAG, "[ * ] [Vol+] press event");
            }
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t audio_board_pa_enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "PA enter deep sleep.");
    return tas5805m_enter_deep_sleep();
}

esp_err_t audio_board_pa_exit_deep_sleep(void)
{
    ESP_LOGI(TAG, "PA exit deep sleep.");
    return tas5805m_exit_deep_sleep();
}

/**********************************************************************
 * it is a example for uaer add unit skill and handle data
 * 1. define your g_user_unit_data
 * 2. append your g_user_unit_data to array
 * 3. add your control function on user_unit_cmd_handle
 * NOTE: you can delete it if don't use user unit skill
 * *********************************************************************/
enum {     //define user unit code
    USER_CTL_MUSIC_PAUSE = 0,
    USER_CTL_MUSIC_CONTINUE,
};

/**
 * @brief user handle unit data
 * @param[in] pdata          NLP data
 * @param[in] post_data      user defined unit code
 */
void user_unit_cmd_handle(unit_data_t *pdata, uint32_t code)
{
    switch (code) {
    case USER_CTL_MUSIC_PAUSE:
        set_music_player_state(PAUSE_STATE);
        g_pre_player_need_resume = false;
        ESP_LOGW(TAG, "USER_CTL_MUSIC_PAUSE");
        break;
    case USER_CTL_MUSIC_CONTINUE:
        set_music_player_state(RUNNING_STATE);
        g_pre_player_need_resume = true;
        ESP_LOGW(TAG, "USER_CTL_MUSIC_CONTINUE");
        break;
    default:
        ESP_LOGW(TAG, "UNKOWN CMD");
        break;
    }
}

unit_data_t g_user_unit_data[] = {
      /* unit_code                intend        origin    action_type    { custom_reply  }  slot number {slots   (flexible array)            }  */
    {USER_CTL_MUSIC_PAUSE,     "DEV_ACTION", "1035789", NO_CMP_STR, {NO_CMP_STR, NO_CMP_STR}, 1, {{"user_action", "PAUSE"}}},
    {USER_CTL_MUSIC_CONTINUE,  "DEV_ACTION", "1035789", NO_CMP_STR, {NO_CMP_STR, NO_CMP_STR}, 1, {{"user_action", "CONTINUE"}}},
};

int g_user_unit_array_num = sizeof(g_user_unit_data) / sizeof(g_user_unit_data[0]);    //you must init g_user_unit_array_num if using user unit
/******************************** end ***************************/