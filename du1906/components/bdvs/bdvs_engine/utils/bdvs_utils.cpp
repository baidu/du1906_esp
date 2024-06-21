#include "bdvs_utils.h"

#include "md5.h"
#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <sstream>
#include <sys/time.h>
#include "log.h"

#define  TAG  "COMM_SERV"

int64_t get_current_time_ms()
{
    int64_t utc_ts = 1597825065;
    struct timeval current_time;
    
    gettimeofday(&current_time, NULL);
    bds_hh2_logi(TAG, "current ts: %ld\n", current_time.tv_sec);
    if (current_time.tv_sec < 1597825065) {
        bds_hh2_loge(TAG, "NTP server not synced yet!!!");
        return -1;
    }
    // 当心溢出
    // utc_ts = current_time.tv_sec * 1000 + current_time.tv_usec / 1000;
    int64_t tv_sec, tv_usec;
    tv_sec = current_time.tv_sec; // long -> int64_t
    tv_usec = current_time.tv_usec;
    utc_ts = tv_sec * 1000 + tv_usec / 1000;
    return utc_ts;
}


std::string cal_md5(std::string &src)
{
    MD5_CTX ctx;

    std::string md5String;
    unsigned char md[16] = {0};
    char tmp[33] = {0};

    md5_init(&ctx);
    md5_update(&ctx, (unsigned char *)src.c_str(), src.size());
    md5_final(&ctx, md);

    for(int i = 0; i < 16; ++i) {   
        memset(tmp, 0x00, sizeof(tmp));
        snprintf(tmp, 33, "%02X", md[i]);
        md5String += tmp;
    }

    return md5String;
}

std::string get_signature(long ts, std::string ak, std::string sk) {
    bds_hh2_logi(TAG, "enter %s", __func__);
    std::stringstream stream;
    stream << ts;
    std::string atsString = ak + "&" + stream.str() + sk;
    //std::string atsString = ak + "&" + std::to_string(ts) + sk;
    std::string result = cal_md5(atsString);
    transform(result.begin(), result.end(), result.begin(), ::tolower);
    bds_hh2_loge(TAG, "signature is %s", result.c_str());
    return result;
}

int create_rand_number(int max, int min)
{
    // get a seed
    srand(unsigned(time(0)));

    int result = rand() % (max - min + 1) + min;
    return result;
}
