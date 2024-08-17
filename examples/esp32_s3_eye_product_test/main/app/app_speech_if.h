/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#ifndef SRC_IF_H
#define SRC_IF_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_afe_sr_iface.h"
#include "esp_process_sdkconfig.h"
#include "model_path.h"

typedef void (*sr_cb_t)(void*);

#define SR_CMD_STR_LEN_MAX 64
#define SR_CMD_PHONEME_LEN_MAX 64
typedef enum {
    SR_CB_TYPE_WAKE     = 0,     /*!< Wake up callback function */
    SR_CB_TYPE_CMD      = 1,     /*!< Command callback function */
    SR_CB_TYPE_CMD_EXIT = 2,     /*!< Exit command mode, wait for wakeup*/
    SR_CB_TYPE_MAX,
} sr_cb_type_t;

typedef struct sr_cmd_t {
    char str[SR_CMD_STR_LEN_MAX];
    char phoneme[SR_CMD_PHONEME_LEN_MAX];
} sr_cmd_t;

typedef enum
{
    COMMAND_TIMEOUT = -2,
    COMMAND_NOT_DETECTED = -1,

    MENU_STOP_WORKING = 0,
    MENU_DISPLAY_ONLY = 1,
    MENU_FACE_RECOGNITION = 2,
    MENU_MOTION_DETECTION = 3,

    ACTION_ENROLL = 4,
    ACTION_DELETE = 5,
    ACTION_RECOGNIZE = 6
} command_word_t;

/**
* @brief Initialize speech recognition
*
* @return
*      - ESP_OK: Create speech recognition task successfully
*/
esp_err_t speech_recognition_init(void);

#endif
