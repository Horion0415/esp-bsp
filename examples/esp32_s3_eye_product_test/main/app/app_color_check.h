#ifndef ESP_COLOR_CHECK_H
#define ESP_COLOR_CHECK_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_color_check_init(void);   // Initialize the color check

#ifdef __cplusplus
}
#endif
#endif