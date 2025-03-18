/**
 * @file app_video_stream.c
 * @brief Video streaming application implementation
 *
 * This module provides functionality for initializing and managing video streaming
 * from a camera to a display. It handles camera initialization, buffer management,
 * and frame processing with scaling capabilities.
 */

#include <sys/stat.h> 
#include <dirent.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_private/esp_cache_private.h"
#include "driver/ppa.h"
#include "driver/jpeg_encode.h"
#include "bsp/esp-bsp.h"

#include "esp_audio_enc_default.h"
#include "esp_audio_enc_reg.h"
#include "esp_audio_enc.h"
#include "esp_muxer.h"
#include "mp4_muxer.h"

#include "ui_extra.h"
#include "app_video.h"
#include "app_storage.h"
#include "app_video_stream.h"
#include "app_album.h"

/* Constants */
#define ALIGN_UP(num, align)    (((num) + ((align) - 1)) & ~((align) - 1))
#define SCALE_LEVELS            5                         // Resolution scale levels
#define DEBUG_MODE              1
#define CROP_PHOTO_WIDTH        1280
#define CROP_PHOTO_HEIGHT       960

#define JPEG_COMPRESSION_RATIO  5             // Assuming 5:1 compression ratio
#define CAMERA_INIT_FRAMES      50            // Number of frames needed for camera initialization
#define JPEG_PHOTO_QUALITY      90            // JPEG quality setting
#define JPEG_VIDEO_QUALITY      65            // JPEG quality setting

#define REC_AUDIO_SAMPLE_RATE     16000
#define REC_AUDIO_CHANNEL         2
#define REC_AUDIO_BITS_PER_SAMPLE 16

#define FILE_SLICE_DURATION       600000
#define VIDEO_BUF_MULTIPLIER      10
#define VIDEO_FRAME_RATE          30

/* Type definitions */
/**
 * @brief Camera state structure
 */
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

/**
 * @brief Camera buffer management structure
 */
typedef struct {
    void *canvas_buf[EXAMPLE_CAM_BUF_NUM];
    uint8_t *photo_buf;
    uint8_t *jpg_buf;
    uint32_t jpg_size;
    size_t rx_buffer_size;
    uint8_t *scaled_camera_buf;
} camera_buffer_t;

/**
 * @brief Audio data structure
 */
typedef struct {
    uint8_t *data;
    size_t len;
} audio_data_t;

/**
 * @brief Recorder context structure
 */
typedef struct {
    esp_muxer_handle_t muxer;           // MP4 muxer handle
    int video_stream_idx;               // Video stream index
    int audio_stream_idx;               // Audio stream index
    bool recording;                     // Recording status flag
    SemaphoreHandle_t recording_mutex;  // Recording mutex
    esp_audio_enc_handle_t encoder;     // Audio encoder handle
    uint32_t start_time;                // Recording start time
    TaskHandle_t audio_capture_task_handle; // Audio capture task handle
    uint32_t video_frame_count;          // Video frame count
} recorder_ctx_t;

/* Static variables */
static const char *TAG = "app_video_stream";

static size_t data_cache_line_size = 0;
static ppa_client_handle_t ppa_srm_handle = NULL;
static jpeg_encoder_handle_t jpeg_handle;

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

// Global recorder context
static recorder_ctx_t recorder_ctx = {
    .muxer = NULL,
    .video_stream_idx = -1,
    .audio_stream_idx = -1,
    .recording = false,
    .recording_mutex = NULL,
    .encoder = NULL,
    .start_time = 0,
    .video_frame_count = 0
};

static const uint32_t photo_resolution_width[PHOTO_RESOLUTION_MAX] = {640, 1280, 1920};
static const uint32_t photo_resolution_height[PHOTO_RESOLUTION_MAX] = {480, 720, 1080};

static int scale_level_res[SCALE_LEVELS] = {960, 480, 240, 120, 80};
static const uint32_t adj_resolution_width[SCALE_LEVELS] = {1920, 1200, 960, 480, 240};
static const uint32_t adj_resolution_height[SCALE_LEVELS] = {1080, 675, 540, 270, 135};

