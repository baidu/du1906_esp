#include <inttypes.h>
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "freertos/event_groups.h"
#include "esp_event_loop.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <string.h>
#include "play_list.h"
#include "audio_mem.h"
#include "bdsc_tools.h"
#include "play_list.h"

#define TAG "PLAY_LIST"

pls_handle_t* pls_create()
{
    pls_handle_t *handle = (pls_handle_t*)audio_calloc(1, sizeof(pls_handle_t));
    return handle;
}

int pls_destroy(pls_handle_t *handle)
{
    pls_clean_list(handle);
    audio_free(handle);
    return 0;
}

int pls_get_length(pls_handle_t *handle)
{
    int cnt = 0;
    music_t *tmp = handle->pls_head;
    //ESP_LOGI(TAG, "==> pls_get_length %p", tmp);
    while (tmp) {
        //printf("%d\n", tmp->type);
        cnt++;
        tmp = tmp->next;
    }
    //ESP_LOGI(TAG, "len: %d", cnt);
    return cnt;
}


void pls_dump(pls_handle_t *handle)
{
    int cnt = 0;

    music_t *tmp = handle->pls_head;
    ESP_LOGD(TAG, "==> pls_dump");
    while (tmp) {
        printf("%d %d %d\n", tmp->type, tmp->action_type, tmp->play_state);
        cnt++;
        tmp = tmp->next;
    }
    printf("\n");
}

void delete_music(music_t *node)
{
    if (node == NULL) {
        return;
    }
    if (node->data) {
        audio_free(node->data);
    }
    audio_free(node);
}

music_t* create_music(music_queue_t pQueue_data);
int pls_cache_music(pls_handle_t *handle, music_queue_t pQueue_data)
{
    int pls_len = 0;
    music_t *new = NULL;

    pls_len = pls_get_length(handle);
    ESP_LOGD(TAG, "==> pls_cache_next_music");
    if (pls_len > 2) {
        ERR_OUT(ERR_RET, "pls full, quit");
    }
    if (!(new = create_music(pQueue_data))) {
        ERR_OUT(ERR_RET, "create_music fail");
    }
    if (pls_len == 0) {
        handle->pls_head = new;
    } else {
        delete_music(handle->pls_head->next);
        handle->pls_head->next = new;
    }
    return 0;
ERR_RET:
    return -1;
}

music_t* pls_change_to_next_music(pls_handle_t *handle)
{
    music_t *tmp = NULL;
    int pls_len = 0;

    ESP_LOGD(TAG, "==> pls_change_to_next_music");
    pls_len = pls_get_length(handle);
    if (pls_len != 2) {
        ERR_OUT(ERR_RET, "next not exist, quit");
    }
    tmp = handle->pls_head;
    handle->pls_head = handle->pls_head->next;
    delete_music(tmp);
    
    return handle->pls_head;
ERR_RET:
    return NULL;
}


void pls_clean_list(pls_handle_t *handle)
{
    music_t *tmp = NULL;

    ESP_LOGD(TAG, "==> pls_clean_list");
    while (handle->pls_head) {
        tmp = handle->pls_head;
        handle->pls_head = handle->pls_head->next;
        delete_music(tmp);
    }
}

int pls_add_music_to_tail(pls_handle_t *handle, music_queue_t pQueue_data)
{
    music_t *tmp = handle->pls_head;
    music_t *new = NULL;

    ESP_LOGD(TAG, "==> pls_add_music");
    new = create_music(pQueue_data);
    if (!tmp) {
        handle->pls_head = new;
    } else  {
        while (tmp->next) {
            tmp = tmp->next;
        }
        tmp->next = new;
    }

    return 0;
}

int pls_add_music_to_head(pls_handle_t *handle, music_queue_t pQueue_data)
{
    music_t *new = NULL;

    ESP_LOGD(TAG, "==> pls_add_music_to_head");
    pls_dump(handle);
    new = create_music(pQueue_data);
    if (!handle->pls_head) {
        handle->pls_head = new;
    } else {
        new->next = handle->pls_head;
        handle->pls_head = new;
    }
    pls_dump(handle);

    return 0;
}

music_t* pls_get_current_music(pls_handle_t *handle)
{
    ESP_LOGD(TAG, "==> pls_get_current_music");
    if (pls_get_length(handle) == 0) {
        return NULL;
    }
    return handle->pls_head;
}

music_t* pls_get_second_music(pls_handle_t *handle)
{
    ESP_LOGD(TAG, "==> pls_get_second_music");
    if (pls_get_length(handle) > 1) {
        return handle->pls_head->next;
    }
    return NULL;
}

int pls_delete_second_music(pls_handle_t *handle)
{
    music_t *tmp = NULL;
    ESP_LOGD(TAG, "==> pls_delete_second_music");

    if (pls_get_length(handle) > 1) {
        tmp = handle->pls_head->next;
        handle->pls_head->next = handle->pls_head->next->next;
        delete_music(tmp);
        return 0;
    }
    return -1;
}

int pls_delete_head_music(pls_handle_t *handle)
{
    music_t *tmp = NULL;
    ESP_LOGD(TAG, "==> pls_delete_head_music");

    if (pls_get_length(handle) > 0) {
        tmp = handle->pls_head;
        handle->pls_head = handle->pls_head->next;
        delete_music(tmp);
        return 0;
    }
    return -1;
}


music_t* create_music(music_queue_t pQueue_data)
{
    music_t *node = NULL;

    if (!(node = audio_calloc(1,sizeof(music_t)))) {
        ERR_OUT(ERR_RET, "calloc fail");
    }
    node->data = pQueue_data.data;
    node->type = pQueue_data.type;
    node->action_type = pQueue_data.action_type;
    node->user_cb = pQueue_data.user_cb;
    node->is_big_data_analyse = pQueue_data.is_big_data_analyse;
    node->next = NULL;
    return node;

ERR_RET:
    return NULL;
}

int pls_set_current_music_player_state(pls_handle_t *handle, music_play_state_t state)
{
    ESP_LOGD(TAG, "==> pls_set_current_music_player_state");
    music_t* current_music = pls_get_current_music(handle);
    if (!current_music) {
        return -1;
    }
    current_music->play_state = state;
    return 0;
}

music_play_state_t pls_get_current_music_player_state(pls_handle_t *handle)
{
    ESP_LOGD(TAG, "==> pls_get_current_music_player_state");
    music_t* current_music = pls_get_current_music(handle);
    if (!current_music) {
        return UNKNOW_STATE;
    }
    return current_music->play_state;
}
