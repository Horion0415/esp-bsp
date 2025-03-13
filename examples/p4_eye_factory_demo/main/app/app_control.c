#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "bsp/esp-bsp.h"

#include "ui_extra.h"
#include "app_video_stream.h"
#include "app_album.h"

static const char *TAG = "app_control";

static button_handle_t btns[BSP_BUTTON_NUM];

static int knob_step_counter = 0;
static int knob_last_direction = 0;  // 0: no direction, 1: right, -1: left
static int64_t knob_last_time = 0;   // timestamp of last rotation
static const int knob_timeout_ms = 500;  // timeout in milliseconds
static int knob_step_threshold = 6;  // threshold for knob step counter

static void btn_handler(void *arg, void *data)
{
    int button_id = (int)data;
    
    bsp_display_lock(0);

    if(ui_extra_get_current_page() == UI_PAGE_USB_DISK) {
        ui_extra_goto_page(UI_PAGE_MAIN);
        bsp_display_unlock();
        return;
    }
    
    switch (button_id) {
        case BSP_BUTTON_1:
            ui_extra_btn_menu();
            break;
            
        case BSP_BUTTON_2:
            ui_extra_btn_up();
            if (ui_extra_get_current_page() == UI_PAGE_ALBUM) {
                app_album_prev_image();
            }
            break;
            
        case BSP_BUTTON_3:
            ui_extra_btn_down();
            if (ui_extra_get_current_page() == UI_PAGE_ALBUM) {
                app_album_next_image();
            }
            break;
            
        case BSP_BUTTON_ED:
            ui_extra_btn_encoder();
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown button ID: %d", button_id);
            break;
    }
    
    bsp_display_unlock();
}

static void knob_right_cb(void *arg, void *data)
{
    if(ui_extra_get_current_page() == UI_PAGE_ALBUM || ui_extra_get_current_page() == UI_PAGE_USB_DISK) {
        return;
    }
    
    int64_t current_time = esp_timer_get_time() / 1000;  // get current time in milliseconds
    
    // Check for timeout or direction change
    if (current_time - knob_last_time > knob_timeout_ms || knob_last_direction == 1) {
        // Timeout or direction change, reset counter
        knob_step_counter = 0;
        knob_last_direction = -1;
    }
    
    // Increment step counter
    knob_step_counter++;
    knob_last_time = current_time;
    
    // Trigger action when accumulated steps reach threshold
    if (knob_step_counter >= knob_step_threshold) {
        ESP_LOGD(TAG, "Continuous left rotation detected: %d steps, value -1", knob_step_counter);

        knob_step_counter = 0;  // Reset counter
        
        bsp_display_lock(0);
        if(ui_extra_get_current_page() == UI_PAGE_CAMERA || ui_extra_get_current_page() == UI_PAGE_INTERVAL_CAM || ui_extra_get_current_page() == UI_PAGE_VIDEO_MODE) {
            app_extra_set_magnification_factor(app_extra_get_magnification_factor() - 1);
        } else if(ui_extra_get_current_page() == UI_PAGE_MAIN) {
            ui_extra_btn_up();
        }
        bsp_display_unlock();
    }
}

static void knob_left_cb(void *arg, void *data)
{
    if(ui_extra_get_current_page() == UI_PAGE_ALBUM || ui_extra_get_current_page() == UI_PAGE_USB_DISK) {
        return;
    }

    int64_t current_time = esp_timer_get_time() / 1000;  // get current time in milliseconds
    
    // Check for timeout or direction change
    if (current_time - knob_last_time > knob_timeout_ms || knob_last_direction == -1) {
        // Timeout or direction change, reset counter
        knob_step_counter = 0;
        knob_last_direction = 1;
    }
    
    // Increment step counter
    knob_step_counter++;
    knob_last_time = current_time;
    
    // Trigger action when accumulated steps reach threshold
    if (knob_step_counter >= knob_step_threshold) {
        knob_step_counter = 0;  // Reset counter
        
        ESP_LOGD(TAG, "Continuous right rotation detected: %d steps, value +1", knob_step_counter);
        
        bsp_display_lock(0);
        if(ui_extra_get_current_page() == UI_PAGE_CAMERA || ui_extra_get_current_page() == UI_PAGE_INTERVAL_CAM || ui_extra_get_current_page() == UI_PAGE_VIDEO_MODE) {
            app_extra_set_magnification_factor(app_extra_get_magnification_factor() + 1);
        } else if(ui_extra_get_current_page() == UI_PAGE_MAIN) {
            ui_extra_btn_down();
        }
        bsp_display_unlock();
    }
}

// Function to set encoder step threshold
void app_control_set_knob_sensitivity(int threshold)
{
    if (threshold > 0) {
        knob_step_threshold = threshold;
        ESP_LOGI(TAG, "Knob sensitivity set to %d steps", knob_step_threshold);
    }
}

esp_err_t app_control_init(void)
{
    // Initialize the wake buttons
    const gpio_config_t config = {
        .pin_bit_mask = BIT(BSP_BUTTON_NUM1) | BIT(BSP_BUTTON_NUM2) | BIT(BSP_BUTTON_NUM3),
        .mode = GPIO_MODE_INPUT,
    };

    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_ERROR_CHECK(esp_deep_sleep_enable_gpio_wakeup(BIT(BSP_BUTTON_NUM1) | BIT(BSP_BUTTON_NUM2) | BIT(BSP_BUTTON_NUM3), 0));

    // Initialize the buttons
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_1], BUTTON_PRESS_DOWN, btn_handler, (void *) BSP_BUTTON_1));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_2], BUTTON_PRESS_DOWN, btn_handler, (void *) BSP_BUTTON_2));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_3], BUTTON_PRESS_DOWN, btn_handler, (void *) BSP_BUTTON_3));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_ED], BUTTON_PRESS_UP, btn_handler, (void *) BSP_BUTTON_ED));

    // Initialize the knob
    ESP_ERROR_CHECK(bsp_knob_init());
    // Register callback functions
    ESP_ERROR_CHECK(bsp_knob_register_cb(KNOB_LEFT, knob_left_cb, NULL));
    ESP_ERROR_CHECK(bsp_knob_register_cb(KNOB_RIGHT, knob_right_cb, NULL));

    return ESP_OK;
}