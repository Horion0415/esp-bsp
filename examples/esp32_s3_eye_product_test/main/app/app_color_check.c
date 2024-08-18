#include "soc/soc_caps.h"
#include "esp_err.h"
#include "esp_log.h"

#include "app_color_check.h"
#include "app_button.h"
#include "bsp/esp-bsp.h"
#include "ui.h"

static char *TAG = "app_color_check";

static uint8_t color_switch_num = 0;

static void color_check_timer(lv_timer_t * timer)
{
    switch (color_switch_num) {
    case 0:
        lv_obj_set_style_bg_color(ui_ScreenColor, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        break;
    case 1:
        lv_obj_set_style_bg_color(ui_ScreenColor, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
        break;
    case 2:
        lv_obj_set_style_bg_color(ui_ScreenColor, lv_color_hex(0x0000FF), LV_PART_MAIN | LV_STATE_DEFAULT);
        break;
    case 3:
        lv_obj_set_style_bg_color(ui_ScreenColor, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        break;
    case 4:
        lv_obj_set_style_bg_color(ui_ScreenColor, lv_color_hex(0xE4F9F5), LV_PART_MAIN | LV_STATE_DEFAULT);
        break;
    default:
        _ui_screen_change(&ui_ScreenButton, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_ScreenButton_screen_init);
        app_button_change_screen(ScreenButton);

        lv_timer_del(timer);
        break;
    }

    color_switch_num++;
}

void ui_ScreenColor_event_cb(lv_event_t *e)
{
    lv_timer_t * timer = lv_timer_create(color_check_timer, 1000,  NULL);
}

esp_err_t app_color_check_init(void)
{
    lv_obj_add_event_cb(ui_ScreenColor, ui_ScreenColor_event_cb, LV_EVENT_SCREEN_LOADED, NULL);

    return ESP_OK;
}