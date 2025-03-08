#include <stdio.h>
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_private/esp_cache_private.h"
#include "driver/ppa.h"
#include "driver/jpeg_encode.h"
#include "bsp/esp-bsp.h"

#include "ui_extra.h"
#include "app_video.h"
#include "app_storage.h"
#include "app_video_stream.h"
#include "app_album.h"

#define ALIGN_UP(num, align)    (((num) + ((align) - 1)) & ~((align) - 1))
#define SCALE_LEVELS 6                         // resolution scale levels
#define DEBUG_MODE   1

static const char *TAG = "app_video_stream";

static size_t data_cache_line_size = 0;
static ppa_client_handle_t ppa_srm_handle = NULL;
static void *canvas_buf[EXAMPLE_CAM_BUF_NUM];

static jpeg_encoder_handle_t jpeg_handle;
static uint32_t jpg_size;
static uint8_t *jpg_buf;
static size_t rx_buffer_size = 0;

static uint8_t *photo_buf = NULL;

static int scale_level_res[SCALE_LEVELS] = {960, 480, 240, 120, 80, 60};

static bool is_take_photo = false;
static bool is_take_video = false;
static bool is_interval_photo_active = false;
static bool is_camera_initialized = false;
static bool is_flash_light_on = false;

static uint32_t next_wake_time = 0;
static uint16_t current_interval_minutes = 0;

static TaskHandle_t photo_task_handle = NULL;
static QueueHandle_t photo_queue = NULL;

static uint32_t camera_init_count = 0;

static SemaphoreHandle_t photo_take_sem = NULL;
typedef struct {
    uint8_t *camera_buf;
    uint32_t width;
    uint32_t height;
} photo_task_params_t;

static const uint32_t photo_resolution_width[PHOTO_RESOLUTION_MAX] = {640, 1280, 1920};
static const uint32_t photo_resolution_height[PHOTO_RESOLUTION_MAX] = {480, 720, 1080};
static photo_resolution_t current_photo_resolution = PHOTO_RESOLUTION_1080P; // default 1080P

static void photo_task(void *pvParameters);
static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, uint32_t camera_buf_hes, uint32_t camera_buf_ves, size_t camera_buf_len);

// Enter deep sleep
static void enter_deep_sleep(uint16_t sleep_minutes)
{
    // Calculate the next wake up time (current time + interval time)
    next_wake_time = esp_timer_get_time() / 1000000 + sleep_minutes * 60;
    
    // Save the interval photo state
    app_storage_save_interval_state(is_interval_photo_active, next_wake_time);
    app_storage_save_photo_count(app_extra_get_saved_photo_count());

    // Initialize the sleep IO
    bsp_sleep_io_init();

    // Set the wake up time (microseconds)
#if DEBUG_MODE
    uint64_t sleep_time_us = sleep_minutes * 1000000ULL;
#else
    uint64_t sleep_time_us = sleep_minutes * 60 * 1000000ULL;
#endif
    
    ESP_LOGI(TAG, "Entering deep sleep for %d minutes", sleep_minutes);
    
    // Configure the RTC wake up timer
    esp_sleep_enable_timer_wakeup(sleep_time_us);
    
    // Enter deep sleep
    esp_deep_sleep_start();
}

// Handle the interval photo complete callback
static void interval_photo_complete_callback(void)
{
    ESP_LOGI(TAG, "Interval photo completed, saved photo count: %d", app_extra_get_saved_photo_count());
    
    // If the interval photo is still active, enter deep sleep
    if (is_interval_photo_active) {
        // Enter deep sleep until the next photo time
        enter_deep_sleep(current_interval_minutes);
    }
}

// Start interval photo
esp_err_t app_video_stream_start_interval_photo(uint16_t interval_minutes)
{
    current_interval_minutes = interval_minutes;
    is_interval_photo_active = true;
    
    // Save the interval photo state
    next_wake_time = esp_timer_get_time() / 1000000 + interval_minutes * 60;
    app_storage_save_interval_state(true, next_wake_time);
    
    // Take a photo immediately
    app_video_stream_take_photo();
    ESP_LOGI(TAG, "Interval photo started with interval %d minutes", interval_minutes);
    
    return ESP_OK;
}

