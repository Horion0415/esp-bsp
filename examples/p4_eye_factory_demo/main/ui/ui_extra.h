#ifndef UI_EXTRA_H
#define UI_EXTRA_H

#include "ui.h"
typedef enum {
    UI_PAGE_MAIN,           // main page
    UI_PAGE_CAMERA,         // camera page
    UI_PAGE_INTERVAL_CAM,   // interval camera page
    UI_PAGE_VIDEO_MODE,     // video mode page
    UI_PAGE_ALBUM,          // album page
    UI_PAGE_USB_DISK,       // usb disk page
    UI_PAGE_SETTINGS,       // settings page
    UI_PAGE_MAX             // page count
} ui_page_t;

typedef struct {
    const char* language;
    const char* resolution;
    const char* flash;
} settings_info_t;

void ui_extra_init(void);

void ui_extra_btn_menu(void);
void ui_extra_btn_up(void);
void ui_extra_btn_down(void);
void ui_extra_clear_popup_window(void);
ui_page_t ui_extra_get_current_page(void);
ui_page_t ui_extra_get_choosed_page(void);
settings_info_t ui_extra_get_settings_info(void);
void ui_extra_set_magnification_factor(uint16_t factor);
uint16_t ui_extra_get_magnification_factor(void);
void ui_extra_set_interval_time(uint16_t time);
uint16_t ui_extra_get_interval_time(void);

#endif
