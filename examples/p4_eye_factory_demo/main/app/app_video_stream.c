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

// Define constants to replace magic numbers
#define JPEG_COMPRESSION_RATIO 5  // Assuming 10:1 compression ratio
#define CAMERA_INIT_FRAMES 20     // Number of frames needed for camera initialization
#define JPEG_QUALITY 90           // JPEG quality setting

// Define camera state structure to integrate global variables
typedef struct {
    bool is_initialized;
    bool is_take_photo;
    bool is_take_video;
    bool is_interval_photo_active;
    bool is_flash_light_on;
    uint32_t init_count;
    uint16_t current_interval_minutes;
    uint32_t next_wake_time;
    photo_resolution_t current_resolution;
} camera_state_t;

// Define buffer management structure
typedef struct {
    void *canvas_buf[EXAMPLE_CAM_BUF_NUM];
    uint8_t *photo_buf;
    uint8_t *jpg_buf;
    uint32_t jpg_size;
    size_t rx_buffer_size;
} camera_buffer_t;

typedef struct {
    uint8_t *camera_buf;
    uint32_t width;
    uint32_t height;
} photo_task_params_t;

static const char *TAG = "app_video_stream";

// Replace scattered global variables
static size_t data_cache_line_size = 0;
static ppa_client_handle_t ppa_srm_handle = NULL;
static jpeg_encoder_handle_t jpeg_handle;

// Use structures to integrate related variables
static camera_state_t camera_state = {
    .is_initialized = false,
    .is_take_photo = false,
    .is_take_video = false,
    .is_interval_photo_active = false,
    .is_flash_light_on = false,
    .init_count = 0,
    .current_interval_minutes = 0,
    .next_wake_time = 0,
    .current_resolution = PHOTO_RESOLUTION_1080P
};

static camera_buffer_t camera_buffer = {0};

static TaskHandle_t photo_task_handle = NULL;
static QueueHandle_t photo_queue = NULL;
static SemaphoreHandle_t photo_take_sem = NULL;

static const uint32_t photo_resolution_width[PHOTO_RESOLUTION_MAX] = {640, 1280, 1920};
static const uint32_t photo_resolution_height[PHOTO_RESOLUTION_MAX] = {480, 720, 1080};
static int scale_level_res[SCALE_LEVELS] = {960, 480, 240, 120, 80, 60};

static void photo_task(void *pvParameters);
static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, uint32_t camera_buf_hes, uint32_t camera_buf_ves, size_t camera_buf_len);

// Optimize deep sleep function
static void enter_deep_sleep(uint16_t sleep_minutes)
{
    // Calculate next wake-up time
    camera_state.next_wake_time = esp_timer_get_time() / 1000000 + sleep_minutes * 60;
    
    // Save interval photo state
    app_storage_save_interval_state(camera_state.is_interval_photo_active, camera_state.next_wake_time);

    // Initialize sleep IO
    bsp_sleep_io_init();

    // Set wake-up time (microseconds)
#if DEBUG_MODE
    uint64_t sleep_time_us = sleep_minutes * 1000000ULL;
#else
    uint64_t sleep_time_us = sleep_minutes * 60 * 1000000ULL;
#endif
    
    ESP_LOGI(TAG, "Entering deep sleep for %d minutes", sleep_minutes);
    
    // Configure RTC wake-up timer
    esp_sleep_enable_timer_wakeup(sleep_time_us);
    
    // Enter deep sleep
    esp_deep_sleep_start();
}

// Optimize interval photo completion callback
static void interval_photo_complete_callback(void)
{
    ESP_LOGI(TAG, "Interval photo completed, saved photo count: %d", app_extra_get_saved_photo_count());
    
    // If interval photo is still active, enter deep sleep
    if (camera_state.is_interval_photo_active) {
        // Enter deep sleep until next photo time
        enter_deep_sleep(camera_state.current_interval_minutes);
    }
}

// Optimize start interval photo function
esp_err_t app_video_stream_start_interval_photo(uint16_t interval_minutes)
{
    camera_state.current_interval_minutes = interval_minutes;
    camera_state.is_interval_photo_active = true;
    
    // Save interval photo state
    camera_state.next_wake_time = esp_timer_get_time() / 1000000 + interval_minutes * 60;
    app_storage_save_interval_state(true, camera_state.next_wake_time);
    
    // Take a photo immediately
    app_video_stream_take_photo();
    ESP_LOGI(TAG, "Interval photo started with interval %d minutes", interval_minutes);
    
    return ESP_OK;
}

