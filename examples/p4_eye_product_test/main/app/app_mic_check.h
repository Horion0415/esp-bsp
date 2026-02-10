#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int sample_rate;
    int channels;
    int window_ms;
    int baseline_windows;
    int hit_windows;
    int32_t min_threshold;
    int32_t baseline_mul_num;
    int32_t baseline_mul_den;
} app_mic_check_cfg_t;

typedef struct {
    app_mic_check_cfg_t cfg;
    int samples_per_window;
    size_t bytes_per_window;
    int16_t *pcm;
    int collected;
    int hit;
    int64_t baseline_sum;
    int32_t baseline;
    int32_t threshold;
    bool threshold_ready;
} app_mic_checker_t;

/**
 * @brief Initialize microphone activity checker
 *
 * @param checker Checker instance to initialize
 * @param cfg Checker configuration
 * @return ESP_OK on success
 */
esp_err_t app_mic_checker_init(app_mic_checker_t *checker, const app_mic_check_cfg_t *cfg);

/**
 * @brief Deinitialize microphone activity checker
 *
 * @param checker Checker instance to deinitialize
 */
void app_mic_checker_deinit(app_mic_checker_t *checker);

/**
 * @brief Read one window and update activity state
 *
 * @param checker Checker instance
 * @param out_level Optional output for current mean-absolute level
 * @return true if activity is detected, false otherwise
 */
bool app_mic_checker_step(app_mic_checker_t *checker, int32_t *out_level);

#ifdef __cplusplus
}
#endif
