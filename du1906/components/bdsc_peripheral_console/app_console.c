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

#include "periph_console.h"
#include "esp_log.h"
#include "bds_common_utility.h"
#include "bdsc_engine.h"

static const char *TAG = "CUPID_CONSOLE";
extern bool print_task_status;

static esp_err_t cli_print_on(esp_periph_handle_t periph, int argc, char *argv[])
{
    if (argc != 0) {
        // no parameter
        // make wrong trigger return directly.
        return ESP_FAIL;
    }

    print_task_status = true;
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("A2DP_STREAM", ESP_LOG_INFO);
    esp_log_level_set("AUDIO_ELEMENT", ESP_LOG_INFO);
    esp_log_level_set("AUDIO_PIPELINE", ESP_LOG_ERROR);
    esp_log_level_set("spi_master", ESP_LOG_WARN);
    esp_log_level_set("ESP_AUDIO_CTRL", ESP_LOG_INFO);
    esp_log_level_set("ESP_AUDIO_TASK", ESP_LOG_INFO);
    esp_log_level_set("wakeup_hal", ESP_LOG_WARN);

    ESP_LOGI(TAG, "print on.");

    return ESP_OK;
}

static esp_err_t cli_print_off(esp_periph_handle_t periph, int argc, char *argv[])
{
    if (argc != 0) {
        // no parameter
        // make wrong trigger return directly.
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "print off.");
    
    esp_log_level_set("*", ESP_LOG_NONE);
    print_task_status = false;
    
    return ESP_OK;
}

static esp_err_t cli_print_change(esp_periph_handle_t periph, int argc, char *argv[])
{
    if (argc != 2) {
        // should have 2 arguments.
        // make wrong trigger return directly.
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "print change.");

    // level <= 5, mapping to esp_log_level_t
    // level > 5, use level = 10, 11, 12, 13, 14 as 0, 1, 2, 3, 4
    int level;
    level = atoi(argv[1]);
    if (level > 5) {
        if ((g_bdsc_engine != NULL) && (g_bdsc_engine->g_client_handle != NULL)) {
            bds_set_log_level((level - 10));
        }
    } else {
        esp_log_level_set(argv[0], level);
    }
    
    return ESP_OK;
}

const periph_console_cmd_t cupid_cli_cmd[] = {
    {.cmd = "print_on", .id = 1, .help = "Print on", .func = cli_print_on},
    {.cmd = "print_off", .id = 2, .help = "Print off", .func = cli_print_off},
    {.cmd = "print_change", .id = 3, .help = "Print level change", .func = cli_print_change},
};

esp_err_t app_console_init(esp_periph_set_handle_t set)
{
    esp_err_t ret;
    periph_console_cfg_t console_cfg = {
        .command_num = sizeof(cupid_cli_cmd) / sizeof(periph_console_cmd_t),
        .commands = cupid_cli_cmd,
        .buffer_size = 384,
    };
    esp_periph_handle_t console_handle = periph_console_init(&console_cfg);
    ret = esp_periph_start(set, console_handle); 

    return ret;
}
