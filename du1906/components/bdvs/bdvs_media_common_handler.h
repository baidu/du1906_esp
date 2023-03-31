/**
 * @file media_common.h
 * @author wangmeng (wangmeng43@baidu.com)
 * @brief 
 * @version 0.1
 * @date 2022-03-22
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#ifndef _MEDIA_COMMON_H
#define _MEDIA_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief next media
 * 
 * @param domain :one of "music" "fm" "news"
 */
void media_control_event_next(char *domain);

/**
 * @brief 
 * 
* @param domain :one of "music" "fm" "news"
 */
void media_control_event_pre(char *domain);

/**
 * @brief get the media progress
 * 
 * @param domain :one of "music" "fm" "news"
 */
void media_control_event_progress(char *domain);

/**
 * @brief init the media handle function
 * 
 */
void media_handle_init();

#ifdef __cplusplus
}
#endif

#endif
