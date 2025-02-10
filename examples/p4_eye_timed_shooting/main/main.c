/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <dirent.h> 
#include <fcntl.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_memory_utils.h"

#include "driver/jpeg_encode.h"
#include "driver/ppa.h"
#include "bsp/esp-bsp.h"

#include "esp_sleep.h"

#include "lvgl.h"

#include "app_video.h"
#include "app_usb_msc.h"
#include "app_smtp.h"
#include "app_wifi.h"
#include "app_sntp.h"
#include "ui.h"

#define LOG_MEMORY_SYSTEM_INFO         (0)
#define LOG_TASK_SYSTEM_INFO           (0)
#define LOG_TIME_INTERVAL_MS           (2000)
#define SYS_TASKS_ELAPSED_TIME_MS      (2000)   // Period of stats measurement

#define LED_LIGHT_ON                               (1)
#define WIFI_SWITCH_ON                             (1)

#define ALIGN_UP(num, align)    (((num) + ((align) - 1)) & ~((align) - 1))

#define TIMER_SEC_INTERVAL                         (1 * 1000000)
#define TIMER_MIN_INTERVAL                         (60 * 1000000)
#define UNIT_TIME                                  (TIMER_SEC_INTERVAL)

#define CONFIG_FILE                                BSP_SD_MOUNT_POINT"/info_config.txt"

#define CAPTURE_INDEX                              (10)

#define SCALE_LEVELS 15                             // 总档位数
#define STEPS_PER_LEVEL 6                           // Steps needed for each level

enum {
    SCREEN_EYE_CAMERA,
    SCREEN_EYE_SET,
    SCREEN_EYE_USB,
} screen_index;

static const char *TAG = "main";

static i2c_master_bus_handle_t i2c_handle;
static ppa_client_handle_t ppa_srm_handle = NULL;
static size_t data_cache_line_size = 0;

static void *canvas_buf[EXAMPLE_CAM_BUF_NUM];
static lv_obj_t* cam_canvas;

static uint32_t timed_min = 5;
static bool timed_shooting = false;

// #define DEEP_SLEEP_EVENT_BIT BIT0
// static bool wifi_configured = false;
// static bool email_configured = false;
// static bool smtp_connected = false;

typedef enum {
    WIFI_CONFIGURED_BIT   = BIT1,
    EMAIL_CONFIGURED_BIT  = BIT2,
    SMTP_CONNECTED_BIT    = BIT3,
    DEEP_SLEEP_BIT        = BIT4,
} AppEventBits;

// static EventGroupHandle_t deep_sleep_event_group;
static EventGroupHandle_t app_event_group;

static jpeg_encoder_handle_t jpeg_handle;
static uint32_t jpg_size;
static uint8_t *jpg_buf;
static size_t rx_buffer_size = 0;

static nvs_handle_t nvs_save_handle;

static int scale_levels = SCALE_LEVELS;
static int scale_level_res[SCALE_LEVELS] = {1, 2, 4, 5, 8, 10, 16, 20, 40, 60, 80, 120, 240, 480, 960};
static int knob_count = (SCALE_LEVELS - 1) * STEPS_PER_LEVEL;

static int video_cam_fd0 = -1;

typedef struct {
    char ssid[32];
    char password[64];
    char smtp_server[32];
    char port[16];
    char sender_email[32];
    char sender_password[32];
    char recipient_email[32];
} wifi_email_config;
static wifi_email_config we_config;

static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, uint32_t camera_buf_hes, uint32_t camera_buf_ves, size_t camera_buf_len);
static int get_next_file_index(const char *path);
static void increase_btn_handler(void *button_handle, void *usr_data);
static void decrease_btn_handler(void *button_handle, void *usr_data);
static void mode_switch_btn_handler(void *button_handle, void *usr_data);
static void detect_usb_task(void *arg);
static void wifi_connect_task(void *arg);
static void deep_sleep_task(void *arg);
static bool read_sdcard_config(char *ssid, char *password);
static bool read_email_config(char *smtp_server, char *port, char *sender_email, char *sender_password, char *recipient_email); 
static void deep_sleep_register_rtc_timer_wakeup(void);
static void knob_left_cb(void *arg, void *data);
static void knob_right_cb(void *arg, void *data);
static void encoder_btn_handler(void *arg, void *data);

