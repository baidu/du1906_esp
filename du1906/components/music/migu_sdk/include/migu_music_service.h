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
#ifndef __MIGU_MUSIC__H
#define __MIGU_MUSIC__H

#ifdef __cplusplus
extern "C" {
#endif

int migu_music_service_init();

int migu_active_music_license();

void migu_big_data_event(void);

/*
 * bref: loop fetch url by id interface
 */
extern EventGroupHandle_t g_get_url_evt_group;
#define GET_URL_REQUEST     (BIT0)
#define GET_URL_SUCCESS     (BIT1)
#define GET_URL_FAIL        (BIT2)

extern char g_migu_music_id_in[1024];
extern char g_migu_music_url_out[1024];

#ifdef __cplusplus
}
#endif

#endif