#ifndef ESP_BUTTON_H
#define ESP_BUTTON_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

typedef enum {
    ScreenSpeech = 0,
    ScreenColor,
    ScreenButton,
    ScreenCamera,
    ScreenIMU,
    ScreenSuccess,
} ScreenType;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_button_init(void);
esp_err_t app_button_change_screen(ScreenType new_screen);
ScreenType app_button_get_screen(void);
esp_err_t app_sdcard_init(void);
esp_err_t app_sdcard_write_result(void);

#ifdef __cplusplus
}
#endif
#endif