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

#ifdef __cplusplus
}
#endif