/* Forward declarations */
static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, 
                                        uint32_t camera_buf_hes, uint32_t camera_buf_ves, 
                                        size_t camera_buf_len);
static esp_err_t take_and_save_photo(uint8_t *camera_buf, uint32_t width, uint32_t height);
static esp_err_t take_and_save_video(uint8_t *camera_buf, uint32_t width, uint32_t height);
static esp_err_t app_video_stream_set_photo_resolution(photo_resolution_t resolution);
static void interval_photo_complete_callback(void);
static void enter_deep_sleep(uint16_t sleep_minutes);
static esp_err_t app_video_stream_start_recording(void);
static esp_err_t app_video_stream_stop_recording(void);
static esp_err_t init_mp4_muxer(void);
static esp_err_t deinit_mp4_muxer(void);
static int get_next_file_number(const char *dir_path);
static int file_pattern_cb(char *file_name, int len, int slice_idx);

/* Utility functions */
/**
 * @brief Swap RGB565 bytes for correct display format
 * 
 * @param buffer RGB565 buffer to process
 * @param pixel_count Number of pixels in the buffer
 */
void swap_rgb565_bytes(uint16_t *buffer, int pixel_count)
{
    for (int i = 0; i < pixel_count; i++) {
        uint16_t swap16 = *(buffer + i);
        swap16 = (swap16 >> 8) | (swap16 << 8);
        *(buffer + i) = swap16;
    }
}

/**
 * @brief Enter deep sleep mode for interval photography
 * 
 * @param sleep_minutes Minutes to sleep before waking up
 */
static void enter_deep_sleep(uint16_t sleep_minutes)
{
    // Set wake-up time (microseconds)
#if DEBUG_MODE
    uint64_t sleep_time_us = sleep_minutes * 1000000ULL;
#else
    uint64_t sleep_time_us = sleep_minutes * 60 * 1000000ULL;
#endif
    
    ESP_LOGI(TAG, "Entering deep sleep for %d minutes", sleep_minutes);
    
    // Configure RTC wake-up timer
    esp_sleep_enable_timer_wakeup(sleep_time_us);
    
    // Initialize sleep IO
    bsp_sleep_io_init();

    // Enter deep sleep
    esp_deep_sleep_start();
}

/* Photo resolution management */
/**
 * @brief Set photo resolution
 * 
 * @param resolution Resolution enum value
 * @return ESP_OK on success, error code otherwise
 */
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

/**
 * @brief Set photo resolution by string
 * 
 * @param resolution_str Resolution string ("480P", "720P", "1080P")
 * @return ESP_OK on success, error code otherwise
 */
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

/**
 * @brief Get current photo resolution
 * 
 * @return Current photo resolution enum value
 */
photo_resolution_t app_video_stream_get_photo_resolution(void)
{
    return camera_state.current_resolution;
}

/* Photo and video control functions */
/**
 * @brief Set flash light state
 * 
 * @param is_on Whether to turn flash on or off
 * @return ESP_OK on success
 */
esp_err_t app_video_stream_set_flash_light(bool is_on)
{
    camera_state.is_flash_light_on = is_on;
    return ESP_OK;
}

/**
 * @brief Trigger photo capture
 * 
 * @return ESP_OK on success
 */
esp_err_t app_video_stream_take_photo(void)
{
    camera_state.is_take_photo = true;
    return ESP_OK;
}

/**
 * @brief Stop photo capture
 * 
 * @return ESP_OK on success
 */
esp_err_t app_video_stream_stop_take_photo(void)
{
    camera_state.is_take_photo = false;
    return ESP_OK;
}

/**
 * @brief Start video recording
 * 
 * @return ESP_OK on success
 */
esp_err_t app_video_stream_take_video(void)
{
    camera_state.is_take_video = true;
    app_video_stream_start_recording();
    return ESP_OK;
}

/**
 * @brief Stop video recording
 * 
 * @return ESP_OK on success
 */
esp_err_t app_video_stream_stop_take_video(void)
{
    camera_state.is_take_video = false;
    app_video_stream_stop_recording();
    return ESP_OK;
}

/* Interval photo functions */
/**
 * @brief Callback when interval photo is completed
 */
