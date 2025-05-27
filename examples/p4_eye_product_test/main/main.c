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
#include "esp_hosted_api.h"
#include "driver/ppa.h"
#include "esp_private/esp_cache_private.h"

#include "app_usb_hid.h"
#include "app_sr.h"
#include "app_wifi_scan.h"
#include "app_video.h"
#include "app_gpio.h"
#include "ui.h"

#define TEST_DISK_PATH  BSP_SD_MOUNT_POINT
#define TEST_RESULT_FILE_FORMAT "%s/%s.txt" 

/* WiFi test configuration */
#define WIFI_TEST_TARGET_SSID    "P4-EYE-WIFI-TEST"    // Target SSID for WiFi test
#define WIFI_TEST_MIN_RSSI      -70         // Minimum acceptable signal strength in dBm

#define BUTTON_1_BIT BIT0
#define BUTTON_2_BIT BIT1
#define BUTTON_3_BIT BIT2
#define ALL_BUTTONS_BITS (BUTTON_1_BIT | BUTTON_2_BIT | BUTTON_3_BIT)

#define KNOB_LEFT_BIT BIT3
#define KNOB_RIGHT_BIT BIT4
#define KNOB_PRESS_BIT BIT5
#define ALL_KNOB_BITS (KNOB_LEFT_BIT | KNOB_RIGHT_BIT | KNOB_PRESS_BIT)

#define LED_WHITE_BIT BIT6
#define CAMERA_EXIT_BIT BIT7

#define ALIGN_UP(num, align)    (((num) + ((align) - 1)) & ~((align) - 1))

static const char *TAG = "main";

static EventGroupHandle_t button_event_group;
static EventBits_t bits;
static lv_obj_t* cam_canvas;
static lv_obj_t* cam_label;
static ppa_client_handle_t ppa_srm_handle = NULL;
static size_t data_cache_line_size = 0;
static void *canvas_buf[EXAMPLE_CAM_BUF_NUM];

static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, uint32_t camera_buf_hes, uint32_t camera_buf_ves, size_t camera_buf_len);

static esp_err_t create_and_write_file(const char *path, char *data, bool append)
{
    FILE *f = fopen(path, append ? "a" : "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return ESP_FAIL;
    }
    fprintf(f, "%s\n", data);
    fclose(f);

    if (append) {
        ESP_LOGI(TAG, "[Done] %s", data);
    } 

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

    if(current_bits & LED_WHITE_BIT) {
        xEventGroupSetBits(button_event_group, CAMERA_EXIT_BIT);
    }
}

static void knob_left_cb(void *arg, void *data)
{
    ESP_LOGI(TAG, "Knob left pressed");
    EventBits_t current_bits = xEventGroupGetBits(button_event_group);
    // Check if all buttons have been pressed
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
    // Check if all buttons have been pressed
    if((current_bits & ALL_BUTTONS_BITS) == ALL_BUTTONS_BITS) {
        xEventGroupSetBits(button_event_group, KNOB_RIGHT_BIT);
    } else {
        ESP_LOGI(TAG, "Please press all three buttons first!");
    }
}

