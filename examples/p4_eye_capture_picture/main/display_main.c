/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>

#include "esp_log.h"
#include "esp_video_init.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"
#include "esp_timer.h"
#include "esp_sleep.h"

#include "driver/jpeg_encode.h"
#include "driver/jpeg_encode.h"

#include <dirent.h> 
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/errno.h>
#include "linux/videodev2.h"

#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "app_video.h"

#define P4_EYE_C6_EN_PIN                           (GPIO_NUM_5)
#define P4_EYE_CAMERA_EN_PIN                       (GPIO_NUM_15)

static i2c_master_bus_handle_t i2c_handle;
static size_t data_cache_line_size = 0;
static void *camera_buf[EXAMPLE_CAM_BUF_NUM];

static jpeg_encoder_handle_t jpeg_handle;
static uint32_t jpg_size;
static uint8_t *jpg_buf;
static size_t rx_buffer_size = 0;

static lv_obj_t *file_label;
static lv_obj_t *time_label;

static int wakeup_time_sec;

static const char *TAG = "main";

static void deep_sleep_register_rtc_timer_wakeup(void)
{
    printf("Enabling timer wakeup, %ds\n", wakeup_time_sec);

    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(wakeup_time_sec * 1000000));
}

static void set_slave_power(bool on)
{
    gpio_set_level(P4_EYE_C6_EN_PIN, on);
}

static void set_camera_power(bool on)
{
    gpio_set_level(P4_EYE_CAMERA_EN_PIN, on);
}

static int get_next_file_index(const char *path) {
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

static void video_capture_task(void *arg)
{
    int video_fd = *((int *)arg);

    struct v4l2_buffer v4l2_buf;

    uint32_t camera_buf_hes = 0;
    uint32_t camera_buf_ves = 0;

    int image_count = get_next_file_index("/sdcard/pic_save");;

    video_get_hes_ves(&camera_buf_hes, &camera_buf_ves);

    while(1) {
        memset(&v4l2_buf, 0, sizeof(v4l2_buf));
        v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        v4l2_buf.memory = V4L2_MEMORY_USERPTR;

        int res = ioctl(video_fd, VIDIOC_DQBUF, &v4l2_buf);
        if (res != 0) {
            ESP_LOGE(TAG, "failed to receive video frame");
        }

        bsp_led_set(BSP_LED_WHITE, 1);

        v4l2_buf.m.userptr = (unsigned long)camera_buf[v4l2_buf.index];
        v4l2_buf.length = app_video_get_buf_size();

        jpeg_encode_cfg_t enc_config = {
            .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
            .sub_sample = JPEG_DOWN_SAMPLING_YUV420,
            .image_quality = 80,
            .width = camera_buf_hes,
            .height = camera_buf_ves,
        };

        ESP_ERROR_CHECK(jpeg_encoder_process(jpeg_handle, &enc_config, camera_buf[v4l2_buf.index], app_video_get_buf_size(), jpg_buf, rx_buffer_size, &jpg_size));

        char file_name[64];
        snprintf(file_name, sizeof(file_name), "/sdcard/pic_save/OUTJPG_%d.JPG", image_count++);        

        bsp_display_lock(0);
        lv_label_set_text_fmt(file_label, "Saved count: %d", image_count);
        bsp_display_unlock();

        FILE *file_jpg = fopen(file_name, "wb");
        ESP_LOGI(TAG, "Writing jpg to %s", file_name);
        if (file_jpg == NULL) {
            ESP_LOGE(TAG, "fopen file_jpg error");
        }

        fwrite(jpg_buf, 1, jpg_size, file_jpg);
        fclose(file_jpg);

        if (ioctl(video_fd, VIDIOC_QBUF, &v4l2_buf) != 0) {
            ESP_LOGE(TAG, "failed to free video frame");
        }

        bsp_led_set(BSP_LED_WHITE, 0);

        // enter deep sleep
        set_camera_power(false);
        esp_deep_sleep_start();
    }
}

void app_main(void)
{   
    ESP_LOGI(TAG, "LEDs initialized");
    ESP_ERROR_CHECK(bsp_leds_init());

    const gpio_config_t led_io_config = {
        .pin_bit_mask = BIT64(P4_EYE_C6_EN_PIN),
        .mode = GPIO_MODE_OUTPUT, 
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&led_io_config));
    set_slave_power(false);

    const gpio_config_t camera_io_config = {
        .pin_bit_mask = BIT64(P4_EYE_CAMERA_EN_PIN),
        .mode = GPIO_MODE_OUTPUT, 
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&camera_io_config));
    set_camera_power(true);

    ESP_ERROR_CHECK(bsp_sdcard_mount());
    ESP_LOGI(TAG, "SD card mounted");

    // Initialize the display
    bsp_display_start();
    bsp_display_backlight_on();

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

    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &data_cache_line_size));
    for (int i = 0; i < EXAMPLE_CAM_BUF_NUM; i++) {
        camera_buf[i] = heap_caps_aligned_calloc(data_cache_line_size, 1, app_video_get_buf_size(), MALLOC_CAP_SPIRAM);
        if (camera_buf[i] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate canvas buffer");
            return;
        }
    }

    ESP_LOGI(TAG, "Using user buffer");
    ESP_ERROR_CHECK(app_video_set_bufs(video_cam_fd0, EXAMPLE_CAM_BUF_NUM, (void*)camera_buf));

    ESP_ERROR_CHECK(video_stream_start(video_cam_fd0));

    deep_sleep_register_rtc_timer_wakeup();

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

    bsp_display_lock(0);

    time_label = lv_label_create(lv_scr_act());
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_24, 0);
    lv_label_set_text_fmt(time_label, "Timed Shooting: %d", wakeup_time_sec);

    file_label = lv_label_create(lv_scr_act());
    lv_obj_align(file_label, LV_ALIGN_CENTER, 0, -50);
    lv_obj_set_style_text_font(file_label, &lv_font_montserrat_24, 0);
    lv_label_set_text_fmt(file_label, "Start filming");

    bsp_display_unlock();

    // Start the video capture task
    xTaskCreatePinnedToCore(video_capture_task, "video capture task", 4 * 1024, &video_cam_fd0, 4, NULL, 0);
}