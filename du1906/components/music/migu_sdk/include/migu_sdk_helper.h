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
#ifndef __MIGU_SDK_HELPER__H
#define __MIGU_SDK_HELPER__H

#ifdef __cplusplus
extern "C" {
#endif

int migu_post_to_unit_server(const char *uri, const char *private_body_str, char **ret_data_out, size_t *data_out_len);

void migu_upload_music_info_to_unit(int start_time, int end_time);

int migu_get_url_by_id(const char *id, char *url, uint32_t max_len);

int migu_request_next_music(void);

int migu_get_sdk_init_info(char *ret_info, size_t len);

int migu_active_music_license();

int migu_refresh_token();

#ifdef __cplusplus
}
#endif

#endif