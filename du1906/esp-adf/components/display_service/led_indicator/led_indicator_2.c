/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2019 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
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

#include "periph_led.h"
#include "esp_log.h"
#include "led_indicator_2.h"
#include "driver/gpio.h"
#include "audio_mem.h"

static char *TAG = "LED_INDI2";

typedef struct led_indicator_2_impl {
    gpio_num_t              gpio_num1;
    gpio_num_t              gpio_num2;
    gpio_num_t              gpio_num3;
    esp_periph_handle_t     periph_handle1;
} led_indicator_2_impl_t;

led_indicator_2_handle_t led_indicator_2_init(gpio_num_t num1, gpio_num_t num2, gpio_num_t num3)
{
    led_indicator_2_impl_t *impl =  audio_calloc(1, sizeof(led_indicator_2_impl_t));
    AUDIO_MEM_CHECK(TAG, impl, return NULL);
    impl->gpio_num1 = num1;
    impl->gpio_num2 = num2;
    impl->gpio_num3 = num3;
    periph_led_cfg_t led1_cfg = {
        .led_speed_mode = LEDC_LOW_SPEED_MODE,
        .led_duty_resolution = LEDC_TIMER_10_BIT,
        .led_timer_num = LEDC_TIMER_0,
        .led_freq_hz = 10000,
    };
    impl->periph_handle1  = periph_led_init(&led1_cfg);
    esp_periph_init(impl->periph_handle1);

    return impl;
}

esp_err_t led_indicator_2_pattern(void *handle, int pat, int value)
{
    AUDIO_NULL_CHECK(TAG, handle, return ESP_FAIL);
    led_indicator_2_handle_t h = (led_indicator_2_handle_t)handle;
    ESP_LOGW(TAG, "pat:%d, gpio1:%d, gpio2:%d, gpio3:%d", pat, 
                   h->gpio_num1, h->gpio_num2, h->gpio_num3);
    switch (pat) {
        case DISPLAY_PATTERN_WIFI_SETTING:  // Blue blink*
            periph_led_blink(h->periph_handle1, h->gpio_num3, 500, 500, false, -1, PERIPH_LED_IDLE_LEVEL_HIGH);
            break;
        case DISPLAY_PATTERN_WIFI_CONNECTED:    // Blue Off*
            periph_led_stop(h->periph_handle1, h->gpio_num3);
            break;
        case DISPLAY_PATTERN_WIFI_DISCONNECTED: // Blue blink slowly*
            periph_led_blink(h->periph_handle1, h->gpio_num3, 1000, 1000, false, -1, PERIPH_LED_IDLE_LEVEL_HIGH);
            break;
        case DISPLAY_PATTERN_TURN_ON:
            periph_led_stop(h->periph_handle1, h->gpio_num1);
            periph_led_stop(h->periph_handle1, h->gpio_num2);
            periph_led_stop(h->periph_handle1, h->gpio_num3);
            break;
        case DISPLAY_PATTERN_MUTE_OFF:  // Yellow Off*
            periph_led_stop(h->periph_handle1, h->gpio_num1);
            periph_led_stop(h->periph_handle1, h->gpio_num2);
            break;
        case DISPLAY_PATTERN_TURN_OFF:  // Blue Off*
            periph_led_stop(h->periph_handle1, h->gpio_num3);
            break;
        case DISPLAY_PATTERN_BOOT:  // Blue*
            // boot should clear other leds
            // Make sure index/pin are allocated to channel.
            // We must blink first.
            periph_led_blink(h->periph_handle1, h->gpio_num1, 50, 0, false, -1, PERIPH_LED_IDLE_LEVEL_HIGH);
            periph_led_stop(h->periph_handle1, h->gpio_num1);
            periph_led_blink(h->periph_handle1, h->gpio_num2, 50, 0, false, -1, PERIPH_LED_IDLE_LEVEL_HIGH);
            periph_led_stop(h->periph_handle1, h->gpio_num2);
            periph_led_blink(h->periph_handle1, h->gpio_num3, 300, 0, false, -1, PERIPH_LED_IDLE_LEVEL_HIGH);
            break;
        case DISPLAY_PATTERN_BT_CONNECTED:  // Blue*
            periph_led_blink(h->periph_handle1, h->gpio_num3, 300, 0, false, -1, PERIPH_LED_IDLE_LEVEL_HIGH);
            break;
        case DISPLAY_PATTERN_BT_DISCONNECTED:   // Blue Off*
            periph_led_stop(h->periph_handle1, h->gpio_num3);
            break;
        case DISPLAY_PATTERN_WAKEUP_ON: // Blue blink once*
            periph_led_blink(h->periph_handle1, h->gpio_num3, 100, 0, false, 1, PERIPH_LED_IDLE_LEVEL_HIGH);
            break;
        case DISPLAY_PATTERN_VOLUMN_UP: // Blue blink once*
        case DISPLAY_PATTERN_VOLUMN_DOWN:
            periph_led_blink(h->periph_handle1, h->gpio_num3, 100, 0, false, 1, PERIPH_LED_IDLE_LEVEL_HIGH);
            break;
        case DISPLAY_PATTERN_MUTE_ON:  // Yellow(Green + Red)*
            periph_led_blink(h->periph_handle1, h->gpio_num1, 300, 0, false, -1, PERIPH_LED_IDLE_LEVEL_HIGH);
            periph_led_blink(h->periph_handle1, h->gpio_num2, 300, 0, false, -1, PERIPH_LED_IDLE_LEVEL_HIGH);
            break;
        case DISPLAY_PATTERN_WIFI_SETTING_FINISHED: // Blue Off*
            periph_led_stop(h->periph_handle1, h->gpio_num3);
            break;
        case DISPLAY_PATTERN_BATTERY_LOW:   // Red blink slowly
            periph_led_blink(h->periph_handle1, h->gpio_num1, 1000, 1000, false, 10, PERIPH_LED_IDLE_LEVEL_HIGH);
            break;
        case DISPLAY_PATTERN_OTA:   // Blue blink rapidly
            periph_led_blink(h->periph_handle1, h->gpio_num3, 50, 50, false, -1, PERIPH_LED_IDLE_LEVEL_HIGH);
            break;
        case DISPLAY_PATTERN_POWER_OFF: // All Off*
            periph_led_stop(h->periph_handle1, h->gpio_num1);
            periph_led_stop(h->periph_handle1, h->gpio_num2);
            periph_led_stop(h->periph_handle1, h->gpio_num3);
            break;
        default:
            ESP_LOGW(TAG, "The led mode is invalid");
            break;
    }

    return ESP_OK;
}

void led_indicator_2_deinit(led_indicator_2_handle_t handle)
{
    AUDIO_NULL_CHECK(TAG, handle, return);
    esp_periph_destroy (handle->periph_handle1);
    //esp_periph_destroy (handle->periph_handle2);
    //esp_periph_destroy (handle->periph_handle3);
}