esp_err_t print_real_time_mem_stats(void);

void app_main(void)
{
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    err = nvs_open("storage", NVS_READWRITE, &nvs_save_handle);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        printf("Done\n");

        // Read
        printf("Reading shutter flag from NVS ... ");
        err |= nvs_get_u32(nvs_save_handle, "timed_min", &timed_min);
        err |= nvs_get_i8(nvs_save_handle, "timed_shooting", (int8_t *)&timed_shooting);
        switch (err) {
            case ESP_OK:
                ESP_LOGI(TAG, "Done\n");
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                printf("The value is not initialized yet!\n");
                break;
            default :
                printf("Error (%s) reading!\n", esp_err_to_name(err));
        }
    }

#if WIFI_SWITCH_ON
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
#endif

    if(esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        ESP_LOGI(TAG, "Wakeup by timer");
        timed_shooting = true;
    } else {
        ESP_LOGI(TAG, "Wakeup by other");
        timed_shooting = false;
    }

    ESP_ERROR_CHECK(bsp_p4_eye_init());

    app_event_group = xEventGroupCreate();

    if(timed_min) {
        const gpio_config_t config = {
            .pin_bit_mask = BIT(GPIO_NUM_3) | BIT(GPIO_NUM_4) | BIT(GPIO_NUM_5),
            .mode = GPIO_MODE_INPUT,
        };

        ESP_ERROR_CHECK(gpio_config(&config));
        ESP_ERROR_CHECK(esp_deep_sleep_enable_gpio_wakeup(BIT(GPIO_NUM_3) | BIT(GPIO_NUM_4) | BIT(GPIO_NUM_5), 0));

        deep_sleep_register_rtc_timer_wakeup();
    }

    // Initialize the knob
    ESP_ERROR_CHECK(bsp_knob_init());
    // Register callback functions
    ESP_ERROR_CHECK(bsp_knob_register_cb(KNOB_LEFT, knob_left_cb, NULL));
    ESP_ERROR_CHECK(bsp_knob_register_cb(KNOB_RIGHT, knob_right_cb, NULL));

    // Initialize the display
    bsp_display_start();

    // Initialize the led
    ESP_ERROR_CHECK(bsp_leds_init());

    // Initialize the PPA
    ppa_client_config_t ppa_srm_config = {
        .oper_type = PPA_OPERATION_SRM,
    };
    ESP_ERROR_CHECK(ppa_register_client(&ppa_srm_config, &ppa_srm_handle));
    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &data_cache_line_size));

    // Initialize the SD card
    ESP_ERROR_CHECK(bsp_sdcard_mount());
    ESP_LOGI(TAG, "SD card mounted");

    if(read_sdcard_config(we_config.ssid, we_config.password)) {
        ESP_LOGI(TAG, "Read wifi config from SD card: SSID: %s, Password: %s", we_config.ssid, we_config.password);

        // wifi_configured = true;
        xEventGroupSetBits(app_event_group, WIFI_CONFIGURED_BIT);
    } else {
        ESP_LOGE(TAG, "Failed to read wifi config from SD card");

        // wifi_configured = false;
        xEventGroupClearBits(app_event_group, WIFI_CONFIGURED_BIT);
    }

    if(read_email_config(we_config.smtp_server, we_config.port, we_config.sender_email, we_config.sender_password, we_config.recipient_email)) {
        ESP_LOGI(TAG, "Read email config from SD card: SMTP Server: %s, Port: %s, Sender Email: %s, Sender Password: %s, Recipient Email: %s", we_config.smtp_server, we_config.port, we_config.sender_email, we_config.sender_password, we_config.recipient_email);

        // email_configured = true;
        xEventGroupSetBits(app_event_group, EMAIL_CONFIGURED_BIT);
    } else {
        ESP_LOGE(TAG, "Failed to read email config from SD card");

        // email_configured = false;
        xEventGroupClearBits(app_event_group, EMAIL_CONFIGURED_BIT);
    }

    // Initialize the USB MSC
    app_usb_msc_init();

    // Initialize the I2C
    ESP_ERROR_CHECK(bsp_i2c_init());
    bsp_get_i2c_bus_handle(&i2c_handle);

    // Initialize the video camera
    esp_err_t ret = app_video_main(i2c_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "video main init failed with error 0x%x", ret);
        return;
    }

    // Open the video device
    video_cam_fd0 = app_video_open(EXAMPLE_CAM_DEV_PATH, APP_VIDEO_FMT);
    if (video_cam_fd0 < 0) {
        ESP_LOGE(TAG, "video cam open failed");
        return;
    }

    // Initialize video capture device
    ESP_ERROR_CHECK(app_video_set_bufs(video_cam_fd0, EXAMPLE_CAM_BUF_NUM, NULL));
    
    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &data_cache_line_size));
    for (int i = 0; i < EXAMPLE_CAM_BUF_NUM; i++) {
        canvas_buf[i] = heap_caps_aligned_calloc(data_cache_line_size, 1, app_video_get_buf_size(), MALLOC_CAP_SPIRAM);
        if (canvas_buf[i] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate canvas buffer");
            return;
        }
    }

    // Initialize the JPEG encoder
    jpeg_encode_engine_cfg_t encode_eng_cfg = {
        .timeout_ms = 70,
    };

    ESP_ERROR_CHECK(jpeg_new_encoder_engine(&encode_eng_cfg, &jpeg_handle));

    jpeg_encode_memory_alloc_cfg_t rx_mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };

    jpg_buf = (uint8_t*)jpeg_alloc_encoder_mem(app_video_get_buf_size() / 10, &rx_mem_cfg, &rx_buffer_size); // Assume that compression ratio of 10 to 1
    assert(jpg_buf != NULL);

    // Register the video frame operation callback
    ESP_ERROR_CHECK(app_video_register_frame_operation_cb(camera_video_frame_operation));

    // Start the camera stream task
    ESP_ERROR_CHECK(app_video_stream_task_start(video_cam_fd0, 0));

    // Initialize the UI
    bsp_display_lock(0);
    screen_index = SCREEN_EYE_CAMERA;

    ui_init();

    lv_label_set_text_fmt(ui_LabelSet, "Set time: %ld minutes\n\n\n\n\n\n\n", timed_min);

    cam_canvas = lv_canvas_create(ui_ScreenMain);
    lv_obj_set_size(cam_canvas, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_align(cam_canvas, LV_ALIGN_CENTER);

    bsp_display_unlock();
    bsp_display_backlight_on();

    // deep_sleep_event_group = xEventGroupCreate();
    xTaskCreatePinnedToCore(deep_sleep_task, "deep_sleep_task", 4096, NULL, 5, NULL, 0);

    /* Init Buttons */
    button_handle_t btns[BSP_BUTTON_NUM];
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_1], BUTTON_PRESS_DOWN, mode_switch_btn_handler, (void *) BSP_BUTTON_1));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_2], BUTTON_PRESS_DOWN, increase_btn_handler, (void *) BSP_BUTTON_2));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_3], BUTTON_PRESS_DOWN, decrease_btn_handler, (void *) BSP_BUTTON_3));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_ED], BUTTON_PRESS_DOWN, encoder_btn_handler, (void *) BSP_BUTTON_ED));

    xTaskCreatePinnedToCore(detect_usb_task, "detect_usb_task", 4096, NULL, 5, NULL, 0);
