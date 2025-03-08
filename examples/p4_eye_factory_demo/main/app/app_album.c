#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "driver/jpeg_decode.h"
#include "driver/ppa.h"
#include "esp_private/esp_cache_private.h"

#include "ui_extra.h"

#include "app_storage.h"
#include "app_video.h"
#include "app_video_stream.h"

static const char *TAG = "app_album";

#define ALIGN_UP(num, align)    (((num) + ((align) - 1)) & ~((align) - 1))

#define MAX_IMAGES   500
#define MAX_PATH_LEN 300
#define PIC_FOLDER_NAME "esp32_p4_pic_save"

typedef struct {
    char filenames[MAX_IMAGES][MAX_PATH_LEN];
    int count;
    int current_index;
    lv_obj_t *canvas;
    void *img_buffer;       // Buffer for JPEG file
    void *ppa_buffer;
    size_t buffer_size;
    void *canvas_buffer;    // Buffer for decoded RGB565 image
    int canvas_width;
    int canvas_height;
    jpeg_decoder_handle_t jpeg_handle;
    ppa_client_handle_t ppa_handle;
} album_context_t;

static album_context_t album_ctx;
static size_t data_cache_line_size = 0;
static size_t tx_buffer_size = 0;

static const uint32_t album_res[PHOTO_RESOLUTION_MAX] = {480, 720, 960};
static photo_resolution_t current_album_resolution = PHOTO_RESOLUTION_1080P; // default 1080P

// Scan images from SD card
static esp_err_t app_album_scan_images(void) {
    DIR *dir = opendir(BSP_SD_MOUNT_POINT"/"PIC_FOLDER_NAME);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory %s/%s", BSP_SD_MOUNT_POINT, PIC_FOLDER_NAME);
        return ESP_FAIL;
    }

    struct dirent *entry;
    album_ctx.count = 0;

    // Clear filename array
    memset(album_ctx.filenames, 0, sizeof(album_ctx.filenames));

    // Scan all jpg files in the directory
    while ((entry = readdir(dir)) != NULL && album_ctx.count < MAX_IMAGES) {
        if ((strstr(entry->d_name, ".jpg") || strstr(entry->d_name, ".JPG")) && 
            strncmp(entry->d_name, "._", 2) != 0) {
            snprintf(album_ctx.filenames[album_ctx.count], MAX_PATH_LEN, 
                    "%s/%s/%s", BSP_SD_MOUNT_POINT, PIC_FOLDER_NAME, entry->d_name);
            album_ctx.count++;
        }
        ESP_LOGD(TAG, "image %d: %s", album_ctx.count, album_ctx.filenames[album_ctx.count - 1]);
    }

    closedir(dir);
    
    if (album_ctx.count == 0) {
        ESP_LOGW(TAG, "No images found in %s/%s", BSP_SD_MOUNT_POINT, PIC_FOLDER_NAME);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Found %d images in %s/%s", album_ctx.count, BSP_SD_MOUNT_POINT, PIC_FOLDER_NAME);
    album_ctx.current_index = 0;
    
    return ESP_OK;
}