// Stop interval photo
esp_err_t app_video_stream_stop_interval_photo(void)
{
    is_interval_photo_active = false;
    
    // Save the interval photo state (close)
    app_storage_save_interval_state(false, 0);

    app_storage_save_photo_count(app_extra_get_saved_photo_count());
    
    ESP_LOGI(TAG, "Interval photo stopped");
    
    return ESP_OK;
}

void swap_rgb565_bytes(uint16_t *buffer, int pixel_count)
{
    for (int i = 0; i < pixel_count; i++) {
        uint16_t swap16 = *(buffer + i);
        swap16 = (swap16 >> 8) | (swap16 << 8);
        *(buffer + i) = swap16;
    }
}

esp_err_t app_video_stream_take_photo(void)
{
    is_take_photo = true;

    return ESP_OK;
}

esp_err_t app_video_stream_stop_take_photo(void)
{
    is_take_photo = false;

    return ESP_OK;
}

esp_err_t app_video_stream_take_video(void)
{
    is_take_video = true;

    return ESP_OK;
}

esp_err_t app_video_stream_stop_take_video(void)
{
    is_take_video = false;

    return ESP_OK;
}

esp_err_t app_video_stream_set_flash_light(bool is_on)
{
    is_flash_light_on = is_on;

    return ESP_OK;
}

photo_resolution_t app_video_stream_get_photo_resolution(void)
{
    return current_photo_resolution;
}

static esp_err_t app_video_stream_set_photo_resolution(photo_resolution_t resolution)
{
    if (resolution >= PHOTO_RESOLUTION_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    current_photo_resolution = resolution;
    ESP_LOGI(TAG, "Photo resolution set to %dx%d", 
             photo_resolution_width[current_photo_resolution],
             photo_resolution_height[current_photo_resolution]);
    
    return ESP_OK;
}

esp_err_t app_video_stream_set_photo_resolution_by_string(const char *resolution_str)
{
    if (strcmp(resolution_str, "480P") == 0 || strcmp(resolution_str, "480p") == 0) {
        return app_video_stream_set_photo_resolution(PHOTO_RESOLUTION_480P);
    } else if (strcmp(resolution_str, "720P") == 0 || strcmp(resolution_str, "720p") == 0) {
        return app_video_stream_set_photo_resolution(PHOTO_RESOLUTION_720P);
    } else if (strcmp(resolution_str, "1080P") == 0 || strcmp(resolution_str, "1080p") == 0) {
        return app_video_stream_set_photo_resolution(PHOTO_RESOLUTION_1080P);
    } else {
        ESP_LOGW(TAG, "Unknown resolution string: %s, using default 720P", resolution_str);
        return app_video_stream_set_photo_resolution(PHOTO_RESOLUTION_720P);
    }
}

esp_err_t app_video_stream_init(i2c_master_bus_handle_t i2c_handle)
{
    // Initialize the PPA
    ppa_client_config_t ppa_srm_config = {
        .oper_type = PPA_OPERATION_SRM,
    };
    ESP_ERROR_CHECK(ppa_register_client(&ppa_srm_config, &ppa_srm_handle));
    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &data_cache_line_size));

    // Initialize the video camera
    esp_err_t ret = app_video_main(i2c_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "video main init failed with error 0x%x", ret);
        return ESP_FAIL;
    }

    // Open the video device
    int video_cam_fd0 = app_video_open(EXAMPLE_CAM_DEV_PATH, APP_VIDEO_FMT);
    if (video_cam_fd0 < 0) {
        ESP_LOGE(TAG, "video cam open failed");
        return ESP_FAIL;
    }

    // Allocate the canvas buffer
    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &data_cache_line_size));
    for (int i = 0; i < EXAMPLE_CAM_BUF_NUM; i++) {
        canvas_buf[i] = heap_caps_aligned_calloc(data_cache_line_size, 1, BSP_LCD_H_RES * BSP_LCD_V_RES * 2, MALLOC_CAP_SPIRAM);
        if (canvas_buf[i] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate canvas buffer");
            return ESP_FAIL;
        }
    }

    // Initialize the JPEG encoder
    jpeg_encode_engine_cfg_t encode_eng_cfg = {
        .timeout_ms = 70,
    };

    ESP_ERROR_CHECK(jpeg_new_encoder_engine(&encode_eng_cfg, &jpeg_handle));

    // Initialize video capture device
    ESP_ERROR_CHECK(app_video_set_bufs(video_cam_fd0, EXAMPLE_CAM_BUF_NUM, NULL));

    // Register the video frame operation callback
    ESP_ERROR_CHECK(app_video_register_frame_operation_cb(camera_video_frame_operation));

    photo_queue = xQueueCreate(2, sizeof(photo_task_params_t));
    if (photo_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create photo queue");
        return ESP_FAIL;
    }
    
    BaseType_t task_created = xTaskCreate(
        photo_task,
        "photo_task",
        4096,
        NULL,
        5,
        &photo_task_handle);
    
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create photo task");
        return ESP_FAIL;
    }

    photo_take_sem = xSemaphoreCreateBinary();
    if (photo_take_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create photo take semaphore");
        return ESP_FAIL;
    }

    uint16_t saved_count = 0;
    if (app_storage_get_photo_count(&saved_count) == ESP_OK) {
        app_extra_set_saved_photo_count(saved_count);
    }

    // Start the camera stream task
    ESP_ERROR_CHECK(app_video_stream_task_start(video_cam_fd0, 0));

    return ret;
}