#if WIFI_SWITCH_ON
    xTaskCreatePinnedToCore(wifi_connect_task, "wifi_connect_task", 4096, NULL, 5, NULL, 0);
#endif

#if LOG_MEMORY_SYSTEM_INFO
    static char buffer[2048];
    while (1) {
        sprintf(buffer, "\t  Biggest /     Free /    Total\n"
                " SRAM : [%8d / %8d / %8d]\n"
                "PSRAM : [%8d / %8d / %8d]\n",
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
        printf("------------ Memory ------------\n");
        printf("%s\n", buffer);

        ESP_ERROR_CHECK(print_real_time_mem_stats());
        printf("\n");

        vTaskDelay(pdMS_TO_TICKS(LOG_TIME_INTERVAL_MS));
    }
#endif
}

static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, uint32_t camera_buf_hes, uint32_t camera_buf_ves, size_t camera_buf_len)
{
    static uint8_t capture_index = 0;
    capture_index++;

    uint16_t block_w = scale_level_res[scale_levels - 1];
    uint16_t block_h = scale_level_res[scale_levels - 1];
    float scale_x = (float)BSP_LCD_H_RES / block_w;
    float scale_y = (float)BSP_LCD_V_RES / block_h;

    ppa_srm_oper_config_t srm_config = {
        .in.buffer = camera_buf,
        .in.pic_w = camera_buf_hes,
        .in.pic_h = camera_buf_ves,
        .in.block_w = block_w,
        .in.block_h = block_h,
        .in.block_offset_x = (camera_buf_hes - block_w) / 2,
        .in.block_offset_y = (camera_buf_ves - block_h) / 2,
        .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .out.buffer = canvas_buf[camera_buf_index],
        .out.buffer_size = ALIGN_UP(BSP_LCD_H_RES * BSP_LCD_V_RES * 2, data_cache_line_size),
        .out.pic_w = BSP_LCD_H_RES,
        .out.pic_h = BSP_LCD_V_RES,
        .out.block_offset_x = 0,
        .out.block_offset_y = 0,
        .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = scale_x,
        .scale_y = scale_y,
        .rgb_swap = 0,
        .byte_swap = 0,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    ESP_ERROR_CHECK(ppa_do_scale_rotate_mirror(ppa_srm_handle, &srm_config));

    uint16_t *canvas_buf_ptr = (uint16_t *)canvas_buf[camera_buf_index];
    for(int i =0 ;i< BSP_LCD_H_RES * BSP_LCD_V_RES; i++) {
        uint16_t swap16 = *(canvas_buf_ptr + i);
        swap16 = (swap16 >> 8) | (swap16 << 8);
        *(canvas_buf_ptr + i) = swap16;
    }

    bsp_display_lock(0);
    lv_canvas_set_buffer(cam_canvas, canvas_buf[camera_buf_index], BSP_LCD_H_RES, BSP_LCD_V_RES, LV_IMG_CF_TRUE_COLOR);
    bsp_display_unlock();

    if(timed_shooting && capture_index > CAPTURE_INDEX) {
        jpeg_encode_cfg_t enc_config = {
            .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
            .sub_sample = JPEG_DOWN_SAMPLING_YUV420,
            .image_quality = 70,
            .width = camera_buf_hes,
            .height = camera_buf_ves,
        };

        timed_shooting = false;
        char file_name[64];

#if LED_LIGHT_ON
        bsp_led_set(BSP_LED_WHITE, 1); // Turn on the white LED
#endif

        ESP_ERROR_CHECK(jpeg_encoder_process(jpeg_handle, &enc_config, camera_buf, app_video_get_buf_size(), jpg_buf, rx_buffer_size, &jpg_size));

        int image_count = get_next_file_index(BSP_SD_MOUNT_POINT"/pic_save");
        snprintf(file_name, sizeof(file_name), BSP_SD_MOUNT_POINT"/pic_save/OUTJPG_%d.JPG", image_count++);

        FILE *file_jpg = fopen(file_name, "wb");
        ESP_LOGI(TAG, "Writing jpg to %s", file_name);
        if (file_jpg == NULL) {
            ESP_LOGE(TAG, "fopen file_jpg error");
        }
        fwrite(jpg_buf, 1, jpg_size, file_jpg);
        fclose(file_jpg);

#if LED_LIGHT_ON
        bsp_led_set(BSP_LED_WHITE, 0);  // Turn off the white LED
#endif

#if WIFI_SWITCH_ON
        if(xEventGroupGetBits(app_event_group) & SMTP_CONNECTED_BIT) {
            ESP_ERROR_CHECK(app_smtp_tls_init());
            ESP_ERROR_CHECK(app_smtp_connect_server());
            ESP_ERROR_CHECK(app_smtp_perform_authentication());
            app_smtp_compose_email(jpg_buf, jpg_size, file_name);
        }
#endif  
        vTaskDelay(300 / portTICK_PERIOD_MS);
        xEventGroupSetBits(app_event_group, DEEP_SLEEP_BIT);
    }
}

static void detect_usb_task(void *arg)
{
    while (1) {
        if(app_usb_msc_stage()) {
            app_usb_set_exposed(false);
            
            bsp_display_lock(0);
            screen_index = SCREEN_EYE_USB;
            _ui_screen_change(&ui_ScreenUSB, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_ScreenUSB_screen_init);
            bsp_display_unlock();

            vTaskDelete(NULL);
        }

        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

static void wifi_connect_task(void *arg)
{
    // Connect to the wifi network
    if(xEventGroupGetBits(app_event_group) & WIFI_CONFIGURED_BIT) {
        wifi_init_sta((uint8_t *)we_config.ssid, (uint8_t *)we_config.password);
    }
    
    if(app_wifi_get_connected() && (xEventGroupGetBits(app_event_group) & EMAIL_CONFIGURED_BIT)) {
        app_smtp_set_config(we_config.smtp_server, we_config.port, we_config.sender_email, we_config.sender_password, we_config.recipient_email);
        
        app_sntp_init();

        ESP_ERROR_CHECK(app_smtp_tls_init());
        ESP_ERROR_CHECK(app_smtp_connect_server());
        ESP_ERROR_CHECK(app_smtp_perform_authentication());

        xEventGroupSetBits(app_event_group, SMTP_CONNECTED_BIT);
    }

    ESP_LOGI(TAG, "wifi_connect_task end");
    vTaskDelete(NULL);
}

// Deep sleep task
static void deep_sleep_task(void *arg)
{
    while (1) {
        // Wait for the deep sleep event
        xEventGroupWaitBits(app_event_group, DEEP_SLEEP_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

        nvs_set_i8(nvs_save_handle, "timed_shooting", 1);

        app_video_stream_task_stop(video_cam_fd0);
        app_video_wait_video_stop();
        app_video_close(video_cam_fd0);

        bsp_sdcard_unmount();

        bsp_display_enter_sleep();

        bsp_sleep_io_init();

        ESP_LOGI(TAG, "Deep sleep event triggered");
        esp_deep_sleep_start();
    }
}

static int get_next_file_index(const char *path) 
{
    DIR *dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory %s", path);
        return 0;
    }

    struct dirent *entry;
    int max_index = -1;  

    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, "OUTJPG_") && strstr(entry->d_name, ".JPG")) {
            int index;
            if (sscanf(entry->d_name, "OUTJPG_%d.JPG", &index) == 1) {
                if (index > max_index) {
                    max_index = index;  
                }
            }
        }
    }

    closedir(dir);
    return max_index + 1;  
}

    static void mode_switch_btn_handler(void *button_handle, void *usr_data)
{
    if(screen_index == SCREEN_EYE_USB) {
        return;
    }

    if(screen_index == SCREEN_EYE_CAMERA) {
        screen_index = SCREEN_EYE_SET;

        bsp_display_lock(0);
        _ui_screen_change(&ui_ScreenSet, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_ScreenSet_screen_init);
        bsp_display_unlock();
    } else {
        screen_index = SCREEN_EYE_CAMERA;

        deep_sleep_register_rtc_timer_wakeup();

        bsp_display_lock(0);
        _ui_screen_change(&ui_ScreenMain, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_ScreenMain_screen_init);
        bsp_display_unlock();
    }
}

