/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2020 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "board.h"
#include "audio_mem.h"
#include "esp_log.h"
#include "raw_stream.h"
#include "http_stream.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"
#include "tone_stream.h"

#include "wav_decoder.h"
#include "mp3_decoder.h"
#include "aac_decoder.h"
#include "amr_decoder.h"
#include "flac_decoder.h"
#include "ogg_decoder.h"
#include "opus_decoder.h"
#include "pcm_decoder.h"
#include "esp_decoder.h"

#include "app_bt_init.h"
#include "audio_player_helper.h"
#include "audio_player_type.h"
#include "audio_player.h"
#include "app_player_init.h"
#include "audio_player_pipeline_int_tone.h"
#include "bdsc_ota_partitions.h"

typedef struct i2s_stream {
    audio_stream_type_t type;
    i2s_stream_cfg_t    config;
    bool                is_open;
    bool                use_alc;
    void                *volume_handle;
    int                 volume;
    bool                uninstall_drv;
} i2s_stream_t;

static const char *TAG = "APP_PLAYER_INIT";

static int _http_stream_event_handle(http_stream_event_msg_t *msg)
{
    if (msg->event_id == HTTP_STREAM_RESOLVE_ALL_TRACKS) {
        return ESP_OK;
    }

    if (msg->event_id == HTTP_STREAM_FINISH_TRACK) {
        return http_stream_next_track(msg->el);
    }
    if (msg->event_id == HTTP_STREAM_FINISH_PLAYLIST) {
        return http_stream_restart(msg->el);
    }
    return ESP_OK;
}

int my_i2s_write(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    i2s_stream_t *i2s = (i2s_stream_t *)audio_element_getdata(self);
    size_t bytes_written = 0;
#ifdef CONFIG_CUPID_BOARD_V2
    i2s_write_expand(i2s->config.i2s_port, buffer, len, 16, 32, &bytes_written, ticks_to_wait);
#else
    i2s_write_expand(i2s->config.i2s_port, buffer, len, 16, 16, &bytes_written, ticks_to_wait);
#endif
    return bytes_written;
}

esp_periph_handle_t g_bt_periph;

