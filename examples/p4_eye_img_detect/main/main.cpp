/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <vector>
#include <string.h>
#include <stdio.h>
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "driver/ppa.h"
#include "esp_private/esp_cache_private.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

#include "esp_painter.h"

#include "app_video.h"
#include "app_pedestrian_detect.h"
#include "app_humanface_detect.h"
#include "app_coco_detect.h"
#include "app_camera_pipeline.hpp"
#include "app_drawing_utils.h"

#define ALIGN_UP(num, align)    (((num) + ((align) - 1)) & ~((align) - 1))
#define HOR_RES 1920
#define VER_RES 1080

using namespace std;

static const char *TAG = "main";

static vector<vector<int>> detect_bound;
static vector<vector<int>> detect_keypoints;
static std::list<dl::detect::result_t> detect_results;
static vector<pair<int, float>> detect_categories; // Store category and score for COCO detection
static PedestrianDetect *ped_detect = NULL;
static HumanFaceDetect *hum_detect = NULL;
static COCODetect *g_coco_detect = NULL;
static pipeline_handle_t feed_pipeline;
static pipeline_handle_t detect_pipeline;

static TaskHandle_t detect_task_handle;

static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;

static esp_painter_handle_t painter = NULL;

static button_handle_t btns[BSP_BUTTON_NUM];

static ppa_client_handle_t ppa_srm_handle = NULL;
static size_t data_cache_line_size = 0;
static void *canvas_buf[EXAMPLE_CAM_BUF_NUM];
static void *rgb565_swap_buf = NULL; // Intermediate buffer for RGB565 byte swapping

static bool pedestrian_detected = true;
static bool humanface_detected = false;
static bool coco_detected = false;
static uint8_t detect_mode = 0; // 0: Pedestrian detection, 1: Face detection, 2: COCO detection

static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, uint32_t camera_buf_hes, uint32_t camera_buf_ves, size_t camera_buf_len);
void camera_dectect_task(void);

void swap_rgb565_bytes(uint16_t *buffer, int pixel_count)
{
    for (int i = 0; i < pixel_count; i++) {
        uint16_t swap16 = *(buffer + i);
        swap16 = (swap16 >> 8) | (swap16 << 8);
        *(buffer + i) = swap16;
    }
}

void copy_and_swap_rgb565(void* dst, const void* src, int pixel_count)
{
    uint16_t *dst_buf = (uint16_t*)dst;
    const uint16_t *src_buf = (const uint16_t*)src;
    
    for (int i = 0; i < pixel_count; i++) {
        uint16_t swap16 = *(src_buf + i);
        swap16 = (swap16 >> 8) | (swap16 << 8);
        *(dst_buf + i) = swap16;
    }
}

static void btn_handler(void *arg, void *data)
{
    if((int)data == BSP_BUTTON_1) {
        // Cycle through detection modes: Pedestrian -> Face -> COCO -> Pedestrian
        detect_mode = (detect_mode + 1) % 3;
        
        // Set detection flags based on current mode
        pedestrian_detected = (detect_mode == 0);
        humanface_detected = (detect_mode == 1);
        coco_detected = (detect_mode == 2);
        
        ESP_LOGI(TAG, "Switched to mode %d: %s", detect_mode, 
                detect_mode == 0 ? "Pedestrian Detection" : 
                detect_mode == 1 ? "Face Detection" : "COCO Detection");
    }
}