static void interval_photo_complete_callback(void)
{   
    // If interval photo is still active, enter deep sleep
    if (camera_state.is_interval_photo_active) {
        // Enter deep sleep until next photo time
        enter_deep_sleep(camera_state.current_interval_minutes);
    }
}

/**
 * @brief Start interval photo capture
 * 
 * @param interval_minutes Interval between photos in minutes
 * @return ESP_OK on success
 */
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

/**
 * @brief Stop interval photo capture
 * 
 * @return ESP_OK on success
 */
esp_err_t app_video_stream_stop_interval_photo(void)
{
    camera_state.is_interval_photo_active = false;
    
    // Save interval photo state (closed)
    app_storage_save_interval_state(false, 0);

    app_storage_save_photo_count(app_extra_get_saved_photo_count());
    
    ESP_LOGI(TAG, "Interval photo stopped");
    
    return ESP_OK;
}

/**
 * @brief Process and save a video frame
 * 
 * @param camera_buf Camera buffer containing the image
 * @param width Image width
 * @param height Image height
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t take_and_save_video(uint8_t *camera_buf, uint32_t width, uint32_t height)
{
    esp_err_t ret = ESP_OK;
    uint8_t *pic_buf = NULL;

    uint32_t photo_width = photo_resolution_width[camera_state.current_resolution];
    uint32_t photo_height = photo_resolution_height[camera_state.current_resolution];

    // Check if we can store a new image
    if (!app_video_stream_can_store_new_mp4()) {
        // Show warning to user that storage is full or low
        ESP_LOGE(TAG, "Cannot store more images");
        return false;
    }

    // Adjust resolution to match camera capabilities
    if (photo_width > width) {
        ESP_LOGW(TAG, "Requested width %d exceeds camera capability %d, adjusting", photo_width, width);
        photo_width = width;
    }
    if (photo_height > height) {
        ESP_LOGW(TAG, "Requested height %d exceeds camera capability %d, adjusting", photo_height, height);
        photo_height = height;
    }

    uint16_t magnification_factor = app_extra_get_magnification_factor();
    uint8_t *pre_handle_buf = camera_buf;

    // Apply magnification if needed
    if(magnification_factor > 1) {
        ppa_srm_oper_config_t adj_srm_config = {
            .in.buffer = camera_buf,
            .in.pic_w = width,
            .in.pic_h = height,
            .in.block_w = adj_resolution_width[magnification_factor - 1],
            .in.block_h = adj_resolution_height[magnification_factor - 1],
            .in.block_offset_x = (width - adj_resolution_width[magnification_factor - 1]) / 2,
            .in.block_offset_y = (height - adj_resolution_height[magnification_factor - 1]) / 2,
            .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            .out.buffer = camera_buffer.scaled_camera_buf,
            .out.buffer_size = ALIGN_UP(width * height * 2, data_cache_line_size),
            .out.pic_w = width,
            .out.pic_h = height,
            .out.block_offset_x = 0,
            .out.block_offset_y = 0,
            .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
            .scale_x = (float)width / adj_resolution_width[magnification_factor - 1],
            .scale_y = (float)height / adj_resolution_height[magnification_factor - 1],
            .rgb_swap = 0,
            .byte_swap = 0,
            .mode = PPA_TRANS_MODE_BLOCKING,
        };

        ret = ppa_do_scale_rotate_mirror(ppa_srm_handle, &adj_srm_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to scale image: 0x%x", ret);
            goto cleanup;
        }
        pre_handle_buf = camera_buffer.scaled_camera_buf;
    } else {
        pre_handle_buf = camera_buf;
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
            .in.buffer = pre_handle_buf,
            .in.pic_w = width,
            .in.pic_h = height,
            .in.block_w = CROP_PHOTO_WIDTH,
            .in.block_h = CROP_PHOTO_HEIGHT,
            .in.block_offset_x = (width - CROP_PHOTO_WIDTH) / 2,
            .in.block_offset_y = (height - CROP_PHOTO_HEIGHT) / 2,
            .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            .out.buffer = camera_buffer.photo_buf,
            .out.buffer_size = ALIGN_UP(photo_width * photo_height * 2, data_cache_line_size),
            .out.pic_w = photo_width,
            .out.pic_h = photo_height,
            .out.block_offset_x = 0,
            .out.block_offset_y = 0,
            .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
            .scale_x = (float)photo_width / CROP_PHOTO_WIDTH,
            .scale_y = (float)photo_height / CROP_PHOTO_HEIGHT,
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
        if(magnification_factor > 1) {
            pic_buf = camera_buffer.scaled_camera_buf;
        } else {
            pic_buf = camera_buf;
        }
    }

    // Configure JPEG encoding
    jpeg_encode_cfg_t enc_config = {
        .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
        .sub_sample = JPEG_DOWN_SAMPLING_YUV420,
        .image_quality = JPEG_VIDEO_QUALITY,
        .width = photo_width,
        .height = photo_height,
    };

    // Perform JPEG encoding
    ret = jpeg_encoder_process(jpeg_handle, &enc_config, pic_buf, photo_width * photo_height * 2, 
                              camera_buffer.jpg_buf, camera_buffer.rx_buffer_size, &camera_buffer.jpg_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "JPEG encoding failed: 0x%x", ret);
        goto cleanup;
    }

    uint32_t frame_time = esp_timer_get_time() / 1000 - recorder_ctx.start_time;
    ESP_LOGD(TAG, "frame_time: %d", frame_time);

    xSemaphoreTake(recorder_ctx.recording_mutex, portMAX_DELAY);
    if (recorder_ctx.recording) {
        esp_muxer_video_packet_t video_packet = {
            .data = camera_buffer.jpg_buf,
            .len = camera_buffer.jpg_size,
            .pts = frame_time,
            .dts = frame_time,
            .key_frame = (recorder_ctx.video_frame_count % 30 == 0), 
        };
        
        int ret = esp_muxer_add_video_packet(recorder_ctx.muxer, recorder_ctx.video_stream_idx, &video_packet);
        if (ret != ESP_MUXER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to add video packet, error: %d", ret);
        } else {
            recorder_ctx.video_frame_count++;
        }
    }
    xSemaphoreGive(recorder_ctx.recording_mutex);
    
cleanup:
    // Free resources
    if (camera_buffer.photo_buf != NULL && pic_buf == camera_buffer.photo_buf) {
        heap_caps_free(camera_buffer.photo_buf);
        camera_buffer.photo_buf = NULL;
    }

    return ret;
}

/**
 * @brief Process and save a photo
 * 
 * @param camera_buf Camera buffer containing the image
 * @param width Image width
 * @param height Image height
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t take_and_save_photo(uint8_t *camera_buf, uint32_t width, uint32_t height)
{
    esp_err_t ret = ESP_OK;
    uint8_t *pic_buf = NULL;

    // Check if we can store a new image
    if (!app_album_can_store_new_image()) {
        // Show warning to user that storage is full or low
        ESP_LOGE(TAG, "Cannot store more images");
        return false;
    }

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

    uint16_t magnification_factor = app_extra_get_magnification_factor();
    uint8_t *pre_handle_buf = camera_buf;

    // Apply magnification if needed
    if(magnification_factor > 1) {
        ppa_srm_oper_config_t adj_srm_config = {
            .in.buffer = camera_buf,
            .in.pic_w = width,
            .in.pic_h = height,
            .in.block_w = adj_resolution_width[magnification_factor - 1],
            .in.block_h = adj_resolution_height[magnification_factor - 1],
            .in.block_offset_x = (width - adj_resolution_width[magnification_factor - 1]) / 2,
            .in.block_offset_y = (height - adj_resolution_height[magnification_factor - 1]) / 2,
            .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            .out.buffer = camera_buffer.scaled_camera_buf,
            .out.buffer_size = ALIGN_UP(width * height * 2, data_cache_line_size),
            .out.pic_w = width,
            .out.pic_h = height,
            .out.block_offset_x = 0,
            .out.block_offset_y = 0,
            .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
            .scale_x = (float)width / adj_resolution_width[magnification_factor - 1],
            .scale_y = (float)height / adj_resolution_height[magnification_factor - 1],
            .rgb_swap = 0,
            .byte_swap = 0,
            .mode = PPA_TRANS_MODE_BLOCKING,
        };

        ret = ppa_do_scale_rotate_mirror(ppa_srm_handle, &adj_srm_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to scale image: 0x%x", ret);
            goto cleanup;
        }
        pre_handle_buf = camera_buffer.scaled_camera_buf;
    } else {
        pre_handle_buf = camera_buf;
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
            .in.buffer = pre_handle_buf,
            .in.pic_w = width,
            .in.pic_h = height,
            .in.block_w = CROP_PHOTO_WIDTH,
            .in.block_h = CROP_PHOTO_HEIGHT,
            .in.block_offset_x = (width - CROP_PHOTO_WIDTH) / 2,
            .in.block_offset_y = (height - CROP_PHOTO_HEIGHT) / 2,
            .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            .out.buffer = camera_buffer.photo_buf,
            .out.buffer_size = ALIGN_UP(photo_width * photo_height * 2, data_cache_line_size),
            .out.pic_w = photo_width,
            .out.pic_h = photo_height,
            .out.block_offset_x = 0,
            .out.block_offset_y = 0,
            .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
            .scale_x = (float)photo_width / CROP_PHOTO_WIDTH,
            .scale_y = (float)photo_height / CROP_PHOTO_HEIGHT,
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
        if(magnification_factor > 1) {
            pic_buf = camera_buffer.scaled_camera_buf;
        } else {
            pic_buf = camera_buf;
        }
    }

    // Configure JPEG encoding
    jpeg_encode_cfg_t enc_config = {
        .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
        .sub_sample = JPEG_DOWN_SAMPLING_YUV420,
        .image_quality = JPEG_PHOTO_QUALITY,
        .width = photo_width,
        .height = photo_height,
    };

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

    bsp_led_set(BSP_LED_WHITE, false);
    bsp_display_backlight_on();

    // Handle interval photo
    if (camera_state.is_interval_photo_active && ret == ESP_OK) {
        app_extra_set_saved_photo_count(app_extra_get_saved_photo_count() + 1);
        interval_photo_complete_callback();
    }

    return ret;
}

/**
 * @brief Process camera video frame
 * 
 * @param camera_buf Camera buffer containing the frame
 * @param camera_buf_index Buffer index
 * @param camera_buf_hes Horizontal resolution
 * @param camera_buf_ves Vertical resolution
 * @param camera_buf_len Buffer length
 */
