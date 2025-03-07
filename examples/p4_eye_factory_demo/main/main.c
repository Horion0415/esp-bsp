/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "esp_log.h"
#include "bsp/esp-bsp.h"

#include "ui_extra.h"

#include "app_control.h"
#include "app_video_stream.h"
#include "app_storage.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Initialize the P4 Eye");
    ESP_ERROR_CHECK(bsp_p4_eye_init());

    // Initialize the display
    ESP_LOGI(TAG, "Initialize the display");
    bsp_display_start();

    bsp_display_lock(0);
    ui_extra_init();
    bsp_display_unlock();

    // Initialize the storage
    ESP_LOGI(TAG, "Initialize the storage");
    ESP_ERROR_CHECK(app_storage_init());

    bsp_display_backlight_on();

    // Initialize the application control module
    ESP_LOGI(TAG, "Initialize the application control module");
    ESP_ERROR_CHECK(app_control_init());

    // Initialize the I2C
    ESP_LOGI(TAG, "Initialize the I2C");
    i2c_master_bus_handle_t i2c_handle;
    ESP_ERROR_CHECK(bsp_i2c_init());
    bsp_get_i2c_bus_handle(&i2c_handle);

    // Initialize the video streaming application
    ESP_LOGI(TAG, "Initialize the video streaming application");
    ESP_ERROR_CHECK(app_video_stream_init(i2c_handle));
}