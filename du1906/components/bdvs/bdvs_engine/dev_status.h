/**
 * @file dev_status.h
 * @author wangmeng (wangmeng43@baidu.com)
 * @brief 
 * @version 0.1
 * @date 2022-04-02
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#ifndef _DEV_STATUS_H
#define _DEV_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dev_status {
    // player status
    char play_state[8];
    char domain[8];
    long progress;
    long total_length;

    char *track_id;

    // map status
    char map_state[5];
} dev_status_t;

extern dev_status_t g_dev_handle;

void dev_status_set_play_state(dev_status_t *in_dev, char *state);

char *dev_status_get_play_state(dev_status_t *in_dev);

void dev_status_set_domain(dev_status_t *in_dev, char *domain);

char *dev_status_get_domain(dev_status_t *in_dev);

void dev_status_set_progress(dev_status_t *in_dev, long progress);

long dev_status_get_progress(dev_status_t *in_dev);

void dev_status_set_total_len(dev_status_t *in_dev, long total_len);

long dev_status_get_total_len(dev_status_t *in_dev);

void dev_status_set_map_state(dev_status_t *in_dev, char *map_state);

char *dev_status_get_map_state(dev_status_t *in_dev);

void dev_status_set_trackid(dev_status_t *in_dev, char *trackid);

char *dev_status_get_trackid(dev_status_t *in_dev);

#ifdef __cplusplus
}
#endif

#endif
