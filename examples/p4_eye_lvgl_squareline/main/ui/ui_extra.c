#include <stdio.h>
#include "esp_log.h"
#include "lvgl.h"

#include "ui.h"

#define ZOOM_FACTOR 2.3
#define IMG_ZOOM_FACTOR 2.4
#define ZOOM_OFFSET -110

static const char * TAG = "ui_extra";

lv_obj_t * scroll_cont = NULL;
lv_coord_t btn_width = 0;
lv_coord_t btn_height = 0;
lv_obj_t * selected_btn = NULL;  
lv_obj_t * info_label = NULL;  

lv_obj_t * create_img_button(lv_obj_t *parent, const void *img_src, const char *btn_text) {
    lv_obj_t * btn = lv_btn_create(parent);
    
    lv_obj_set_user_data(btn, (void *)btn_text);
    
    // 设置按钮样式 - 所有属性透明
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_shadow_spread(btn, 0, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_ofs_x(btn, 0, 0);
    lv_obj_set_style_shadow_ofs_y(btn, 0, 0);
    
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_height(btn, 40);

    // 创建并配置图标
    lv_obj_t * img = lv_img_create(btn);
    lv_img_set_src(img, img_src);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lv_img_set_zoom(img, 60);
    lv_img_set_size_mode(img, LV_IMG_SIZE_MODE_REAL);
    lv_obj_add_flag(img, LV_OBJ_FLAG_FLOATING);
    
    return btn;
}

static void scroll_event_cb(lv_event_t * e)
{
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

        // Apply transformations
        lv_obj_set_style_translate_x(child, x, 0);
        lv_obj_set_style_opa(child, LV_OPA_COVER - lv_map(x, 0, r, LV_OPA_TRANSP, LV_OPA_COVER), 0);
        // lv_obj_set_style_transform_zoom(child, 256, 0);
        lv_obj_set_size(child, btn_width, btn_height);

        // Reset icon position
        lv_obj_t * img = lv_obj_get_child(child, 0);
        if(img) {
            // lv_obj_invalidate(img);
            lv_img_cache_invalidate_src(lv_img_get_src(img));
            lv_obj_set_style_transform_zoom(img, 256, 0);
            lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
        }
    }
    
    // Apply special effects to closest child
    if(closest_child) {
        // lv_obj_set_style_transform_zoom(closest_child, 256 * ZOOM_FACTOR, 0);
        lv_obj_set_size(closest_child, btn_width * ZOOM_FACTOR, btn_height * ZOOM_FACTOR);
        lv_obj_t * img = lv_obj_get_child(closest_child, 0);
        if(img) {
            // lv_obj_invalidate(img);
            lv_img_cache_invalidate_src(lv_img_get_src(img));
            // lv_obj_set_size(img, btn_width * ZOOM_FACTOR, btn_height * ZOOM_FACTOR);
            lv_obj_set_style_transform_zoom(img, 256 * IMG_ZOOM_FACTOR, 0);
            lv_obj_set_pos(img, ZOOM_OFFSET, -25);
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
            ESP_LOGI(TAG, "selected: %s", btn_text);

            if (info_label) {
                lv_label_set_text(info_label, btn_text);
                lv_obj_clear_flag(info_label, LV_OBJ_FLAG_HIDDEN);

                if(strcmp(btn_text, "CAMERA") == 0) {
                    lv_obj_align(info_label, LV_ALIGN_CENTER, 6, 50);
                } else if(strcmp(btn_text, "INTERVAL CAM") == 0) {
                    lv_obj_align(info_label, LV_ALIGN_CENTER, 3, 50);
                } else if(strcmp(btn_text, "VIDEO MODE") == 0) {
                    lv_obj_align(info_label, LV_ALIGN_CENTER, 6, 45);
                } else if(strcmp(btn_text, "ALBUM") == 0) {
                    lv_obj_align(info_label, LV_ALIGN_CENTER, 6, 55);
                } else if(strcmp(btn_text, "USB DISK") == 0) {
                    lv_obj_align(info_label, LV_ALIGN_CENTER, 6, 60);
                } else if(strcmp(btn_text, "SETTINGS") == 0) {
                    lv_obj_align(info_label, LV_ALIGN_CENTER, 6, 60);
                }
            }
        }

        // reset the styles of all children
        for(uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_t * child = lv_obj_get_child(cont, i);
            // lv_obj_set_style_transform_zoom(child, 256, 0);  // reset to normal size
            lv_obj_set_size(child, btn_width, btn_height);

            // reset the icon position to the default position
            lv_obj_t * img = lv_obj_get_child(child, 0);
            if(img) {
                // lv_obj_invalidate(img);
                lv_img_cache_invalidate_src(lv_img_get_src(img));
                lv_obj_set_style_transform_zoom(img, 256, 0);
                lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
            }
        }
        
        // zoom the selected child
        // lv_obj_set_style_transform_zoom(closest_child, 256 * ZOOM_FACTOR, 0);  // zoom to 140%
        lv_obj_set_size(closest_child, btn_width * ZOOM_FACTOR, btn_height * ZOOM_FACTOR);

        // set the special position for the center button
        lv_obj_t * img = lv_obj_get_child(closest_child, 0);
        if(img) {
            // lv_obj_invalidate(img);
            lv_img_cache_invalidate_src(lv_img_get_src(img));
            lv_obj_set_style_transform_zoom(img, 256 * IMG_ZOOM_FACTOR, 0);
            lv_obj_set_pos(img, ZOOM_OFFSET, -25);
        }
        
        // scroll to the view
        lv_obj_scroll_to_view(closest_child, LV_ANIM_ON);
    }
}

