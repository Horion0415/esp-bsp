/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "esp_log.h"

#include "app_usb_msc.h"

// #define TEST_DISK_PATH "/disk"
// #define TEST_RESULT_FILE "test.txt"

static const char *TAG = "main";

const char *disk_path = "/disk";

// static esp_err_t create_and_write_file(const char *path, char *data, bool append)
// {
//     ESP_LOGI(TAG, "Opening file %s", path);
//     FILE *f = fopen(path, append ? "a" : "w");
//     if (f == NULL) {
//         ESP_LOGE(TAG, "Failed to open file for writing");
//         return ESP_FAIL;
//     }
//     fprintf(f, "%s\n", data);
//     fclose(f);
//     ESP_LOGI(TAG, "File written");

//     return ESP_OK;
// }

void create_and_write_file(const char *file_path, const char *content)
{
    FILE *f = fopen(file_path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }
    fprintf(f, "%s\n", content);
    fclose(f); 
    ESP_LOGI(TAG, "File written: %s", file_path);
}

void app_main(void)
{
    // ESP_ERROR_CHECK(bsp_p4_eye_init());

    ESP_ERROR_CHECK(app_usb_msc_init(disk_path));
    create_and_write_file("/disk/result.txt", "sssss");

    // bsp_display_start();
    // bsp_display_lock(0);

    // lv_obj_t *label = lv_label_create(lv_scr_act());
    // lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);
    // lv_label_set_text(label, "Auto detecting");
    // lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    
    // bsp_display_unlock();
    // bsp_display_backlight_on();

    // while(!app_usb_msc_stage()) {
    //     lv_label_set_text(label, "Detecting USB HS...");
    //     vTaskDelay(300 / portTICK_PERIOD_MS);
    // }

    // lv_label_set_text(label, "USB HS detected");
    // create_and_write_file(TEST_DISK_PATH "/" TEST_RESULT_FILE, "USB HS: PASS\n", true);
    // vTaskDelay(1000 / portTICK_PERIOD_MS);
}

