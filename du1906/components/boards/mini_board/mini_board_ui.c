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
#include "app_control.h"

#define  TAG  "MINI_BOARD_UI"
static esp_err_t wifi_service_cb(periph_service_handle_t handle, periph_service_event_t *evt, void *ctx)
{
    ESP_LOGD(TAG, "event type:%d,source:%p, data:%p,len:%d,ctx:%p",
             evt->type, evt->source, evt->data, evt->len, ctx);
    if (evt->type == WIFI_SERV_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "PERIPH_WIFI_CONNECTED [%d]", __LINE__);
        display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_WIFI_CONNECTED, 0);
        if (g_wifi_config_event) {
            xEventGroupSetBits(g_wifi_config_event, 1);
        }
        if (g_wifi_setting_flag) {
            g_wifi_setting_flag = false;
#ifdef CONFIG_ESP_BLUFI
            blufi_send_customized_data(g_wifi_setting);
            blufi_set_sta_connected_flag(g_wifi_setting, true);
            ble_gatts_module_stop_adv();
#endif
        }
        // BDSC Engine need net connected notifier to restart link
        bdsc_engine_net_connected_cb();
    } else if (evt->type == WIFI_SERV_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "PERIPH_WIFI_DISCONNECTED [%d]", __LINE__);
#ifdef CONFIG_ESP_BLUFI
        blufi_set_sta_connected_flag(g_wifi_setting, false);
#endif
        display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_WIFI_DISCONNECTED, 0);
    } else if (evt->type == WIFI_SERV_EVENT_SETTING_TIMEOUT) {
        g_wifi_setting_flag = false;
    }

    return ESP_OK;
}

esp_err_t input_key_service_cb(periph_service_handle_t handle, periph_service_event_t *evt, void *ctx)
{
    ESP_LOGD(TAG, "type=%d, source=%d, data=%d, len=%d", evt->type, (int)evt->source, (int)evt->data, evt->len);
    audio_player_state_t st = {0};
    switch ((int)evt->data) {
        case INPUT_KEY_USER_ID_SET:
            ESP_LOGI(TAG, "[ * ] [Set] Setting Wi-Fi");
#ifdef CONFIG_ESP_BLUFI
            ble_gatts_module_start_adv();
            blufi_set_sta_connected_flag(g_wifi_setting, false);
#endif
            if (evt->type == INPUT_KEY_SERVICE_ACTION_PRESS) {
                if (g_wifi_setting_flag == false) {
                    audio_player_state_get(&st);
                    if (((int)st.status == AUDIO_STATUS_RUNNING)) {
                        audio_player_stop();
                    }
                    bdsc_play_hint(BDSC_HINT_SC);
                    wifi_service_setting_start(g_wifi_serv, 0);
                    g_wifi_setting_flag = true;
                    ESP_LOGI(TAG, "AUDIO_USER_KEY_WIFI_SET, WiFi setting started.");
                    display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_WIFI_SETTING, 0);
                } else {
                    ESP_LOGW(TAG, "AUDIO_USER_KEY_WIFI_SET, WiFi setting will be stopped.");
                    wifi_service_setting_stop(g_wifi_serv, 0);
                    g_wifi_setting_flag = false;
                    display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_WIFI_SETTING_FINISHED, 0);
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

#include "tas5805m.h"
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