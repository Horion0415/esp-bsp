#include <stdio.h>
#include "esp_log.h"
#include "lvgl.h"

#include "ui_extra.h"

#include "app_storage.h"
#include "app_album.h"
#include "app_video_stream.h"

#define IMG_BASE_ZOOM       60
#define BTN_ZOOM_FACTOR     2.3
#define IMG_ZOOM_FACTOR     2.4
#define IMG_ZOOM_OFFSET     -80

#define MIN_INTERVAL_TIME   5
#define MAX_INTERVAL_TIME   120
#define INTERVAL_TIME_STEP  5
#define MAX_MAGNIFICATION_FACTOR 6
#define MIN_MAGNIFICATION_FACTOR 1

#define DEFAULT_MAGNIFICATION_FACTOR 1
#define DEFAULT_INTERVAL_TIME 30
#define DEFAULT_SAVED_PHOTO_COUNT 0

static const char * TAG = "ui_extra";

static uint16_t magnification_factor = DEFAULT_MAGNIFICATION_FACTOR;
static uint16_t interval_time = DEFAULT_INTERVAL_TIME;
static uint16_t saved_photo_count = DEFAULT_SAVED_PHOTO_COUNT;

// language options
static const char* const language_options[] = {"English", "English"};
// resolution options
static const char* const resolution_options[] = {"720P", "1080P", "480P"};
// flash options
static const char* const flash_options[] = {"Off", "On"};

static lv_coord_t btn_width = 0;
static lv_coord_t btn_height = 0;

static lv_obj_t * selected_btn = NULL;  
static bool is_scrolling = false; 

static lv_obj_t * scroll_cont = NULL;
static lv_obj_t * info_label = NULL;  

static ui_page_t current_page = UI_PAGE_MAIN;

static lv_timer_t *lv_popup_timer = NULL;
static lv_timer_t *lv_additional_photo_timer = NULL;

static bool is_sd_card_mounted = false;
static bool is_usb_disk_mounted = false;

typedef struct {
    const char* const* options;  
    int option_count;      
    int current_option;    
    lv_obj_t* label;       
} setting_options_t;

// All settings options
static int current_settings_item = 0;
static lv_obj_t* settings_items[4]; 
static setting_options_t settings_options[4];
static settings_info_t current_settings;

typedef struct {
    const char *name;
    int page;
} PageMapping;

static const PageMapping page_map[] = {
    {"CAMERA", UI_PAGE_CAMERA},
    {"INTERVAL CAM", UI_PAGE_INTERVAL_CAM},
    {"VIDEO MODE", UI_PAGE_VIDEO_MODE},
    {"ALBUM", UI_PAGE_ALBUM},
    {"USB DISK", UI_PAGE_USB_DISK},
    {"SETTINGS", UI_PAGE_SETTINGS},
    {NULL, -1}  
};

// Other functions
static void save_current_settings(void)
{
    settings_info_t *settings = &current_settings;
    uint16_t interval = interval_time;
    uint16_t magnify = magnification_factor;
    
    app_storage_save_settings(settings, interval, magnify);
}

static void update_setting_display(int setting_index) {
    if (setting_index < 0 || setting_index >= 4) {
        ESP_LOGW(TAG, "Invalid setting index: %d", setting_index);
        return;
    }
    
    setting_options_t* opt = &settings_options[setting_index];
    const char* current_text = opt->options[opt->current_option];
    
    // update the label text
    if (opt->label) {
        lv_label_set_text(opt->label, current_text);
    }
    
    // update the current settings info
    switch (setting_index) {
        case 0:
            current_settings.language = current_text;
            break;
        case 1:
            current_settings.resolution = current_text;
            break;
        case 2:
            current_settings.flash = current_text;
            break;
    }
    
    save_current_settings();

    ESP_LOGD(TAG, "Setting %d updated to: %s", setting_index, current_text);
}

static void init_settings_options(void) {
    // language options
    settings_options[0].options = language_options;
    settings_options[0].option_count = sizeof(language_options) / sizeof(language_options[0]);
    settings_options[0].current_option = 0;
    settings_options[0].label = ui_LabelPanelPanelSettingsLanguageBody;
    
    // resolution options
    settings_options[1].options = resolution_options;
    settings_options[1].option_count = sizeof(resolution_options) / sizeof(resolution_options[0]);
    settings_options[1].current_option = 0;
    settings_options[1].label = ui_LabelPanelPanelSettingsResBody;
    
    // flash options
    settings_options[2].options = flash_options;
    settings_options[2].option_count = sizeof(flash_options) / sizeof(flash_options[0]);
    settings_options[2].current_option = 0;
    settings_options[2].label = ui_LabelPanelPanelSettingsFlashBody;
    
    // menu options
    settings_options[3].options = NULL;
    settings_options[3].option_count = 0;
    settings_options[3].current_option = 0;
    settings_options[3].label = ui_LabelPanelSettingsMenu;
    
    // initialize the current settings info
    current_settings.language = language_options[0];
    current_settings.resolution = resolution_options[0];
    current_settings.flash = flash_options[0];
}

static void init_settings_display(void) {
    for (int i = 0; i < 3; i++) {  // only update the first three settings items
        update_setting_display(i);
    }
}

