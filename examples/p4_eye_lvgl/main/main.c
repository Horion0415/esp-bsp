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

static const char *TAG = "main";

lv_obj_t * cont = NULL;

static void scroll_event_cb(lv_event_t * e)
{
    lv_obj_t * cont = lv_event_get_target(e);

    lv_area_t cont_a;
    lv_obj_get_coords(cont, &cont_a);
    lv_coord_t cont_y_center = cont_a.y1 + lv_area_get_height(&cont_a) / 2;

    lv_coord_t r = lv_obj_get_height(cont) * 7 / 10;
    uint32_t i;
    uint32_t child_cnt = lv_obj_get_child_cnt(cont);
    
    // 找到最接近中心的子元素
    lv_obj_t * closest_child = NULL;
    lv_coord_t min_diff = LV_COORD_MAX;
    
    for(i = 0; i < child_cnt; i++) {
        lv_obj_t * child = lv_obj_get_child(cont, i);
        lv_area_t child_a;
        lv_obj_get_coords(child, &child_a);

        lv_coord_t child_y_center = child_a.y1 + lv_area_get_height(&child_a) / 2;
        lv_coord_t diff_y = LV_ABS(child_y_center - cont_y_center);
        
        // 记录最接近中心的子元素
        if(diff_y < min_diff) {
            min_diff = diff_y;
            closest_child = child;
        }

        /*Get the x of diff_y on a circle.*/
        lv_coord_t x;
        /*If diff_y is out of the circle use the last point of the circle (the radius)*/
        if(diff_y >= r) {
            x = r;
        }
        else {
            /*Use Pythagoras theorem to get x from radius and y*/
            uint32_t x_sqr = r * r - diff_y * diff_y;
            lv_sqrt_res_t res;
            lv_sqrt(x_sqr, &res, 0x8000);   /*Use lvgl's built in sqrt root function*/
            // x = r - res.i;
            x = res.i - r;
        }

        /*Translate the item by the calculated X coordinate*/
        lv_obj_set_style_translate_x(child, x, 0);

        /*Use some opacity with larger translations*/
        lv_opa_t opa = lv_map(x, 0, r, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_obj_set_style_opa(child, LV_OPA_COVER - opa, 0);
        
        // 重置所有按钮的缩放和图标位置
        lv_obj_set_style_transform_zoom(child, 256, 0);  // 重置为正常大小
        lv_obj_t * img = lv_obj_get_child(child, 0);
        if(img) {
            lv_obj_align(img, LV_ALIGN_RIGHT_MID, 0, 0);
        }
    }
    
    // 只放大最接近中心的按钮
    if(closest_child) {
        // 放大选中的子元素
        lv_obj_set_style_transform_zoom(closest_child, 256 * 1.5, 0);  // 放大到140%
        
        // 只为中心按钮设置特殊位置
        lv_obj_t * img = lv_obj_get_child(closest_child, 0);
        if(img) {
            lv_obj_set_pos(img, -50, 0);
        }
    }
}

// static void scroll_end_event_cb(lv_event_t * e)
// {
//     lv_obj_t * cont = lv_event_get_target(e);
    
//     // get container center coordinates
//     lv_area_t cont_a;
//     lv_obj_get_coords(cont, &cont_a);
//     lv_coord_t cont_y_center = cont_a.y1 + lv_area_get_height(&cont_a) / 2;
    
//     // find the child element closest to the center
//     uint32_t child_cnt = lv_obj_get_child_cnt(cont);
//     lv_obj_t * closest_child = NULL;
//     lv_coord_t min_diff = LV_COORD_MAX;
    
//     for(uint32_t i = 0; i < child_cnt; i++) {
//         lv_obj_t * child = lv_obj_get_child(cont, i);
//         lv_area_t child_a;
//         lv_obj_get_coords(child, &child_a);
        
//         lv_coord_t child_y_center = child_a.y1 + lv_area_get_height(&child_a) / 2;
//         lv_coord_t diff_y = LV_ABS(child_y_center - cont_y_center);
        
//         if(diff_y < min_diff) {
//             min_diff = diff_y;
//             closest_child = child;
//         }
//     }
//     // scroll the closest child element to the center of the view
//     if(closest_child) {
//         // reset the style of all child elements
//         for(uint32_t i = 0; i < child_cnt; i++) {
//             lv_obj_t * child = lv_obj_get_child(cont, i);
//             lv_obj_set_style_transform_zoom(child, 256, 0);  // reset to normal size
            
//             // // reset the font size of the label
//             // lv_obj_t * label = lv_obj_get_child(child, 0);
//             // if(label) {
//             //     lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
//             // }
            
//             // 重置所有图标位置到默认位置
//             lv_obj_t * img = lv_obj_get_child(child, 0);
//             if(img) {
//                 lv_obj_align(img, LV_ALIGN_RIGHT_MID, 0, 0);
//             }
//         }
        
//         // zoom the selected child element
//         lv_obj_set_style_transform_zoom(closest_child, 256 * 1.45, 0);  // zoom to 130%
        
//         // // zoom the text of the selected child element
//         // lv_obj_t * label = lv_obj_get_child(closest_child, 0);
//         // if(label) {
//         //     lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);  // use a larger font
//         // }

//         // 只为中心按钮设置特殊位置
//         lv_obj_t * img = lv_obj_get_child(closest_child, 0);
//         if(img) {
//             lv_obj_set_pos(img, -45, 0);
//         }
        
//         // scroll to the view
//         lv_obj_scroll_to_view(closest_child, LV_ANIM_ON);
//     }
// }

static void scroll_end_event_cb(lv_event_t * e)
{
    lv_obj_t * cont = lv_event_get_target(e);
    
    // 获取容器中心坐标
    lv_area_t cont_a;
    lv_obj_get_coords(cont, &cont_a);
    lv_coord_t cont_y_center = cont_a.y1 + lv_area_get_height(&cont_a) / 2;
    
    // 找到最接近中心的子元素
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
    
    // 滚动结束后，重新应用样式
    if(closest_child) {
        // 重置所有子元素的样式
        for(uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_t * child = lv_obj_get_child(cont, i);
            lv_obj_set_style_transform_zoom(child, 256, 0);  // 重置为正常大小
            
            // 重置所有图标位置到默认位置
            lv_obj_t * img = lv_obj_get_child(child, 0);
            if(img) {
                lv_obj_align(img, LV_ALIGN_RIGHT_MID, 0, 0);
            }
        }
        
        // 放大选中的子元素
        lv_obj_set_style_transform_zoom(closest_child, 256 * 1.5, 0);  // 放大到140%
        
        // 只为中心按钮设置特殊位置
        lv_obj_t * img = lv_obj_get_child(closest_child, 0);
        if(img) {
            lv_obj_set_pos(img, -50, 0);
        }
        
        // 滚动到视图
        lv_obj_scroll_to_view(closest_child, LV_ANIM_ON);
    }
}

/**
 * Translate the object as they scroll
 */
void lv_example_scroll_6(void)
{
    cont = lv_obj_create(lv_scr_act());
    lv_obj_set_style_pad_row(cont, 20, 0);  
    lv_obj_set_size(cont, 240, 240);
    // lv_obj_center(cont);
    lv_obj_set_pos(cont, -160, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(cont, scroll_event_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(cont, scroll_end_event_cb, LV_EVENT_SCROLL_END, NULL);  // add scroll end event
    // lv_obj_add_event_cb(cont, scroll_begin_event_cb, LV_EVENT_SCROLL_BEGIN, NULL);  // add scroll begin event
    lv_obj_set_style_radius(cont, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(cont, true, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(cont, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

    // set container opacity
    lv_obj_set_style_bg_opa(cont, LV_OPA_10, 0);  // background opacity set to 70%
    lv_obj_set_style_border_opa(cont, LV_OPA_TRANSP, 0);  // border opacity set to 50%
    
    // optional: set container background color, make transparent effect more obvious
    lv_obj_set_style_bg_color(cont, lv_color_hex(0xcccccc), 0);  // light gray background

    uint32_t i;
    for(i = 0; i < 20; i++) {
        lv_obj_t * btn = lv_btn_create(cont);
        
        // set button opacity
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        
        // remove shadow effect
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_shadow_spread(btn, 0, 0); 
        lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
        
        // remove shadow offset
        lv_obj_set_style_shadow_ofs_x(btn, 0, 0);
        lv_obj_set_style_shadow_ofs_y(btn, 0, 0);
        
        // set button width
        lv_obj_set_width(btn, lv_pct(100));

        if(i % 2 == 0) {
            lv_obj_t * img1 = lv_img_create(btn);
            lv_img_set_src(img1, &camera_icon);
            lv_obj_align(img1, LV_ALIGN_RIGHT_MID, 0, 0);
            lv_img_set_zoom(img1, 140);
            lv_img_set_size_mode(img1, LV_IMG_SIZE_MODE_REAL);
        } else {
            lv_obj_t * img1 = lv_img_create(btn);
            lv_img_set_src(img1, &timer_icon);
            lv_obj_align(img1, LV_ALIGN_RIGHT_MID, 0, 0);
            lv_img_set_zoom(img1, 140);
            lv_img_set_size_mode(img1, LV_IMG_SIZE_MODE_REAL);
        }
    }

    /*Update the buttons position manually for first*/
    lv_event_send(cont, LV_EVENT_SCROLL, NULL);

    /*Be sure the fist button is in the middle*/
    lv_obj_scroll_to_view(lv_obj_get_child(cont, 0), LV_ANIM_OFF);
}

static void btn_handler(void *arg, void *data)
{
    if((int)data == BSP_BUTTON_2) {
        ESP_LOGI(TAG, "scroll up");
        lv_obj_scroll_by(cont, 0, 80, LV_ANIM_ON);
        lv_event_send(cont, LV_EVENT_SCROLL, NULL);
    } else if((int)data == BSP_BUTTON_3) {
        ESP_LOGI(TAG, "scroll down");
        lv_obj_scroll_by(cont, 0, -80, LV_ANIM_ON);
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
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0); // 深绿色 #000000

    bsp_display_unlock();
    bsp_display_backlight_on();
}