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

#include <string.h>
#include "esp_log.h"
#include "app_bt_init.h"
#include "a2dp_stream.h"
#include "ble_gatts_module.h"
#include "bdsc_tools.h"
#include "bluetooth_service.h"

static const char *TAG = "APP_BT_INIT";

#if CONFIG_CUPID_BOARD_V2
#define BT_DEVICE_NAME  "ESP_BT_CUPID"
#else
#define BT_DEVICE_NAME  "ESP_BT_DU1906"
#endif

char sn[16] = {0};
static bool bt_connected_flag;

esp_periph_handle_t app_bluetooth_init(esp_periph_set_handle_t set)
{
    ESP_LOGI(TAG, "Init Bluetooth module");
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BTDM));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ble_gatts_module_init();

    size_t length = 16; // _get_sn function need length no less than sn length
    esp_err_t set_dev_name_ret;
    set_dev_name_ret = bdsc_get_sn(sn, &length);
    ESP_LOGI(TAG, "sn = %s, length = %d, return %d\n", sn, length, set_dev_name_ret);
    if (set_dev_name_ret == ESP_OK) {
        set_dev_name_ret = esp_bt_dev_set_device_name(sn);
    } else {
        set_dev_name_ret = esp_bt_dev_set_device_name(BT_DEVICE_NAME);
    }

    if (set_dev_name_ret) {
        ESP_LOGE(TAG, "set device name failed, error code = %x", set_dev_name_ret);
    }
    esp_periph_handle_t bt_periph = bt_create_periph();
    esp_periph_start(set, bt_periph);
    ESP_LOGI(TAG, "Start Bluetooth peripherals");

    esp_bt_gap_set_scan_mode(ESP_BT_SCAN_MODE_NONE);
    return bt_periph;
}

esp_err_t app_bt_stop(esp_bd_addr_t bda)
{
    esp_err_t ret = ESP_OK;
    ESP_LOGI(TAG, "bt stop");
    if (bt_connected_flag == true) {
        ret |= esp_bt_gap_set_scan_mode(ESP_BT_SCAN_MODE_NONE);
        ret |= esp_a2d_sink_disconnect(bda);
        ret |= esp_a2d_sink_deinit();
        // ret |= esp_avrc_ct_deinit();
        bt_connected_flag = false;
    }
    return ret;
}

esp_err_t app_bt_start(void)
{
    esp_err_t ret = ESP_OK;
    ESP_LOGI(TAG, "bt start");
    if (bt_connected_flag == false) {
        ret |= esp_a2d_sink_init();
        // ret |= esp_avrc_ct_init();
        // ret |= esp_avrc_ct_register_callback(bt_app_rc_ct_cb);
        ret |= esp_bt_gap_set_scan_mode(ESP_BT_SCAN_MODE_CONNECTABLE_DISCOVERABLE);
        bt_connected_flag = true;
    }
    return ret;
}

extern esp_periph_handle_t g_bt_periph;

esp_err_t app_bt_pause(void)
{
    ESP_LOGI(TAG, "bt pause");
    if (bt_connected_flag == true) {
        periph_bluetooth_pause(g_bt_periph);
    }
    return 0;
}

esp_err_t app_bt_continue(void)
{
    ESP_LOGI(TAG, "bt continue");
    if (bt_connected_flag == true) {
        periph_bluetooth_play(g_bt_periph);
    }
    return 0;
}

// esp_err_t app_bt_stop(void)
// {
//     esp_err_t ret = ESP_OK;
//     ESP_LOGI(TAG, "bt stop");
//     if (bt_connected_flag == true) {
//         periph_bluetooth_stop(g_bt_periph);
//     }
// }

void app_bluetooth_deinit(void)
{
    ESP_LOGI(TAG, "Deinit Bluetooth module");
    ESP_ERROR_CHECK(esp_bluedroid_disable());
    ESP_ERROR_CHECK(esp_bluedroid_deinit());
    ESP_ERROR_CHECK(esp_bt_controller_disable());
    ESP_ERROR_CHECK(esp_bt_controller_deinit());

    ble_gatts_module_deinit();
}