static esp_audio_handle_t setup_app_esp_audio_instance(esp_audio_cfg_t *cfg, esp_a2d_cb_t a2dp_cb)
{
    if (cfg == NULL) {
        esp_audio_cfg_t default_cfg = DEFAULT_ESP_AUDIO_CONFIG();
        default_cfg.vol_handle = NULL;
        default_cfg.vol_set = (audio_volume_set)audio_hal_set_volume;
        default_cfg.vol_get = (audio_volume_get)audio_hal_get_volume;
        default_cfg.prefer_type = ESP_AUDIO_PREFER_MEM;
        default_cfg.evt_que = NULL;
        ESP_LOGI(TAG, "stack:%d", default_cfg.task_stack);
        cfg = &default_cfg;
    }
    esp_audio_handle_t handle = esp_audio_create(cfg);
    AUDIO_MEM_CHECK(TAG, handle, return NULL;);

    // 1. Create readers and add to esp_audio
    // 1.1 fatfs_stream
    fatfs_stream_cfg_t fs_reader = FATFS_STREAM_CFG_DEFAULT();
    fs_reader.type = AUDIO_STREAM_READER;
    fs_reader.task_prio = 12;
    esp_audio_input_stream_add(handle, fatfs_stream_init(&fs_reader));

    // 1.2 tone_stream
    tone_stream_cfg_t tn_reader = TONE_STREAM_CFG_DEFAULT();
    tn_reader.type = AUDIO_STREAM_READER;
    //tn_reader.task_core = 1;
    tn_reader.task_prio = 12;
    tn_reader.buf_sz = (20 * 2048);
    tn_reader.out_rb_size = (20 * 1024);
    tn_reader.partition = bdsc_partitions_get_bootable_flash_tone_label();
    esp_audio_input_stream_add(handle, tone_stream_init(&tn_reader));
    ESP_LOGI(TAG, "bootbale tone lable: %s", bdsc_partitions_get_bootable_flash_tone_label());

    // 1.3 a2dp_stream
    a2dp_stream_config_t a2dp_config = {
        .type = AUDIO_STREAM_READER,
        .user_callback.user_a2d_cb = a2dp_cb,
    };
    audio_element_handle_t bt_stream_reader = a2dp_stream_init(&a2dp_config);
    esp_audio_input_stream_add(handle, bt_stream_reader);
    ap_helper_a2dp_handle_set(bt_stream_reader);

    // 1.4 raw_stream
    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_WRITER;
    audio_element_handle_t raw_play_handle = raw_stream_init(&raw_cfg);
    esp_audio_input_stream_add(handle, raw_play_handle);
    ap_helper_raw_handle_set(raw_play_handle);

    // 1.5 http_stream
    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_cfg.event_handle = _http_stream_event_handle;
    http_cfg.type = AUDIO_STREAM_READER;
    http_cfg.enable_playlist_parser = true;
    http_cfg.task_prio = 12;
    http_cfg.stack_in_ext = true;
    audio_element_handle_t http_stream_reader = http_stream_init(&http_cfg);
    esp_audio_input_stream_add(handle, http_stream_reader);  
    ESP_LOGI(TAG, "http_stream_reader0:%p", http_stream_reader);
    http_stream_reader = http_stream_init(&http_cfg);
    esp_audio_input_stream_add(handle, http_stream_reader);
    ESP_LOGI(TAG, "http_stream_reader1:%p", http_stream_reader);
    http_stream_reader = http_stream_init(&http_cfg);
    esp_audio_input_stream_add(handle, http_stream_reader);
    ESP_LOGI(TAG, "http_stream_reader2:%p", http_stream_reader);

    // 2. Add decoders and encoders to esp_audio
    // 2.1 mp3_decoder
    mp3_decoder_cfg_t mp3_dec_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_dec_cfg.task_core = 1;
    mp3_dec_cfg.task_prio = 12;
    esp_audio_codec_lib_add(handle, AUDIO_CODEC_TYPE_DECODER, mp3_decoder_init(&mp3_dec_cfg));

    // 2.2 aac_decoder
    aac_decoder_cfg_t aac_cfg = DEFAULT_AAC_DECODER_CONFIG();
    aac_cfg.task_core = 1;
    aac_cfg.task_prio = 12;
    esp_audio_codec_lib_add(handle, AUDIO_CODEC_TYPE_DECODER, aac_decoder_init(&aac_cfg));

    // 2.3 pcm_decoder
    pcm_decoder_cfg_t pcm_dec_cfg = DEFAULT_PCM_DECODER_CONFIG();
    esp_audio_codec_lib_add(handle, AUDIO_CODEC_TYPE_DECODER, pcm_decoder_init(&pcm_dec_cfg));

    // 2.4 auto_decoder
    audio_decoder_t auto_decode[] = {
        DEFAULT_ESP_AMRNB_DECODER_CONFIG(),
        DEFAULT_ESP_AMRWB_DECODER_CONFIG(),
        DEFAULT_ESP_MP3_DECODER_CONFIG(),
        DEFAULT_ESP_WAV_DECODER_CONFIG(),
        DEFAULT_ESP_AAC_DECODER_CONFIG(),
        DEFAULT_ESP_M4A_DECODER_CONFIG(),
        DEFAULT_ESP_TS_DECODER_CONFIG(),
        DEFAULT_ESP_PCM_DECODER_CONFIG(),
    };
    esp_decoder_cfg_t auto_dec_cfg = DEFAULT_ESP_DECODER_CONFIG();
    auto_dec_cfg.task_core = 1;
    auto_dec_cfg.task_prio = 12;
    esp_audio_codec_lib_add(handle, AUDIO_CODEC_TYPE_DECODER, esp_decoder_init(&auto_dec_cfg, auto_decode, sizeof(auto_decode) / sizeof(audio_decoder_t)));

    // 2.5 m4a_decoder
    audio_element_handle_t m4a_dec_cfg = aac_decoder_init(&aac_cfg);
    audio_element_set_tag(m4a_dec_cfg, "m4a");
    esp_audio_codec_lib_add(handle, AUDIO_CODEC_TYPE_DECODER, m4a_dec_cfg);

    // 2.6 ts_decoder
    audio_element_handle_t ts_dec_cfg = aac_decoder_init(&aac_cfg);
    audio_element_set_tag(ts_dec_cfg, "ts");
    esp_audio_codec_lib_add(handle, AUDIO_CODEC_TYPE_DECODER, ts_dec_cfg);

    // 3. Set default volume
    esp_audio_vol_set(handle, 60);
    AUDIO_MEM_SHOW(TAG);
    ESP_LOGI(TAG, "esp_audio instance is:%p", handle);
    esp_audio_helper_set_instance(handle);
    return handle;
}

