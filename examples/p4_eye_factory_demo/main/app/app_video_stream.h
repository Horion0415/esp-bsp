/**
 * @file app_video_stream.h
 * @brief Video streaming application interface
 *
 * This module provides functionality for initializing and managing video streaming
 * from a camera to a display. It handles camera initialization, buffer management,
 * and frame processing with scaling capabilities.
 */

#pragma once

#include <esp_err.h>
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PHOTO_RESOLUTION_480P = 0,  // 640x480
    PHOTO_RESOLUTION_720P = 1,  // 1280x720
    PHOTO_RESOLUTION_1080P = 2, // 1920x1080
    PHOTO_RESOLUTION_MAX
} photo_resolution_t;

/**
 * @brief Initialize the video streaming application
 *
 * This function initializes the PPA (Parallel Pixel Accelerator), camera, and
 * allocates necessary buffers for video streaming. It sets up the video capture
 * device and starts the streaming task.
 *
 * @param i2c_handle I2C master bus handle for camera communication
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t app_video_stream_init(i2c_master_bus_handle_t i2c_handle);

esp_err_t app_video_stream_take_photo(void);
esp_err_t app_video_stream_stop_take_photo(void);
esp_err_t app_video_stream_take_video(void);
esp_err_t app_video_stream_stop_take_video(void);

esp_err_t app_video_stream_start_interval_photo(uint16_t interval_minutes);
esp_err_t app_video_stream_stop_interval_photo(void);
esp_err_t app_video_stream_check_interval_wakeup(void);

esp_err_t app_video_stream_set_flash_light(bool is_on);

photo_resolution_t app_video_stream_get_photo_resolution(void);
esp_err_t app_video_stream_set_photo_resolution_by_string(const char *resolution_str);

void swap_rgb565_bytes(uint16_t *buffer, int pixel_count);

#ifdef __cplusplus
}
#endif