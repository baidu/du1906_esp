#ifndef __CUPID_DEVICE_MANAGER_H__
#define __CUPID_DEVICE_MANAGER_H__

#include <inttypes.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "freertos/event_groups.h"
#include "esp_event_loop.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <string.h>
#include "cJSON.h"
#include "bdsc_engine.h"

typedef struct {
    int st;
} cupid_device_manager_t;


int cupid_device_manager_init(cupid_device_manager_t *dm);

int cupid_device_manager_feed_data(cupid_device_manager_t *dm, 
                                    uint8_t *data, size_t data_len, void *userdata);

int cupid_device_manager_deinit(cupid_device_manager_t *dm);
#endif
