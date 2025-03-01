/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "app_usb_msc.h"
#include "app_sr.h"
#include "app_wifi_scan.h"

#define TEST_DISK_PATH "/disk"
#define TEST_RESULT_FILE_FORMAT "%s/%s.txt" 

#define BUTTON_1_BIT BIT0
#define BUTTON_2_BIT BIT1
#define BUTTON_3_BIT BIT2
#define ALL_BUTTONS_BITS (BUTTON_1_BIT | BUTTON_2_BIT | BUTTON_3_BIT)

#define KNOB_LEFT_BIT BIT3
#define KNOB_RIGHT_BIT BIT4
#define KNOB_PRESS_BIT BIT5
#define ALL_KNOB_BITS (KNOB_LEFT_BIT | KNOB_RIGHT_BIT | KNOB_PRESS_BIT)

#define LED_WHITE_BIT BIT6
static const char *TAG = "main";

static EventGroupHandle_t button_event_group;
static EventBits_t bits;

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

static void btn_handler(void *arg, void *data)
{
    ESP_LOGD(TAG, "Button pressed: %d", (int)data);

    if ((int)data == BSP_BUTTON_1) {
        ESP_LOGI(TAG, "Button 1 pressed");
        xEventGroupSetBits(button_event_group, BUTTON_1_BIT);
    } else if ((int)data == BSP_BUTTON_2) {
        ESP_LOGI(TAG, "Button 2 pressed");
        xEventGroupSetBits(button_event_group, BUTTON_2_BIT);
    } else if ((int)data == BSP_BUTTON_3) {
        ESP_LOGI(TAG, "Button 3 pressed");
        xEventGroupSetBits(button_event_group, BUTTON_3_BIT);
    } else if ((int)data == BSP_BUTTON_ED) {
        ESP_LOGI(TAG, "Button ED pressed");
        EventBits_t current_bits = xEventGroupGetBits(button_event_group);
        if((current_bits & ALL_BUTTONS_BITS) == ALL_BUTTONS_BITS) {
            xEventGroupSetBits(button_event_group, KNOB_PRESS_BIT);
        } else {
            ESP_LOGI(TAG, "Please press all three buttons first!");
        }
    }

    EventBits_t current_bits = xEventGroupGetBits(button_event_group);
    if((current_bits & ALL_KNOB_BITS) == ALL_KNOB_BITS && bsp_get_led_status(BSP_LED_WHITE)) {
        bsp_led_set(BSP_LED_WHITE, 0);  // Turn off the white LED
        xEventGroupSetBits(button_event_group, LED_WHITE_BIT);
    }
}

static void knob_left_cb(void *arg, void *data)
{
    ESP_LOGI(TAG, "Knob left pressed");
    EventBits_t current_bits = xEventGroupGetBits(button_event_group);
    // 检查是否所有按钮都已经按下
    if((current_bits & ALL_BUTTONS_BITS) == ALL_BUTTONS_BITS) {
        xEventGroupSetBits(button_event_group, KNOB_LEFT_BIT);
    } else {
        ESP_LOGI(TAG, "Please press all three buttons first!");
    }
}

