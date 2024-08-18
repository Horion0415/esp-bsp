// This file was generated by SquareLine Studio
// SquareLine Studio version: SquareLine Studio 1.4.1
// LVGL version: 8.3.11
// Project name: eye_s3_product_test

#include "../ui.h"

void ui_ScreenButton_screen_init(void)
{
    ui_ScreenButton = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_ScreenButton, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    ui_PanelMenu = lv_obj_create(ui_ScreenButton);
    lv_obj_set_width(ui_PanelMenu, 66);
    lv_obj_set_height(ui_PanelMenu, 50);
    lv_obj_set_x(ui_PanelMenu, -80);
    lv_obj_set_y(ui_PanelMenu, -70);
    lv_obj_set_align(ui_PanelMenu, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_PanelMenu, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_PanelMenu, lv_color_hex(0x13D0F4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_PanelMenu, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_PanelMenu, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_PanelMenu, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_PanelDown = lv_obj_create(ui_ScreenButton);
    lv_obj_set_width(ui_PanelDown, 66);
    lv_obj_set_height(ui_PanelDown, 50);
    lv_obj_set_x(ui_PanelDown, 80);
    lv_obj_set_y(ui_PanelDown, 70);
    lv_obj_set_align(ui_PanelDown, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_PanelDown, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_PanelDown, lv_color_hex(0x13D0F4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_PanelDown, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_PanelDown, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_PanelDown, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_PanelPlay = lv_obj_create(ui_ScreenButton);
    lv_obj_set_width(ui_PanelPlay, 66);
    lv_obj_set_height(ui_PanelPlay, 50);
    lv_obj_set_x(ui_PanelPlay, -80);
    lv_obj_set_y(ui_PanelPlay, 70);
    lv_obj_set_align(ui_PanelPlay, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_PanelPlay, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_PanelPlay, lv_color_hex(0x13D0F4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_PanelPlay, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_PanelPlay, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_PanelPlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_PanelUp = lv_obj_create(ui_ScreenButton);
    lv_obj_set_width(ui_PanelUp, 66);
    lv_obj_set_height(ui_PanelUp, 50);
    lv_obj_set_x(ui_PanelUp, 80);
    lv_obj_set_y(ui_PanelUp, -70);
    lv_obj_set_align(ui_PanelUp, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_PanelUp, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_PanelUp, lv_color_hex(0x13D0F4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_PanelUp, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_PanelUp, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_PanelUp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);


}
