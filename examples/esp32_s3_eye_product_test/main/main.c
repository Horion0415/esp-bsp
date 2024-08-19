/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "lv_demos.h"
#include "bsp/esp-bsp.h"

#include "app_qma6100.h"
#include "app_camera.h"
#include "app_speech_if.h"
#include "app_color_check.h"
#include "app_button.h"

#include "ui.h"

static char *TAG = "app_main";

#define LOG_MEM_INFO    (0)

void app_main(void)
{
    /* Initialize display and LVGL */
    bsp_display_start();
    bsp_display_backlight_on();
    
    bsp_display_lock(0);
    ui_init();
    bsp_display_unlock();
    
    app_color_check_init();

    if(app_sdcard_init() == ESP_OK) {
        ESP_LOGI(TAG, "SD card init success");
        speech_recognition_init();
    }
    app_button_init();

    app_camera_init();
    app_camera_begin();

    QMA7981_init();
    QMA7981_begin();

#if LOG_MEM_INFO
    static char buffer[128];    /* Make sure buffer is enough for `sprintf` */
    while (1) {
        sprintf(buffer, "   Biggest /     Free /    Total\n"
                "\t  SRAM : [%8d / %8d / %8d]\n"
                "\t PSRAM : [%8d / %8d / %8d]",
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
        ESP_LOGI("MEM", "%s", buffer);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
#endif
}