static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, 
                                        uint32_t camera_buf_hes, uint32_t camera_buf_ves, 
                                        size_t camera_buf_len)
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
    if (camera_state.is_initialized) {
        // Handle photo request
        if (camera_state.is_take_photo && 
            (ui_extra_get_current_page() == UI_PAGE_CAMERA || ui_extra_get_current_page() == UI_PAGE_INTERVAL_CAM)) {
            // Reset photo flag and take a photo
            camera_state.is_take_photo = false;
            take_and_save_photo(camera_buf, camera_buf_hes, camera_buf_ves);
        } 
        // Handle video request
        else if (camera_state.is_take_video && ui_extra_get_current_page() == UI_PAGE_VIDEO_MODE) {
            take_and_save_video(camera_buf, camera_buf_hes, camera_buf_ves);
        }
    }
}

/**
 * @brief Get the next available file number by scanning the directory
 * 
 * @param dir_path Directory path to scan
 * @return Next available file number
 */
static int get_next_file_number(const char *dir_path)
{
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s", dir_path);
        return 0;
    }
    
    int max_num = -1;
    struct dirent *entry;
    
    // Scan all files in the directory
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {  // Regular file
            int file_num;
            // Try to parse file number from "mp4_X.mp4" format
            if (sscanf(entry->d_name, "mp4_%d.mp4", &file_num) == 1) {
                if (file_num > max_num) {
                    max_num = file_num;
                }
            }
        }
    }
    
    closedir(dir);
    return max_num + 1;  // Return next available number
}

