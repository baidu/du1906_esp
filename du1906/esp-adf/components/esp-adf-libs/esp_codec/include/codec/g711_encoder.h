// Copyright (c) 2020 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
// All rights reserved.

#ifndef _G711_ENCODER_H_
#define _G711_ENCODER_H_

#include "esp_err.h"
#include "audio_element.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief      G711 Encoder configurations
 */
typedef struct {
    int                     out_rb_size;    /*!< Size of output ringbuffer */
    int                     task_stack;     /*!< Task stack size */
    int                     task_core;      /*!< Task running in core (0 or 1) */
    int                     task_prio;      /*!< Task priority (based on freeRTOS priority) */
    bool                    stack_in_ext;   /*!< Try to allocate stack in external memory */
    bool                    enc_mode;       /*!< 0: a-law  1: u-law */
} g711_encoder_cfg_t;

#define G711_ENCODER_TASK_STACK          (3 * 1024)
#define G711_ENCODER_TASK_CORE           (0)
#define G711_ENCODER_TASK_PRIO           (5)
#define G711_ENCODER_RINGBUFFER_SIZE     (2 * 1024)

#define DEFAULT_G711_ENCODER_CONFIG() {\
    .out_rb_size        = G711_ENCODER_RINGBUFFER_SIZE,\
    .task_stack         = G711_ENCODER_TASK_STACK,\
    .task_core          = G711_ENCODER_TASK_CORE,\
    .task_prio          = G711_ENCODER_TASK_PRIO,\
    .stack_in_ext       = true,\
    .enc_mode           = 0,\
}

/**
 * @brief      Create a handle to an Audio Element to encode incoming data using G711 format
 *
 * @param      config  The configuration
 *
 * @return     The audio element handle
 */
audio_element_handle_t g711_encoder_init(g711_encoder_cfg_t *config);


#ifdef __cplusplus
}
#endif

#endif
