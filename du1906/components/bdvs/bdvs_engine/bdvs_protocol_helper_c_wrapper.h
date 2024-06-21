#ifndef _PROTO_HELPER_H
#define _PROTO_HELPER_H

#include "generate_pam.h"
#include "bdsc_tools.h"
#include "bdsc_profile.h"
#include "log.h"
#include "bdsc_http.h"

#ifdef __cplusplus
extern "C" {
#endif

char* bdvs_device_active_request_build_c_wrapper();

char* bdvs_active_tts_request_build_c_wrapper(char* in_text);

char* bdvs_asr_pam_build_c_wrapper(char* ak, char* sk, char* pk, char* fc, int pid);

char* bdvs_asr_data_parse_c_wrapper(char* in_str, char* sn);

int bdvs_nlp_data_parse_c_wrapper(char* in_str);

int bdvs_action_media_play_parse_c_wrapper(cJSON *action);

#ifdef __cplusplus
}
#endif

#endif