/**
 * @brief File name pattern callback function for MP4 muxer
 * 
 * @param file_name Buffer to store the generated file name
 * @param len Buffer length
 * @param slice_idx Slice index
 * @return 0 on success, -1 on failure
 */
static int file_pattern_cb(char *file_name, int len, int slice_idx)
{
    // Ensure directory exists
    char dir_path[64];
    snprintf(dir_path, sizeof(dir_path), "%s/esp32_p4_mp4_save", BSP_SD_MOUNT_POINT);
    
    struct stat st;
    if (stat(dir_path, &st) != 0) {
        // Directory doesn't exist, create it
        if (mkdir(dir_path, 0755) != 0) {
            ESP_LOGE(TAG, "Failed to create directory: %s", dir_path);
            return -1;
        }
        ESP_LOGI(TAG, "Created directory: %s", dir_path);
    }
    
    // Get the next available file number
    int file_num = get_next_file_number(dir_path);
    
    // Generate filename with new format
    snprintf(file_name, len, "%s/esp32_p4_mp4_save/mp4_%d.mp4", 
             BSP_SD_MOUNT_POINT, file_num);
    
    ESP_LOGI(TAG, "Creating file: %s", file_name);
    return 0;
}

/**
 * @brief Initialize MP4 muxer
 * 
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t init_mp4_muxer(void)
{
    ESP_LOGI(TAG, "Initializing MP4 muxer");
    
    // Register MP4 muxer
    mp4_muxer_register();
    
    // Configure MP4 muxer
    mp4_muxer_config_t mp4_cfg = {0};
    esp_muxer_config_t *base_cfg = &mp4_cfg.base_config;
    base_cfg->muxer_type = ESP_MUXER_TYPE_MP4;
    base_cfg->url_pattern = file_pattern_cb;
    base_cfg->slice_duration = FILE_SLICE_DURATION;
    base_cfg->ram_cache_size = 32 * 1024;  // 32KB cache
    mp4_cfg.display_in_order = true;
    
    // Open muxer
    recorder_ctx.muxer = esp_muxer_open(base_cfg, sizeof(mp4_muxer_config_t));
    if (recorder_ctx.muxer == NULL) {
        ESP_LOGE(TAG, "Failed to open muxer");
        return ESP_FAIL;
    }
    
    // Add video stream
    esp_muxer_video_stream_info_t video_stream = {
        .width = photo_resolution_width[camera_state.current_resolution],
        .height = photo_resolution_height[camera_state.current_resolution],
        .fps = VIDEO_FRAME_RATE,
        .codec = ESP_MUXER_VDEC_MJPEG,  
    };
    int ret = esp_muxer_add_video_stream(recorder_ctx.muxer, &video_stream, &recorder_ctx.video_stream_idx);
    if (ret != ESP_MUXER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to add video stream, error: %d", ret);
        return ESP_FAIL;
    }
    
    // Add audio stream
    esp_muxer_audio_stream_info_t audio_stream = {
        .bits_per_sample = REC_AUDIO_BITS_PER_SAMPLE,
        .sample_rate = REC_AUDIO_SAMPLE_RATE,
        .channel = REC_AUDIO_CHANNEL,  
        .codec = ESP_MUXER_ADEC_PCM,  
    };
    ret = esp_muxer_add_audio_stream(recorder_ctx.muxer, &audio_stream, &recorder_ctx.audio_stream_idx);
    if (ret != ESP_MUXER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to add audio stream, error: %d", ret);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "MP4 muxer initialized successfully");
    return ESP_OK;
}

/**
 * @brief Deinitialize MP4 muxer
 * 
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t deinit_mp4_muxer(void)
{
    esp_err_t ret = ESP_OK;
    ESP_LOGI(TAG, "Deinitializing MP4 muxer");
    
    // Close muxer
    if (recorder_ctx.muxer) {
        esp_muxer_close(recorder_ctx.muxer);
        recorder_ctx.muxer = NULL;
        ESP_LOGI(TAG, "MP4 muxer closed successfully");
    }
    
    // unregister all muxer
    esp_muxer_unreg_all();  

    ESP_LOGI(TAG, "MP4 muxer deinitialized successfully");
    return ret;
}

/**
 * @brief Combined audio capture and encode task
 * 
 * @param pvParameters Task parameters (unused)
 */
