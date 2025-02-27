/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#ifndef APP_MIC_H
#define APP_MIC_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/**
* @brief Initialize speech recognition
*
* @return
*      - ESP_OK: Create speech recognition task successfully
*/

esp_err_t app_audio_init(void);

bool get_mic_effective(void);

esp_err_t app_audio_deinit(void);

#endif
