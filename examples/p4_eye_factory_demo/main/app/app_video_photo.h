#ifndef APP_VIDEO_PHOTO_H
#define APP_VIDEO_PHOTO_H


esp_err_t app_video_photo_init(void);

esp_err_t take_and_save_photo(uint8_t *camera_buf, uint32_t width, uint32_t height);

esp_err_t app_video_stream_set_photo_resolution(photo_resolution_t resolution);

#endif
