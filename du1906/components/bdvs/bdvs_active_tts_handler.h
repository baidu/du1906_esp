/**
 * @file active_tts.h
 * @author wangmeng (wangmeng43@baidu.com)
 * @brief 
 * @version 0.1
 * @date 2022-03-22
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#ifndef _ACTIVE_TTS_H
#define _ACTIVE_TTS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief used to set tts param, while open later
 * 
 * @param ttsparam 
 */
void active_tts_set_tts_param(char *ttsparam);

/**
 * @brief send active tts data
 * 
 */
void active_tts_send_data(char *in_text);

/**
 * @brief active tts handle init function
 * 
 */
void active_tts_handle_init();

#ifdef __cplusplus
}
#endif

#endif
