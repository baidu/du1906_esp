/**
 * @file dev_status.cpp
 * @author wangmeng (wangmeng43@baidu.com)
 * @brief 
 * @version 0.1
 * @date 2022-04-02
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "dev_status.h"

#include "string.h"
#include "stdlib.h"

dev_status_t g_dev_handle;

// play state
void dev_status_set_play_state(dev_status_t *in_dev, char *state)
{
    if (in_dev == NULL || state == NULL) {
        return;
    }

    memcpy(in_dev->play_state, state, strlen(state) + 1);
}

char *dev_status_get_play_state(dev_status_t *in_dev)
{
    if (in_dev == NULL) {
        return NULL;
    }

    return in_dev->play_state;
}

// domain
void dev_status_set_domain(dev_status_t *in_dev, char *domain)
{
    if (in_dev == NULL || domain == NULL) {
        return;
    }

    memcpy(in_dev->domain, domain, strlen(domain) + 1);
}

char *dev_status_get_domain(dev_status_t *in_dev)
{
    if (in_dev == NULL) {
        return NULL;
    }

    return in_dev->domain;
}

// progress
void dev_status_set_progress(dev_status_t *in_dev, long progress)
{
    if (in_dev == nullptr) {
        return;
    }

    in_dev->progress = progress;
}

long dev_status_get_progress(dev_status_t *in_dev)
{
    if (in_dev == nullptr) {
        return 0;
    }

    return in_dev->progress;
}

// totallen
void dev_status_set_total_len(dev_status_t *in_dev, long total_len)
{
    if (in_dev == nullptr) {
        return;
    }

    in_dev->total_length = total_len;
}

long dev_status_get_total_len(dev_status_t *in_dev)
{
    if (in_dev == nullptr) {
        return 0;
    }

    return in_dev->total_length;
}

// map state
void dev_status_set_map_state(dev_status_t *in_dev, char *map_state)
{
    if (in_dev == NULL || map_state == NULL) {
        return;
    }

    memcpy(in_dev->map_state, map_state, strlen(map_state) + 1);
}

char *dev_status_get_map_state(dev_status_t *in_dev)
{
    if (in_dev == NULL) {
        return NULL;
    }

    return in_dev->map_state;
}

void dev_status_set_trackid(dev_status_t *in_dev, char *trackid)
{
    if (in_dev == NULL || trackid == NULL) {
        return;
    }

    int len = strlen(trackid);

    if (in_dev->track_id) {
        free(in_dev->track_id);
    }
    in_dev->track_id = (char *)malloc(len + 1);
    memcpy(in_dev->track_id, trackid, strlen(trackid) + 1);
}

char *dev_status_get_trackid(dev_status_t *in_dev)
{
    if (in_dev == NULL) {
        return NULL;
    }

    return in_dev->track_id;
}
