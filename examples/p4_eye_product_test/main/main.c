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
#include "app_sr.h"
#include "app_wifi_scan.h"

#define TEST_DISK_PATH "/disk"
#define TEST_RESULT_FILE "test.txt"

static const char *TAG = "main";

static esp_err_t create_and_write_file(const char *path, char *data, bool append)
{
    ESP_LOGI(TAG, "Opening file %s", path);
    
    FILE *f = fopen(path, append ? "a" : "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return ESP_FAIL;
    }
    fprintf(f, "%s\n", data);
    fclose(f);

    ESP_LOGI(TAG, "File written: %s, data: %s", path, data);

    return ESP_OK;
}

void app_main(void)
{
    ESP_ERROR_CHECK(bsp_p4_eye_init());

    ESP_ERROR_CHECK(app_usb_msc_init(TEST_DISK_PATH));
    create_and_write_file(TEST_DISK_PATH "/" TEST_RESULT_FILE, "", false);

    bsp_display_start();
    bsp_display_lock(0);

    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_label_set_text(label, "Auto detecting");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    
    bsp_display_unlock();
    bsp_display_backlight_on();

    // Wait for USB HS
    while(!app_usb_msc_stage()) {
        lv_label_set_text(label, "Detecting USB HS...");
        vTaskDelay(300 / portTICK_PERIOD_MS);
    }

    lv_label_set_text(label, "USB HS detected");
    create_and_write_file(TEST_DISK_PATH "/" TEST_RESULT_FILE, "LCD: PASS", true);
    create_and_write_file(TEST_DISK_PATH "/" TEST_RESULT_FILE, "USB HS: PASS", true);

    bsp_extra_pdm_codec_init();
    app_sr_start(false);

    // Wait for wakeup
    while (!app_sr_get_wakeup_result()) {
        lv_label_set_text(label, "Detecting wakeup...");
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    lv_label_set_text(label, "Wakeup detected");
    create_and_write_file(TEST_DISK_PATH "/" TEST_RESULT_FILE, "Wakeup: PASS", true);

    // Scan WiFi
    lv_label_set_text(label, "Scanning WiFi...");
    app_wifi_scan();
    uint16_t ap_count = app_wifi_scan_get_ap_count();
    if (ap_count > 0) {
        lv_label_set_text(label, "WiFi scan: PASS");
        create_and_write_file(TEST_DISK_PATH "/" TEST_RESULT_FILE, "WiFi scan: PASS", true);
    } else {
        lv_label_set_text(label, "WiFi scan: FAIL");
        create_and_write_file(TEST_DISK_PATH "/" TEST_RESULT_FILE, "WiFi scan: FAIL", true);
    }
}

