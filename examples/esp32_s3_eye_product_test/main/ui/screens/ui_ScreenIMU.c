// This file was generated by SquareLine Studio
// SquareLine Studio version: SquareLine Studio 1.4.1
// LVGL version: 8.3.11
// Project name: eye_s3_product_test

#include "../ui.h"

void ui_ScreenIMU_screen_init(void)
{
    ui_ScreenIMU = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_ScreenIMU, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    ui_LabelPass = lv_label_create(ui_ScreenIMU);
    lv_obj_set_width(ui_LabelPass, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_LabelPass, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_LabelPass, 0);
    lv_obj_set_y(ui_LabelPass, 35);
    lv_obj_set_align(ui_LabelPass, LV_ALIGN_CENTER);
    lv_label_set_text(ui_LabelPass, "PASS");
    lv_obj_set_style_text_color(ui_LabelPass, lv_color_hex(0x2FF6AA), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_LabelPass, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_LabelPass, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_LabelPrompt = lv_label_create(ui_ScreenIMU);
    lv_obj_set_width(ui_LabelPrompt, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_LabelPrompt, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_LabelPrompt, -1);
    lv_obj_set_y(ui_LabelPrompt, -60);
    lv_obj_set_align(ui_LabelPrompt, LV_ALIGN_CENTER);
    lv_label_set_text(ui_LabelPrompt, "请将开发板平放在\n         桌面上");
    lv_obj_set_style_text_font(ui_LabelPrompt, &ui_font_FontPromt, LV_PART_MAIN | LV_STATE_DEFAULT);

}
