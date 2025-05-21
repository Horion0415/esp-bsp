#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"

#include "app_camera.h"

// #include "lvgl.h"
#include "bsp/esp-bsp.h"

#include "app_ai_detect.h"

static const char *TAG = "app_camera";

static bool camera_running = false;
static bool camera_finished = false;

static esp_lcd_panel_handle_t st_lcd_panel;

esp_err_t app_camera_init(esp_lcd_panel_handle_t lcd_panel)
{
    gpio_config_t conf;
    conf.mode = GPIO_MODE_INPUT;
    conf.pull_up_en = GPIO_PULLUP_ENABLE;
    conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    conf.intr_type = GPIO_INTR_DISABLE;
    conf.pin_bit_mask = 1LL << 13;
    gpio_config(&conf);
    conf.pin_bit_mask = 1LL << 14;
    gpio_config(&conf);

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = CAMERA_PIN_D0;
    config.pin_d1 = CAMERA_PIN_D1;
    config.pin_d2 = CAMERA_PIN_D2;
    config.pin_d3 = CAMERA_PIN_D3;
    config.pin_d4 = CAMERA_PIN_D4;
    config.pin_d5 = CAMERA_PIN_D5;
    config.pin_d6 = CAMERA_PIN_D6;
    config.pin_d7 = CAMERA_PIN_D7;
    config.pin_xclk = CAMERA_PIN_XCLK;
    config.pin_pclk = CAMERA_PIN_PCLK;
    config.pin_vsync = CAMERA_PIN_VSYNC;
    config.pin_href = CAMERA_PIN_HREF;
    config.pin_sscb_sda = CAMERA_PIN_SIOD;
    config.pin_sscb_scl = CAMERA_PIN_SIOC;
    config.pin_pwdn = CAMERA_PIN_PWDN;
    config.pin_reset = CAMERA_PIN_RESET;
    config.xclk_freq_hz = XCLK_FREQ_HZ;
    config.pixel_format = PIXFORMAT_RGB565;
    config.frame_size = FRAMESIZE_240X240;
    config.jpeg_quality = 12;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    // save lcd panel
    st_lcd_panel = lcd_panel;

    // camera init
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    s->set_vflip(s, 1); // flip it back
    // initial sensors are flipped vertically and colors are a bit saturated
    if (s->id.PID == OV3660_PID)
    {
        s->set_brightness(s, 1);  // up the blightness just a bit
        s->set_saturation(s, -2); // lower the saturation
    }
    s->set_sharpness(s, 2);
    s->set_awb_gain(s, 2);

    return ESP_OK;
}

static void app_camera_task(void *arg)
{
    ESP_LOGD(TAG, "Start");
    while (true)
    {
        if (camera_running) {
            camera_finished = false;
            camera_fb_t *frame = esp_camera_fb_get();
            if(frame == NULL) {
                ESP_LOGE(TAG, "Camera capture failed");
            } else {
                app_coco_od_detect((uint16_t *)frame->buf, 240, 240);
                esp_lcd_panel_draw_bitmap(st_lcd_panel, 0, 0, 240, 240, frame->buf);
                vTaskDelay(100 / portTICK_PERIOD_MS);
                ESP_LOGW(TAG, "Draw bitmap");

                esp_camera_fb_return(frame);
            }
            camera_finished = true;
        } else {
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }
    ESP_LOGD(TAG, "Stop");
    vTaskDelete(NULL);
}

esp_err_t app_camera_begin(void)
{
    xTaskCreatePinnedToCore(app_camera_task, "app_camera_task", 4096, NULL, 5, NULL, 1);
    return ESP_OK;
}

esp_err_t app_camera_start(void)
{
    camera_running = true;
    return ESP_OK;
}

esp_err_t app_camera_stop(void)
{
    camera_running = false;
    return ESP_OK;
}

bool app_camera_is_finished(void)
{
    return camera_finished;
}