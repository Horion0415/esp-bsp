/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <dirent.h> 
#include <fcntl.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"
#include "esp_timer.h"
#include "esp_event.h"

#include "driver/jpeg_encode.h"
#include "driver/ppa.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

#include "protocol_examples_common.h"
#include "app_video.h"
#include "app_usb_msc.h"
#include "app_smtp.h"
#include "ui.h"

#define ALIGN_UP(num, align)    (((num) + ((align) - 1)) & ~((align) - 1))
#define P4_EYE_CAMERA_EN_PIN                       (GPIO_NUM_15)
#define TIMER_SEC_INTERVAL                         (1 * 1000000)
#define TIMER_MIN_INTERVAL                         (60 * 1000000)

enum {
    SCREEN_EYE_CAMERA,
    SCREEN_EYE_SET,
} screen_index;

static const char *TAG = "main";

static i2c_master_bus_handle_t i2c_handle;
static ppa_client_handle_t ppa_srm_handle = NULL;
static size_t data_cache_line_size = 0;

static void *canvas_buf[EXAMPLE_CAM_BUF_NUM];
static lv_obj_t* cam_canvas;

static uint32_t timed_min = 5;
static bool timed_shooting = false;

static jpeg_encoder_handle_t jpeg_handle;
static uint32_t jpg_size;
static uint8_t *jpg_buf;
static size_t rx_buffer_size = 0;

static esp_timer_handle_t periodic_timer;

static nvs_handle_t nvs_save_handle;

static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, uint32_t camera_buf_hes, uint32_t camera_buf_ves, size_t camera_buf_len);
static void periodic_timer_callback(void* arg);
static int get_next_file_index(const char *path);
static void increase_btn_handler(void *button_handle, void *usr_data);
static void decrease_btn_handler(void *button_handle, void *usr_data);
static void mode_switch_btn_handler(void *button_handle, void *usr_data);
void detect_usb_task(void *arg);

void app_main(void)
{
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    err = nvs_open("storage", NVS_READWRITE, &nvs_save_handle);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        printf("Done\n");

        // Read
        printf("Reading shutter flag from NVS ... ");
        err |= nvs_get_u32(nvs_save_handle, "timed_min", &timed_min);
        switch (err) {
            case ESP_OK:
                ESP_LOGI(TAG, "Done\n");
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                printf("The value is not initialized yet!\n");
                break;
            default :
                printf("Error (%s) reading!\n", esp_err_to_name(err));
        }
    }

    // Connect to the wifi network
    ESP_ERROR_CHECK(example_connect());
    ESP_ERROR_CHECK(app_smtp_tls_init());
    ESP_ERROR_CHECK(app_smtp_connect_server());
    ESP_ERROR_CHECK(app_smtp_perform_authentication());

    // Initialize the display
    bsp_display_start();

    // Initialize the led
    ESP_ERROR_CHECK(bsp_leds_init());

    // Initialize the PPA
    ppa_client_config_t ppa_srm_config = {
        .oper_type = PPA_OPERATION_SRM,
    };
    ESP_ERROR_CHECK(ppa_register_client(&ppa_srm_config, &ppa_srm_handle));
    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &data_cache_line_size));

    // Initialize the SD card
    ESP_ERROR_CHECK(bsp_sdcard_mount());
    ESP_LOGI(TAG, "SD card mounted");

    // Initialize the USB MSC
    app_usb_msc_init();

    // Initialize the I2C
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
    
    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &data_cache_line_size));
    for (int i = 0; i < EXAMPLE_CAM_BUF_NUM; i++) {
        canvas_buf[i] = heap_caps_aligned_calloc(data_cache_line_size, 1, app_video_get_buf_size(), MALLOC_CAP_SPIRAM);
        if (canvas_buf[i] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate canvas buffer");
            return;
        }
    }

    // Initialize the JPEG encoder
    jpeg_encode_engine_cfg_t encode_eng_cfg = {
        .timeout_ms = 70,
    };

    ESP_ERROR_CHECK(jpeg_new_encoder_engine(&encode_eng_cfg, &jpeg_handle));

    jpeg_encode_memory_alloc_cfg_t rx_mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };

    jpg_buf = (uint8_t*)jpeg_alloc_encoder_mem(app_video_get_buf_size() / 10, &rx_mem_cfg, &rx_buffer_size); // Assume that compression ratio of 10 to 1
    assert(jpg_buf != NULL);

    // Initialize the timer
    const esp_timer_create_args_t periodic_timer_args = {
            .callback = &periodic_timer_callback,
            .name = "periodic"
    };
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, timed_min * TIMER_SEC_INTERVAL));

    // Register the video frame operation callback
    ESP_ERROR_CHECK(app_video_register_frame_operation_cb(camera_video_frame_operation));

    // Start the camera stream task
    ESP_ERROR_CHECK(app_video_stream_task_start(video_cam_fd0, 0));

    // Initialize the UI
    bsp_display_lock(0);
    screen_index = SCREEN_EYE_CAMERA;

    ui_init();

    lv_label_set_text_fmt(ui_LabelSet, "Set time: %ld minutes\n\n\n\n\n\n\n", timed_min);

    cam_canvas = lv_canvas_create(ui_ScreenMain);
    lv_obj_set_size(cam_canvas, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_align(cam_canvas, LV_ALIGN_CENTER);

    bsp_display_unlock();
    bsp_display_backlight_on();

    /* Init Buttons */
    button_handle_t btns[BSP_BUTTON_NUM];
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_1], BUTTON_PRESS_DOWN, mode_switch_btn_handler, (void *) BSP_BUTTON_1));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_2], BUTTON_PRESS_DOWN, increase_btn_handler, (void *) BSP_BUTTON_2));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_3], BUTTON_PRESS_DOWN, decrease_btn_handler, (void *) BSP_BUTTON_3));

    xTaskCreatePinnedToCore(detect_usb_task, "detect_usb_task", 4096, NULL, 5, NULL, 0);
}

