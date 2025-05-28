#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "bsp/esp-bsp.h"
#include "app_gpio.h"

static const char *TAG = "GPIO_TEST";

// Define first group of GPIO pins (as sender and receiver)
#define GROUP1_PIN_NUM 5
const int group1_pins[GROUP1_PIN_NUM] = {10, 8, 53, 51, 38};

// Define second group of GPIO pins (as receiver and sender)
#define GROUP2_PIN_NUM 5
const int group2_pins[GROUP2_PIN_NUM] = {34, 7, 52, 50, 37};

// Initialize GPIO pins
static void configure_gpio_pins(const int* output_pins, int output_num, const int* input_pins, int input_num) {
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

// Initialize all GPIO pins for testing
void init_gpio(void) {
    ESP_LOGI(TAG, "Initializing GPIO pins for testing");
    
    // Configure all pins as input/output capable (we'll reconfigure as needed during tests)
    for (int i = 0; i < GROUP1_PIN_NUM; i++) {
        const gpio_config_t io_config = {
            .pin_bit_mask = BIT64(group1_pins[i]),
            .mode = GPIO_MODE_INPUT_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_ERROR_CHECK(gpio_config(&io_config));
    }
    
    for (int i = 0; i < GROUP2_PIN_NUM; i++) {
        const gpio_config_t io_config = {
            .pin_bit_mask = BIT64(group2_pins[i]),
            .mode = GPIO_MODE_INPUT_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_ERROR_CHECK(gpio_config(&io_config));
    }
    
    ESP_LOGI(TAG, "GPIO initialization completed");
}

// Test GPIO connections (sender -> receiver)
static bool test_gpio_group_connection(const int* output_pins, int output_num, 
                                     const int* input_pins, int input_num, 
                                     const char* test_name) {
    bool all_passed = true;
    
    ESP_LOGI(TAG, "Starting %s test", test_name);
    
    // Reconfigure pins for this test
    configure_gpio_pins(output_pins, output_num, input_pins, input_num);
    
    // Test each pin with high level
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
                ESP_LOGE(TAG, "%s failed: Output pin %d -> Input pin %d, Expected: %d, Actual: %d", 
                         test_name, output_pins[i], input_pins[j], expected, level);
            }
        }
        
        if (pin_passed) {
            ESP_LOGI(TAG, "%s passed: Output pin %d -> Input pin %d", test_name, output_pins[i], input_pins[i]);
        }
    }
    
    return all_passed;
}

// Test all GPIO connections
bool test_gpio_connection(void) {
    bool all_tests_passed = true;
    
    ESP_LOGI(TAG, "Starting comprehensive GPIO connection test");
    
    // Test 1: Group1 as output -> Group2 as input
    bool test1_passed = test_gpio_group_connection(group1_pins, GROUP1_PIN_NUM, 
                                                  group2_pins, GROUP2_PIN_NUM, 
                                                  "Group1->Group2");
    
    // Test 2: Group2 as output -> Group1 as input  
    bool test2_passed = test_gpio_group_connection(group2_pins, GROUP2_PIN_NUM,
                                                  group1_pins, GROUP1_PIN_NUM,
                                                  "Group2->Group1");
    
    all_tests_passed = test1_passed && test2_passed;
    
    if (all_tests_passed) {
        ESP_LOGI(TAG, "All GPIO connection tests PASSED");
    } else {
        ESP_LOGE(TAG, "Some GPIO connection tests FAILED");
    }
    
    return all_tests_passed;
}