extern "C" void app_main(void)
{
    // Initialize the display
    const bsp_display_config_t bsp_disp_cfg = {
        .max_transfer_sz = BSP_LCD_DRAW_BUFF_SIZE * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(bsp_display_new(&bsp_disp_cfg, &panel_handle, &io_handle));

    esp_lcd_panel_disp_on_off(panel_handle, true);

    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_1], BUTTON_PRESS_DOWN, btn_handler, (void *) BSP_BUTTON_1));

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
    
    // allocate the rgb565 swap buffer
    rgb565_swap_buf = heap_caps_aligned_calloc(data_cache_line_size, 1, HOR_RES * VER_RES * 2, MALLOC_CAP_SPIRAM);
    if (rgb565_swap_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate RGB565 swap buffer");
        return;
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
    int video_cam_fd0 = app_video_open(EXAMPLE_CAM_DEV_PATH, APP_VIDEO_FMT_RGB565);
    if (video_cam_fd0 < 0) {
        ESP_LOGE(TAG, "video cam open failed");
        return;
    }

    // Initialize video capture device
    ESP_ERROR_CHECK(app_video_set_bufs(video_cam_fd0, EXAMPLE_CAM_BUF_NUM, NULL));

    // Register the video frame operation callback
    ESP_ERROR_CHECK(app_video_register_frame_operation_cb(camera_video_frame_operation));

    size_t detect_buf_size = ALIGN_UP(HOR_RES * VER_RES / 8, data_cache_line_size);

    camera_pipeline_cfg_t PPA_feed_cfg = {
        .elem_num = 4,
        .elements = NULL,
        .align_size = 1,
        .caps = MALLOC_CAP_SPIRAM,
        .buffer_size = detect_buf_size,
    };

    camera_element_pipeline_new(&PPA_feed_cfg, &feed_pipeline);

    camera_pipeline_cfg_t detect_feed_cfg = {
        .elem_num = 4,
        .elements = NULL,
        .align_size = 1,
        .caps = MALLOC_CAP_SPIRAM,
        .buffer_size = 20 * sizeof(int),
    };
    camera_element_pipeline_new(&detect_feed_cfg, &detect_pipeline);

    ped_detect = get_pedestrian_detect();
    assert(ped_detect != NULL);
    
    hum_detect = get_humanface_detect();
    assert(hum_detect != NULL);

    g_coco_detect = get_coco_detect();
    assert(g_coco_detect != NULL);

    set_screen_dimensions(HOR_RES, VER_RES);

    xTaskCreatePinnedToCore((TaskFunction_t)camera_dectect_task, "Camera Detect", 1024 * 8, NULL, 5, &detect_task_handle, 1);

    // Initialize esp_painter
    esp_painter_config_t painter_config = {
        .canvas = {
            .width = HOR_RES,
            .height = VER_RES
        },
        .color_format = ESP_PAINTER_COLOR_FORMAT_RGB565,
        .default_font = &esp_painter_basic_font_48
    };
    ESP_ERROR_CHECK(esp_painter_init(&painter_config, &painter));

    // Start the camera stream task
    ESP_ERROR_CHECK(app_video_stream_task_start(video_cam_fd0, 0));

    bsp_display_backlight_on();
}