static void app_extra_img_set_zoom(lv_obj_t * obj, uint16_t zoom)
{
    if (!obj) return;
    
    lv_img_t * img = (lv_img_t *)obj;
    if(zoom == img->zoom) return;

    if(zoom == 0) zoom = 1;

    lv_coord_t w = lv_obj_get_width(obj);
    lv_coord_t h = lv_obj_get_height(obj);
    lv_area_t a;
    _lv_img_buf_get_transformed_area(&a, w, h, img->angle, img->zoom >> 8, &img->pivot);
    a.x1 += obj->coords.x1 - 1;
    a.y1 += obj->coords.y1 - 1;
    a.x2 += obj->coords.x1 + 1;
    a.y2 += obj->coords.y1 + 1;
    lv_obj_invalidate_area(obj, &a);

    img->zoom = zoom;

    /* Disable invalidations because lv_obj_refresh_ext_draw_size would invalidate
     * the whole ext draw area */
    lv_disp_t * disp = lv_obj_get_disp(obj);
    lv_disp_enable_invalidation(disp, false);
    lv_obj_refresh_ext_draw_size(obj);
    lv_disp_enable_invalidation(disp, true);

    _lv_img_buf_get_transformed_area(&a, w, h, img->angle, img->zoom, &img->pivot);
    a.x1 += obj->coords.x1 - 1;
    a.y1 += obj->coords.y1 - 1;
    a.x2 += obj->coords.x1 + 1;
    a.y2 += obj->coords.y1 + 1;
    lv_obj_invalidate_area(obj, &a);
}

