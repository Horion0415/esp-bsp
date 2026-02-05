#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wait for voice activity based on PDM PCM energy
 *
 * @param timeout_ms Total timeout in milliseconds
 * @return true if voice activity is detected, false otherwise
 */
bool app_mic_wait_for_voice(uint32_t timeout_ms);

/**
 * @brief Read PDM PCM stream and check if it is active
 *
 * @param sample_rate Sample rate in Hz (e.g., 16000)
 * @param channels Channel count (e.g., 2)
 * @param window_ms Window size in milliseconds (e.g., 50)
 * @param total_windows Total windows to check
 * @param hit_windows Required windows above threshold
 * @return true if audio activity is detected, false otherwise
 */
bool app_mic_check_activity(int sample_rate, int channels, int window_ms, int total_windows, int hit_windows);

#ifdef __cplusplus
}
#endif