audio_element_handle_t g_player_i2s_write_handle;
esp_err_t app_player_init(QueueHandle_t que, audio_player_evt_callback cb, esp_periph_set_handle_t set, esp_a2d_cb_t a2dp_cb)
{
    // TAS5805M must get I2S signals ahead of I2C signals.
    // Create writers and add to esp_audio
    i2s_stream_cfg_t i2s_writer = I2S_STREAM_CFG_DEFAULT();
    i2s_writer.type = AUDIO_STREAM_WRITER;
    i2s_writer.stack_in_ext = true;
    i2s_writer.i2s_config.sample_rate = 48000;
    i2s_writer.i2s_config.mode = I2S_MODE_MASTER | I2S_MODE_TX;

#ifdef CONFIG_CUPID_BOARD_V2
    i2s_writer.i2s_config.bits_per_sample = 32; //for cupid digital loopback
#else
    i2s_writer.i2s_config.bits_per_sample = 16;
#endif
    g_player_i2s_write_handle = i2s_stream_init(&i2s_writer);
    audio_element_set_write_cb(g_player_i2s_write_handle, my_i2s_write, NULL);
    

    //init the audio board and codec
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);

    esp_periph_handle_t bt_periph = app_bluetooth_init(set);
    g_bt_periph = bt_periph;

    esp_audio_cfg_t default_cfg = DEFAULT_ESP_AUDIO_CONFIG();
    default_cfg.vol_handle = board_handle->audio_hal;
    default_cfg.vol_set = (audio_volume_set)audio_hal_set_volume;
    default_cfg.vol_get = (audio_volume_get)audio_hal_get_volume;
    default_cfg.prefer_type = ESP_AUDIO_PREFER_MEM;
    default_cfg.evt_que = NULL;
    default_cfg.resample_rate = 48000;
    default_cfg.task_prio = 13;

    audio_player_cfg_t cfg = DEFAULT_AUDIO_PLAYER_CONFIG();
    cfg.external_ram_stack = true;
    cfg.task_prio = 13;
    cfg.task_stack = 3 * 1024;
    cfg.evt_cb = cb;
    cfg.evt_ctx = que;
    cfg.music_list = NULL;
    cfg.handle = setup_app_esp_audio_instance(&default_cfg, a2dp_cb);
    esp_audio_output_stream_add(cfg.handle, g_player_i2s_write_handle);

    audio_err_t ret = audio_player_init(&cfg);
    ret |= default_http_player_init();
#if CONFIG_CUPID_BOARD_V2
#else
    ret |= default_sdcard_player_init("/sdcard/ut");
#endif
    ret |= default_flash_music_player_init();
    ret |= default_flash_tone_player_init();
    ret |= default_a2dp_player_init(bt_periph);
    ret |= default_raw_player_init();
    ret |= audio_player_int_tone_init();
    return ESP_OK;
}