void lv_scroll_create(void)
{
    // Create main container
    scroll_cont = lv_obj_create(ui_PanelCanvas);
    lv_obj_set_size(scroll_cont, 240, 240);
    lv_obj_set_pos(scroll_cont, -50, 0);
    
    // Set container properties
    lv_obj_set_style_pad_row(scroll_cont, 0, 0);
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

    info_label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(info_label, lv_color_hex(0x000000), 0);
    lv_obj_align(info_label, LV_ALIGN_CENTER, 3, 50);
    lv_label_set_text(info_label, "");  

    const char* btn_texts[] = {
        "CAMERA", "INTERVAL CAM", "VIDEO MODE", "ALBUM", 
        "USB DISK", "SETTINGS",
    };

    lv_obj_t * btn = create_img_button(
            scroll_cont, 
            &ui_img_camera_big_png,
            btn_texts[0]
        );

    btn = create_img_button(
            scroll_cont, 
            &ui_img_interval_big_png,
            btn_texts[1]
        );

    btn = create_img_button(
            scroll_cont, 
            &ui_img_video_big_png,
            btn_texts[2]
        );

    btn = create_img_button(
            scroll_cont, 
            &ui_img_album_big_png,
            btn_texts[3]
        );

    btn = create_img_button(
            scroll_cont, 
            &ui_img_usb_big_png,
            btn_texts[4]
        );

    btn = create_img_button(
            scroll_cont, 
            &ui_img_settings_big_png,
            btn_texts[5]
        );
        
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

void ui_extra_init(void)
{
    ui_init();

    lv_scroll_create();

    lv_obj_clear_flag(ui_ImageCanvasSelect, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_ImageCanvasUp, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_ImageCanvasDown, LV_OBJ_FLAG_HIDDEN);

    lv_obj_clear_flag(ui_PanelCanvasMaskLarge, LV_OBJ_FLAG_HIDDEN);
}

void ui_extra_scroll_up(void)
{
    lv_obj_scroll_by(scroll_cont, 0, 50, LV_ANIM_ON);
    lv_event_send(scroll_cont, LV_EVENT_SCROLL, NULL);
    lv_obj_add_flag(info_label, LV_OBJ_FLAG_HIDDEN);
}

void ui_extra_scroll_down(void)
{
    lv_obj_scroll_by(scroll_cont, 0, -50, LV_ANIM_ON);
    lv_event_send(scroll_cont, LV_EVENT_SCROLL, NULL);
    lv_obj_add_flag(info_label, LV_OBJ_FLAG_HIDDEN);
}