// Optimize stop interval photo function
esp_err_t app_video_stream_stop_interval_photo(void)
{
    camera_state.is_interval_photo_active = false;
    
    // Save interval photo state (closed)
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

// Optimize photo taking functions
esp_err_t app_video_stream_take_photo(void)
{
    camera_state.is_take_photo = true;
    return ESP_OK;
}

esp_err_t app_video_stream_stop_take_photo(void)
{
    camera_state.is_take_photo = false;
    return ESP_OK;
}

esp_err_t app_video_stream_take_video(void)
{
    camera_state.is_take_video = true;
    return ESP_OK;
}

esp_err_t app_video_stream_stop_take_video(void)
{
    camera_state.is_take_video = false;
    return ESP_OK;
}

esp_err_t app_video_stream_set_flash_light(bool is_on)
{
    camera_state.is_flash_light_on = is_on;
    return ESP_OK;
}

photo_resolution_t app_video_stream_get_photo_resolution(void)
{
    return camera_state.current_resolution;
}

// Optimize set photo resolution function
static esp_err_t app_video_stream_set_photo_resolution(photo_resolution_t resolution)
{
    if (resolution >= PHOTO_RESOLUTION_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    camera_state.current_resolution = resolution;
    ESP_LOGI(TAG, "Photo resolution set to %dx%d", 
             photo_resolution_width[camera_state.current_resolution],
             photo_resolution_height[camera_state.current_resolution]);
    
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

// Optimize initialization function, add resource cleanup
esp_err_t app_video_stream_init(i2c_master_bus_handle_t i2c_handle)
{
    esp_err_t ret = ESP_OK;
    int video_cam_fd0 = -1;
    bool resources_initialized = false;

    // Initialize PPA
    ppa_client_config_t ppa_srm_config = {
        .oper_type = PPA_OPERATION_SRM,
    };
    
    ret = ppa_register_client(&ppa_srm_config, &ppa_srm_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register PPA client: 0x%x", ret);
        goto cleanup;
    }
    
    ret = esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &data_cache_line_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get cache alignment: 0x%x", ret);
        goto cleanup;
    }

    // Initialize video camera
    ret = app_video_main(i2c_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Video main init failed with error 0x%x", ret);
        goto cleanup;
    }

    // Open video device
    video_cam_fd0 = app_video_open(EXAMPLE_CAM_DEV_PATH, APP_VIDEO_FMT);
    if (video_cam_fd0 < 0) {
        ESP_LOGE(TAG, "Video cam open failed");
        ret = ESP_FAIL;
        goto cleanup;
    }

    // Allocate canvas buffers
    for (int i = 0; i < EXAMPLE_CAM_BUF_NUM; i++) {
        camera_buffer.canvas_buf[i] = heap_caps_aligned_calloc(data_cache_line_size, 1, 
                                                             BSP_LCD_H_RES * BSP_LCD_V_RES * 2, 
                                                             MALLOC_CAP_SPIRAM);
        if (camera_buffer.canvas_buf[i] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate canvas buffer %d", i);
            ret = ESP_FAIL;
            goto cleanup;
        }
    }

    // Initialize JPEG encoder
    jpeg_encode_engine_cfg_t encode_eng_cfg = {
        .timeout_ms = 70,
    };

    ret = jpeg_new_encoder_engine(&encode_eng_cfg, &jpeg_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create JPEG encoder: 0x%x", ret);
        goto cleanup;
    }

    // Initialize video capture device
    ret = app_video_set_bufs(video_cam_fd0, EXAMPLE_CAM_BUF_NUM, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set video buffers: 0x%x", ret);
        goto cleanup;
    }

    // Register video frame operation callback
    ret = app_video_register_frame_operation_cb(camera_video_frame_operation);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register frame operation callback: 0x%x", ret);
        goto cleanup;
    }

    photo_queue = xQueueCreate(2, sizeof(photo_task_params_t));
    if (photo_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create photo queue");
        ret = ESP_FAIL;
        goto cleanup;
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
        ret = ESP_FAIL;
        goto cleanup;
    }

    photo_take_sem = xSemaphoreCreateBinary();
    if (photo_take_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create photo take semaphore");
        ret = ESP_FAIL;
        goto cleanup;
    }

    uint16_t saved_count = 0;
    if (app_storage_get_photo_count(&saved_count) == ESP_OK) {
        app_extra_set_saved_photo_count(saved_count);
    }

    // Start camera stream task
    ret = app_video_stream_task_start(video_cam_fd0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start video stream task: 0x%x", ret);
        goto cleanup;
    }

    resources_initialized = true;

cleanup:
    if (!resources_initialized) {
        // Clean up resources
        if (photo_take_sem != NULL) {
            vSemaphoreDelete(photo_take_sem);
            photo_take_sem = NULL;
        }
        
        if (photo_task_handle != NULL) {
            vTaskDelete(photo_task_handle);
            photo_task_handle = NULL;
        }
        
        if (photo_queue != NULL) {
            vQueueDelete(photo_queue);
            photo_queue = NULL;
        }
        
        if (jpeg_handle != NULL) {
            jpeg_del_encoder_engine(jpeg_handle);
            jpeg_handle = NULL;
        }
        
        for (int i = 0; i < EXAMPLE_CAM_BUF_NUM; i++) {
            if (camera_buffer.canvas_buf[i] != NULL) {
                heap_caps_free(camera_buffer.canvas_buf[i]);
                camera_buffer.canvas_buf[i] = NULL;
            }
        }
        
        if (video_cam_fd0 >= 0) {
            app_video_close(video_cam_fd0);
        }
        
        if (ppa_srm_handle != NULL) {
            ppa_unregister_client(ppa_srm_handle);
            ppa_srm_handle = NULL;
        }
    }

    return ret;
}

// Optimize take and save photo function, improve error handling and resource management
static esp_err_t take_and_save_photo(uint8_t *camera_buf, uint32_t width, uint32_t height)
{
    esp_err_t ret = ESP_OK;
    uint8_t *pic_buf = NULL;
    
    bsp_display_backlight_off();

    camera_state.is_flash_light_on ? bsp_led_set(BSP_LED_WHITE, true) : bsp_led_set(BSP_LED_WHITE, false);

    uint32_t photo_width = photo_resolution_width[camera_state.current_resolution];
    uint32_t photo_height = photo_resolution_height[camera_state.current_resolution];

    // Adjust resolution to match camera capabilities
    if (photo_width > width) {
        ESP_LOGW(TAG, "Requested width %d exceeds camera capability %d, adjusting", photo_width, width);
        photo_width = width;
    }
    if (photo_height > height) {
        ESP_LOGW(TAG, "Requested height %d exceeds camera capability %d, adjusting", photo_height, height);
        photo_height = height;
    }

    // Process image based on resolution
    if(camera_state.current_resolution != PHOTO_RESOLUTION_1080P) {
        camera_buffer.photo_buf = (uint8_t*)heap_caps_aligned_calloc(data_cache_line_size, 1, 
                                                                   photo_width * photo_height * 2, 
                                                                   MALLOC_CAP_SPIRAM);
        if (camera_buffer.photo_buf == NULL) {
            ESP_LOGE(TAG, "Failed to allocate photo buffer");
            ret = ESP_FAIL;
            goto cleanup;
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
            .out.buffer = camera_buffer.photo_buf,
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

        ret = ppa_do_scale_rotate_mirror(ppa_srm_handle, &srm_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to scale image: 0x%x", ret);
            goto cleanup;
        }

        pic_buf = camera_buffer.photo_buf;
    } else {
        pic_buf = camera_buf;
    }

    // Configure JPEG encoding
    jpeg_encode_cfg_t enc_config = {
        .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
        .sub_sample = JPEG_DOWN_SAMPLING_YUV420,
        .image_quality = JPEG_QUALITY,
        .width = photo_width,
        .height = photo_height,
    };

    jpeg_encode_memory_alloc_cfg_t rx_mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };

    // Allocate JPEG buffer, assuming compression ratio of JPEG_COMPRESSION_RATIO
    camera_buffer.jpg_buf = (uint8_t*)jpeg_alloc_encoder_mem(
        photo_width * photo_height * 2 / JPEG_COMPRESSION_RATIO, 
        &rx_mem_cfg, 
        &camera_buffer.rx_buffer_size
    );
    
    if (camera_buffer.jpg_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate JPEG buffer");
        ret = ESP_FAIL;
        goto cleanup;
    }

    // Perform JPEG encoding
    ret = jpeg_encoder_process(jpeg_handle, &enc_config, pic_buf, photo_width * photo_height * 2, 
                              camera_buffer.jpg_buf, camera_buffer.rx_buffer_size, &camera_buffer.jpg_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "JPEG encoding failed: 0x%x", ret);
        goto cleanup;
    }
    
    // Save the picture
    ret = app_storage_save_picture(camera_buffer.jpg_buf, camera_buffer.jpg_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save picture: 0x%x", ret);
    } else {
        ESP_LOGI(TAG, "Picture saved successfully");
    }

cleanup:
    // Free resources
    if (camera_buffer.photo_buf != NULL && pic_buf == camera_buffer.photo_buf) {
        heap_caps_free(camera_buffer.photo_buf);
        camera_buffer.photo_buf = NULL;
    }

    if (camera_buffer.jpg_buf != NULL) {
        heap_caps_free(camera_buffer.jpg_buf);
        camera_buffer.jpg_buf = NULL;
    }

    xSemaphoreGive(photo_take_sem);

    bsp_led_set(BSP_LED_WHITE, false);
    bsp_display_backlight_on();

    // Handle interval photo
    if (camera_state.is_interval_photo_active && ret == ESP_OK) {
        app_extra_set_saved_photo_count(app_extra_get_saved_photo_count() + 1);
        interval_photo_complete_callback();
    }

    return ret;
}

