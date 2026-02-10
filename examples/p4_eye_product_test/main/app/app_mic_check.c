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

esp_err_t app_mic_checker_init(app_mic_checker_t *checker, const app_mic_check_cfg_t *cfg)
{
    if (checker == NULL || cfg == NULL) {
        ESP_LOGE(TAG, "Invalid checker or cfg");
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->sample_rate <= 0 || cfg->channels <= 0 || cfg->window_ms <= 0 ||
        cfg->baseline_windows <= 0 || cfg->hit_windows <= 0 ||
        cfg->baseline_mul_den == 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    memset(checker, 0, sizeof(*checker));
    checker->cfg = *cfg;
    checker->samples_per_window = (cfg->sample_rate * cfg->window_ms) / 1000;
    if (checker->samples_per_window <= 0) {
        ESP_LOGE(TAG, "Window too small");
        return ESP_ERR_INVALID_ARG;
    }
    checker->bytes_per_window = (size_t)checker->samples_per_window * cfg->channels * sizeof(int16_t);
    checker->pcm = (int16_t *)heap_caps_malloc(checker->bytes_per_window, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (checker->pcm == NULL) {
        ESP_LOGE(TAG, "No memory for PCM buffer");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Mic checker init: %d Hz, %d ch, %d ms", cfg->sample_rate, cfg->channels, cfg->window_ms);
    return ESP_OK;
}

void app_mic_checker_deinit(app_mic_checker_t *checker)
{
    if (checker == NULL) {
        return;
    }
    if (checker->pcm) {
        heap_caps_free(checker->pcm);
        checker->pcm = NULL;
    }
}

bool app_mic_checker_step(app_mic_checker_t *checker, int32_t *out_level)
{
    if (checker == NULL || checker->pcm == NULL) {
        ESP_LOGE(TAG, "Checker not initialized");
        return false;
    }

    size_t bytes_read = 0;
    esp_err_t ret = bsp_extra_pdm_i2s_read((void *)checker->pcm, checker->bytes_per_window, &bytes_read, portMAX_DELAY);
    if (ret != ESP_OK || bytes_read != checker->bytes_per_window) {
        ESP_LOGW(TAG, "PDM read failed: ret=%d, read=%u", ret, (unsigned)bytes_read);
        return false;
    }

    int32_t mean_abs = calc_mean_abs(checker->pcm, checker->samples_per_window * checker->cfg.channels);
    if (out_level) {
        *out_level = mean_abs;
    }

    if (!checker->threshold_ready) {
        checker->baseline_sum += mean_abs;
        checker->collected++;
        if (checker->collected >= checker->cfg.baseline_windows) {
            checker->baseline = (int32_t)(checker->baseline_sum / checker->collected);
            checker->threshold = (checker->baseline * checker->cfg.baseline_mul_num) / checker->cfg.baseline_mul_den;
            if (checker->threshold < checker->cfg.min_threshold) {
                checker->threshold = checker->cfg.min_threshold;
            }
            checker->threshold_ready = true;
            ESP_LOGI(TAG, "Baseline=%d, Threshold=%d", checker->baseline, checker->threshold);
        }
        return false;
    }

    if (mean_abs >= checker->threshold) {
        checker->hit++;
        if (checker->hit >= checker->cfg.hit_windows) {
            ESP_LOGI(TAG, "Audio activity detected (mean_abs=%d)", mean_abs);
            return true;
        }
    }

    return false;
}