static void increase_btn_handler(void *button_handle, void *usr_data)
{
    timed_min += 5;
    if(timed_min > 120) {
        timed_min = 5;
    }

    bsp_display_lock(0);
    lv_label_set_text_fmt(ui_LabelSet, "Set time: %ld minutes\n\n\n\n\n\n\n", timed_min);
    bsp_display_unlock();
    
    ESP_LOGI(TAG, "timed_min: %ld", timed_min);

    ESP_ERROR_CHECK(nvs_set_u32(nvs_save_handle, "timed_min", timed_min));
}

static void decrease_btn_handler(void *button_handle, void *usr_data)
{
    timed_min -= 5;
    if(timed_min < 5) {
        timed_min = 120;
    }

    bsp_display_lock(0);
    lv_label_set_text_fmt(ui_LabelSet, "Set time: %ld minutes\n\n\n\n\n\n\n", timed_min);
    bsp_display_unlock();

    ESP_LOGI(TAG, "timed_min: %ld", timed_min);

    ESP_ERROR_CHECK(nvs_set_u32(nvs_save_handle, "timed_min", timed_min));
}

static bool read_sdcard_config(char *ssid, char *password) 
{
    FILE *file = fopen(CONFIG_FILE, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open Wi-Fi config file");
        return false;
    }

    char buffer[100];
    while (fgets(buffer, sizeof(buffer), file)) {
        // 去除行尾换行符
        buffer[strcspn(buffer, "\r\n")] = 0;

        if (strncmp(buffer, "SSID: ", 6) == 0) {
            strncpy(ssid, buffer + 6, 32); // 读取 "SSID: " 后的内容
        } else if (strncmp(buffer, "PASSWORD: ", 10) == 0) {
            strncpy(password, buffer + 10, 64); // 读取 "PASSWORD: " 后的内容
        }
    }

    fclose(file);
    return (strlen(ssid) > 0 && strlen(password) > 0); // 确保SSID和密码不为空
}

