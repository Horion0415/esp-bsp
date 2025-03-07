#ifndef APP_STORAGE_H
#define APP_STORAGE_H

esp_err_t app_storage_init(void);

esp_err_t app_storage_save_picture(const uint8_t *data, size_t len);

// Management of settings
esp_err_t app_storage_save_settings(settings_info_t *settings, uint16_t interval_time, uint16_t magnification);
esp_err_t app_storage_load_settings(settings_info_t *settings, uint16_t *interval_time, uint16_t *magnification);

// Management of interval state
esp_err_t app_storage_save_interval_state(bool is_active, uint32_t next_wake_time);
esp_err_t app_storage_get_interval_state(bool *is_active, uint32_t *next_wake_time);

#endif
