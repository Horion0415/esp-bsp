#ifndef ESP_QMA6100_H
#define ESP_QMA6100_H

#include "esp_err.h"
#include "qma6100p.h"

// Application layer status check results
typedef enum {
    QMA6100_STATUS_OK = 0,              // Device is working normally
    QMA6100_STATUS_NOT_INITIALIZED,     // Device not initialized
    QMA6100_STATUS_COMMUNICATION_ERROR, // I2C communication error
    QMA6100_STATUS_INVALID_DATA,        // Sensor data is invalid
    QMA6100_STATUS_UNSTABLE_DATA,       // Sensor data is unstable
    QMA6100_STATUS_UNKNOWN_ERROR        // Unknown error occurred
} qma6100_status_t;

// Test configuration
typedef struct {
    float min_gravity;                  // Minimum expected gravity value (g)
    float max_gravity;                  // Maximum expected gravity value (g)
    int stability_test_count;           // Number of readings for stability test
    int stability_test_delay_ms;        // Delay between readings in stability test
} qma6100_test_config_t;

// Default test configuration
#define QMA6100_DEFAULT_TEST_CONFIG() { \
    .min_gravity = 0.1f, \
    .max_gravity = 5.0f, \
    .stability_test_count = 5, \
    .stability_test_delay_ms = 10 \
}

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize QMA6100 sensor for application testing
 * 
 * @param i2c_bus_handle I2C bus handle from BSP
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_qma6100_init(i2c_master_bus_handle_t i2c_bus_handle);

/**
 * @brief Deinitialize QMA6100 sensor
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_qma6100_deinit(void);

/**
 * @brief Perform comprehensive status check of QMA6100 sensor
 * 
 * @param config Test configuration parameters (can be NULL for default)
 * @return qma6100_status_t Status result
 */
qma6100_status_t app_qma6100_status_check(const qma6100_test_config_t *config);

/**
 * @brief Get accelerometer data from QMA6100
 * 
 * @param acce_data Pointer to store acceleration data
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_qma6100_get_data(qma6100p_acce_value_t *acce_data);

/**
 * @brief Test data reading functionality
 * 
 * @param config Test configuration parameters
 * @return bool true if test passed
 */
bool app_qma6100_test_data_reading(const qma6100_test_config_t *config);

/**
 * @brief Test data stability
 * 
 * @param config Test configuration parameters
 * @return bool true if test passed
 */
bool app_qma6100_test_data_stability(const qma6100_test_config_t *config);

#ifdef __cplusplus
}
#endif
#endif