#ifndef UI_EXTRA_H
#define UI_EXTRA_H

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

void ui_extra_init(void);

void ui_extra_scroll_up(void);
void ui_extra_scroll_down(void);

#endif
