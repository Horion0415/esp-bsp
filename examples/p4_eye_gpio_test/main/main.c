/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "GPIO_TEST";

// Define first group of GPIO pins (as sender and receiver)
#define GROUP1_PIN_NUM 7
const int group1_pins[GROUP1_PIN_NUM] = {10, 8, 6, 54, 53, 51, 38};

// Define second group of GPIO pins (as receiver and sender)
#define GROUP2_PIN_NUM 7
const int group2_pins[GROUP2_PIN_NUM] = {34, 7, 14, 13, 52, 50, 37};

// Initialize GPIO pins
void init_gpio(const int* output_pins, int output_num, const int* input_pins, int input_num) {
    // Configure output pins
    for (int i = 0; i < output_num; i++) {
        const gpio_config_t output_io_config = {
            .pin_bit_mask = BIT64(output_pins[i]),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_ERROR_CHECK(gpio_config(&output_io_config));
    }
    
    // Configure input pins
    for (int i = 0; i < input_num; i++) {
        const gpio_config_t input_io_config = {
            .pin_bit_mask = BIT64(input_pins[i]),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,  // Pull down to ensure default state
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_ERROR_CHECK(gpio_config(&input_io_config));
    }
}

// Test GPIO connections (sender -> receiver)
bool test_gpio_connection(const int* output_pins, int output_num, 
                         const int* input_pins, int input_num) {
    bool all_passed = true;
    
    // Test each pin with high level
    ESP_LOGI(TAG, "Starting high level test");
    for (int i = 0; i < output_num && i < input_num; i++) {
        // Set current output pin to high, others to low
        for (int j = 0; j < output_num; j++) {
            gpio_set_level(output_pins[j], (j == i) ? 1 : 0);
        }
        
        // Short delay to stabilize levels
        vTaskDelay(pdMS_TO_TICKS(10));
        
        // Check if input pins match expected values
        bool pin_passed = true;
        for (int j = 0; j < input_num; j++) {
            int level = gpio_get_level(input_pins[j]);
            bool expected = (j == i);
            if (level != expected) {
                pin_passed = false;
                all_passed = false;
                ESP_LOGE(TAG, "High level test failed: Output pin %d -> Input pin %d, Expected: %d, Actual: %d", 
                         output_pins[i], input_pins[j], expected, level);
            }
        }
        
        if (pin_passed) {
            ESP_LOGI(TAG, "High level test passed: Output pin %d -> Input pin %d", output_pins[i], input_pins[i]);
        }
    }
    
    return all_passed;
}

void app_main(void)
{
    // Initialize the LEDs
    ESP_ERROR_CHECK(bsp_leds_init());

    ESP_LOGI(TAG, "GPIO test starting");
    
    // First round: Group 1 pins send, Group 2 pins receive
    ESP_LOGI(TAG, "======= First Test: Group 1 pins sending, Group 2 pins receiving =======");
    init_gpio(group1_pins, GROUP1_PIN_NUM, group2_pins, GROUP2_PIN_NUM);
    bool test1_result = test_gpio_connection(group1_pins, GROUP1_PIN_NUM, group2_pins, GROUP2_PIN_NUM);
    
    // Second round: Group 2 pins send, Group 1 pins receive
    ESP_LOGI(TAG, "======= Second Test: Group 2 pins sending, Group 1 pins receiving =======");
    init_gpio(group2_pins, GROUP2_PIN_NUM, group1_pins, GROUP1_PIN_NUM);
    bool test2_result = test_gpio_connection(group2_pins, GROUP2_PIN_NUM, group1_pins, GROUP1_PIN_NUM);
    
    // Test results
    if (test1_result && test2_result) {
        ESP_LOGI(TAG, "All GPIO tests passed!");
        bsp_led_set(BSP_LED_WHITE, 1);
    } else {
        ESP_LOGE(TAG, "GPIO tests failed! Please check connections");
    }
}