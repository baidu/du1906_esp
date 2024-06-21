/**
 * @file asr_event_send.cpp
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2022-03-23
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "asr_event_send.h"

#include <stdio.h>
#include <stdlib.h>

#include "log.h"
#include "cJSON.h"
#include "bdsc_tools.h"
#include "bdsc_profile.h"
#include "bdsc_engine.h"
#include "dev_status.h"

#include "bds_common_utility.h"

#define TAG "ASR_EVENT_SEND"
static void con_start_event_pam(char *pam_data)
{
    if (pam_data == NULL) {
        return;
    }

    char g_pam[] = "{ \
        \"dueros-device-id\": \"6bbe894db80cbfd2b3463e17e8614853\", \
        \"StandbyDeviceId\": \"\", \
        \"user-agent\": \"test\", \
        \"Authorization\": \"Bearer 24.87f8cff7ef53b5175e1b4a3b382f1e7f.2592000.1643513268.282335-16030937\", \
        \"from\": \"dumi\", \
        \"LinkVersion\": 2 \
    }";

    memcpy(pam_data, g_pam, strlen(g_pam) + 1);
}

int start_event_send_data(void* handle, char* in_data)
{
    /*
     * CMD_EVENTUPLOAD_START,CMD_EVENTUPLOAD_CANCEL,CMD_EVENTUPLOAD_DATA
    */

    if (in_data == NULL) {
        return -1;
    }

    // start
    char sn[37];
    int ret = 0;
    vendor_info_t *g_vendor_info = g_bdsc_engine->g_vendor_info;
    bds_generate_uuid(sn);
    char *pam_data = malloc(4096);
    if (pam_data == NULL) {
        return -1;
    }

    con_start_event_pam(pam_data);

    if ((g_vendor_info->bdvs_pid == NULL) || (g_vendor_info->bdvs_key == NULL)) {
        bds_hh2_loge(TAG, "bdvsid pid or bdvs_key is empty");
        free(pam_data);
        return -1;
    }

    bds_hh2_loge(TAG, "bdvsid pid is %s, bdvs_key is %s, sn is %s", g_vendor_info->bdvs_pid, g_vendor_info->bdvs_key, sn);

    bdsc_eventupload_params_t *event_params = bdsc_event_params_create(sn, atoi(g_vendor_info->bdvs_pid), 
                                                                       g_vendor_info->bdvs_key, 
                                                                       g_bdsc_engine->cuid, strlen(pam_data) + 1, pam_data);
    bds_client_command_t event_start = {
            .key = CMD_EVENTUPLOAD_START,
            .content = event_params,
            .content_length = sizeof(bdsc_eventupload_params_t) + strlen(pam_data) + 1
    };

    ret = bds_client_send(handle, &event_start);
    if (ret != 0) {
        bds_hh2_loge(TAG, "send the start data failed %d", ret);
        bdsc_event_params_destroy(event_params);
        free(pam_data);
        return -1;
    }

    bdsc_event_params_destroy(event_params);
    free(pam_data);

    // data
    bdsc_cmd_data_t *data = bdsc_cmd_data_create(FLAG_TAIL, strlen(in_data), (uint8_t*)in_data, sn);
    bds_client_command_t event_data = {
            .key = CMD_EVENTUPLOAD_DATA,
            .content = data,
            .content_length = sizeof(bdsc_cmd_data_t) + strlen(in_data)
    };

    ret = bds_client_send(handle, &event_data);
    if (ret != 0) {
        bds_hh2_loge(TAG, "send the data failed %d", ret);
        bdsc_cmd_data_destroy(data);
        return -1;
    }

    bdsc_cmd_data_destroy(data);

    return 0;
}

int cancel_event_send_data(void *handle)
{
    bds_client_command_t event_cancel = {
        .key = CMD_EVENTUPLOAD_CANCEL,
        .content = NULL,
        .content_length = 0
    };

    bds_client_send(handle, &event_cancel);
    return 0;
}
