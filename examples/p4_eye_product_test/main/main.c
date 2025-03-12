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
#include "driver/ppa.h"
#include "esp_private/esp_cache_private.h"

#include "app_usb_hid.h"
#include "app_sr.h"
#include "app_wifi_scan.h"
#include "app_video.h"

#define TEST_DISK_PATH "/spiflash"
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
    // ESP_LOGI(TAG, "Opening file %s", path);

    // FILE *f = fopen(path, append ? "a" : "w");
    // if (f == NULL) {
    //     ESP_LOGE(TAG, "Failed to open file for writing");
    //     return ESP_FAIL;
    // }
    // fprintf(f, "%s\n", data);
    // fclose(f);

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
    ESP_LOGI(TAG, "[MAC address]: %s", mac_str);

    char file_path[128];
    snprintf(file_path, sizeof(file_path), TEST_RESULT_FILE_FORMAT, TEST_DISK_PATH, mac_str);

    // Initialize USB MSC
    app_usb_hid_init();
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
    while(!app_usb_hid_stage()) {
        lv_label_set_text(label, "Detecting USB HS...");
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "[Done] USB HS detected!");

    bsp_extra_pdm_codec_init();
    app_sr_start(false);

    // Wait for wakeup
    while (!app_sr_get_wakeup_result()) {
        lv_label_set_text(label, "Detecting wakeup...");
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "[Done] Wakeup detected!");

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
    
    lv_label_set_text(label, "Please press the \n three buttons \n  on the right.");

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
    ESP_ERROR_CHECK(bsp_knob_register_cb(KNOB_LEFT, knob_right_cb, NULL));
    ESP_ERROR_CHECK(bsp_knob_register_cb(KNOB_RIGHT, knob_left_cb, NULL));    
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_ED], BUTTON_PRESS_UP, btn_handler, (void *) BSP_BUTTON_ED));

    lv_label_set_text(label, "   Toggle \n the knob left");
    xEventGroupWaitBits(button_event_group, KNOB_LEFT_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    lv_label_set_text(label, "   Toggle \n the knob right");
    xEventGroupWaitBits(button_event_group, KNOB_RIGHT_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    lv_label_set_text(label, "Press the knob");
    xEventGroupWaitBits(button_event_group, KNOB_PRESS_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    lv_label_set_text(label, "Knob test \n passed!");
    create_and_write_file(file_path, "Knob: PASS", true);

    // Initialize the led
    ESP_ERROR_CHECK(bsp_leds_init());
    lv_label_set_text(label, "Check fill light \n        if on \n press any key.");

    bsp_led_set(BSP_LED_WHITE, 1);  // Turn on the white LED
    
    xEventGroupWaitBits(button_event_group, LED_WHITE_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    lv_label_set_text(label, "LED test \n passed!");
    create_and_write_file(file_path, "LED: PASS", true);
    bsp_display_lock(0);

    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);

    cam_canvas = lv_canvas_create(lv_scr_act());
    lv_obj_set_size(cam_canvas, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_align(cam_canvas, LV_ALIGN_CENTER);
    
    cam_label = lv_label_create(cam_canvas);
    lv_obj_set_style_text_font(cam_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_align(cam_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(cam_label, lv_color_make(255, 0, 0), LV_PART_MAIN);
    lv_label_set_text(cam_label, "If normal \n press any key \n to exit.");
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
    lv_label_set_text(cam_label, "Camera test \n passed!");
    create_and_write_file(file_path, "Camera: PASS", true);
}

static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, uint32_t camera_buf_hes, uint32_t camera_buf_ves, size_t camera_buf_len)
{
    ppa_srm_oper_config_t srm_config = {
        .in.buffer = camera_buf,
        .in.pic_w = camera_buf_hes,
        .in.pic_h = camera_buf_ves,
        // .in.block_w = camera_buf_hes > camera_buf_ves ? camera_buf_ves : camera_buf_hes,
        // .in.block_h = camera_buf_hes > camera_buf_ves ? camera_buf_ves : camera_buf_hes,
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

    // srm_config.scale_x = (float)BSP_LCD_H_RES / (camera_buf_hes > camera_buf_ves ? camera_buf_ves : camera_buf_hes);
    // srm_config.scale_y = (float)BSP_LCD_V_RES / (camera_buf_hes > camera_buf_ves ? camera_buf_ves : camera_buf_hes);
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