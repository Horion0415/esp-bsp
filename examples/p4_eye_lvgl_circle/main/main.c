/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "esp_log.h"

LV_IMG_DECLARE(camera_icon);
LV_IMG_DECLARE(timer_icon);

#define ZOOM_FACTOR 1.6
#define IMG_ZOOM_FACTOR 2.5
#define ZOOM_OFFSET -5

static const char *TAG = "main";

lv_obj_t * cont = NULL;

lv_coord_t btn_width = 0;
lv_coord_t btn_height = 0;

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
            lv_obj_set_style_transform_zoom(img, 256, 0);
            lv_obj_align(img, LV_ALIGN_CENTER, 30, 0);
        }
    }
    
    // Apply special effects to closest child
    if(closest_child) {
        // lv_obj_set_style_transform_zoom(closest_child, 256 * ZOOM_FACTOR, 0);
        lv_obj_set_size(closest_child, btn_width * ZOOM_FACTOR, btn_height * ZOOM_FACTOR);
        lv_obj_t * img = lv_obj_get_child(closest_child, 0);
        if(img) {
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
        // reset the styles of all children
        for(uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_t * child = lv_obj_get_child(cont, i);
            // lv_obj_set_style_transform_zoom(child, 256, 0);  // reset to normal size
            lv_obj_set_size(child, btn_width, btn_height);

            // reset the icon position to the default position
            lv_obj_t * img = lv_obj_get_child(child, 0);
            if(img) {
                lv_obj_set_style_transform_zoom(img, 256, 0);
                lv_obj_align(img, LV_ALIGN_CENTER, 30, 0);
            }
        }
        
        // zoom the selected child
        // lv_obj_set_style_transform_zoom(closest_child, 256 * ZOOM_FACTOR, 0);  // zoom to 140%
        lv_obj_set_size(closest_child, btn_width * ZOOM_FACTOR, btn_height * ZOOM_FACTOR);

        // set the special position for the center button
        lv_obj_t * img = lv_obj_get_child(closest_child, 0);
        if(img) {
            lv_obj_set_style_transform_zoom(img, 256 * IMG_ZOOM_FACTOR, 0);
            lv_obj_set_pos(img, ZOOM_OFFSET, -25);
        }
        
        // scroll to the view
        lv_obj_scroll_to_view(closest_child, LV_ANIM_ON);
    }
}

/**
 * Translate the object as they scroll
 */
void lv_example_scroll_6(void)
{
    // Create main container
    cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(cont, 240, 240);
    lv_obj_set_pos(cont, -90, 0);
    
    // Set container properties
    lv_obj_set_style_pad_row(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_radius(cont, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(cont, true, 0);
    
    // Configure scrolling behavior
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(cont, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    
    // Add scroll event handlers
    lv_obj_add_event_cb(cont, scroll_event_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(cont, scroll_end_event_cb, LV_EVENT_SCROLL_END, NULL);

    // Set container visual style
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(cont, LV_OPA_TRANSP, 0);

    // Create buttons with alternating icons
    for(uint32_t i = 0; i < 6; i++) {
        lv_obj_t * btn = lv_btn_create(cont);
        
        // Set button style - make all properties transparent
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

        // Create and configure icon
        lv_obj_t * img = lv_img_create(btn);
        lv_img_set_src(img, (i % 2 == 0) ? &camera_icon : &timer_icon);
        lv_obj_align(img, LV_ALIGN_CENTER, 30, 0);
        lv_img_set_zoom(img, 120);
        lv_img_set_size_mode(img, LV_IMG_SIZE_MODE_REAL);

        lv_obj_update_layout(btn);
        btn_width = lv_obj_get_width(btn);
        btn_height = lv_obj_get_height(btn);
    }

    // Initialize scroll position
    lv_event_send(cont, LV_EVENT_SCROLL, NULL);
    lv_obj_scroll_to_view(lv_obj_get_child(cont, 0), LV_ANIM_OFF);
}

static void btn_handler(void *arg, void *data)
{
    if((int)data == BSP_BUTTON_2) {
        ESP_LOGI(TAG, "scroll up");
        lv_obj_scroll_by(cont, 0, 30, LV_ANIM_ON);
        lv_event_send(cont, LV_EVENT_SCROLL, NULL);
    } else if((int)data == BSP_BUTTON_3) {
        ESP_LOGI(TAG, "scroll down");
        lv_obj_scroll_by(cont, 0, -30, LV_ANIM_ON);
        lv_event_send(cont, LV_EVENT_SCROLL, NULL);
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(bsp_p4_eye_init());

   /* Init Buttons */
    button_handle_t btns[BSP_BUTTON_NUM];
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_2], BUTTON_PRESS_DOWN, btn_handler, (void *) BSP_BUTTON_2));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_3], BUTTON_PRESS_DOWN, btn_handler, (void *) BSP_BUTTON_3));

    bsp_display_start();
    bsp_display_lock(0);

    lv_example_scroll_6();
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0xffffff), 0); // dark green #000000

    bsp_display_unlock();
    bsp_display_backlight_on();
}