static esp_err_t take_and_save_photo(uint8_t *camera_buf, uint32_t width, uint32_t height)
{
    esp_err_t ret = ESP_OK;
    
    bsp_display_backlight_off();

    is_flash_light_on ? bsp_led_set(BSP_LED_WHITE, true) : bsp_led_set(BSP_LED_WHITE, false);

    uint32_t photo_width = photo_resolution_width[current_photo_resolution];
    uint32_t photo_height = photo_resolution_height[current_photo_resolution];

    uint8_t *pic_buf = NULL;
    
    if (photo_width > width) {
        photo_width = width;
    }
    if (photo_height > height) {
        photo_height = height;
    }

    if(current_photo_resolution != PHOTO_RESOLUTION_1080P) {
        photo_buf = (uint8_t*)heap_caps_aligned_calloc(data_cache_line_size, 1, photo_width * photo_height * 2, MALLOC_CAP_SPIRAM);
        if (photo_buf == NULL) {
            ESP_LOGE(TAG, "Failed to allocate photo buffer");
            return ESP_FAIL;
        }

        ppa_srm_oper_config_t srm_config = {
            .in.buffer = camera_buf,
            .in.pic_w = width,
            .in.pic_h = height,
            .in.block_w = photo_width,
            .in.block_h = photo_height,
            .in.block_offset_x = (width - photo_width) / 2,
            .in.block_offset_y = (height - photo_height) / 2,
            .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            .out.buffer = photo_buf,
            .out.buffer_size = ALIGN_UP(photo_width * photo_height * 2, data_cache_line_size),
            .out.pic_w = photo_width,
            .out.pic_h = photo_height,
            .out.block_offset_x = 0,
            .out.block_offset_y = 0,
            .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
            .scale_x = 1,
            .scale_y = 1,
            .rgb_swap = 0,
            .byte_swap = 0,
            .mode = PPA_TRANS_MODE_BLOCKING,
        };

        ESP_ERROR_CHECK(ppa_do_scale_rotate_mirror(ppa_srm_handle, &srm_config));

        pic_buf = photo_buf;
    } else {
        pic_buf = camera_buf;
    }

    jpeg_encode_cfg_t enc_config = {
        .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
        .sub_sample = JPEG_DOWN_SAMPLING_YUV420,
        .image_quality = 90,
        .width = photo_width,
        .height = photo_height,
    };

    jpeg_encode_memory_alloc_cfg_t rx_mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };

    jpg_buf = (uint8_t*)jpeg_alloc_encoder_mem(photo_width * photo_height * 2 / 10, &rx_mem_cfg, &rx_buffer_size); // Assume that compression ratio of 10 to 1
    assert(jpg_buf != NULL);

    ret = jpeg_encoder_process(jpeg_handle, &enc_config, pic_buf, photo_width * photo_height * 2, 
                              jpg_buf, rx_buffer_size, &jpg_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "JPEG encoding failed: 0x%x", ret);
        bsp_display_backlight_on();
        return ret;
    }
    
    if(pic_buf != camera_buf) {
        heap_caps_free(pic_buf);
        pic_buf = NULL;
    }

    ret = app_storage_save_picture(jpg_buf, jpg_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save picture: 0x%x", ret);
    } else {
        ESP_LOGI(TAG, "Picture saved successfully");
    }

    if(jpg_buf != NULL) {
        heap_caps_free(jpg_buf);
        jpg_buf = NULL;
    }

    xSemaphoreGive(photo_take_sem);

    bsp_led_set(BSP_LED_WHITE, false);
    
    bsp_display_backlight_on();

    if (is_interval_photo_active) {
        app_extra_set_saved_photo_count(app_extra_get_saved_photo_count() + 1);
        interval_photo_complete_callback();
    }

    return ret;
}

