/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "esp_log.h"

#include "esp_video_init.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"
#include "esp_timer.h"
#include "driver/ppa.h"
#include "driver/jpeg_encode.h"
#include "app_video.h"
#include "app_audio.h"

#define ALIGN_UP(num, align)    (((num) + ((align) - 1)) & ~((align) - 1))

static ppa_client_handle_t ppa_srm_handle = NULL;
static size_t data_cache_line_size = 0;
static lv_obj_t* cam_canvas;
static void *canvas_buf[EXAMPLE_CAM_BUF_NUM];

static i2c_master_bus_handle_t i2c_handle;

static jpeg_encoder_handle_t jpeg_handle;
static size_t rx_buffer_size = 0;
static uint32_t jpg_size_240p;
static uint8_t *jpg_buf_240p;
static bool jpg_save_flag = false;

#if CONFIG_EXAMPLE_ENABLE_PRINT_FPS_RATE_VALUE
static int fps_count;
static int64_t start_time;
#endif

extern void example_lvgl_demo_ui(lv_obj_t *scr);
static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, uint32_t camera_buf_hes, uint32_t camera_buf_ves, size_t camera_buf_len);

static const char *TAG = "main";

static void btn_handler(void *button_handle, void *usr_data)
{
    int button_pressed = (int)usr_data;
    ESP_LOGI(TAG, "Button %d pressed", button_pressed);

    if(button_pressed == BSP_BUTTON_1) {
        bsp_led_set(BSP_LED_WHITE, 1);
    } else {
        bsp_led_set(BSP_LED_WHITE, 0);
    }
}

void app_main(void)
{
    /* Init Buttons */
    button_handle_t btns[BSP_BUTTON_NUM];
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    for (int i = 0; i < BSP_BUTTON_NUM; i++) {
        ESP_ERROR_CHECK(iot_button_register_cb(btns[i], BUTTON_PRESS_DOWN, btn_handler, (void *) i));
    }
    
    ESP_LOGI(TAG, "LEDs initialized");
    ESP_ERROR_CHECK(bsp_leds_init());

    ESP_ERROR_CHECK(bsp_sdcard_mount());
    ESP_LOGI(TAG, "SD card mounted");

    // Initialize the PPA
    ppa_client_config_t ppa_srm_config = {
        .oper_type = PPA_OPERATION_SRM,
    };
    ESP_ERROR_CHECK(ppa_register_client(&ppa_srm_config, &ppa_srm_handle));
    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &data_cache_line_size));

    // Initialize the JPEG encoder
    jpeg_encode_engine_cfg_t encode_eng_cfg = {
        .timeout_ms = 70,
    };

    ESP_ERROR_CHECK(jpeg_new_encoder_engine(&encode_eng_cfg, &jpeg_handle));

    jpeg_encode_memory_alloc_cfg_t rx_mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };

    jpg_buf_240p = (uint8_t*)jpeg_alloc_encoder_mem(640 * 480 * 2 / 10, &rx_mem_cfg, &rx_buffer_size); // Assume that compression ratio of 10 to 1
    assert(jpg_buf_240p != NULL);

    // Initialize the video camera
    ESP_ERROR_CHECK(bsp_i2c_init());
    bsp_get_i2c_bus_handle(&i2c_handle);
    esp_err_t ret = app_video_main(i2c_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "video main init failed with error 0x%x", ret);
        return;
    }
    // Open the video device
    int video_cam_fd0 = app_video_open(0);
    if (video_cam_fd0 < 0) {
        ESP_LOGE(TAG, "video cam open failed");
        return;
    }

    // Initialize video capture device
    ret = app_video_init(video_cam_fd0, APP_VIDEO_FMT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Video cam init failed with error 0x%x", ret);
        return;
    }

    for (int i = 0; i < EXAMPLE_CAM_BUF_NUM; i++) {
        canvas_buf[i] = heap_caps_aligned_calloc(data_cache_line_size, 1, app_video_get_buf_size(), MALLOC_CAP_SPIRAM);
        if (canvas_buf[i] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate canvas buffer");
            return;
        }
    }

    ESP_LOGI(TAG, "Using map buffer");
    ESP_ERROR_CHECK(app_video_set_bufs(video_cam_fd0, EXAMPLE_CAM_BUF_NUM, NULL)); // When setting the camera video buffer, it can be written as NULL to automatically allocate the buffer using mapping

    // Initialize the audio
    bsp_extra_pdm_codec_init();

    app_audio_init();

    // Initialize the display
    bsp_display_start();

    bsp_display_lock(0);

    cam_canvas = lv_canvas_create(lv_scr_act());
    lv_obj_set_size(cam_canvas, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_align(cam_canvas, LV_ALIGN_CENTER);

    bsp_display_unlock();
    bsp_display_backlight_on();

    // Register the video frame operation callback
    ESP_ERROR_CHECK(app_video_register_frame_operation_cb(camera_video_frame_operation));

    // Start the camera stream task
    ESP_ERROR_CHECK(app_video_stream_task_start(video_cam_fd0, 0));

#if CONFIG_EXAMPLE_ENABLE_PRINT_FPS_RATE_VALUE
    start_time = esp_timer_get_time();  // Get the initial time for frame rate statistics
#endif
}

