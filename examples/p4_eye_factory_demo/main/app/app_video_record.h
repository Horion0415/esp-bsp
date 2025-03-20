#ifndef APP_VIDEO_RECORD_H
#define APP_VIDEO_RECORD_H


esp_err_t app_video_record_init(void);

esp_err_t app_video_stream_set_video_resolution(photo_resolution_t resolution);

esp_err_t take_and_save_video(uint8_t *camera_buf, uint32_t width, uint32_t height);

esp_err_t app_video_stream_start_recording(void);

esp_err_t app_video_stream_stop_recording(void);

#endif