static lv_obj_t * create_img_button(lv_obj_t *parent, const void *img_src, const char *btn_text) {
    if (!parent || !img_src || !btn_text) {
        ESP_LOGW(TAG, "Invalid parameters for create_img_button");
        return NULL;
    }
    
    lv_obj_t * btn = lv_btn_create(parent);
    
    lv_obj_set_user_data(btn, (void *)btn_text);
    
    // Set the button style
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_shadow_spread(btn, 0, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_ofs_x(btn, 0, 0);
    lv_obj_set_style_shadow_ofs_y(btn, 0, 0);
    
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_height(btn, 30);

    // Create and configure the icon
    lv_obj_t * img = lv_img_create(btn);
    lv_img_set_src(img, img_src);
    lv_obj_set_size(img, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lv_img_set_zoom(img, IMG_BASE_ZOOM);
    lv_obj_refr_size(img);
    lv_img_set_size_mode(img, LV_IMG_SIZE_MODE_REAL);
    lv_obj_add_flag(img, LV_OBJ_FLAG_FLOATING);
    
    return btn;
}

static void scroll_event_cb(lv_event_t * e)
{
    is_scrolling = true; 
    lv_obj_t * cont = lv_event_get_target(e);
    lv_area_t cont_a;
    lv_obj_get_coords(cont, &cont_a);
    lv_coord_t cont_y_center = cont_a.y1 + lv_area_get_height(&cont_a) / 2;

    // Calculate radius for circular motion effect
    lv_coord_t r = lv_obj_get_height(cont) * 7 / 10;
    uint32_t child_cnt = lv_obj_get_child_cnt(cont);
    
    // Find the closest child to center
    lv_obj_t * closest_child = NULL;
    lv_coord_t min_diff = LV_COORD_MAX;
    
    // Process each child button
    for(uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t * child = lv_obj_get_child(cont, i);
        lv_area_t child_a;
        lv_obj_get_coords(child, &child_a);

        // Calculate vertical distance from center
        lv_coord_t child_y_center = child_a.y1 + lv_area_get_height(&child_a) / 2;
        lv_coord_t diff_y = LV_ABS(child_y_center - cont_y_center);
        
        if(diff_y < min_diff) {
            min_diff = diff_y;
            closest_child = child;
        }

        // Calculate horizontal offset using circular motion
        lv_sqrt_res_t sqrt_res;
        lv_sqrt(r * r - diff_y * diff_y, &sqrt_res, 0x8000);
        lv_coord_t x = (diff_y >= r) ? r : sqrt_res.i - r;

        // // Apply transformations
        lv_obj_set_style_translate_x(child, x, 0);
        lv_obj_set_style_opa(child, LV_OPA_COVER - lv_map(x, 0, r, LV_OPA_TRANSP, LV_OPA_COVER), 0);
        lv_obj_set_size(child, btn_width, btn_height);

        // Reset icon position
        lv_obj_t * img = lv_obj_get_child(child, 0);
        if(img) {
            app_extra_img_set_zoom(img, IMG_BASE_ZOOM);
            lv_obj_refr_size(img);
            lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
        }
    }
    
    // Apply special effects to closest child
    if(closest_child) {
        lv_obj_set_size(closest_child, btn_width * BTN_ZOOM_FACTOR, btn_height * BTN_ZOOM_FACTOR);
        lv_obj_t * img = lv_obj_get_child(closest_child, 0);
        if(img) {
            app_extra_img_set_zoom(img, IMG_BASE_ZOOM * IMG_ZOOM_FACTOR);
            lv_obj_refr_size(img);
            lv_obj_set_pos(img, IMG_ZOOM_OFFSET, 0);
        }
    }
}

static void scroll_end_event_cb(lv_event_t * e)
{
    lv_obj_t * cont = lv_event_get_target(e);
    
    // get the center coordinates of the container
    lv_area_t cont_a;
    lv_obj_get_coords(cont, &cont_a);
    lv_coord_t cont_y_center = cont_a.y1 + lv_area_get_height(&cont_a) / 2;
    
    // find the closest child to the center
    uint32_t child_cnt = lv_obj_get_child_cnt(cont);
    lv_obj_t * closest_child = NULL;
    lv_coord_t min_diff = LV_COORD_MAX;
    
    for(uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t * child = lv_obj_get_child(cont, i);
        lv_area_t child_a;
        lv_obj_get_coords(child, &child_a);
        
        lv_coord_t child_y_center = child_a.y1 + lv_area_get_height(&child_a) / 2;
        lv_coord_t diff_y = LV_ABS(child_y_center - cont_y_center);
        
        if(diff_y < min_diff) {
            min_diff = diff_y;
            closest_child = child;
        }
    }
    
    // after scroll end, apply the styles again
    if(closest_child) {
        selected_btn = closest_child;
        const char* btn_text = lv_obj_get_user_data(selected_btn);
        if (btn_text) {
            ESP_LOGD(TAG, "selected: %s", btn_text);

            if (info_label) {
                lv_label_set_text(info_label, btn_text);

                if(current_page == UI_PAGE_MAIN) {
                    lv_obj_clear_flag(info_label, LV_OBJ_FLAG_HIDDEN);
                }

                if(strcmp(btn_text, "CAMERA") == 0) {
                    lv_obj_align(info_label, LV_ALIGN_CENTER, -9, 50);
                } else if(strcmp(btn_text, "INTERVAL CAM") == 0) {
                    lv_obj_align(info_label, LV_ALIGN_CENTER, -12, 45);
                } else if(strcmp(btn_text, "VIDEO MODE") == 0) {
                    lv_obj_align(info_label, LV_ALIGN_CENTER, -9, 45);
                } else if(strcmp(btn_text, "ALBUM") == 0) {
                    lv_obj_align(info_label, LV_ALIGN_CENTER, -9, 55);
                } else if(strcmp(btn_text, "USB DISK") == 0) {
                    lv_obj_align(info_label, LV_ALIGN_CENTER, -7, 60);
                } else if(strcmp(btn_text, "SETTINGS") == 0) {
                    lv_obj_align(info_label, LV_ALIGN_CENTER, -9, 60);
                }
            }
        }

        // reset the styles of all children
        for(uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_t * child = lv_obj_get_child(cont, i);
            lv_obj_set_size(child, btn_width, btn_height);

            // reset the icon position to the default position
            lv_obj_t * img = lv_obj_get_child(child, 0);
            if(img) {
                lv_img_set_zoom(img, IMG_BASE_ZOOM);
                lv_obj_refr_size(img);
                lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
            }
        }
        
        // zoom the selected child
        lv_obj_set_size(closest_child, btn_width * BTN_ZOOM_FACTOR, btn_height * BTN_ZOOM_FACTOR);

        // set the special position for the center button
        lv_obj_t * img = lv_obj_get_child(closest_child, 0);
        if(img) {
            lv_img_set_zoom(img, IMG_BASE_ZOOM * IMG_ZOOM_FACTOR);
            lv_obj_refr_size(img);
            lv_obj_set_pos(img, IMG_ZOOM_OFFSET, 0);
        }
        
        // scroll to the view
        lv_obj_scroll_to_view(closest_child, LV_ANIM_ON);
    }

    is_scrolling = false; 
}

static void lv_scroll_create(void)
{
    // Create main container
    scroll_cont = lv_obj_create(ui_PanelCanvas);
    lv_obj_set_size(scroll_cont, 240, 240);
    lv_obj_align(scroll_cont, LV_ALIGN_CENTER, -65, 0);
    
    // Set container properties
    lv_obj_set_style_pad_row(scroll_cont, 10, 0);
    lv_obj_set_flex_flow(scroll_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_radius(scroll_cont, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(scroll_cont, true, 0);
    
    // Configure scrolling behavior
    lv_obj_set_scroll_dir(scroll_cont, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(scroll_cont, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(scroll_cont, LV_SCROLLBAR_MODE_OFF);
    
    // Add scroll event handlers
    lv_obj_add_event_cb(scroll_cont, scroll_event_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(scroll_cont, scroll_end_event_cb, LV_EVENT_SCROLL_END, NULL);

    // Set container visual style
    lv_obj_set_style_bg_opa(scroll_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(scroll_cont, LV_OPA_TRANSP, 0);

    info_label = lv_label_create(ui_PanelCanvas);
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(info_label, lv_color_hex(0x000000), 0);
    lv_obj_align(info_label, LV_ALIGN_CENTER, 3, 50);
    lv_label_set_text(info_label, "");  

    const char* btn_texts[] = {
        "CAMERA", "INTERVAL CAM", "VIDEO MODE", "ALBUM", 
        "USB DISK", "SETTINGS",
    };

    // define the image source for each button
    const void* img_srcs[] = {
        &ui_img_camera_big_png,
        &ui_img_interval_big_png,
        &ui_img_video_big_png,
        &ui_img_album_big_png,
        &ui_img_usb_big_png,
        &ui_img_settings_big_png
    };
    
    // Use a loop to create all buttons
    lv_obj_t *btn = NULL;
    for (int i = 0; i < sizeof(btn_texts)/sizeof(btn_texts[0]); i++) {
        btn = create_img_button(
            scroll_cont,
            img_srcs[i],
            btn_texts[i]
        );
    }
        
    lv_obj_update_layout(btn);
    btn_width = lv_obj_get_width(btn);
    btn_height = lv_obj_get_height(btn);

    // Initialize scroll position
    lv_event_send(scroll_cont, LV_EVENT_SCROLL, NULL);
    lv_obj_scroll_to_view(lv_obj_get_child(scroll_cont, 0), LV_ANIM_OFF);

    const char* initial_text = lv_obj_get_user_data(lv_obj_get_child(scroll_cont, 0));
    if (initial_text) {
        lv_label_set_text(info_label, initial_text);
    }
}

void ui_extra_clear_page(void)
{
    lv_obj_add_flag(ui_ImageCanvasSelect, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_add_flag(ui_ImageCanvasUp, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_add_flag(ui_ImageCanvasDown, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_add_flag(ui_PanelCanvasMaskLarge, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_add_flag(ui_PanelCanvasPopupCamera, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_add_flag(ui_ImageCanvasNOSDcard, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_add_flag(ui_ImageCanvasSDcard, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_add_flag(ui_ImageCanvasMenu, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_add_flag(ui_LabelCanvas2X, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_add_flag(ui_LabelCanvas3X, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_add_flag(ui_LabelCanvasFactor, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_add_flag(ui_PanelCanvasMaskCamera, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_add_flag(ui_PanelCanvasPopupCameraInterval, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_add_flag(ui_PanelCanvasMaskCameraInterval, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_add_flag(ui_LabelCanvas5mplus, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_add_flag(ui_LabelCanvas5mSub, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_add_flag(ui_LabelCanvasInvervalTime, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_add_flag(ui_PanelCanvasPopupVideoMode, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_add_flag(ui_PanelCanvasMaskVideoMode, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_add_flag(ui_PanelCanvasPopupSDWarning, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_add_flag(ui_PanelCanvasPopupIntervalTimerWarning, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_add_flag(ui_PanelCanvasPopupIntervalTimerWarningEnd, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_add_flag(ui_ImageRedDot, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_add_flag(ui_LabelRedDotTime, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_add_flag(ui_PanelSettings, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_add_flag(ui_PanelSettingsMenu, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_add_flag(scroll_cont, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_add_flag(info_label, LV_OBJ_FLAG_HIDDEN);     /// Flags
}

static void pop_up_timer_callback(lv_timer_t * timer)
{
    if(timer->user_data == ui_PanelCanvasPopupCamera) {
        lv_obj_add_flag(ui_PanelCanvasPopupCamera, LV_OBJ_FLAG_HIDDEN);
    
        lv_obj_clear_flag(ui_PanelCanvasMaskCamera, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_LabelCanvasFactor, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_LabelCanvas2X, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_LabelCanvas3X, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_ImageCanvasMenu, LV_OBJ_FLAG_HIDDEN);
        
        is_sd_card_mounted ? lv_obj_clear_flag(ui_ImageCanvasSDcard, LV_OBJ_FLAG_HIDDEN) : lv_obj_clear_flag(ui_ImageCanvasNOSDcard, LV_OBJ_FLAG_HIDDEN);

    } else if(timer->user_data == ui_PanelCanvasPopupCameraInterval) {
        lv_obj_add_flag(ui_PanelCanvasPopupCameraInterval, LV_OBJ_FLAG_HIDDEN);

        lv_obj_clear_flag(ui_PanelCanvasMaskCameraInterval, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_LabelCanvasFactor, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_LabelCanvas5mplus, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_LabelCanvas5mSub, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_ImageCanvasMenu, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_LabelCanvasInvervalTime, LV_OBJ_FLAG_HIDDEN);

        is_sd_card_mounted ? lv_obj_clear_flag(ui_ImageCanvasSDcard, LV_OBJ_FLAG_HIDDEN) : lv_obj_clear_flag(ui_ImageCanvasNOSDcard, LV_OBJ_FLAG_HIDDEN);

    } else if(timer->user_data == ui_PanelCanvasPopupVideoMode) {
        lv_obj_add_flag(ui_PanelCanvasPopupVideoMode, LV_OBJ_FLAG_HIDDEN);

        lv_obj_clear_flag(ui_PanelCanvasMaskVideoMode, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_ImageCanvasMenu, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_LabelCanvas2X, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_LabelCanvas3X, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_LabelCanvasFactor, LV_OBJ_FLAG_HIDDEN);

        is_sd_card_mounted ? lv_obj_clear_flag(ui_ImageCanvasSDcard, LV_OBJ_FLAG_HIDDEN) : lv_obj_clear_flag(ui_ImageCanvasNOSDcard, LV_OBJ_FLAG_HIDDEN);
    }

    if(lv_popup_timer){
        lv_timer_del(lv_popup_timer);
        lv_popup_timer = NULL;
    }
}

static void pop_up_additional_photo_callback(lv_timer_t * timer)
{
    if(timer->user_data == ui_PanelCanvasPopupIntervalTimerWarning) {
        lv_obj_add_flag(ui_PanelCanvasPopupIntervalTimerWarning, LV_OBJ_FLAG_HIDDEN);

        app_video_stream_start_interval_photo(interval_time);
    } else if (timer->user_data == ui_PanelCanvasPopupIntervalTimerWarningEnd) {
        lv_obj_add_flag(ui_PanelCanvasPopupIntervalTimerWarningEnd, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_clear_flag(ui_PanelCanvasMaskCameraInterval, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_LabelCanvasFactor, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_LabelCanvas5mplus, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_LabelCanvas5mSub, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_ImageCanvasMenu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_LabelCanvasInvervalTime, LV_OBJ_FLAG_HIDDEN);

    is_sd_card_mounted ? lv_obj_clear_flag(ui_ImageCanvasSDcard, LV_OBJ_FLAG_HIDDEN) : lv_obj_clear_flag(ui_ImageCanvasNOSDcard, LV_OBJ_FLAG_HIDDEN);
    
    if(lv_additional_photo_timer){
        lv_timer_del(lv_additional_photo_timer);
        lv_additional_photo_timer = NULL;
    }
}

static void update_settings_focus(int new_item)
{
    // Clear the focus of all settings items
    for (int i = 0; i < 4; i++) {
        lv_event_send(settings_items[i], LV_EVENT_DEFOCUSED, NULL);
    }
    
    // Set the focus of the new selected item
    current_settings_item = new_item;
    lv_event_send(settings_items[current_settings_item], LV_EVENT_FOCUSED, NULL);
    ESP_LOGD(TAG, "Settings: selected item %d", current_settings_item);
}

// Redirect to page functions
static void ui_extra_redirect_to_main_page(void)
{
    current_page = UI_PAGE_MAIN;

    ui_extra_clear_page();

    lv_obj_clear_flag(scroll_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(info_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_ImageCanvasSelect, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_ImageCanvasUp, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_ImageCanvasDown, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_PanelCanvasMaskLarge, LV_OBJ_FLAG_HIDDEN);

    _ui_screen_change(&ui_ScreenCamera, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_ScreenCamera_screen_init);
}

static void ui_extra_redirect_to_camera_page(void)
{
    current_page = UI_PAGE_CAMERA;

    ui_extra_clear_page();

    lv_obj_clear_flag(ui_PanelCanvasPopupCamera, LV_OBJ_FLAG_HIDDEN);
    if(!lv_popup_timer){
        lv_popup_timer = lv_timer_create(pop_up_timer_callback, 5000, ui_PanelCanvasPopupCamera);
    }
}

static void ui_extra_redirect_to_interval_camera_page(void)
{
    current_page = UI_PAGE_INTERVAL_CAM;

    ui_extra_clear_page();

    lv_obj_clear_flag(ui_PanelCanvasPopupCameraInterval, LV_OBJ_FLAG_HIDDEN);
    if(!lv_popup_timer){
        lv_popup_timer = lv_timer_create(pop_up_timer_callback, 5000, ui_PanelCanvasPopupCameraInterval);
    }
}

static void ui_extra_redirect_to_video_mode_page(void)
{
    current_page = UI_PAGE_VIDEO_MODE;

    ui_extra_clear_page();
    
    lv_obj_clear_flag(ui_PanelCanvasPopupVideoMode, LV_OBJ_FLAG_HIDDEN);
    if(!lv_popup_timer){
        lv_popup_timer = lv_timer_create(pop_up_timer_callback, 5000, ui_PanelCanvasPopupVideoMode);
    }
}

static void ui_extra_redirect_to_album_page(void)
{
    current_page = UI_PAGE_ALBUM;

    ui_extra_clear_page();
    
    _ui_screen_change(&ui_ScreenAlbum, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_ScreenAlbum_screen_init);
}

static void ui_extra_redirect_to_usb_disk_page(void)
{
    current_page = UI_PAGE_USB_DISK;

    ui_extra_clear_page();
    
    _ui_screen_change(&ui_ScreenUSB, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_ScreenUSB_screen_init);

    if(is_usb_disk_mounted) {
        lv_obj_add_flag(ui_ImageScreenUSB, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_ImageScreenUSBSuccess, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(ui_ImageScreenUSB, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_ImageScreenUSBSuccess, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_extra_redirect_to_settings_page(void)
{
    current_page = UI_PAGE_SETTINGS;

    ui_extra_clear_page();
    
    lv_obj_clear_flag(ui_PanelSettings, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_PanelSettingsMenu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_ImageCanvasSelect, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_ImageCanvasUp, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_ImageCanvasDown, LV_OBJ_FLAG_HIDDEN);

    // Initialize settings items
    settings_items[0] = ui_PanelPanelSettingsLanguage;
    settings_items[1] = ui_PanelPanelSettingsRes;
    settings_items[2] = ui_PanelPanelSettingsFlash;
    settings_items[3] = ui_PanelSettingsMenu;
    
    // Initialize the settings display
    init_settings_display();
    
    // reset the current selected item and focus the first item
    current_settings_item = 0;
    update_settings_focus(current_settings_item);
}

static void ui_extra_clear_popup_window(void)
{
    if(!lv_obj_has_flag(ui_PanelCanvasPopupCamera, LV_OBJ_FLAG_HIDDEN) || 
       !lv_obj_has_flag(ui_PanelCanvasPopupCameraInterval, LV_OBJ_FLAG_HIDDEN) ||
       !lv_obj_has_flag(ui_PanelCanvasPopupVideoMode, LV_OBJ_FLAG_HIDDEN) ||
       !lv_obj_has_flag(ui_PanelCanvasPopupSDWarning, LV_OBJ_FLAG_HIDDEN) ||
       !lv_obj_has_flag(ui_PanelCanvasPopupIntervalTimerWarning, LV_OBJ_FLAG_HIDDEN) ||
       !lv_obj_has_flag(ui_PanelCanvasPopupIntervalTimerWarningEnd, LV_OBJ_FLAG_HIDDEN) ||
       !lv_obj_has_flag(ui_PanelCanvasPopupSDWarning, LV_OBJ_FLAG_HIDDEN)) {

            if(lv_popup_timer) {
                lv_timer_ready(lv_popup_timer);
            }

            if(lv_additional_photo_timer) {
                lv_timer_ready(lv_additional_photo_timer);
            }

            lv_obj_add_flag(ui_PanelCanvasPopupCamera, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_PanelCanvasPopupCameraInterval, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_PanelCanvasPopupVideoMode, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_PanelCanvasPopupSDWarning, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_PanelCanvasPopupIntervalTimerWarning, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_PanelCanvasPopupIntervalTimerWarningEnd, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_PanelCanvasPopupSDWarning, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_extra_popup_interval_timer_warning(void)
{
    if(!(current_page == UI_PAGE_INTERVAL_CAM)) {
        return;
    }
    app_storage_get_photo_count(&saved_photo_count);
    ui_extra_clear_page();
    lv_label_set_text_fmt(ui_LabelPanelCanvasPopupIntervalTimerEnd, "Ended %d min", interval_time);
    lv_label_set_text_fmt(ui_LabelPanelCanvasPopupCameraIntervalTimerWarningEnd, "%d photos saved to \n       SD Card", saved_photo_count);
    lv_obj_clear_flag(ui_PanelCanvasPopupIntervalTimerWarningEnd, LV_OBJ_FLAG_HIDDEN);

    if(!lv_additional_photo_timer){
        lv_additional_photo_timer = lv_timer_create(pop_up_additional_photo_callback, 5000, ui_PanelCanvasPopupIntervalTimerWarningEnd);
    }
}

void ui_extra_goto_page(ui_page_t page)
{
    // save the current page
    current_page = page;
    
    // redirect to the page
    switch(page) {
        case UI_PAGE_MAIN:
            ui_extra_redirect_to_main_page();
            break;
        case UI_PAGE_CAMERA:
            ui_extra_redirect_to_camera_page();
            break;
        case UI_PAGE_INTERVAL_CAM:
            ui_extra_redirect_to_interval_camera_page();
            break;
        case UI_PAGE_VIDEO_MODE:
            ui_extra_redirect_to_video_mode_page();
            break;
        case UI_PAGE_ALBUM:
            app_album_refresh();
            ui_extra_redirect_to_album_page();
            break;
        case UI_PAGE_USB_DISK:
            ui_extra_redirect_to_usb_disk_page();
            break;
        case UI_PAGE_SETTINGS:
            ui_extra_redirect_to_settings_page();
            break;
        default:
            ui_extra_redirect_to_main_page();
            break;
    }   
}

// Set and get functions
ui_page_t ui_extra_get_current_page(void)
{
    return current_page;
}

ui_page_t ui_extra_get_choosed_page(void)
{
    const char * user_data = lv_obj_get_user_data(selected_btn);

    for (int i = 0; page_map[i].name != NULL; i++) {
        if (strcmp(user_data, page_map[i].name) == 0) {
            return page_map[i].page;
        }
    }

    return UI_PAGE_MAIN;
}

settings_info_t* ui_extra_get_settings(void)
{
    return &current_settings;
}

void app_extra_set_magnification_factor(uint16_t factor)
{
    if(factor > MAX_MAGNIFICATION_FACTOR) {
        factor = MAX_MAGNIFICATION_FACTOR;
    } else if(factor < MIN_MAGNIFICATION_FACTOR) {
        factor = MIN_MAGNIFICATION_FACTOR;
    }

    magnification_factor = factor;
    
    lv_label_set_text_fmt(ui_LabelCanvasFactor, "%dX", magnification_factor);

    save_current_settings();
}

uint16_t app_extra_get_magnification_factor(void)
{
    return magnification_factor;
}   

void app_extra_set_saved_photo_count(uint16_t count)
{
    saved_photo_count = count;
    app_storage_save_photo_count(saved_photo_count);
}

uint16_t app_extra_get_saved_photo_count(void)
{
    return saved_photo_count;
}

void app_extra_set_interval_time(uint16_t time)
{
    // Limit time range
    if(time > MAX_INTERVAL_TIME) {
        time = MIN_INTERVAL_TIME;
    } else if(time < MIN_INTERVAL_TIME) {
        time = MAX_INTERVAL_TIME;
    }

    interval_time = time;

    lv_label_set_text_fmt(ui_LabelCanvasInvervalTime, "%dmin", interval_time);

    save_current_settings();
}

uint16_t app_extra_get_interval_time(void)
{
    return interval_time;
}

void ui_extra_set_sd_card_mounted(bool mounted)
{
    is_sd_card_mounted = mounted;
    if(is_sd_card_mounted && (current_page == UI_PAGE_CAMERA || current_page == UI_PAGE_INTERVAL_CAM || current_page == UI_PAGE_VIDEO_MODE)) {
        lv_obj_add_flag(ui_ImageCanvasNOSDcard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_ImageCanvasSDcard, LV_OBJ_FLAG_HIDDEN);
    }
}

bool ui_extra_get_sd_card_mounted(void)
{
    return is_sd_card_mounted;
}

void ui_extra_set_usb_disk_mounted(bool mounted)
{
    is_usb_disk_mounted = mounted;
    if(current_page == UI_PAGE_MAIN || current_page == UI_PAGE_USB_DISK) {
        ui_extra_goto_page(UI_PAGE_USB_DISK);
    }
}

bool ui_extra_get_usb_disk_mounted(void)
{
    return is_usb_disk_mounted;
}

bool ui_extra_get_popup_window_visible(void)
{
    if(!lv_obj_has_flag(ui_PanelCanvasPopupCamera, LV_OBJ_FLAG_HIDDEN) || 
       !lv_obj_has_flag(ui_PanelCanvasPopupCameraInterval, LV_OBJ_FLAG_HIDDEN) ||
       !lv_obj_has_flag(ui_PanelCanvasPopupVideoMode, LV_OBJ_FLAG_HIDDEN) ||
       !lv_obj_has_flag(ui_PanelCanvasPopupSDWarning, LV_OBJ_FLAG_HIDDEN) ||
       !lv_obj_has_flag(ui_PanelCanvasPopupIntervalTimerWarning, LV_OBJ_FLAG_HIDDEN) ||
       !lv_obj_has_flag(ui_PanelCanvasPopupIntervalTimerWarningEnd, LV_OBJ_FLAG_HIDDEN)) {
        
        return true;
    }

    return false;
}

void ui_extra_start_interval_timer(void)
{
    ui_extra_clear_page();
    lv_label_set_text_fmt(ui_LabelPanelCanvasPopupIntervalTimer, "Starting %d min", interval_time);
    lv_obj_clear_flag(ui_PanelCanvasPopupIntervalTimerWarning, LV_OBJ_FLAG_HIDDEN);
}

static void ui_extra_focus_on_picture_delete(void)
{
    if(lv_obj_has_state(ui_ButtonPanelImageScreenAlbumDeleteYES, LV_STATE_FOCUSED)) {
        lv_obj_add_state(ui_ButtonPanelImageScreenAlbumDeleteNO, LV_STATE_FOCUSED);
        lv_obj_clear_state(ui_ButtonPanelImageScreenAlbumDeleteYES, LV_STATE_FOCUSED);
    } else {
        lv_obj_clear_state(ui_ButtonPanelImageScreenAlbumDeleteNO, LV_STATE_FOCUSED);
        lv_obj_add_state(ui_ButtonPanelImageScreenAlbumDeleteYES, LV_STATE_FOCUSED);
    }
}

void ui_extra_popup_picture_delete_warning(void)
{
    lv_obj_clear_flag(ui_PanelImageScreenAlbumDelete, LV_OBJ_FLAG_HIDDEN);
    ui_extra_focus_on_picture_delete();
}

void ui_extra_popup_picture_delete_success(void)
{
    lv_obj_add_flag(ui_PanelImageScreenAlbumDelete, LV_OBJ_FLAG_HIDDEN);
}

// Button event handler
void ui_extra_btn_up(void)
{
    switch(current_page) {
        case UI_PAGE_MAIN:
            lv_obj_scroll_by(scroll_cont, 0, 40, LV_ANIM_ON);
            lv_event_send(scroll_cont, LV_EVENT_SCROLL, NULL);
            lv_obj_add_flag(info_label, LV_OBJ_FLAG_HIDDEN);
            break;
            
        case UI_PAGE_SETTINGS:
            if(current_settings_item > 0) {
                update_settings_focus(current_settings_item - 1);
            }
            break;
            
        case UI_PAGE_CAMERA:
        case UI_PAGE_VIDEO_MODE:
            app_extra_set_magnification_factor(2);
            break;
            
        case UI_PAGE_INTERVAL_CAM:
            app_extra_set_interval_time(interval_time + INTERVAL_TIME_STEP);
            break;

        case UI_PAGE_ALBUM:
            if(!lv_obj_has_flag(ui_PanelImageScreenAlbumDelete, LV_OBJ_FLAG_HIDDEN)) {
                ui_extra_focus_on_picture_delete();
            }
            break;
            
        default:
            break;
    }
}

void ui_extra_btn_down(void)
{
    switch(current_page) {
        case UI_PAGE_MAIN:
            lv_obj_scroll_by(scroll_cont, 0, -40, LV_ANIM_ON);
            lv_event_send(scroll_cont, LV_EVENT_SCROLL, NULL);
            lv_obj_add_flag(info_label, LV_OBJ_FLAG_HIDDEN);
            break;
            
        case UI_PAGE_SETTINGS:
            if(current_settings_item < 3) {
                update_settings_focus(current_settings_item + 1);
            }
            break;
            
        case UI_PAGE_CAMERA:
        case UI_PAGE_VIDEO_MODE:
            app_extra_set_magnification_factor(3);
            break;
            
        case UI_PAGE_INTERVAL_CAM:
            app_extra_set_interval_time(interval_time - INTERVAL_TIME_STEP);
            break;

        case UI_PAGE_ALBUM:
            if(!lv_obj_has_flag(ui_PanelImageScreenAlbumDelete, LV_OBJ_FLAG_HIDDEN)) {
                ui_extra_focus_on_picture_delete();
            }
            break;
            
        default:
            break;
    }
}

void ui_extra_btn_menu(void)
{
    // Check if there are any popup windows that need to be cleared
    if(!lv_obj_has_flag(ui_PanelCanvasPopupCamera, LV_OBJ_FLAG_HIDDEN) || 
       !lv_obj_has_flag(ui_PanelCanvasPopupCameraInterval, LV_OBJ_FLAG_HIDDEN) ||
       !lv_obj_has_flag(ui_PanelCanvasPopupVideoMode, LV_OBJ_FLAG_HIDDEN) ||
       !lv_obj_has_flag(ui_PanelCanvasPopupSDWarning, LV_OBJ_FLAG_HIDDEN) ||
       !lv_obj_has_flag(ui_PanelCanvasPopupIntervalTimerWarning, LV_OBJ_FLAG_HIDDEN) ||
       !lv_obj_has_flag(ui_PanelCanvasPopupIntervalTimerWarningEnd, LV_OBJ_FLAG_HIDDEN)) {
        
        ui_extra_clear_popup_window();
        return;
    }

    // Ignore selection during scroll
    if (is_scrolling && current_page == UI_PAGE_MAIN) {
        ESP_LOGI(TAG, "Ignoring selection during scroll");
        return;
    }

    // Perform different actions based on current page
    switch(current_page) {
        case UI_PAGE_MAIN:
            ui_extra_goto_page(ui_extra_get_choosed_page());
            break;
            
        case UI_PAGE_SETTINGS:
            if(current_settings_item == 3 && settings_items[current_settings_item] == ui_PanelSettingsMenu) {
                // If current setting item is menu item, return to main page
                ui_extra_goto_page(UI_PAGE_MAIN);

                if(strcmp(current_settings.flash, "On") == 0) {
                    app_video_stream_set_flash_light(true);
                } else {
                    app_video_stream_set_flash_light(false);
                }

                app_video_stream_set_photo_resolution_by_string(current_settings.resolution);
            } else {
                // Otherwise, cycle through options
                setting_options_t* opt = &settings_options[current_settings_item];
                opt->current_option = (opt->current_option + 1) % opt->option_count;
                update_setting_display(current_settings_item);
            }
            break;
            
        case UI_PAGE_ALBUM:
            if(!lv_obj_has_flag(ui_PanelImageScreenAlbumDelete, LV_OBJ_FLAG_HIDDEN)) {
                if(lv_obj_has_state(ui_ButtonPanelImageScreenAlbumDeleteYES, LV_STATE_FOCUSED)) {
                    app_album_delete_current_image();
                    ui_extra_popup_picture_delete_success();
                } else {
                    ui_extra_popup_picture_delete_success();
                }
            } else {
                ui_extra_goto_page(UI_PAGE_MAIN);
            }
            break;
            
        default:
            // For other pages, return to main page
            ui_extra_goto_page(UI_PAGE_MAIN);
            break;
    }
}

void ui_extra_btn_encoder(void)
{
    if(!lv_obj_has_flag(ui_PanelCanvasPopupCamera, LV_OBJ_FLAG_HIDDEN) || 
       !lv_obj_has_flag(ui_PanelCanvasPopupCameraInterval, LV_OBJ_FLAG_HIDDEN) ||
       !lv_obj_has_flag(ui_PanelCanvasPopupVideoMode, LV_OBJ_FLAG_HIDDEN) ||
       !lv_obj_has_flag(ui_PanelCanvasPopupIntervalTimerWarning, LV_OBJ_FLAG_HIDDEN) ||
       !lv_obj_has_flag(ui_PanelCanvasPopupIntervalTimerWarningEnd, LV_OBJ_FLAG_HIDDEN)) {

        ui_extra_clear_popup_window();
        return;
    }

    if(!lv_obj_has_flag(ui_PanelCanvasPopupSDWarning, LV_OBJ_FLAG_HIDDEN)) {
        switch(current_page) {
            case UI_PAGE_CAMERA:
                ui_extra_goto_page(UI_PAGE_CAMERA);
                break;
            case UI_PAGE_INTERVAL_CAM:
                ui_extra_goto_page(UI_PAGE_INTERVAL_CAM);
                break;
            case UI_PAGE_VIDEO_MODE:
                ui_extra_goto_page(UI_PAGE_VIDEO_MODE);
                break;
            default:
                break;
        }
       
        ui_extra_clear_popup_window();
        return;
    }

    if(!lv_obj_has_flag(ui_PanelCanvasPopupIntervalTimerWarning, LV_OBJ_FLAG_HIDDEN) || 
       !lv_obj_has_flag(ui_PanelCanvasPopupIntervalTimerWarningEnd, LV_OBJ_FLAG_HIDDEN)) {
        
        ui_extra_goto_page(UI_PAGE_INTERVAL_CAM);
        ui_extra_clear_popup_window();
        return;
    }

    if(!is_sd_card_mounted) {
        switch(current_page) {
            case UI_PAGE_CAMERA:
            case UI_PAGE_INTERVAL_CAM:
            case UI_PAGE_VIDEO_MODE:
                ui_extra_clear_page();
                lv_obj_clear_flag(ui_PanelCanvasPopupSDWarning, LV_OBJ_FLAG_HIDDEN);
                break;
            case UI_PAGE_MAIN:
                ui_extra_btn_menu();
                break;
            default:
                break;
        }
        return;
    }

    // Ignore selection during scroll
    if (is_scrolling && current_page == UI_PAGE_MAIN) {
        ESP_LOGI(TAG, "Ignoring selection during scroll");
        return;
    }

    switch(current_page) {
        case UI_PAGE_MAIN:
            ui_extra_btn_menu();
            break;
        case UI_PAGE_INTERVAL_CAM:
            ui_extra_start_interval_timer();
            
            if(!lv_additional_photo_timer){
                lv_additional_photo_timer = lv_timer_create(pop_up_additional_photo_callback, 7000, ui_PanelCanvasPopupIntervalTimerWarning);
            }
            break;
        case UI_PAGE_CAMERA:
            app_video_stream_take_photo();
            break;
        case UI_PAGE_ALBUM:
            if(lv_obj_has_flag(ui_PanelImageScreenAlbumDelete, LV_OBJ_FLAG_HIDDEN)) {
                ui_extra_popup_picture_delete_warning();
                ui_extra_focus_on_picture_delete();
            } else {
                if(lv_obj_has_state(ui_ButtonPanelImageScreenAlbumDeleteYES, LV_STATE_FOCUSED)) {
                    app_album_delete_current_image();
                    ui_extra_popup_picture_delete_success();
                } else {
                    ui_extra_popup_picture_delete_success();
                }
            }
            break;
        default:
            break;
    }

    return;
}

// Initialize the UI extra module
void ui_extra_init(void)
{
    ui_init();

    init_settings_options();

    lv_scroll_create();

    // Load settings from NVS
    settings_info_t settings;
    uint16_t loaded_interval_time;
    uint16_t loaded_magnification;
    
    // Set default values
    settings.language = language_options[0];
    settings.resolution = resolution_options[0];
    settings.flash = flash_options[0];
    loaded_interval_time = DEFAULT_INTERVAL_TIME;
    loaded_magnification = DEFAULT_MAGNIFICATION_FACTOR;
    
    // Load settings from NVS
    esp_err_t err = app_storage_load_settings(&settings, &loaded_interval_time, &loaded_magnification);
    ESP_LOGD(TAG, "loaded_interval_time: %d, loaded_magnification: %d", loaded_interval_time, loaded_magnification);
    if (err == ESP_OK) {
        // Apply loaded settings
        // Update language settings
        for (int i = 0; i < settings_options[0].option_count; i++) {
            if (strcmp(settings.language, settings_options[0].options[i]) == 0) {
                settings_options[0].current_option = i;
                break;
            }
        }
        
        // Update resolution settings
        for (int i = 0; i < settings_options[1].option_count; i++) {
            if (strcmp(settings.resolution, settings_options[1].options[i]) == 0) {
                settings_options[1].current_option = i;
                break;
            }
        }
        
        // Update flash settings
        for (int i = 0; i < settings_options[2].option_count; i++) {
            if (strcmp(settings.flash, settings_options[2].options[i]) == 0) {
                settings_options[2].current_option = i;
                break;
            }
        }
        
        // Update current settings
        current_settings.language = settings.language;
        current_settings.resolution = settings.resolution;
        current_settings.flash = settings.flash;

        if(strcmp(current_settings.flash, "On") == 0) {
            app_video_stream_set_flash_light(true);
        } else {
            app_video_stream_set_flash_light(false);
        }

        app_video_stream_set_photo_resolution_by_string(current_settings.resolution);
        
        // Update interval time and magnification
        interval_time = loaded_interval_time;
        magnification_factor = loaded_magnification;
        
        // Update the display
        lv_label_set_text_fmt(ui_LabelCanvasFactor, "%dX", magnification_factor);
        lv_label_set_text_fmt(ui_LabelCanvasInvervalTime, "%dmin", interval_time);

        // Update display
        init_settings_display();
    }

    // redirect to the main page
    ui_extra_goto_page(UI_PAGE_MAIN);
}