#include <stdio.h>
#include "esp_log.h"
#include "esp_private/esp_cache_private.h"
#include "driver/ppa.h"
#include "driver/jpeg_encode.h"
#include "bsp/esp-bsp.h"

#include "ui_extra.h"
#include "app_video.h"
#include "app_storage.h"

#define ALIGN_UP(num, align)    (((num) + ((align) - 1)) & ~((align) - 1))
#define SCALE_LEVELS 6                         // resolution scale levels

static const char *TAG = "app_video_stream";

static size_t data_cache_line_size = 0;
static ppa_client_handle_t ppa_srm_handle = NULL;
static void *canvas_buf[EXAMPLE_CAM_BUF_NUM];

static jpeg_encoder_handle_t jpeg_handle;
static uint32_t jpg_size;
static uint8_t *jpg_buf;
static size_t rx_buffer_size = 0;

static int scale_level_res[SCALE_LEVELS] = {960, 480, 240, 120, 80, 60};

static bool is_take_photo = false;
static bool is_take_video = false;

static TaskHandle_t photo_task_handle = NULL;
static QueueHandle_t photo_queue = NULL;

typedef struct {
    uint8_t *camera_buf;
    uint32_t width;
    uint32_t height;
} photo_task_params_t;

static void photo_task(void *pvParameters);
static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, uint32_t camera_buf_hes, uint32_t camera_buf_ves, size_t camera_buf_len);

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

    jpeg_encode_memory_alloc_cfg_t rx_mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };

    jpg_buf = (uint8_t*)jpeg_alloc_encoder_mem(app_video_get_buf_size() / 10, &rx_mem_cfg, &rx_buffer_size); // Assume that compression ratio of 10 to 1
    assert(jpg_buf != NULL);

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

    // Start the camera stream task
    ESP_ERROR_CHECK(app_video_stream_task_start(video_cam_fd0, 0));

    return ret;
}

static esp_err_t take_and_save_photo(uint8_t *camera_buf, uint32_t width, uint32_t height)
{
    esp_err_t ret = ESP_OK;
    
    bsp_display_backlight_off();
    bsp_led_set(BSP_LED_WHITE, true);

    jpeg_encode_cfg_t enc_config = {
        .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
        .sub_sample = JPEG_DOWN_SAMPLING_YUV420,
        .image_quality = 70,
        .width = width,
        .height = height,
    };

    ret = jpeg_encoder_process(jpeg_handle, &enc_config, camera_buf, app_video_get_buf_size(), 
                              jpg_buf, rx_buffer_size, &jpg_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "JPEG encoding failed: 0x%x", ret);
        bsp_display_backlight_on();
        return ret;
    }
    
    ret = app_storage_save_picture(jpg_buf, jpg_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save picture: 0x%x", ret);
    }
    
    bsp_led_set(BSP_LED_WHITE, false);
    bsp_display_backlight_on();
    
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

    jpeg_encode_cfg_t enc_config = {
        .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
        .sub_sample = JPEG_DOWN_SAMPLING_YUV420,
        .image_quality = 70,
        .width = camera_buf_hes,
        .height = camera_buf_ves,
    };

    ESP_ERROR_CHECK(ppa_do_scale_rotate_mirror(ppa_srm_handle, &srm_config));

    swap_rgb565_bytes(canvas_buf[camera_buf_index], BSP_LCD_H_RES * BSP_LCD_V_RES);

    bsp_display_lock(0);
    lv_canvas_set_buffer(ui_PanelCanvas, canvas_buf[camera_buf_index], BSP_LCD_H_RES, BSP_LCD_V_RES, LV_IMG_CF_TRUE_COLOR);
    lv_refr_now(NULL);
    bsp_display_unlock();

    if(is_take_photo && ui_extra_get_current_page() == UI_PAGE_CAMERA) {
        photo_task_params_t params = {
            .camera_buf = camera_buf,
            .width = camera_buf_hes,
            .height = camera_buf_ves
        };
        
        // send the photo task params to the photo task
        if (xQueueSend(photo_queue, &params, 0) != pdTRUE) {
            ESP_LOGW(TAG, "photo queue is full, skip this photo");
        }

        // reset the photo flag
        is_take_photo = false;
    }
}