bool read_email_config(char *smtp_server, char *port, char *sender_email, char *sender_password, char *recipient_email) 
{
    FILE *file = fopen(CONFIG_FILE, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open email config file");
        return false;
    }

    char buffer[100];
    while (fgets(buffer, sizeof(buffer), file)) {
        buffer[strcspn(buffer, "\r\n")] = 0;  // 去除行尾换行符

        if (strncmp(buffer, "SMTP_SERVER: ", 13) == 0) {
            strncpy(smtp_server, buffer + 13, 32);
        } else if (strncmp(buffer, "PORT: ", 6) == 0) {
            strncpy(port, buffer + 6, 16);
        } else if (strncmp(buffer, "SENDER_EMAIL: ", 14) == 0) {
            strncpy(sender_email, buffer + 14, 32);
        } else if (strncmp(buffer, "SENDER_PASSWORD: ", 17) == 0) {
            strncpy(sender_password, buffer + 17, 32);
        } else if (strncmp(buffer, "RECIPIENT_EMAIL: ", 17) == 0) {
            strncpy(recipient_email, buffer + 17, 32);
        }
    }

    fclose(file);
    return (strlen(smtp_server) > 0 && strlen(port) > 0 && strlen(sender_email) > 0 && strlen(sender_password) > 0 && strlen(recipient_email) > 0); // 确保所有配置项不为空
}