// Load current image into buffer and decode it
static esp_err_t app_album_load_current_image(void) {
    if (album_ctx.count == 0) {
        ESP_LOGE(TAG, "No images available");
        return ESP_FAIL;
    }
    
    // Free previous image buffer
    if (album_ctx.img_buffer) {
        free(album_ctx.img_buffer);
        album_ctx.img_buffer = NULL;
    }
    
    // Open file
    FILE *f = fopen(album_ctx.filenames[album_ctx.current_index], "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file: %s", album_ctx.filenames[album_ctx.current_index]);
        return ESP_FAIL;
    }
    
    // Get file size
    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // Allocate memory for JPEG data
    album_ctx.img_buffer = heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM);
    if (!album_ctx.img_buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for image");
        fclose(f);
        return ESP_FAIL;
    }
    
    // Read file content
    size_t bytes_read = fread(album_ctx.img_buffer, 1, file_size, f);
    fclose(f);
    
    if (bytes_read != file_size) {
        ESP_LOGE(TAG, "Failed to read file: %s", album_ctx.filenames[album_ctx.current_index]);
        free(album_ctx.img_buffer);
        album_ctx.img_buffer = NULL;
        return ESP_FAIL;
    }
    
    album_ctx.buffer_size = file_size;
    ESP_LOGI(TAG, "Loaded image: %s (%u bytes)", album_ctx.filenames[album_ctx.current_index], file_size);
    
    uint32_t out_size = 0;
    jpeg_decode_picture_info_t header_info;

    jpeg_decode_cfg_t decode_cfg_rgb = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
    };
    ESP_ERROR_CHECK(jpeg_decoder_get_info(album_ctx.img_buffer, album_ctx.buffer_size, &header_info));
    ESP_LOGD(TAG, "header parsed, width is %" PRId32 ", height is %" PRId32, header_info.width, header_info.height);
        
    ESP_ERROR_CHECK(jpeg_decoder_process(album_ctx.jpeg_handle, &decode_cfg_rgb, album_ctx.img_buffer, album_ctx.buffer_size, album_ctx.ppa_buffer, tx_buffer_size, &out_size));
    
    current_album_resolution = app_video_stream_get_photo_resolution();

    ppa_srm_oper_config_t srm_config = {
        .in.buffer = album_ctx.ppa_buffer,
        .in.pic_w = header_info.width,
        .in.pic_h = header_info.height,
        .in.block_w = album_res[current_album_resolution],
        .in.block_h = album_res[current_album_resolution],
        .in.block_offset_x = (header_info.width - album_res[current_album_resolution]) / 2,
        .in.block_offset_y = (header_info.height - album_res[current_album_resolution]) / 2,
        .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .out.buffer = album_ctx.canvas_buffer,
        .out.buffer_size = ALIGN_UP(BSP_LCD_H_RES * BSP_LCD_V_RES * 2, data_cache_line_size),
        .out.pic_w = BSP_LCD_H_RES,
        .out.pic_h = BSP_LCD_V_RES,
        .out.block_offset_x = 0,
        .out.block_offset_y = 0,
        .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = (float)BSP_LCD_H_RES / album_res[current_album_resolution],
        .scale_y = (float)BSP_LCD_V_RES / album_res[current_album_resolution],
        .rgb_swap = 0,
        .byte_swap = 0,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    ESP_ERROR_CHECK(ppa_do_scale_rotate_mirror(album_ctx.ppa_handle, &srm_config));

    swap_rgb565_bytes(album_ctx.canvas_buffer, BSP_LCD_H_RES * BSP_LCD_V_RES);

    return ESP_OK;
}

// Display current image on canvas
static esp_err_t app_album_display_current_image(void) {
    if (!album_ctx.canvas_buffer || !album_ctx.canvas) {
        ESP_LOGE(TAG, "Canvas buffer or canvas object not initialized");
        return ESP_FAIL;
    }
    
    // Set canvas buffer with decoded image
    bsp_display_lock(0);
    lv_canvas_set_buffer(album_ctx.canvas, album_ctx.canvas_buffer, 
                         album_ctx.canvas_width, album_ctx.canvas_height, 
                         LV_IMG_CF_TRUE_COLOR);
    
    // Force refresh
    lv_obj_invalidate(album_ctx.canvas);
    bsp_display_unlock();
    
    return ESP_OK;
}

// Switch to next image
esp_err_t app_album_next_image(void) {
    if (album_ctx.count == 0) {
        ESP_LOGE(TAG, "No images available");
        return ESP_FAIL;
    }
    
    album_ctx.current_index = (album_ctx.current_index + 1) % album_ctx.count;
    ESP_LOGI(TAG, "Switching to next image: %d/%d", album_ctx.current_index + 1, album_ctx.count);
    
    if (app_album_load_current_image() != ESP_OK) {
        return ESP_FAIL;
    }
    
    return app_album_display_current_image();
}

