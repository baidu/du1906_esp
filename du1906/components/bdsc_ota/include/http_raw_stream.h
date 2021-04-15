/*
 * Copyright (c) 2020 Baidu.com, Inc. All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); 
 * you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on
 * an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 */
#ifndef _HTTP_RAW_STREAM_H_
#define _HTTP_RAW_STREAM_H_

#include "audio_error.h"
#include "audio_element.h"
#include "audio_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief      HTTP RAW Stream configurations
 *             Default value will be used if any entry is zero
 */
typedef struct {
    audio_stream_type_t         type;                   /*!< Type of stream */
    int                         out_rb_size;            /*!< Size of output ringbuffer */
    int                         task_stack;             /*!< Task stack size */
    int                         task_core;              /*!< Task running in core (0 or 1) */
    int                         task_prio;              /*!< Task priority (based on freeRTOS priority) */
    bool                        stack_in_ext;           /*!< Try to allocate stack in external memory */
    void                        *user_data;             /*!< User data context */
} http_raw_stream_cfg_t;


#define HTTP_STREAM_TASK_STACK          (6 * 1024)
#define HTTP_STREAM_TASK_CORE           (0)
#define HTTP_STREAM_TASK_PRIO           (4)
#define HTTP_STREAM_RINGBUFFER_SIZE     (20 * 1024)

#define HTTP_RAW_STREAM_CFG_DEFAULT() {              \
    .type = AUDIO_STREAM_READER,                 \
    .out_rb_size = HTTP_STREAM_RINGBUFFER_SIZE,  \
    .task_stack = HTTP_STREAM_TASK_STACK,        \
    .task_core = HTTP_STREAM_TASK_CORE,          \
    .task_prio = HTTP_STREAM_TASK_PRIO,          \
    .stack_in_ext = true,                        \
    .user_data = NULL,                           \
}

/**
 * @brief      Create a handle to an Audio Element to stream data from HTTP to another Element
 *             or get data from other elements sent to HTTP, depending on the configuration
 *             the stream type, either AUDIO_STREAM_READER or AUDIO_STREAM_WRITER.
 *
 * @param      config  The configuration
 *
 * @return     The Audio Element handle
 */
audio_element_handle_t http_raw_stream_init(http_raw_stream_cfg_t *config);

#ifdef __cplusplus
}
#endif

#endif