// Optimize photo task, improve memory management
static void photo_task(void *pvParameters)
{
    photo_task_params_t params;
    uint8_t *photo_buffer = NULL;
    size_t buffer_size = app_video_get_buf_size();
    
    // Allocate photo buffer
    photo_buffer = heap_caps_aligned_calloc(data_cache_line_size, 1, buffer_size, MALLOC_CAP_SPIRAM);
    if (photo_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate photo buffer");
        vTaskDelete(NULL);
        return;
    }
    
    while (1) {
        if (xQueueReceive(photo_queue, &params, portMAX_DELAY) == pdTRUE) {
            // Copy image data to local buffer
            memcpy(photo_buffer, params.camera_buf, buffer_size);

            // Process and save the photo
            esp_err_t photo_ret = take_and_save_photo(photo_buffer, params.width, params.height);
            if (photo_ret == ESP_OK) {
                ESP_LOGI(TAG, "Take and save photo success");
            } else {
                ESP_LOGE(TAG, "Take and save photo failed: 0x%x", photo_ret);
            }
        }
    }
    
    // Free photo buffer (this will never execute, but included for code completeness)
    heap_caps_free(photo_buffer);
    vTaskDelete(NULL);
}

// Optimize camera video frame operation function
static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, uint32_t camera_buf_hes, uint32_t camera_buf_ves, size_t camera_buf_len)
{
    int scale_level = app_extra_get_magnification_factor();
    int res_width = scale_level_res[scale_level - 1];
    int res_height = scale_level_res[scale_level - 1];

    // Camera initialization check
    if(!camera_state.is_initialized) {
        camera_state.init_count++;
        if(camera_state.init_count >= CAMERA_INIT_FRAMES) {
            camera_state.is_initialized = true;
            camera_state.init_count = 0;
            ESP_LOGI(TAG, "Camera initialized after %d frames", CAMERA_INIT_FRAMES);
        }
    }

    // Configure scale-rotate-mirror operation
    ppa_srm_oper_config_t srm_config = {
        .in.buffer = camera_buf,
        .in.pic_w = camera_buf_hes,
        .in.pic_h = camera_buf_ves,
        .in.block_w = res_width,
        .in.block_h = res_height,
        .in.block_offset_x = (camera_buf_hes - res_width) / 2,
        .in.block_offset_y = (camera_buf_ves - res_height) / 2,
        .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .out.buffer = camera_buffer.canvas_buf[camera_buf_index],
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

    // Execute scale-rotate-mirror operation
    esp_err_t ret = ppa_do_scale_rotate_mirror(ppa_srm_handle, &srm_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to process frame: 0x%x", ret);
        return;
    }

    // Swap RGB565 bytes
    swap_rgb565_bytes(camera_buffer.canvas_buf[camera_buf_index], BSP_LCD_H_RES * BSP_LCD_V_RES);

    // Update display
    bsp_display_lock(0);
    lv_canvas_set_buffer(ui_PanelCanvas, camera_buffer.canvas_buf[camera_buf_index], BSP_LCD_H_RES, BSP_LCD_V_RES, LV_IMG_CF_TRUE_COLOR);
    lv_refr_now(NULL);
    bsp_display_unlock();

    // Handle photo request
    if(camera_state.is_take_photo && 
       (ui_extra_get_current_page() == UI_PAGE_CAMERA || ui_extra_get_current_page() == UI_PAGE_INTERVAL_CAM) && 
       camera_state.is_initialized) {
        // Reset photo flag
        camera_state.is_take_photo = false;

        photo_task_params_t params = {
            .camera_buf = camera_buf,
            .width = camera_buf_hes,
            .height = camera_buf_ves
        };
        
        // Send photo task parameters to the photo task
        if (xQueueSend(photo_queue, &params, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Photo queue is full, skip this photo");
        } else {
            xSemaphoreTake(photo_take_sem, portMAX_DELAY);
        }
    }
}

esp_err_t app_video_stream_check_interval_wakeup(void)
{
    bool is_active = false;
    uint32_t next_time = 0;
    
    // Get the saved interval photo state
    esp_err_t ret = app_storage_get_interval_state(&is_active, &next_time);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // If interval photo is active, start it
    if (is_active) {
        uint32_t current_time = esp_timer_get_time() / 1000000;
        uint32_t elapsed_time = current_time - next_time;
        
        // Calculate the next interval time
        uint16_t interval_minutes = 0;
        if (elapsed_time < 60) {
            // If less than a minute has passed, use the default interval
            interval_minutes = 1;
        } else {
            // Calculate the interval in minutes
            interval_minutes = elapsed_time / 60;
        }
        
        // Start the interval photo
        app_video_stream_start_interval_photo(interval_minutes);
        
        return ESP_OK;
    }
    
    return ESP_OK;
}