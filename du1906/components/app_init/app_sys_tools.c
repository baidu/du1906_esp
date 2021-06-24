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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "audio_mem.h"
#include "board.h"
#include "audio_thread.h"
#include "app_task_register.h"

static const char *TAG = "APP_SYS_TOOLS";

#define SYS_PRINT_TASK_STATUS_INTERVAL   (5 * 1000)

extern esp_err_t esp_print_real_time_stats(TickType_t xTicksToWait);
void sys_monitor_task(void *para)
{
    while (1) {
        vTaskDelay(SYS_PRINT_TASK_STATUS_INTERVAL / portTICK_PERIOD_MS);
        AUDIO_MEM_SHOW(TAG);
        esp_print_real_time_stats(3000);
        ESP_LOGI(TAG, "Stack: %d", uxTaskGetStackHighWaterMark(NULL));
#ifdef CONFIG_FREERTOS_USE_TRACE_FACILITY
        //static char buf[2048];
        //vTaskList(buf);
        //printf("Task List:\nTask Name    Status   Prio    HWM    Task Number\n%s\n", buf);
#endif
#if CONFIG_CUPID_BOARD_V2
        audio_board_battery_detect();
#endif
    }
    vTaskDelete(NULL);
}

void start_sys_monitor(void)
{
    int ret = app_task_regist(APP_TASK_ID_SYS_MONITOR, sys_monitor_task, NULL, NULL);
    if (ret == ESP_FAIL) {
        ESP_LOGE(TAG, "Couldn't create sys_monitor_task");
    }
}