static void deep_sleep_register_rtc_timer_wakeup(void)
{
    printf("Enabling timer wakeup, %ldmin\n", timed_min);
    // ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(timed_min * 60 * 1000000));
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(timed_min * UNIT_TIME));
}

static int get_current_level(int count)
{
    return (count / STEPS_PER_LEVEL) + 1;
}

static void knob_left_cb(void *arg, void *data)
{
    //knob_handle_t knob = (knob_handle_t)arg;
    knob_count--;
    if (knob_count < 0) {
        knob_count = 0;
    }
    scale_levels = get_current_level(knob_count);
    ESP_LOGD(TAG, "Current level: %d", scale_levels);
}

static void knob_right_cb(void *arg, void *data)
{
    //knob_handle_t knob = (knob_handle_t)arg;
    knob_count++;
    if (knob_count > (SCALE_LEVELS * STEPS_PER_LEVEL - 1)) {
        knob_count = SCALE_LEVELS * STEPS_PER_LEVEL - 1;
    }
    scale_levels = get_current_level(knob_count);
    ESP_LOGD(TAG, "Current level: %d", scale_levels);
}

static void encoder_btn_handler(void *arg, void *data)
{
    ESP_LOGI(TAG, "Encoder button pressed");
    
    xEventGroupSetBits(app_event_group, DEEP_SLEEP_BIT);
}

#if LOG_TASK_SYSTEM_INFO
#define ARRAY_SIZE_OFFSET                   8   // Increase this if audio_sys_get_real_time_stats returns ESP_ERR_INVALID_SIZE

#define audio_malloc    malloc
#define audio_calloc    calloc
#define audio_free      free
#define AUDIO_MEM_CHECK(tag, x, action) if (x == NULL) { \
        ESP_LOGE(tag, "Memory exhausted (%s:%d)", __FILE__, __LINE__); \
        action; \
    }

const char *task_state[] = {
    "Running",
    "Ready",
    "Blocked",
    "Suspended",
    "Deleted"
};

/** @brief
 * "Extr": Allocated task stack from psram, "Intr": Allocated task stack from internel
 */
const char *task_stack[] = {"Extr", "Intr"};

