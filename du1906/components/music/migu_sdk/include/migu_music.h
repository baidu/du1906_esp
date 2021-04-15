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

typedef enum {
    MIGU_START_EVENT,
    END_EVENT,
} migu_time_event_type;

int migu_music_init(void);
int get_migu_url(const char *id, char *url, uint32_t max_len);

void send_migu_event_queue(migu_time_event_type event_type);

int migu_request_next_music(void);

int migu_active_music_license();

#ifdef __cplusplus
}
#endif

#endif