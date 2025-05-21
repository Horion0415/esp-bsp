/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "bsp/esp-bsp.h"

#include "app_camera.h"
#include "app_ai_detect.h"

static char *TAG = "app_main";

#define LOG_MEM_INFO    (1)

esp_lcd_panel_handle_t lcd_panel;
esp_lcd_panel_io_handle_t lcd_io;

void app_main(void)
{
    /* Initialize display and LVGL */
    const bsp_display_config_t bsp_disp_cfg = {
        .max_transfer_sz = BSP_LCD_H_RES * 10 * sizeof(uint16_t),
    };
    bsp_display_new(&bsp_disp_cfg, &lcd_panel, &lcd_io);
    esp_lcd_panel_disp_on_off(lcd_panel, true);
    bsp_display_backlight_on();

    app_ai_detect_init();

    app_camera_init(lcd_panel);
    app_camera_begin();
    app_camera_start();

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
