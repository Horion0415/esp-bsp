// This file was generated by SquareLine Studio
// SquareLine Studio version: SquareLine Studio 1.4.1
// LVGL version: 8.3.11
// Project name: eye_s3_product_test

#include "../ui.h"

void ui_ScreenSpeech_screen_init(void)
{
    ui_ScreenSpeech = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_ScreenSpeech, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    ui_LabelSpeech = lv_label_create(ui_ScreenSpeech);
    lv_obj_set_width(ui_LabelSpeech, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_LabelSpeech, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_LabelSpeech, -10);
    lv_obj_set_y(ui_LabelSpeech, -14);
    lv_obj_set_align(ui_LabelSpeech, LV_ALIGN_CENTER);
    lv_label_set_text(ui_LabelSpeech, "   请对着开发板说\n\n       ''Hi, 乐鑫'' \n\n      以开始测试");
    lv_obj_set_style_text_font(ui_LabelSpeech, &ui_font_FontPromt, LV_PART_MAIN | LV_STATE_DEFAULT);

}
