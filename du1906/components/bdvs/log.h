#ifndef  _BDS_HHS_LOG_H
#define  _BDS_HHS_LOG_H

#include "esp_log.h"
#ifdef __cplusplus
extern "C" {
#endif

#define bds_hh2_logd(tag, fmt, ...) \
do { \
    ESP_LOGD(tag, fmt, ##__VA_ARGS__); \
} while(0)

#define bds_hh2_logi(tag, fmt, ...) \
do { \
    ESP_LOGI(tag, fmt, ##__VA_ARGS__); \
} while(0)

#define bds_hh2_logw(tag, fmt, ...) \
do { \
    ESP_LOGW(tag, fmt, ##__VA_ARGS__); \
} while(0)

#define bds_hh2_loge(tag, fmt, ...) \
do { \
    ESP_LOGE(tag, fmt, ##__VA_ARGS__); \
} while(0)

#ifdef __cplusplus
}
#endif
#endif