static void audio_capture_encode_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Audio capture and encode task started");

    int pcm_frame_size = 0, output_frame_size = 0;
    esp_audio_enc_get_frame_size(recorder_ctx.encoder, &pcm_frame_size, &output_frame_size);
    ESP_LOGI(TAG, "pcm_frame_size: %d, output_frame_size: %d", pcm_frame_size, output_frame_size);
    
    // Use a single buffer for audio capture
    uint8_t *sample_buffer = heap_caps_aligned_calloc(data_cache_line_size, 1, 
                                                     pcm_frame_size * VIDEO_BUF_MULTIPLIER, 
                                                     MALLOC_CAP_SPIRAM);
    if (sample_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate sample buffer");
        vTaskDelete(NULL);
        return;
    }
    
    // Allocate encode buffer
    size_t enc_size = output_frame_size * VIDEO_BUF_MULTIPLIER;
    uint8_t *enc_data = heap_caps_aligned_calloc(data_cache_line_size, 1, 
                                                enc_size, 
                                                MALLOC_CAP_SPIRAM);
    if (enc_data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate encode buffer");
        free(sample_buffer);
        vTaskDelete(NULL);
        return;
    }
    
    while (recorder_ctx.recording) {
        // Capture audio data
        size_t bytes_read = 0;
        esp_err_t ret = bsp_extra_pdm_i2s_read(sample_buffer, pcm_frame_size * 10, &bytes_read, portMAX_DELAY);
        if (ret != ESP_OK || bytes_read == 0) {
            ESP_LOGE(TAG, "I2S read failed: %d, bytes_read: %d", ret, bytes_read);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        
        // Encode audio data directly
        esp_audio_enc_in_frame_t in_frame = {
            .buffer = sample_buffer,
            .len = bytes_read,
        };
        esp_audio_enc_out_frame_t out_frame = {
            .buffer = enc_data,
            .len = enc_size,
        };
        ret = esp_audio_enc_process(recorder_ctx.encoder, &in_frame, &out_frame);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Audio encoding failed: %d", ret);
            continue;
        }
        
        // Add encoded audio packet to muxer
        uint32_t audio_timestamp = esp_timer_get_time() / 1000 - recorder_ctx.start_time;
        ESP_LOGD(TAG, "audio_timestamp: %d", audio_timestamp);
        
        xSemaphoreTake(recorder_ctx.recording_mutex, portMAX_DELAY);
        if (recorder_ctx.recording) {
            esp_muxer_audio_packet_t audio_packet = {
                .data = enc_data,
                .len = out_frame.len,
                .pts = audio_timestamp,
            };
            
            ret = esp_muxer_add_audio_packet(recorder_ctx.muxer, recorder_ctx.audio_stream_idx, &audio_packet);
            if (ret != ESP_MUXER_ERR_OK) {
                ESP_LOGE(TAG, "Failed to add audio packet, error: %d", ret);
            }
        }
        xSemaphoreGive(recorder_ctx.recording_mutex);
    }
    
    // Free resources
    free(enc_data);
    free(sample_buffer);
    
    ESP_LOGI(TAG, "Audio capture and encode task ended");
    vTaskDelete(NULL);
}