static void knob_right_cb(void *arg, void *data)
{
    ESP_LOGI(TAG, "Knob right pressed");
    EventBits_t current_bits = xEventGroupGetBits(button_event_group);
    // 检查是否所有按钮都已经按下
    if((current_bits & ALL_BUTTONS_BITS) == ALL_BUTTONS_BITS) {
        xEventGroupSetBits(button_event_group, KNOB_RIGHT_BIT);
    } else {
        ESP_LOGI(TAG, "Please press all three buttons first!");
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(bsp_p4_eye_init());

    // Get base MAC address
    uint8_t base_mac_addr[6] = {0};
    char mac_str[18];

    esp_read_mac(base_mac_addr, ESP_MAC_EFUSE_FACTORY);
    snprintf(mac_str, sizeof(mac_str), "%02X-%02X-%02X-%02X-%02X-%02X",
             base_mac_addr[0], base_mac_addr[1], base_mac_addr[2],
             base_mac_addr[3], base_mac_addr[4], base_mac_addr[5]);

    char file_path[128];
    snprintf(file_path, sizeof(file_path), TEST_RESULT_FILE_FORMAT, TEST_DISK_PATH, mac_str);
    ESP_LOGI(TAG, "Test result file path: %s", file_path);

    // Initialize USB MSC
    ESP_ERROR_CHECK(app_usb_msc_init(TEST_DISK_PATH));
    create_and_write_file(file_path, "", false);

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
    create_and_write_file(file_path, "LCD: PASS", true);
    create_and_write_file(file_path, "USB HS: PASS", true);

    bsp_extra_pdm_codec_init();
    app_sr_start(false);

    // Wait for wakeup
    while (!app_sr_get_wakeup_result()) {
        lv_label_set_text(label, "Detecting wakeup...");
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    lv_label_set_text(label, "Wakeup detected");
    create_and_write_file(file_path, "Wakeup: PASS", true);

    // Scan WiFi
    lv_label_set_text(label, "Scanning WiFi...");
    app_wifi_scan();
    uint16_t ap_count = app_wifi_scan_get_ap_count();
    if (ap_count > 0) {
        lv_label_set_text(label, "WiFi scan: PASS");
        create_and_write_file(file_path, "WiFi scan: PASS", true);
    } else {
        lv_label_set_text(label, "WiFi scan: FAIL");
        create_and_write_file(file_path, "WiFi scan: FAIL", true);
    }

    /* Init Buttons */
    button_handle_t btns[BSP_BUTTON_NUM];
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_1], BUTTON_PRESS_DOWN, btn_handler, (void *) BSP_BUTTON_1));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_2], BUTTON_PRESS_DOWN, btn_handler, (void *) BSP_BUTTON_2));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_3], BUTTON_PRESS_DOWN, btn_handler, (void *) BSP_BUTTON_3));
    
    lv_label_set_text(label, "Please press the \n three buttons \n on the right.");

    button_event_group = xEventGroupCreate();

    bits = xEventGroupWaitBits(
        button_event_group,   
        ALL_BUTTONS_BITS,     
        pdFALSE,              
        pdTRUE,              
        portMAX_DELAY);      

    if((bits & ALL_BUTTONS_BITS) == ALL_BUTTONS_BITS) {
        lv_label_set_text(label, "All buttons test \n passed!");
        create_and_write_file(file_path, "Buttons: PASS", true);
    }    

    // Initialize the knob
    ESP_ERROR_CHECK(bsp_knob_init());
    // Register callback functions
    ESP_ERROR_CHECK(bsp_knob_register_cb(KNOB_LEFT, knob_left_cb, NULL));
    ESP_ERROR_CHECK(bsp_knob_register_cb(KNOB_RIGHT, knob_right_cb, NULL));    
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_ED], BUTTON_PRESS_UP, btn_handler, (void *) BSP_BUTTON_ED));

    lv_label_set_text(label, "Toggle the knob left");
    xEventGroupWaitBits(button_event_group, KNOB_LEFT_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    lv_label_set_text(label, "Toggle the knob right");
    xEventGroupWaitBits(button_event_group, KNOB_RIGHT_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    lv_label_set_text(label, "Press the knob");
    xEventGroupWaitBits(button_event_group, KNOB_PRESS_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    lv_label_set_text(label, "Knob test \n passed!");
    create_and_write_file(file_path, "Knob: PASS", true);

    // Initialize the led
    ESP_ERROR_CHECK(bsp_leds_init());
    lv_label_set_text(label, "Check fill light \n if on \n press any key.");

    bsp_led_set(BSP_LED_WHITE, 1);  // Turn on the white LED
    
    xEventGroupWaitBits(button_event_group, LED_WHITE_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    lv_label_set_text(label, "LED test \n passed!");
    create_and_write_file(file_path, "LED: PASS", true);
}