esp_err_t print_real_time_mem_stats(void)
{
#if (CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID && CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)
    TaskStatus_t *start_array = NULL, *end_array = NULL;
    UBaseType_t start_array_size, end_array_size;
    uint32_t start_run_time, end_run_time;
    uint32_t total_elapsed_time;
    uint32_t task_elapsed_time, percentage_time;
    esp_err_t ret;

    // Allocate array to store current task states
    start_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    start_array = (TaskStatus_t *)audio_malloc(sizeof(TaskStatus_t) * start_array_size);
    AUDIO_MEM_CHECK(TAG, start_array, {
        ret = ESP_FAIL;
        goto exit;
    });
    // Get current task states
    start_array_size = uxTaskGetSystemState(start_array, start_array_size, &start_run_time);
    if (start_array_size == 0) {
        ESP_LOGE(TAG, "Insufficient array size for uxTaskGetSystemState. Trying increasing ARRAY_SIZE_OFFSET");
        ret = ESP_FAIL;
        goto exit;
    }

    vTaskDelay(pdMS_TO_TICKS(SYS_TASKS_ELAPSED_TIME_MS));

    // Allocate array to store tasks states post delay
    end_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    end_array = (TaskStatus_t *)audio_malloc(sizeof(TaskStatus_t) * end_array_size);
    AUDIO_MEM_CHECK(TAG, start_array, {
        ret = ESP_FAIL;
        goto exit;
    });

    // Get post delay task states
    end_array_size = uxTaskGetSystemState(end_array, end_array_size, &end_run_time);
    if (end_array_size == 0) {
        ESP_LOGE(TAG, "Insufficient array size for uxTaskGetSystemState. Trying increasing ARRAY_SIZE_OFFSET");
        ret = ESP_FAIL;
        goto exit;
    }

    // Calculate total_elapsed_time in units of run time stats clock period.
    total_elapsed_time = (end_run_time - start_run_time);
    if (total_elapsed_time == 0) {
        ESP_LOGE(TAG, "Delay duration too short. Trying increasing SYS_TASKS_ELAPSED_TIME_MS");
        ret = ESP_FAIL;
        goto exit;
    }

    ESP_LOGI(TAG, "| Task              | Run Time    | Per | Prio | HWM       | State   | CoreId   | Stack ");

    // Match each task in start_array to those in the end_array
    for (int i = 0; i < start_array_size; i++) {
        for (int j = 0; j < end_array_size; j++) {
            if (start_array[i].xHandle == end_array[j].xHandle) {

                task_elapsed_time = end_array[j].ulRunTimeCounter - start_array[i].ulRunTimeCounter;
                percentage_time = (task_elapsed_time * 100UL) / (total_elapsed_time * portNUM_PROCESSORS);
                ESP_LOGI(TAG, "| %-17s | %-11d |%2d%%  | %-4u | %-9u | %-7s | %-8x | %s",
                                start_array[i].pcTaskName, (int)task_elapsed_time, (int)percentage_time, start_array[i].uxCurrentPriority,
                                (int)start_array[i].usStackHighWaterMark, task_state[(start_array[i].eCurrentState)],
                                start_array[i].xCoreID, task_stack[esp_ptr_internal(pxTaskGetStackStart(start_array[i].xHandle))]);

                // Mark that task have been matched by overwriting their handles
                start_array[i].xHandle = NULL;
                end_array[j].xHandle = NULL;
                break;
            }
        }
    }

    // Print unmatched tasks
    for (int i = 0; i < start_array_size; i++) {
        if (start_array[i].xHandle != NULL) {
            ESP_LOGI(TAG, "| %s | Deleted", start_array[i].pcTaskName);
        }
    }
    for (int i = 0; i < end_array_size; i++) {
        if (end_array[i].xHandle != NULL) {
            ESP_LOGI(TAG, "| %s | Created", end_array[i].pcTaskName);
        }
    }
    printf("\n");
    ret = ESP_OK;

exit:    // Common return path
    if (start_array) {
        audio_free(start_array);
        start_array = NULL;
    }
    if (end_array) {
        audio_free(end_array);
        end_array = NULL;
    }
    return ret;
#else
    ESP_LOGW(TAG, "Please enbale `CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID` and `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS` in menuconfig");
    return ESP_FAIL;
#endif
}
#endif