// Switch to previous image
esp_err_t app_album_prev_image(void) {
    if (album_ctx.count == 0) {
        ESP_LOGE(TAG, "No images available");
        return ESP_FAIL;
    }
    
    album_ctx.current_index = (album_ctx.current_index + album_ctx.count - 1) % album_ctx.count;
    ESP_LOGI(TAG, "Switching to previous image: %d/%d", album_ctx.current_index + 1, album_ctx.count);
    
    if (app_album_load_current_image() != ESP_OK) {
        return ESP_FAIL;
    }
    
    return app_album_display_current_image();
}

// Initialize album functionality
esp_err_t app_album_init(lv_obj_t *parent) {
    // Initialize context
    memset(&album_ctx, 0, sizeof(album_ctx));
    
    // Set canvas dimensions
    album_ctx.canvas_width = BSP_LCD_H_RES;
    album_ctx.canvas_height = BSP_LCD_V_RES;
    
    // Create canvas object
    album_ctx.canvas = parent;
    if (!album_ctx.canvas) {
        ESP_LOGE(TAG, "Failed to create LVGL canvas object");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &data_cache_line_size));
    
    // Allocate canvas buffer (RGB565 format: 2 bytes per pixel)
    size_t canvas_buf_size = album_ctx.canvas_width * album_ctx.canvas_height * 2;
    album_ctx.canvas_buffer = heap_caps_aligned_calloc(data_cache_line_size, 1, canvas_buf_size, MALLOC_CAP_SPIRAM);
    if (!album_ctx.canvas_buffer) {
        ESP_LOGE(TAG, "Failed to allocate canvas buffer");
        return ESP_FAIL;
    }

    //PPA client config
    ppa_client_config_t ppa_srm_config = {
        .oper_type = PPA_OPERATION_SRM,
    };
    ESP_ERROR_CHECK(ppa_register_client(&ppa_srm_config, &album_ctx.ppa_handle));

    //jpeg decode memory alloc config
    jpeg_decode_memory_alloc_cfg_t tx_mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };

    album_ctx.ppa_buffer = jpeg_alloc_decoder_mem(1920 * 1088 * 3, &tx_mem_cfg, &tx_buffer_size);
    if (!album_ctx.ppa_buffer) {
        ESP_LOGE(TAG, "Failed to allocate PPA buffer");
        return ESP_FAIL;
    }

    jpeg_decode_engine_cfg_t decode_eng_cfg = {
        .timeout_ms = 40,
    };
    ESP_ERROR_CHECK(jpeg_new_decoder_engine(&decode_eng_cfg, &album_ctx.jpeg_handle));
    
    // Set initial canvas buffer (black screen)
    bsp_display_lock(0);
    lv_canvas_set_buffer(album_ctx.canvas, album_ctx.canvas_buffer, 
                         album_ctx.canvas_width, album_ctx.canvas_height, 
                         LV_IMG_CF_TRUE_COLOR);
    bsp_display_unlock();
    
    // Scan images from SD card
    esp_err_t ret = app_album_scan_images();
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Load first image
    ret = app_album_load_current_image();
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Display first image
    ret = app_album_display_current_image();
    
    return ret;
}

// Refresh album (rescan SD card)
esp_err_t app_album_refresh(void) {
    // Free previous image buffer
    if (album_ctx.img_buffer) {
        free(album_ctx.img_buffer);
        album_ctx.img_buffer = NULL;
    }
    
    // Rescan images from SD card
    esp_err_t ret = app_album_scan_images();
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Load first image
    ret = app_album_load_current_image();
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Display first image
    ret = app_album_display_current_image();
    
    return ret;
}

// Clean up album resources
void app_album_deinit(void) {
    if (album_ctx.img_buffer) {
        free(album_ctx.img_buffer);
        album_ctx.img_buffer = NULL;
    }
    
    if (album_ctx.canvas_buffer) {
        free(album_ctx.canvas_buffer);
        album_ctx.canvas_buffer = NULL;
    }
    
    if (album_ctx.jpeg_handle) {
        jpeg_del_decoder_engine(album_ctx.jpeg_handle);
        album_ctx.jpeg_handle = NULL;
    }
    
    // LVGL objects will be deleted with their parent
}