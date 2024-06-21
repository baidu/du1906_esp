/**
 * @file active_device.h
 * @author wangmeng (wangmeng43@baidu.com)
 * @brief 
 * @version 0.1
 * @date 2022-03-15
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#ifndef ACTIVE_DEVICE_H
#define ACTIVE_DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief init the active device receive handle
 * 
 */
void active_device_handle_init();

/**
 * @brief start the active device task
 * 
 * @return int 
 */
int start_active_device_task();

#ifdef __cplusplus
}
#endif

#endif