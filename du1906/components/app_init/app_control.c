/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2020 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
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

#include "board.h"
#include "esp_peripherals.h"
#include "sdkconfig.h"
#include "audio_mem.h"
#include "esp_log.h"

#include "input_key_service.h"
#include "wifi_service.h"
#include "airkiss_config.h"
#include "smart_config.h"
#include "blufi_config.h"
#include "periph_adc_button.h"
#include "bdsc_event_dispatcher.h"
#include "app_player_init.h"
#include "app_bt_init.h"
#include "app_ota_upgrade.h"
#include "audio_player_helper.h"
#include "audio_player_type.h"
#include "audio_player.h"

#include "ble_gatts_module.h"
#include "bds_client_event.h"
#include "bdsc_engine.h"
#include "app_control.h"
#include "rom/rtc.h"
#include "wifi_ssid_manager.h"
#include "app_music.h"
#include "app_voice_control.h"

#if CONFIG_CUPID_BOARD_V2
static const char *TAG = "CUPID_APP";
#else
static const char *TAG = "DU1906_APP";
#endif

bool g_is_mute = false;
periph_service_handle_t g_wifi_serv = NULL;
bool g_wifi_setting_flag = false;
display_service_handle_t g_disp_serv = NULL;
esp_wifi_setting_handle_t g_wifi_setting = NULL;
#if CONFIG_CUPID_BOARD_V2
EventGroupHandle_t g_long_press_power_on = NULL;
#endif
EventGroupHandle_t g_wifi_config_event = NULL;
esp_bd_addr_t g_bd_addr;

extern int get_last_wifi_disconnect_real_reason(int *main_rsn, int *assist_rsn);

// FIXME: use weak function
#if 0
esp_err_t __attribute__((weak)) wifi_service_cb(periph_service_handle_t handle, periph_service_event_t *evt, void *ctx)
{
    ESP_LOGE(TAG, "nothing do on weak wifi_service_cb function,you can add your function on xxx_ui.c");
    return ESP_OK;
}

esp_err_t __attribute__((weak)) input_key_service_cb(periph_service_handle_t handle, periph_service_event_t *evt, void *ctx)
{
    ESP_LOGE(TAG, "nothing do on weak input_key_service_cb function,you can add your function on xxx_ui.c");
    return ESP_OK;
}
#else
extern esp_err_t input_key_service_cb(periph_service_handle_t handle, periph_service_event_t *evt, void *ctx);
extern esp_err_t wifi_service_cb(periph_service_handle_t handle, periph_service_event_t *evt, void *ctx);
#endif

void audio_player_callback(audio_player_state_t *audio, void *ctx)
{
    ESP_LOGE(TAG, "AUDIO_PLAYER_CALLBACK send OK, status:%d, err_msg:%x, media_src:%x, ctx:%p",
             audio->status, audio->err_msg, audio->media_src, ctx);
#ifdef CONFIG_ENABLE_MUSIC_UNIT
    extern void send_music_queue(music_type_t type, unit_data_t *pdata);
    if(audio->status == AUDIO_PLAYER_STATUS_FINISHED && \
        audio->media_src == MEDIA_SRC_TYPE_MUSIC_HTTP && \
        RUNNING_STATE == get_music_player_state()) {
        send_music_queue(ALL_TYPE, NULL);         //start playing next song or resuming after playing finish
    }
#endif
}

static const char *conn_state_str[] = { "Disconnected", "Connecting", "Connected", "Disconnecting" };
void __attribute__((weak)) user_a2dp_sink_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    ESP_LOGI(TAG, "A2DP sink user cb");
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT: {
            esp_a2d_cb_param_t *a2d = param;
            uint8_t *bda = a2d->conn_stat.remote_bda;
            memcpy(&g_bd_addr, &a2d->conn_stat.remote_bda, sizeof(esp_bd_addr_t));
            ESP_LOGI(TAG, "A2DP connection state: %s, [%02x:%02x:%02x:%02x:%02x:%02x]",
                                conn_state_str[a2d->conn_stat.state], bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
            if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
                ESP_LOGI(TAG, "A2DP disconnected");
                display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_BT_DISCONNECTED, 0);
                bdsc_play_hint(BDSC_HINT_BT_DISCONNECTED);
            } else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
                ESP_LOGI(TAG, "A2DP connected");
                display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_BT_CONNECTED, 0);
                bdsc_play_hint(BDSC_HINT_BT_CONNECTED);
            } else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTING) {
            }
            break;
        }
        case ESP_A2D_AUDIO_STATE_EVT: {
            // Some wrong actions occur if use this event to control
            // playing in bluetooth source. So we use avrc control.
            break;
        }
        default:
            ESP_LOGI(TAG, "User cb A2DP event: %d", event);
            break;
    }
}

void check_smartconfig_on_boot()
{
    // step 8. enter wifi config state
    if (wifi_service_get_ssid_num(g_wifi_serv) == 0) {
        g_wifi_config_event = xEventGroupCreate();
        ESP_LOGI(TAG, "app config wifi.");
#ifdef CONFIG_ESP_BLUFI
        ble_gatts_module_start_adv();
        blufi_set_sta_connected_flag(g_wifi_setting, false);
#endif
        wifi_service_setting_start(g_wifi_serv, 0);
        display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_WIFI_SETTING, 0);
        g_wifi_setting_flag = true;
        // Wait for boot hint play done.
        vTaskDelay(6000 / portTICK_PERIOD_MS);
        bdsc_play_hint(BDSC_HINT_GREET);
        // wait wifi config forever!
        xEventGroupWaitBits(g_wifi_config_event, 1, true, false, portMAX_DELAY);
        vEventGroupDelete(g_wifi_config_event);
        g_wifi_config_event = NULL;
    }
}