void camera_dectect_task(void)
{
    while (1) {        
        camera_pipeline_buffer_element *p = camera_pipeline_recv_element(feed_pipeline, portMAX_DELAY);
        if (p) {
            if (pedestrian_detected) {
                detect_results = app_pedestrian_detect((uint16_t *)p->buffer, HOR_RES, VER_RES);
            }  else if (humanface_detected) {
                detect_results = app_humanface_detect((uint16_t *)p->buffer, HOR_RES, VER_RES);
            } else if (coco_detected) {
                copy_and_swap_rgb565(rgb565_swap_buf, p->buffer, HOR_RES * VER_RES);
                detect_results = app_coco_detect((uint16_t *)rgb565_swap_buf, HOR_RES, VER_RES);
            }

            camera_pipeline_queue_element_index(feed_pipeline, p->index);

            camera_pipeline_buffer_element *element = camera_pipeline_get_queued_element(detect_pipeline);
            if (element) {
                element->detect_results = &detect_results;

                camera_pipeline_done_element(detect_pipeline, element);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, uint32_t camera_buf_hes, uint32_t camera_buf_ves, size_t camera_buf_len)
{    
    // Process input frame
    camera_pipeline_buffer_element *input_element = camera_pipeline_get_queued_element(feed_pipeline);
    if (input_element) {
        input_element->buffer = reinterpret_cast<uint16_t*>(camera_buf);
        camera_pipeline_done_element(feed_pipeline, input_element);
    }

    // Get detection results
    camera_pipeline_buffer_element *detect_element = camera_pipeline_recv_element(detect_pipeline, 0);
    if (detect_element) {
        // Process detection results
        detect_keypoints.clear();
        detect_bound.clear();
        detect_categories.clear(); // Clear categories vector
        
        for (const auto& res : *(detect_element->detect_results)) {
            const auto& box = res.box;
            // Check if bounding box is valid
            if (box.size() >= 4 && std::any_of(box.begin(), box.end(), [](int v) { return v != 0; })) {
                detect_bound.push_back(box);

                // Process keypoints only in face detection mode
                if ((humanface_detected) && 
                    res.keypoint.size() >= 10 && 
                    std::any_of(res.keypoint.begin(), res.keypoint.end(), [](int v) { return v != 0; })) {
                    detect_keypoints.push_back(res.keypoint);
                }
                
                // Store category and score for COCO detection
                if (coco_detected) {
                    detect_categories.push_back(make_pair(res.category, res.score));
                }
            }
        }

        camera_pipeline_queue_element_index(detect_pipeline, detect_element->index);
    }

    // Draw detection results
    uint16_t *rgb_buf = reinterpret_cast<uint16_t*>(camera_buf);
    for (size_t i = 0; i < detect_bound.size(); i++) {
        const auto& bound = detect_bound[i];
        // Check if current bounding box is valid
        if (bound.size() >= 4 && std::any_of(bound.begin(), bound.end(), [](int v) { return v != 0; })) {
            // Draw bounding box
            draw_rectangle_rgb(rgb_buf, camera_buf_hes, camera_buf_ves,
                                bound[0], bound[1], bound[2], bound[3],
                                0, 0, 255, 0, 0, 5);

            // Draw keypoints in face detection mode
            if (humanface_detected && 
                i < detect_keypoints.size() && 
                detect_keypoints[i].size() >= 10) {
                draw_green_points(rgb_buf, detect_keypoints[i]);
            }
            
            // Display COCO detection class name
            if (coco_detected && i < detect_categories.size()) {
                int category = detect_categories[i].first;
                float score = detect_categories[i].second;
                
                const char* class_name = get_coco_class_name(category);
                char label[64];
                snprintf(label, sizeof(label), "%s (%.1f%%)", class_name, score * 100.0f);
                
                // Ensure text is displayed above the bounding box and within screen boundaries
                int text_x = bound[0];
                int text_y = bound[1] - 50;  // Display 50 pixels above the bounding box
                if (text_y < 0) text_y = bound[1] + 5;  // If not enough space above, display at the top inside the box
                
                // Use esp_painter to draw text
                if (painter != NULL) {
                    esp_painter_draw_string(painter, (uint8_t*)rgb_buf, 
                                           camera_buf_hes * camera_buf_ves * 2,
                                           text_x, text_y, NULL, 
                                           ESP_PAINTER_COLOR_YELLOW, 
                                           label);
                }
            }
        }
    }

    ppa_srm_oper_config_t oper_config_out = {
        .in = {
            .buffer = (void *)camera_buf,
            .pic_w = HOR_RES,
            .pic_h = VER_RES,
            .block_w = 960,
            .block_h = 960,
            .block_offset_x = (HOR_RES - 960) / 2,
            .block_offset_y = (VER_RES - 960) / 2,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = canvas_buf[camera_buf_index],
            .buffer_size = ALIGN_UP(BSP_LCD_V_RES * BSP_LCD_H_RES * BSP_LCD_BITS_PER_PIXEL / 8, data_cache_line_size),
            .pic_w = BSP_LCD_H_RES,
            .pic_h = BSP_LCD_V_RES,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = (float)BSP_LCD_H_RES / 960,
        .scale_y = (float)BSP_LCD_V_RES / 960,
        .rgb_swap = 0,
        .byte_swap = 0,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    ESP_ERROR_CHECK(ppa_do_scale_rotate_mirror(ppa_srm_handle, &oper_config_out));

    swap_rgb565_bytes(static_cast<uint16_t*>(canvas_buf[camera_buf_index]), BSP_LCD_H_RES * BSP_LCD_V_RES);

    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, BSP_LCD_H_RES, BSP_LCD_V_RES, canvas_buf[camera_buf_index]);
}