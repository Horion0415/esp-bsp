#include <stdio.h>
#include <math.h>
#include <stdbool.h>
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_qma6100.h"
#include "ui.h"

static const char *TAG = "APP_QMA6100";

// QMA6100P sensor handle
static qma6100p_handle_t qma6100_handle = NULL;

// Calculate gravity magnitude from acceleration data
static float calculate_gravity_magnitude(const qma6100p_acce_value_t *acce_data)
{
    return sqrtf(acce_data->acce_x * acce_data->acce_x + 
                 acce_data->acce_y * acce_data->acce_y + 
                 acce_data->acce_z * acce_data->acce_z);
}

esp_err_t app_qma6100_init(i2c_master_bus_handle_t i2c_bus_handle)
{
    if (i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "Invalid I2C bus handle");
        return ESP_ERR_INVALID_ARG;
    }

    if (qma6100_handle != NULL) {
        ESP_LOGW(TAG, "QMA6100 already initialized");
        return ESP_OK;
    }

    // Create QMA6100P sensor instance
    esp_err_t ret = qma6100p_create(i2c_bus_handle, QMA6100P_I2C_ADDRESS, &qma6100_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create QMA6100P instance: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wake up the sensor
    ret = qma6100p_wake_up(qma6100_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to wake up QMA6100P: %s", esp_err_to_name(ret));
        qma6100p_delete(qma6100_handle);
        qma6100_handle = NULL;
        return ret;
    }

    // Configure sensor with default settings (±2g range)
    ret = qma6100p_config(qma6100_handle, ACCE_FS_2G);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure QMA6100P: %s", esp_err_to_name(ret));
        qma6100p_delete(qma6100_handle);
        qma6100_handle = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "QMA6100 initialized successfully");
    return ESP_OK;
}

esp_err_t app_qma6100_deinit(void)
{
    if (qma6100_handle != NULL) {
        qma6100p_delete(qma6100_handle);
        qma6100_handle = NULL;
        ESP_LOGI(TAG, "QMA6100 deinitialized");
    }
    return ESP_OK;
}

esp_err_t app_qma6100_get_data(qma6100p_acce_value_t *acce_data)
{
    if (qma6100_handle == NULL) {
        ESP_LOGE(TAG, "QMA6100 not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (acce_data == NULL) {
        ESP_LOGE(TAG, "Invalid acceleration data pointer");
        return ESP_ERR_INVALID_ARG;
    }

    return qma6100p_get_acce(qma6100_handle, acce_data);
}

bool app_qma6100_test_data_reading(const qma6100_test_config_t *config)
{
    qma6100p_acce_value_t acce_data;
    esp_err_t ret = app_qma6100_get_data(&acce_data);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read acceleration data: %s", esp_err_to_name(ret));
        return false;
    }

    // Calculate gravity magnitude
    float gravity = calculate_gravity_magnitude(&acce_data);
    
    // Check if data is within reasonable range
    if (gravity < config->min_gravity || gravity > config->max_gravity) {
        ESP_LOGW(TAG, "Acceleration data out of range: g=%.3f (X=%.3f, Y=%.3f, Z=%.3f)", 
                 gravity, acce_data.acce_x, acce_data.acce_y, acce_data.acce_z);
        return false;
    }

    ESP_LOGI(TAG, "Data reading test passed: g=%.3f (X=%.3f, Y=%.3f, Z=%.3f)", 
             gravity, acce_data.acce_x, acce_data.acce_y, acce_data.acce_z);
    return true;
}

bool app_qma6100_test_data_stability(const qma6100_test_config_t *config)
{
    qma6100p_acce_value_t readings[config->stability_test_count];
    float gravity_values[config->stability_test_count];
    
    // Collect multiple readings
    for (int i = 0; i < config->stability_test_count; i++) {
        esp_err_t ret = app_qma6100_get_data(&readings[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read data during stability test (reading %d): %s", 
                     i, esp_err_to_name(ret));
            return false;
        }
        
        gravity_values[i] = calculate_gravity_magnitude(&readings[i]);
        
        if (i < config->stability_test_count - 1) {
            vTaskDelay(pdMS_TO_TICKS(config->stability_test_delay_ms));
        }
    }
    
    // Calculate mean and standard deviation
    float mean = 0.0f;
    for (int i = 0; i < config->stability_test_count; i++) {
        mean += gravity_values[i];
    }
    mean /= config->stability_test_count;
    
    float variance = 0.0f;
    for (int i = 0; i < config->stability_test_count; i++) {
        float diff = gravity_values[i] - mean;
        variance += diff * diff;
    }
    variance /= config->stability_test_count;
    float std_dev = sqrtf(variance);
    
    // Check if standard deviation is reasonable (less than 10% of mean)
    float max_allowed_std_dev = mean * 0.1f;
    bool stable = std_dev < max_allowed_std_dev;
    
    if (stable) {
        ESP_LOGI(TAG, "Data stability test passed: mean=%.3f, std_dev=%.3f", mean, std_dev);
    } else {
        ESP_LOGW(TAG, "Data stability test failed: mean=%.3f, std_dev=%.3f (max allowed: %.3f)", 
                 mean, std_dev, max_allowed_std_dev);
    }
    
    return stable;
}

qma6100_status_t app_qma6100_status_check(const qma6100_test_config_t *config)
{
    // Use default config if none provided
    qma6100_test_config_t default_config = QMA6100_DEFAULT_TEST_CONFIG();
    if (config == NULL) {
        config = &default_config;
    }

    ESP_LOGI(TAG, "Starting QMA6100 comprehensive status check...");

    // 1. Check if device is initialized
    if (qma6100_handle == NULL) {
        ESP_LOGE(TAG, "Status check failed: Device not initialized");
        return QMA6100_STATUS_NOT_INITIALIZED;
    }

    // 2. Test device communication by reading device ID
    uint8_t device_id;
    esp_err_t ret = qma6100p_get_deviceid(qma6100_handle, &device_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Status check failed: Communication error - %s", esp_err_to_name(ret));
        return QMA6100_STATUS_COMMUNICATION_ERROR;
    }

    if (device_id != QMA6100P_WHO_AM_I_VAL) {
        ESP_LOGE(TAG, "Status check failed: Invalid device ID. Expected: 0x%02X, Got: 0x%02X", 
                 QMA6100P_WHO_AM_I_VAL, device_id);
        return QMA6100_STATUS_COMMUNICATION_ERROR;
    }
    ESP_LOGI(TAG, "Device ID check passed: 0x%02X", device_id);

    // 3. Test data reading functionality
    if (!app_qma6100_test_data_reading(config)) {
        ESP_LOGE(TAG, "Status check failed: Invalid sensor data");
        return QMA6100_STATUS_INVALID_DATA;
    }

    // 4. Test data stability
    if (!app_qma6100_test_data_stability(config)) {
        ESP_LOGE(TAG, "Status check failed: Unstable sensor data");
        return QMA6100_STATUS_UNSTABLE_DATA;
    }

    ESP_LOGI(TAG, "QMA6100 status check completed successfully - Device is working normally");
    return QMA6100_STATUS_OK;
}

