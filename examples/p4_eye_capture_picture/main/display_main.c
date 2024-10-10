/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "esp_log.h"

#include "esp_video_init.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"
#include "esp_timer.h"
#include "driver/jpeg_encode.h"
#include "app_video.h"

static i2c_master_bus_handle_t i2c_handle;
static size_t data_cache_line_size = 0;
static void *camera_buf[EXAMPLE_CAM_BUF_NUM];

static const char *TAG = "main";

void app_main(void)
{   
    ESP_LOGI(TAG, "LEDs initialized");
    ESP_ERROR_CHECK(bsp_leds_init());

    ESP_ERROR_CHECK(bsp_sdcard_mount());
    ESP_LOGI(TAG, "SD card mounted");

    // Initialize the I2C
    ESP_ERROR_CHECK(bsp_i2c_init());
    bsp_get_i2c_bus_handle(&i2c_handle);

    // Initialize the video camera
    esp_err_t ret = app_video_main(i2c_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "video main init failed with error 0x%x", ret);
        return;
    }

    // Open the video device
    int video_cam_fd0 = app_video_open(EXAMPLE_CAM_DEV_PATH, APP_VIDEO_FMT);
    if (video_cam_fd0 < 0) {
        ESP_LOGE(TAG, "video cam open failed");
        return;
    }

    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &data_cache_line_size));
    for (int i = 0; i < EXAMPLE_CAM_BUF_NUM; i++) {
        camera_buf[i] = heap_caps_aligned_calloc(data_cache_line_size, 1, app_video_get_buf_size(), MALLOC_CAP_SPIRAM);
        if (camera_buf[i] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate canvas buffer");
            return;
        }
    }

    ESP_LOGI(TAG, "Using user buffer");
    ESP_ERROR_CHECK(app_video_set_bufs(video_cam_fd0, EXAMPLE_CAM_BUF_NUM, (void*)camera_buf));

    // Initialize the display
    bsp_display_start();

    bsp_display_lock(0);


    bsp_display_unlock();
    bsp_display_backlight_on();
}