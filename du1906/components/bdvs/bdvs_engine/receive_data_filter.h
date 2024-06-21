#ifndef RECEIVE_DATA_PARSE_H
#define RECEIVE_DATA_PARSE_H

#include <iostream>
#include <string>
#include <map>

#ifdef __cplusplus
extern "C" {
#endif

#include "cJSON.h"

#ifdef __cplusplus
}
#endif

enum bdvs_cmd_bit{
    BDVS_CMD_BIT_RESET                  = (0),
    BDVS_CMD_BIT_VOICE_OUTPUT_SPEAK     = (0x01<<0),    // tts
    BDVS_CMD_BIT_AUDIO_PLAYER_URL_PLAY  = (0x01<<1),    // url
    BDVS_CMD_BIT_AUDIO_PLAYER_ID_PLAY   = (0x01<<2),    // id
    BDVS_CMD_BIT_AUDIO_PLAYER_PAUSE     = (0x01<<3),
    BDVS_CMD_BIT_AUDIO_PLAYER_STOP      = (0x01<<4),
    BDVS_CMD_BIT_AUDIO_PLAYER_CONTINUE  = (0x01<<5)
};

#define CHECK_CMD_BITS(var,bits) ((var) & (bits))
#define SET_CMD_BITS(var,bits)   ((var) |= (bits))

extern int g_bdvs_cmd_bit_set;
extern char *g_bdvs_cmd_url;
extern char *g_bdvs_cmd_id;

typedef int (*CLASS_HANDLE_FUNC)(cJSON *in_value);
typedef int (*ACTION_HANDLE_FUNC)(cJSON *in_value);
typedef int (*INTENT_HANDLE_FUNC)(std::string in_value); // in string so it can Asynchronous Processing

extern std::map<std::string, CLASS_HANDLE_FUNC> class_handle; // origin, action, custom
extern std::map<std::string, ACTION_HANDLE_FUNC> action_handle; // action handle
extern std::map<std::string, INTENT_HANDLE_FUNC> intent_handle; // intent handle

/**
 * @brief try to init the map
 * 
 * @return int 
 */
extern "C" int receive_data_handle_init();

/**
 * @brief add new type handle function
 * 
 * @param type_name name value
 * @param in_func handle function
 * @return int 
 */
int add_new_action_handle(std::string type_name, ACTION_HANDLE_FUNC in_func);

/**
 * @brief add new intent handle function
 * 
 * @param funcset 
 * @param intent 
 * @param in_func 
 * @return int 
 */
int add_new_intent_handle(std::string funcset, std::string intent, INTENT_HANDLE_FUNC in_func);

/**
 * @brief parse asr result
 * 
 * @param in_str 
 * @param sn 
 * @return std::string 
 */
// std::string parse_asr_result(std::string in_str, std::string &sn);

/**
 * @brief parse the nlp result
 * 
 * @param in_str 
 * @return int 
 */
// int parse_nlp_result(std::string in_str);

extern "C" int receive_nlp_data_handle(char *in_str);

#endif
