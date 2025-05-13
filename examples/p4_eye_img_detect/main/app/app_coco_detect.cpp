/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "iostream"
#include "coco_detect.hpp"
#include "dl_tool.hpp"
#include "dl_image_define.hpp"
#include "app_coco_detect.h"

static const char *TAG = "app_coco_detect";
static COCODetect *detect = NULL;

std::list<dl::detect::result_t> app_coco_detect(uint16_t *frame, int width, int height)
{
    dl::image::img_t img;
    img.data = frame;
    img.width = width;
    img.height = height;
    img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565;

    auto &detect_results = detect->run(img);
    
    // 打印检测结果
    for (const auto &res : detect_results) {
        ESP_LOGI(TAG,
                 "[category: %d, score: %f, x1: %d, y1: %d, x2: %d, y2: %d]",
                 res.category,
                 res.score,
                 res.box[0],
                 res.box[1],
                 res.box[2],
                 res.box[3]);
    }

    return detect_results;
}

COCODetect *get_coco_detect()
{
    if (detect == NULL) {
        detect = new COCODetect();
    }

    return detect;
}

void delete_coco_detect()
{
    if (detect) {
        delete detect;
        detect = NULL;
    }
} 