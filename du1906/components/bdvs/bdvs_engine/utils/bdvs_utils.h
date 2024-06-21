/**
 * this file is common function
 * 
 */
#ifndef COMMON_SERVICE_H
#define COMMON_SERVICE_H

#include <string>
/**
 * get current system utc time
 * 
 * unit is ms
 */
// long get_current_time_ms();
int64_t get_current_time_ms();

/**
 * MD5 compute function
 * 
 * this function is used to compute md5
 */
std::string cal_md5(std::string &src);

/**
 * get signature of the function
 * 
 * this function is used to compute the signature
 */
std::string get_signature(long ts, std::string ak, std::string sk);

/**
 * @brief create rand number
 * 
 * this function is used to create random number
 */
int create_rand_number(int max, int min);

#endif
