#ifndef APP_STORAGE_H
#define APP_STORAGE_H

esp_err_t app_storage_init(void);

esp_err_t app_storage_save_picture(const uint8_t *data, size_t len);

#endif