void app_init(void)
{
    // Clear the debug message
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("A2DP_STREAM", ESP_LOG_INFO);
    esp_log_level_set("AUDIO_ELEMENT", ESP_LOG_DEBUG);
    esp_log_level_set("AUDIO_PIPELINE", ESP_LOG_INFO);
    esp_log_level_set("HTTP_STREAM", ESP_LOG_INFO);
    esp_log_level_set("spi_master", ESP_LOG_WARN);
    esp_log_level_set("ESP_AUDIO_CTRL", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_AUDIO_TASK", ESP_LOG_DEBUG);
    esp_log_level_set("AUDIO_MANAGER", ESP_LOG_DEBUG);

#if CONFIG_CUPID_BOARD_V2
    // Only care about power on case and let watchdog or other soft resets work.
    if (rtc_get_reset_reason(0) == POWERON_RESET) {
        g_long_press_power_on = xEventGroupCreate();
    }
    // Make sure power on gpio output high level to power on steady.
    audio_board_force_power_on();
#endif

    // step 1. init the peripherals set and peripherls
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    periph_cfg.extern_stack = true;
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

#if CONFIG_CUPID_BOARD_V2
    audio_board_bat_detect_init();
#endif
    // Can not get correct battery voltage here because of 
    // no adc width config in audio_board_bat_detect_init().
    // So check it later to choose power off or on.

    // step 2. initialize sdcard
#if CONFIG_CUPID_BOARD_V2
#else
    //audio_board_sdcard_init(set, SD_MODE_1_LINE);
#endif

    // step 3. setup display service
    g_disp_serv = audio_board_led_init();

	
    // step 4. setup the input key service
    // Move key initialization ahead of wifi to make long press power on
    // event occur earlier.
    audio_board_key_init(set);
    input_key_service_info_t input_info[] = INPUT_KEY_DEFAULT_INFO();
    input_key_service_cfg_t key_serv_info = INPUT_KEY_SERVICE_DEFAULT_CONFIG();
    key_serv_info.based_cfg.extern_stack = true;
    key_serv_info.handle = set;
    periph_service_handle_t input_key_handle = input_key_service_create(&key_serv_info);

    AUDIO_NULL_CHECK(TAG, input_key_handle, return);
    input_key_service_add_key(input_key_handle, input_info, INPUT_KEY_NUM);
    periph_service_set_callback(input_key_handle, input_key_service_cb, NULL);

#if CONFIG_CUPID_BOARD_V2
    // Check if power off or on
    uint32_t bat_voltage = audio_board_get_battery_voltage();
    if (bat_voltage < POWER_ON_PROHIBIT_LEVEL) {
        display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_BATTERY_LOW, 0);
        vTaskDelay(50 / portTICK_PERIOD_MS);
        ESP_LOGE(TAG, "battery volage is low,force power off");
        audio_board_force_power_off();
    }

    // Check if play key long press from start
    // 200ms got by statistics, can be modified.
    // Only care about power on case and let watchdog or other soft resets work.
    if (rtc_get_reset_reason(0) == POWERON_RESET) {
        if (xEventGroupWaitBits(g_long_press_power_on, 1, true, false, 
                                200 / portTICK_PERIOD_MS) != 1) {
            audio_board_force_power_off();
        }
        else {
            display_service_set_pattern(g_disp_serv, DISPLAY_PATTERN_BOOT, 0);
            vEventGroupDelete(g_long_press_power_on);
            g_long_press_power_on = NULL;
        }
    }
#endif
    //app_console_init(set);
    
    // step 5. setup Wi-Fi service
    wifi_service_config_t cfg = WIFI_SERVICE_DEFAULT_CONFIG();
    cfg.evt_cb = wifi_service_cb;
    cfg.cb_ctx = NULL;
    cfg.setting_timeout_s = 3600;
    cfg.max_retry_time = -1;
    g_wifi_serv = wifi_service_create(&cfg);
    int reg_idx = 0;

#ifdef CONFIG_AIRKISS_ENCRYPT
    airkiss_config_info_t air_info = AIRKISS_CONFIG_INFO_DEFAULT();
    air_info.lan_pack.appid = CONFIG_AIRKISS_APPID;
    air_info.lan_pack.deviceid = CONFIG_AIRKISS_DEVICEID;
    air_info.aes_key = CONFIG_AIRKISS_KEY;
    g_wifi_setting = airkiss_config_create(&air_info);
    ESP_LOGI(TAG, "AIRKISS wifi setting module has been selected");
#elif (defined CONFIG_ESP_SMARTCONFIG)
    smart_config_info_t info = SMART_CONFIG_INFO_DEFAULT();
    g_wifi_setting = smart_config_create(&info);
    ESP_LOGI(TAG, "SMARTCONFIG wifi setting module has been selected");
#elif (defined CONFIG_ESP_BLUFI)
    ESP_LOGI(TAG, "ESP_BLUFI wifi setting module has been selected");
    g_wifi_setting = blufi_config_create(NULL);
#endif
    esp_wifi_setting_regitster_notify_handle(g_wifi_setting, (void *)g_wifi_serv);
    wifi_service_register_setting_handle(g_wifi_serv, g_wifi_setting, &reg_idx);
    if (wifi_service_get_ssid_num(g_wifi_serv) > 0) {
        wifi_service_connect(g_wifi_serv);
    } else {
        // need smartconfig，move to bdsc_engine
        // because we relay on custom data.
    }

    // step 6. setup the esp_player
    app_player_init(NULL, audio_player_callback, set, user_a2dp_sink_cb);
    audio_player_vol_set(40);
}

