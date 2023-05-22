/**
 * @file intent_handle.h
 * @author wangmeng (wangmeng43@baidu.com)
 * @brief 
 * @version 0.1
 * @date 2022-03-22
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#ifndef _INTENT_HANDLE_H
#define _INTENT_HANDLE_H

#ifdef __cplusplus
extern "C" {
#endif

void intent_handle_init();

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

#ifdef __cplusplus
}
#endif

#endif
