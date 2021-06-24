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

#ifndef __APP_CONTROL_H__
#define __APP_CONTROL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_wifi_setting.h"
#include "periph_service.h"
#include "display_service.h"
#include "esp_bt_defs.h"

#define WIFI_CONNECTED_BIT (BIT0)
#define WIFI_WAIT_CONNECT_TIME_MS  (15000 / portTICK_PERIOD_MS)
#if CONFIG_CUPID_BOARD_V2
#define POWER_ON_PROHIBIT_LEVEL (1100)
#endif

/**
 * @brief Initializes du1906 application
 *
 */
void app_system_setup(void);

#if CONFIG_CUPID_BOARD_V2
extern EventGroupHandle_t g_long_press_power_on;
#endif
extern EventGroupHandle_t g_wifi_config_event;
extern periph_service_handle_t g_wifi_serv;
extern display_service_handle_t g_disp_serv;
extern esp_wifi_setting_handle_t g_wifi_setting;
extern esp_bd_addr_t g_bd_addr;
extern bool g_wifi_setting_flag;

#ifdef __cplusplus
}
#endif

#endif
