/**
 * @file asr_event_send.h
 * @author wangmeng (wangmeng43@baidu.com)
 * @brief 
 * @version 0.1
 * @date 2022-03-23
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#ifndef _ASR_EVENT_SEND_H
#define _ASR_EVENT_SEND_H

#ifdef __cplusplus
extern "C" {
#endif

int start_event_send_data(void *handle, char* in_data);

int cancel_event_send_data(void *handle);

int asr_online_pam_send(char *out_pam, int maxlen);

#ifdef __cplusplus
}
#endif

#endif
