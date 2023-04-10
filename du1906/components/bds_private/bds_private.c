#include <stdio.h>
#include <string.h>
#include "bds_client_command.h"
#include "bds_client_context.h"
#include "bds_client_event.h"
#include "bds_client_params.h"
#include "bds_client.h"
#include "bds_common_utility.h"
#include "cJSON.h"
#include "bdsc_engine.h"
#include "bdsc_tools.h"

static const int pid = 1665;

int get_bds_primary_pid()
{
    /*about pid value, you can visit http://wiki.baidu.com/pages/viewpage.action?pageId=1498091635 */
    return pid;
}

int get_bds_assist_pid()
{
    /* APUS format. in longlink version, this param is not used! */
    return 1558;
}

const char *get_bds_key()
{
    return "com.baidu.iot";
}

const char *get_bds_host()
{    /*about host address, you can visit http://wiki.baidu.com/pages/viewpage.action?pageId=1498091635 */
    return "leetest.baidu.com";
}

typedef bdsc_asr_params_t* (*wrapper_func_t)(char *sn,
        uint32_t primary_pid, uint32_t assist_pid, char *key,
        uint16_t audio_rate, char *cuid, int backtrack_time,
        int voice_print, uint16_t pam_len, char *pam);

/**
 * @brief      bdsc_asr_params_create function wrapper, to keep keys in private
 */
bdsc_asr_params_t *bdsc_asr_params_create_wrapper(
        wrapper_func_t func,
        char *sn,
        uint16_t audio_rate, char *cuid, int backtrack_time, int voice_print,
        uint16_t pam_len, char *pam)
{
    return func(sn, get_bds_primary_pid(), get_bds_assist_pid(), get_bds_key(), audio_rate, cuid, backtrack_time,
                                                        voice_print, pam_len, pam);
}

bdsc_engine_params_t *bdsc_engine_params_create_wrapper(
    char *sn, char *app_name,
    uint16_t pam_len, char *pam)
{
    return bdsc_engine_params_create(sn, get_bds_primary_pid(),\
            get_bds_host(), 443, PROTOCOL_TLS, g_bdsc_engine->cuid, app_name,\
            LAUNCH_LOAD_DSP, pam_len, pam);
}

char* generate_md5_checksum_needfree(uint8_t *buf, size_t buf_len);

const char* generate_auth_sig_needfree(const char *ak, const int ts, const char *sk)
{
    char tmp[256] = {0};

    if (!ak || !sk) {
        return NULL;
    }
    sprintf(tmp, "%s&%d%s", ak, ts, sk);
    return generate_md5_checksum_needfree((uint8_t *)tmp, strlen(tmp));
}