static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, uint32_t camera_buf_hes, uint32_t camera_buf_ves, size_t camera_buf_len)
{
#if 0 // Scale, rotate, and mirror the camera frame
    ppa_srm_oper_config_t srm_config = {
        .in.buffer = camera_buf,
        .in.pic_w = camera_buf_hes,
        .in.pic_h = camera_buf_ves,
        .in.block_w = camera_buf_ves,
        .in.block_h = camera_buf_ves,
        .in.block_offset_x = (camera_buf_hes - camera_buf_ves) / 2,
        .in.block_offset_y = 0,
        .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .out.buffer = canvas_buf[camera_buf_index],
        .out.buffer_size = ALIGN_UP(BSP_LCD_H_RES * BSP_LCD_V_RES * 2, data_cache_line_size),
        .out.pic_w = BSP_LCD_H_RES,
        .out.pic_h = BSP_LCD_V_RES,
        .out.block_offset_x = 0,
        .out.block_offset_y = 0,
        .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = 0.3333,
        .scale_y = 0.3333,
        .rgb_swap = 0,
        .byte_swap = 1,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
#else
    ppa_srm_oper_config_t srm_config = {
        .in.buffer = camera_buf,
        .in.pic_w = camera_buf_hes,
        .in.pic_h = camera_buf_ves,
        .in.block_w = BSP_LCD_H_RES,
        .in.block_h = BSP_LCD_V_RES,
        .in.block_offset_x = (camera_buf_hes - BSP_LCD_H_RES) / 2,
        .in.block_offset_y = (camera_buf_ves - BSP_LCD_V_RES) / 2,
        .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .out.buffer = canvas_buf[camera_buf_index],
        .out.buffer_size = ALIGN_UP(BSP_LCD_H_RES * BSP_LCD_V_RES * 2, data_cache_line_size),
        .out.pic_w = BSP_LCD_H_RES,
        .out.pic_h = BSP_LCD_V_RES,
        .out.block_offset_x = 0,
        .out.block_offset_y = 0,
        .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = 1,
        .scale_y = 1,
        .rgb_swap = 0,
        .byte_swap = 1,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
#endif

    ESP_ERROR_CHECK(ppa_do_scale_rotate_mirror(ppa_srm_handle, &srm_config));

    bsp_display_lock(0);
    lv_canvas_set_buffer(cam_canvas, canvas_buf[camera_buf_index], BSP_LCD_H_RES, BSP_LCD_V_RES, LV_IMG_CF_TRUE_COLOR);
    bsp_display_unlock();

    if(timed_shooting) {
        jpeg_encode_cfg_t enc_config = {
            .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
            .sub_sample = JPEG_DOWN_SAMPLING_YUV422,
            .image_quality = 50,
            .width = camera_buf_hes,
            .height = camera_buf_ves,
        };

        timed_shooting = false;

        char file_name[64];

        bsp_led_set(BSP_LED_WHITE, 1); // Turn on the white LED

        ESP_ERROR_CHECK(jpeg_encoder_process(jpeg_handle, &enc_config, camera_buf, app_video_get_buf_size(), jpg_buf, rx_buffer_size, &jpg_size));

        int image_count = get_next_file_index(BSP_SD_MOUNT_POINT"/pic_save");
        snprintf(file_name, sizeof(file_name), BSP_SD_MOUNT_POINT"/pic_save/OUTJPG_%d.JPG", image_count++);

        FILE *file_jpg = fopen(file_name, "wb");
        ESP_LOGI(TAG, "Writing jpg to %s", file_name);
        if (file_jpg == NULL) {
            ESP_LOGE(TAG, "fopen file_jpg error");
        }
        fwrite(jpg_buf, 1, jpg_size, file_jpg);
        fclose(file_jpg);

        bsp_led_set(BSP_LED_WHITE, 0);  // Turn off the white LED

        app_smtp_compose_email(jpg_buf, jpg_size, file_name);
    }
}

void detect_usb_task(void *arg)
{
    while (1) {
        if(app_usb_msc_stage()) {
            app_usb_set_exposed(false);

            ESP_ERROR_CHECK(esp_timer_stop(periodic_timer));

            bsp_display_lock(0);
            _ui_screen_change(&ui_ScreenUSB, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_ScreenUSB_screen_init);
            bsp_display_unlock();
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

static void periodic_timer_callback(void* arg)
{
    timed_shooting = true;
}

static int get_next_file_index(const char *path) 
{
    DIR *dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory %s", path);
        return 0;
    }

    struct dirent *entry;
    int max_index = -1;  

    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, "OUTJPG_") && strstr(entry->d_name, ".JPG")) {
            int index;
            if (sscanf(entry->d_name, "OUTJPG_%d.JPG", &index) == 1) {
                if (index > max_index) {
                    max_index = index;  
                }
            }
        }
    }

    closedir(dir);
    return max_index + 1;  
}

static void mode_switch_btn_handler(void *button_handle, void *usr_data)
{
    if(screen_index == SCREEN_EYE_CAMERA) {
        screen_index = SCREEN_EYE_SET;
        
        ESP_ERROR_CHECK(esp_timer_stop(periodic_timer));

        _ui_screen_change(&ui_ScreenSet, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_ScreenSet_screen_init);
    } else {
        screen_index = SCREEN_EYE_CAMERA;

        ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, timed_min * TIMER_SEC_INTERVAL));

        _ui_screen_change(&ui_ScreenMain, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_ScreenMain_screen_init);
    }
}

static void increase_btn_handler(void *button_handle, void *usr_data)
{
    timed_min += 5;
    if(timed_min > 120) {
        timed_min = 5;
    }

    lv_label_set_text_fmt(ui_LabelSet, "Set time: %ld minutes\n\n\n\n\n\n\n", timed_min);
    ESP_LOGI(TAG, "timed_min: %ld", timed_min);

    ESP_ERROR_CHECK(nvs_set_u32(nvs_save_handle, "timed_min", timed_min));
}

static void decrease_btn_handler(void *button_handle, void *usr_data)
{
    timed_min -= 5;
    if(timed_min < 5) {
        timed_min = 120;
    }


    lv_label_set_text_fmt(ui_LabelSet, "Set time: %ld minutes\n\n\n\n\n\n\n", timed_min);
    ESP_LOGI(TAG, "timed_min: %ld", timed_min);

    ESP_ERROR_CHECK(nvs_set_u32(nvs_save_handle, "timed_min", timed_min));
}