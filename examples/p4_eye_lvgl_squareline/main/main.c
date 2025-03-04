/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "esp_log.h"

#include "ui_extra.h"

static void btn_handler(void *arg, void *data)
{
    if((int)data == BSP_BUTTON_2) {
        ui_extra_scroll_up();
    } else if((int)data == BSP_BUTTON_3) {
        ui_extra_scroll_down();
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(bsp_p4_eye_init());

    bsp_display_start();
    bsp_display_lock(0);

    ui_extra_init();

    bsp_display_unlock();
    bsp_display_backlight_on();

    button_handle_t btns[BSP_BUTTON_NUM];
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_2], BUTTON_PRESS_DOWN, btn_handler, (void *) BSP_BUTTON_2));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_3], BUTTON_PRESS_DOWN, btn_handler, (void *) BSP_BUTTON_3));

}