static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, uint32_t camera_buf_hes, uint32_t camera_buf_ves, size_t camera_buf_len)
{
#if CONFIG_EXAMPLE_ENABLE_PRINT_FPS_RATE_VALUE
    fps_count++;
    if (fps_count == 50) {
        int64_t end_time = esp_timer_get_time();
        ESP_LOGI(TAG, "fps: %f", 1000000.0 / ((end_time - start_time) / 50.0));
        start_time = end_time;
        fps_count = 0;
    
        ESP_LOGI(TAG, "camera_buf_hes: %lu, camera_buf_ves: %lu, camera_buf_len: %d KB", camera_buf_hes, camera_buf_ves, camera_buf_len / 1024);
    }
#endif

#if 0
    ppa_srm_oper_config_t srm_config = {
        .in.buffer = camera_buf,
        .in.pic_w = camera_buf_hes,
        .in.pic_h = camera_buf_ves,
        .in.block_w = BSP_LCD_H_RES * 2,
        .in.block_h = BSP_LCD_V_RES * 2,
        .in.block_offset_x = 0,
        .in.block_offset_y = 0,
        .in.srm_cm = APP_VIDEO_FMT == APP_VIDEO_FMT_RGB565 ? PPA_SRM_COLOR_MODE_RGB565 : PPA_SRM_COLOR_MODE_RGB888,
        .out.buffer = canvas_buf[camera_buf_index],
        // .out.buffer_size = ALIGN_UP(BSP_LCD_H_RES * BSP_LCD_V_RES * (APP_VIDEO_FMT == APP_VIDEO_FMT_RGB565 ? 2 : 3), data_cache_line_size),
        .out.buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES * (APP_VIDEO_FMT == APP_VIDEO_FMT_RGB565 ? 2 : 3),
        .out.pic_w = BSP_LCD_H_RES,
        .out.pic_h = BSP_LCD_V_RES,
        .out.block_offset_x = 0,
        .out.block_offset_y = 0,
        .out.srm_cm = APP_VIDEO_FMT == APP_VIDEO_FMT_RGB565 ? PPA_SRM_COLOR_MODE_RGB565 : PPA_SRM_COLOR_MODE_RGB888,
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = 0.5, 
        .scale_y = 0.5,
        .rgb_swap = 1,
        .byte_swap = 1,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    ESP_ERROR_CHECK(ppa_do_scale_rotate_mirror(ppa_srm_handle, &srm_config));
    
    lv_canvas_set_buffer(cam_canvas, canvas_buf[camera_buf_index], BSP_LCD_H_RES, BSP_LCD_V_RES, LV_IMG_CF_TRUE_COLOR);
#else
    ppa_srm_oper_config_t srm_config = {
        .in.buffer = camera_buf,
        .in.pic_w = camera_buf_hes,
        .in.pic_h = camera_buf_ves,
        .in.block_w = camera_buf_hes,
        .in.block_h = camera_buf_ves,
        .in.block_offset_x = 0,
        .in.block_offset_y = 0,
        .in.srm_cm = APP_VIDEO_FMT == APP_VIDEO_FMT_RGB565 ? PPA_SRM_COLOR_MODE_RGB565 : PPA_SRM_COLOR_MODE_RGB888,
        .out.buffer = canvas_buf[camera_buf_index],
        .out.buffer_size = ALIGN_UP(BSP_LCD_H_RES * BSP_LCD_V_RES * (APP_VIDEO_FMT == APP_VIDEO_FMT_RGB565 ? 2 : 3), data_cache_line_size),
        .out.pic_w = BSP_LCD_H_RES,
        .out.pic_h = BSP_LCD_V_RES,
        .out.block_offset_x = 0,
        .out.block_offset_y = 0,
        .out.srm_cm = APP_VIDEO_FMT == APP_VIDEO_FMT_RGB565 ? PPA_SRM_COLOR_MODE_RGB565 : PPA_SRM_COLOR_MODE_RGB888,
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = 1, 
        .scale_y = 1,
        .rgb_swap = 0,
        .byte_swap = 1,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    srm_config.in.block_w = (camera_buf_hes > BSP_LCD_H_RES) ? BSP_LCD_H_RES : camera_buf_hes;
    srm_config.in.block_h = (camera_buf_ves > BSP_LCD_V_RES) ? BSP_LCD_V_RES : camera_buf_ves;

    ESP_ERROR_CHECK(ppa_do_scale_rotate_mirror(ppa_srm_handle, &srm_config));

    lv_canvas_set_buffer(cam_canvas, canvas_buf[camera_buf_index], srm_config.in.block_w, srm_config.in.block_h, LV_IMG_CF_TRUE_COLOR);

#endif

    if(!jpg_save_flag) {
        jpg_save_flag = !jpg_save_flag;
        ESP_LOGI(TAG, "JPEG encoding start");

        jpeg_encode_cfg_t enc_config = {
            .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
            .sub_sample = JPEG_DOWN_SAMPLING_YUV420,
            .image_quality = 80,
            .width = 240,
            .height = 240,
        };

        ESP_ERROR_CHECK(jpeg_encoder_process(jpeg_handle, &enc_config, srm_config.out.buffer, srm_config.out.buffer_size, jpg_buf_240p, rx_buffer_size, &jpg_size_240p));

        FILE *file_jpg_240p = fopen("/sdcard/outjpg.jpg", "wb");
        ESP_LOGI(TAG, "outfile:%s",  "/sdcard/outjpg.jpg");
        if (file_jpg_240p == NULL) {
            ESP_LOGE(TAG, "fopen file_jpg_1080p error");
            return;
        }

        fwrite(jpg_buf_240p, 1, jpg_size_240p, file_jpg_240p);
        fclose(file_jpg_240p);
    }
} 