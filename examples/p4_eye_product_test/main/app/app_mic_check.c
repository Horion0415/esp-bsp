/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "bsp/esp-bsp.h"
#include "app_mic_check.h"

static const char *TAG = "app_mic_check";

static int32_t calc_mean_abs(const int16_t *buf, int samples)
{
    int64_t sum = 0;
    for (int i = 0; i < samples; i++) {
        int32_t v = buf[i];
        if (v < 0) {
            v = -v;
        }
        sum += v;
    }
    if (samples == 0) {
        return 0;
    }
    return (int32_t)(sum / samples);
}

bool app_mic_check_activity(int sample_rate, int channels, int window_ms, int total_windows, int hit_windows)
{
    if (sample_rate <= 0 || channels <= 0 || window_ms <= 0 || total_windows <= 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }

    int samples_per_window = (sample_rate * window_ms) / 1000;
    if (samples_per_window <= 0) {
        ESP_LOGE(TAG, "Window too small");
        return false;
    }

    size_t bytes_per_window = (size_t)samples_per_window * channels * sizeof(int16_t);
    int16_t *pcm = (int16_t *)heap_caps_malloc(bytes_per_window, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        ESP_LOGE(TAG, "No memory for PCM buffer");
        return false;
    }

    const int baseline_windows = 8; // ~640ms for 80ms windows
    int64_t baseline_sum = 0;
    int baseline_count = 0;

    for (int i = 0; i < baseline_windows; i++) {
        size_t bytes_read = 0;
        esp_err_t ret = bsp_extra_pdm_i2s_read((void *)pcm, bytes_per_window, &bytes_read, portMAX_DELAY);
        if (ret != ESP_OK || bytes_read != bytes_per_window) {
            ESP_LOGW(TAG, "PDM read failed during baseline: ret=%d, read=%u", ret, (unsigned)bytes_read);
            continue;
        }
        int32_t mean_abs = calc_mean_abs(pcm, samples_per_window * channels);
        baseline_sum += mean_abs;
        baseline_count++;
    }

    int32_t baseline = 0;
    if (baseline_count > 0) {
        baseline = (int32_t)(baseline_sum / baseline_count);
    }

    int32_t min_threshold = 80;
    int32_t threshold = (baseline * 3) / 2;
    if (threshold < min_threshold) {
        threshold = min_threshold;
    }

    ESP_LOGI(TAG, "Baseline=%d, Threshold=%d, windows=%d, hit=%d", baseline, threshold, total_windows, hit_windows);

    int hit = 0;
    for (int i = 0; i < total_windows; i++) {
        size_t bytes_read = 0;
        esp_err_t ret = bsp_extra_pdm_i2s_read((void *)pcm, bytes_per_window, &bytes_read, portMAX_DELAY);
        if (ret != ESP_OK || bytes_read != bytes_per_window) {
            ESP_LOGW(TAG, "PDM read failed: ret=%d, read=%u", ret, (unsigned)bytes_read);
            continue;
        }
        int32_t mean_abs = calc_mean_abs(pcm, samples_per_window * channels);
        if (mean_abs >= threshold) {
            hit++;
            if (hit >= hit_windows) {
                ESP_LOGI(TAG, "Audio activity detected (mean_abs=%d)", mean_abs);
                heap_caps_free(pcm);
                return true;
            }
        }
    }

    ESP_LOGW(TAG, "Audio activity not detected");
    heap_caps_free(pcm);
    return false;
}

bool app_mic_wait_for_voice(uint32_t timeout_ms)
{
    const int sample_rate = 16000;
    const int channels = 2;
    const int window_ms = 120;
    const int total_windows = (timeout_ms + window_ms - 1) / window_ms;
    const int hit_windows = 1;

    return app_mic_check_activity(sample_rate, channels, window_ms, total_windows, hit_windows);
}