static void photo_task(void *pvParameters)
{
    photo_task_params_t params;
    uint8_t *photo_buffer = NULL;
    size_t buffer_size = app_video_get_buf_size();
    
    // malloc the photo buffer
    photo_buffer = heap_caps_aligned_calloc(data_cache_line_size, 1, buffer_size, MALLOC_CAP_SPIRAM);
    if (photo_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate photo buffer");
        vTaskDelete(NULL);
        return;
    }
    
    while (1) {
        if (xQueueReceive(photo_queue, &params, portMAX_DELAY) == pdTRUE) {
            // copy the image data to the local buffer
            memcpy(photo_buffer, params.camera_buf, buffer_size);

            // handle the photo and save it
            esp_err_t photo_ret = take_and_save_photo(photo_buffer, params.width, params.height);
            if (photo_ret == ESP_OK) {
                ESP_LOGI(TAG, "take and save photo success");
            }
        }
    }
    
    // free the photo buffer
    heap_caps_free(photo_buffer);
    vTaskDelete(NULL);
}

static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, uint32_t camera_buf_hes, uint32_t camera_buf_ves, size_t camera_buf_len)
{
    int scale_level = app_extra_get_magnification_factor();
    int res_width = scale_level_res[scale_level - 1];
    int res_height = scale_level_res[scale_level - 1];

    if(!is_camera_initialized) {
        camera_init_count++;
        if(camera_init_count >= 20) {
            is_camera_initialized = true;
            camera_init_count = 0;
        }
    }

    ppa_srm_oper_config_t srm_config = {
        .in.buffer = camera_buf,
        .in.pic_w = camera_buf_hes,
        .in.pic_h = camera_buf_ves,
        .in.block_w = res_width,
        .in.block_h = res_height,
        .in.block_offset_x = (camera_buf_hes - res_width) / 2,
        .in.block_offset_y = (camera_buf_ves - res_height) / 2,
        .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .out.buffer = canvas_buf[camera_buf_index],
        .out.buffer_size = ALIGN_UP(BSP_LCD_H_RES * BSP_LCD_V_RES * 2, data_cache_line_size),
        .out.pic_w = BSP_LCD_H_RES,
        .out.pic_h = BSP_LCD_V_RES,
        .out.block_offset_x = 0,
        .out.block_offset_y = 0,
        .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = (float)BSP_LCD_H_RES / res_width,
        .scale_y = (float)BSP_LCD_V_RES / res_height,
        .rgb_swap = 0,
        .byte_swap = 0,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    ESP_ERROR_CHECK(ppa_do_scale_rotate_mirror(ppa_srm_handle, &srm_config));

    swap_rgb565_bytes(canvas_buf[camera_buf_index], BSP_LCD_H_RES * BSP_LCD_V_RES);

    bsp_display_lock(0);
    lv_canvas_set_buffer(ui_PanelCanvas, canvas_buf[camera_buf_index], BSP_LCD_H_RES, BSP_LCD_V_RES, LV_IMG_CF_TRUE_COLOR);
    lv_refr_now(NULL);
    bsp_display_unlock();

    if(is_take_photo && (ui_extra_get_current_page() == UI_PAGE_CAMERA || ui_extra_get_current_page() == UI_PAGE_INTERVAL_CAM) && is_camera_initialized) {
        // reset the photo flag
        is_take_photo = false;

        photo_task_params_t params = {
            .camera_buf = camera_buf,
            .width = camera_buf_hes,
            .height = camera_buf_ves
        };
        
        // send the photo task params to the photo task
        if (xQueueSend(photo_queue, &params, 0) != pdTRUE) {
            ESP_LOGW(TAG, "photo queue is full, skip this photo");
        }

        xSemaphoreTake(photo_take_sem, portMAX_DELAY);
    }
}