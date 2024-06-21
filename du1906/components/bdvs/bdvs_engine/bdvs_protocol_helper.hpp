#ifndef _BDVS_PROTOCOL_HELPER_HPP_
#define _BDVS_PROTOCOL_HELPER_HPP_

#include <string>

class BdvsProtoHelper {
public:
    // static BdvsProtoHelper* get_instance();

    // void set_event_listener(void(*listener)(BDVSMessage&,void*), void* userParam);

    // static void release_instance(BdvsProtoHelper* instance);

    static std::string bdvs_device_active_request_build();

    static std::string bdvs_active_tts_request_build(std::string in_text);

    static std::string bdvs_event_media_control_request_build(std::string cmd);

    static std::string bdvs_asr_pam_build(std::string ak, std::string sk, std::string pk, std::string fc, int pid);

    static std::string bdvs_asr_data_parse(std::string in_str, std::string &sn);

    static int bdvs_nlp_data_parse(std::string in_str);

    static int bdvs_action_media_play_parse(cJSON *action);

protected:
    ~BdvsProtoHelper();
};


#endif