void app_main(void)
{
    // Get base MAC address
    uint8_t base_mac_addr[6] = {0};
    char mac_str[18];
    uint8_t c6_mac[6];

    esp_read_mac(base_mac_addr, ESP_MAC_EFUSE_FACTORY);
    snprintf(mac_str, sizeof(mac_str), "%02X-%02X-%02X-%02X-%02X-%02X",
             base_mac_addr[0], base_mac_addr[1], base_mac_addr[2],
             base_mac_addr[3], base_mac_addr[4], base_mac_addr[5]);
    ESP_LOGI(TAG, "[MAC address]: %s", mac_str);

    char file_path[128];
    snprintf(file_path, sizeof(file_path), TEST_RESULT_FILE_FORMAT, TEST_DISK_PATH, mac_str);

    // Initialize GPIO
    init_gpio();

    // Initialize USB MSC
    app_usb_hid_init();

    bsp_display_start();
    bsp_display_lock(0);

    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(label, &ui_font_shuhei_font_24, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(label, "自动检测中");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    
    bsp_display_unlock();
    bsp_display_backlight_on();

    if(bsp_sdcard_mount() != ESP_OK) {
        lv_obj_set_style_text_color(label, lv_color_make(255, 0, 0), LV_PART_MAIN);
        lv_label_set_text(label, "SD卡测试失败");
        create_and_write_file(file_path, "", false);
        create_and_write_file(file_path, "SD card: FAIL", true);
        return;
    } else {
        lv_label_set_text(label, "SD卡测试通过");
        create_and_write_file(file_path, "", false);
        create_and_write_file(file_path, "SD card: PASS", true);
    }

    if(test_gpio_connection()) {
        lv_label_set_text(label, "GPIO测试通过");
        create_and_write_file(file_path, "GPIO: PASS", true);
    } else {
        lv_obj_set_style_text_color(label, lv_color_make(255, 0, 0), LV_PART_MAIN);
        lv_label_set_text(label, "GPIO测试失败");
        create_and_write_file(file_path, "GPIO: FAIL", true);
        return;
    }

    // Wait for USB HS
    while(!app_usb_hid_stage()) {
        lv_label_set_text(label, "USB HS 检测中");
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "[Done] USB HS detected!");
    create_and_write_file(file_path, "USB HS: PASS", true);

    bsp_extra_pdm_codec_init();
    app_sr_start(false);

    // Wait for wakeup
    while (!app_sr_get_wakeup_result()) {
        lv_label_set_text(label, "请对我说Hi ESP");
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "[Done] Wakeup detected!");
    create_and_write_file(file_path, "Microphone: PASS", true);

    // Scan WiFi
    lv_label_set_text(label, "正在扫描WiFi");
    app_wifi_scan();
    uint16_t ap_count = app_wifi_scan_get_ap_count();
    
    if (ap_count > 0) {
        // Get signal strength for target SSID
        int8_t rssi = app_wifi_scan_get_rssi_by_ssid(WIFI_TEST_TARGET_SSID);

        if (rssi > WIFI_TEST_MIN_RSSI) {
            lv_label_set_text(label, "WiFi扫描通过\n信号强度优");
            create_and_write_file(file_path, "WiFi scan: PASS", true);
            
            esp_wifi_remote_get_mac(WIFI_IF_STA, c6_mac);
            ESP_LOGI(TAG, "[WiFi MAC address]: %02X-%02X-%02X-%02X-%02X-%02X", c6_mac[0], c6_mac[1], c6_mac[2], c6_mac[3], c6_mac[4], c6_mac[5]);
            ESP_LOGI(TAG, "[Signal strength]: %d dBm", rssi);
        } else {
            lv_obj_set_style_text_color(label, lv_color_make(255, 0, 0), LV_PART_MAIN);
            lv_label_set_text(label, "WiFi扫描失败\n信号太弱");
            create_and_write_file(file_path, "WiFi scan: FAIL (Weak signal)", true);
            ESP_LOGE(TAG, "Signal strength (%d dBm) below threshold (%d dBm)", rssi, WIFI_TEST_MIN_RSSI);

            return;
        }
    } else {
        lv_obj_set_style_text_color(label, lv_color_make(255, 0, 0), LV_PART_MAIN);
        lv_label_set_text(label, "WiFi扫描失败\n没有找到WiFi热点");                 
        create_and_write_file(file_path, "WiFi scan: FAIL (No AP)", true);
    
        return;
    }

    /* Init Buttons */
    button_handle_t btns[BSP_BUTTON_NUM];
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_1], BUTTON_PRESS_DOWN, btn_handler, (void *) BSP_BUTTON_1));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_2], BUTTON_PRESS_DOWN, btn_handler, (void *) BSP_BUTTON_2));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_3], BUTTON_PRESS_DOWN, btn_handler, (void *) BSP_BUTTON_3));
    
    lv_label_set_text(label, "请依次按下三个按键");

    button_event_group = xEventGroupCreate();

    bits = xEventGroupWaitBits(
        button_event_group,   
        ALL_BUTTONS_BITS,     
        pdFALSE,              
        pdTRUE,              
        portMAX_DELAY);      

    if((bits & ALL_BUTTONS_BITS) == ALL_BUTTONS_BITS) {
        lv_label_set_text(label, "按键测试通过");
        create_and_write_file(file_path, "Buttons: PASS", true);
    }    

    // Initialize the knob
    ESP_ERROR_CHECK(bsp_knob_init());
    // Register callback functions
    ESP_ERROR_CHECK(bsp_knob_register_cb(KNOB_LEFT, knob_right_cb, NULL));
    ESP_ERROR_CHECK(bsp_knob_register_cb(KNOB_RIGHT, knob_left_cb, NULL));    
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_ED], BUTTON_PRESS_UP, btn_handler, (void *) BSP_BUTTON_ED));

    lv_label_set_text(label, "请向左旋转旋钮");
    xEventGroupWaitBits(button_event_group, KNOB_LEFT_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    lv_label_set_text(label, "请向右旋转旋钮");
    xEventGroupWaitBits(button_event_group, KNOB_RIGHT_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    lv_label_set_text(label, "请按下旋钮");
    xEventGroupWaitBits(button_event_group, KNOB_PRESS_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    lv_label_set_text(label, "旋钮测试通过");
    create_and_write_file(file_path, "Knob: PASS", true);

    // Initialize the led
    ESP_ERROR_CHECK(bsp_leds_init());
    lv_label_set_text(label, "检查补光灯是否打开\n\n 若已打开请按任意键");

    bsp_led_set(BSP_LED_WHITE, 1);  // Turn on the white LED
    
    xEventGroupWaitBits(button_event_group, LED_WHITE_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    lv_label_set_text(label, "LED测试通过");
    create_and_write_file(file_path, "LED: PASS", true);
    bsp_display_lock(0);

    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);

    cam_canvas = lv_canvas_create(lv_scr_act());
    lv_obj_set_size(cam_canvas, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_align(cam_canvas, LV_ALIGN_CENTER);
    
    cam_label = lv_label_create(cam_canvas);
    lv_obj_set_style_text_font(cam_label, &ui_font_shuhei_font_24, LV_PART_MAIN);
    lv_obj_set_style_text_align(cam_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(cam_label, lv_color_make(255, 0, 0), LV_PART_MAIN);
    lv_label_set_text(cam_label, "若显示正常\n\n 按任意键退出");
    lv_obj_align(cam_label, LV_ALIGN_CENTER, 0, 0);

    bsp_display_unlock();

    // Initialize the PPA
    ppa_client_config_t ppa_srm_config = {
        .oper_type = PPA_OPERATION_SRM,
    };
    ESP_ERROR_CHECK(ppa_register_client(&ppa_srm_config, &ppa_srm_handle));
    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &data_cache_line_size));

    // Allocate the canvas buffer
    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &data_cache_line_size));
    for (int i = 0; i < EXAMPLE_CAM_BUF_NUM; i++) {
        canvas_buf[i] = heap_caps_aligned_calloc(data_cache_line_size, 1, BSP_LCD_H_RES * BSP_LCD_V_RES * 2, MALLOC_CAP_SPIRAM);
        if (canvas_buf[i] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate canvas buffer");
            return;
        }
    }

    // Initialize the I2C
    i2c_master_bus_handle_t i2c_handle;
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

    // Initialize video capture device
    ESP_ERROR_CHECK(app_video_set_bufs(video_cam_fd0, EXAMPLE_CAM_BUF_NUM, NULL));

    // Register the video frame operation callback
    ESP_ERROR_CHECK(app_video_register_frame_operation_cb(camera_video_frame_operation));

    // Start the camera stream task
    ESP_ERROR_CHECK(app_video_stream_task_start(video_cam_fd0, 0));

    xEventGroupWaitBits(button_event_group, CAMERA_EXIT_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    lv_label_set_text(cam_label, "检测完成\n测试通过");
    create_and_write_file(file_path, "Camera: PASS", true);
}

static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, uint32_t camera_buf_hes, uint32_t camera_buf_ves, size_t camera_buf_len)
{
    ppa_srm_oper_config_t srm_config = {
        .in.buffer = camera_buf,
        .in.pic_w = camera_buf_hes,
        .in.pic_h = camera_buf_ves,
        .in.block_w = 960,
        .in.block_h = 960,
        .in.block_offset_x = (camera_buf_hes - 960) / 2,
        .in.block_offset_y = (camera_buf_ves - 960) / 2,
        .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .out.buffer = canvas_buf[camera_buf_index],
        .out.buffer_size = ALIGN_UP(BSP_LCD_H_RES * BSP_LCD_V_RES * 2, data_cache_line_size),
        .out.pic_w = BSP_LCD_H_RES,
        .out.pic_h = BSP_LCD_V_RES,
        .out.block_offset_x = 0,
        .out.block_offset_y = 0,
        .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = 1.0,
        .scale_y = 1.0,
        .rgb_swap = 0,
        .byte_swap = 0,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    srm_config.scale_x = (float)BSP_LCD_H_RES / 960;
    srm_config.scale_y = (float)BSP_LCD_V_RES / 960;

    ESP_ERROR_CHECK(ppa_do_scale_rotate_mirror(ppa_srm_handle, &srm_config));

    uint16_t *canvas_buf_ptr = (uint16_t *)canvas_buf[camera_buf_index];
    for(int i =0 ;i< BSP_LCD_H_RES * BSP_LCD_V_RES; i++) {
        uint16_t swap16 = *(canvas_buf_ptr + i);
        swap16 = (swap16 >> 8) | (swap16 << 8);
        *(canvas_buf_ptr + i) = swap16;
    }

    bsp_display_lock(0);
    lv_canvas_set_buffer(cam_canvas, canvas_buf[camera_buf_index], BSP_LCD_H_RES, BSP_LCD_V_RES, LV_IMG_CF_TRUE_COLOR);
    lv_refr_now(NULL);
    bsp_display_unlock();
}