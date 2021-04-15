/*This is tone file*/

const char* tone_uri[] = {
   "flash://tone/0_already_new.mp3",
   "flash://tone/1_bad_net_report.mp3",
   "flash://tone/2_boot.mp3",
   "flash://tone/3_bt_connect.mp3",
   "flash://tone/4_bt_disconnect.mp3",
   "flash://tone/5_downloaded.mp3",
   "flash://tone/6_dsp_load_fail.mp3",
   "flash://tone/7_duHome_greet.mp3",
   "flash://tone/8_greet.mp3",
   "flash://tone/9_linked.mp3",
   "flash://tone/10_not_find.mp3",
   "flash://tone/11_ota_fail.mp3",
   "flash://tone/12_ota_start.mp3",
   "flash://tone/13_shut_down.mp3",
   "flash://tone/14_smart_config.mp3",
   "flash://tone/15_unlinked.mp3",
   "flash://tone/16_unsteady.mp3",
   "flash://tone/17_wakeup.mp3",
   "flash://tone/18_wakeup1.mp3",
   "flash://tone/19_wakeup2.mp3",
   "flash://tone/20_wakeup3.mp3",
   "flash://tone/21_wakeup4.mp3",
   "flash://tone/22_wifi_config_fail.mp3",
   "flash://tone/23_wifi_config_ok.mp3",
   "flash://tone/24_wifi_configuing.mp3",
};

int get_tone_uri_num()
{
    return sizeof(tone_uri) / sizeof(char *) - 1;
}