/**
 * @brief Start video recording
 * 
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t app_video_stream_start_recording(void)
{
    esp_err_t ret = ESP_OK;
    ESP_LOGI(TAG, "Starting recording");

    // Create mutex
    recorder_ctx.recording_mutex = xSemaphoreCreateMutex();
    if (!recorder_ctx.recording_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_FAIL;
    }

    // Initialize MP4 muxer
    ret = init_mp4_muxer();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MP4 muxer: 0x%x", ret);
        return ret;
    }

    // Set recording flag
    recorder_ctx.recording = true;
    recorder_ctx.start_time = esp_timer_get_time() / 1000;

    // Create audio capture and encode task
    xTaskCreatePinnedToCore(audio_capture_encode_task, "audio_cap_enc_task", 4096, NULL, 5, &recorder_ctx.audio_capture_task_handle, 0);

    ESP_LOGI(TAG, "Recording started at %lld", recorder_ctx.start_time);

    return ret;
}

/**
 * @brief Stop video recording
 * 
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t app_video_stream_stop_recording(void)
{
    esp_err_t ret = ESP_OK;
    ESP_LOGI(TAG, "Stopping recording");

    // Set stop flag
    xSemaphoreTake(recorder_ctx.recording_mutex, portMAX_DELAY);
    recorder_ctx.recording = false;
    xSemaphoreGive(recorder_ctx.recording_mutex);

    if (recorder_ctx.audio_capture_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (eTaskGetState(recorder_ctx.audio_capture_task_handle) != eDeleted) {
            ESP_LOGI(TAG, "Waiting for audio capture task to end");
            for (int i = 0; i < 30 && eTaskGetState(recorder_ctx.audio_capture_task_handle) != eDeleted; i++) {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            if (eTaskGetState(recorder_ctx.audio_capture_task_handle) != eDeleted) {
                ESP_LOGW(TAG, "Force deleting audio capture task");
                vTaskDelete(recorder_ctx.audio_capture_task_handle);
            }
        }
        recorder_ctx.audio_capture_task_handle = NULL;
    }

    recorder_ctx.video_frame_count = 0;
    
    // Deinitialize MP4 muxer
    ret = deinit_mp4_muxer();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize MP4 muxer: 0x%x", ret);
    }

    if (recorder_ctx.recording_mutex) {
        vSemaphoreDelete(recorder_ctx.recording_mutex);
        recorder_ctx.recording_mutex = NULL;
    }
    
    ESP_LOGI(TAG, "Recording stopped");

    return ret;
}

/**
 * @brief Initialize video streaming
 * 
 * @param i2c_handle I2C master bus handle for camera communication
 * @return ESP_OK on success, error code otherwise
 */
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

    camera_buffer.scaled_camera_buf = heap_caps_aligned_calloc(data_cache_line_size, 1, 1920 * 1080 * 2, MALLOC_CAP_SPIRAM);
    if (camera_buffer.scaled_camera_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate adjusted camera buffer");
        ret = ESP_FAIL;
        goto cleanup;
    }

    // Allocate JPEG buffer, assuming compression ratio of JPEG_COMPRESSION_RATIO
    jpeg_encode_memory_alloc_cfg_t rx_mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    camera_buffer.jpg_buf = (uint8_t*)jpeg_alloc_encoder_mem(
        1920 * 1088 * 2 / JPEG_COMPRESSION_RATIO, 
        &rx_mem_cfg, 
        &camera_buffer.rx_buffer_size
    );
    if (camera_buffer.jpg_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate JPEG buffer");
        ret = ESP_FAIL;
        goto cleanup;
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

    // Load saved photo count
    uint16_t saved_count = 0;
    if (app_storage_get_photo_count(&saved_count) == ESP_OK) {
        app_extra_set_saved_photo_count(saved_count);
    }

    // Initialize PDM codec
    ret = bsp_extra_pdm_codec_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize PDM codec: 0x%x", ret);
        goto cleanup;
    }

    // Initialize MP4 muxer
    ret = init_mp4_muxer();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MP4 muxer initialization failed");
        goto cleanup;
    }

    // Register PCM encoder
    ESP_ERROR_CHECK(esp_pcm_enc_register());

    esp_pcm_enc_config_t pcm_cfg = {
        .sample_rate = REC_AUDIO_SAMPLE_RATE,
        .channel = REC_AUDIO_CHANNEL,
        .bits_per_sample = REC_AUDIO_BITS_PER_SAMPLE,
    };

    esp_audio_enc_config_t enc_cfg = {
        .type = ESP_AUDIO_TYPE_PCM,
        .cfg = &pcm_cfg,
        .cfg_sz = sizeof(pcm_cfg)
    };

    ESP_ERROR_CHECK(esp_audio_enc_open(&enc_cfg, &recorder_ctx.encoder));

    // Start camera stream task
    ret = app_video_stream_task_start(video_cam_fd0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start video stream task: 0x%x", ret);
        goto cleanup;
    }

    resources_initialized = true;

cleanup:
    if (!resources_initialized) {
        if (jpeg_handle != NULL) {
            jpeg_del_encoder_engine(jpeg_handle);
            jpeg_handle = NULL;
        }
        
        if (camera_buffer.scaled_camera_buf != NULL) {
            heap_caps_free(camera_buffer.scaled_camera_buf);
            camera_buffer.scaled_camera_buf = NULL;
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