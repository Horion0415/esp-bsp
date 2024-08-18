#include "soc/soc_caps.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "app_button.h"
#include "bsp/esp-bsp.h"
#include "ui.h"

static char *TAG = "app_button";

static button_handle_t buttons[BSP_BUTTON_NUM];

static ScreenType screen = ScreenSpeech; 
static unsigned char status = 0;
static bool sd_card_mounted = false;
static char result[64];
static size_t result_len = 64;

static esp_err_t s_example_write_file(const char *path, char *data, bool append)
{
    ESP_LOGI(TAG, "Opening file %s", path);
    FILE *f = fopen(path, append ? "a" : "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return ESP_FAIL;
    }
    fprintf(f, data);
    fclose(f);
    ESP_LOGI(TAG, "File written");

    return ESP_OK;
}

static void button_cb(void *button_handle, void *usr_data)
{
    int button_pressed = (int) usr_data;
    lv_obj_t *parent = lv_obj_get_parent(ui_PanelMenu);
    lv_color_t color = lv_obj_get_style_bg_color(parent, LV_PART_MAIN);
    switch (button_pressed) {
    case BSP_BUTTON_MENU: // Menu
        if(screen == ScreenButton) {
            lv_obj_set_style_bg_color(ui_PanelMenu, color, LV_PART_MAIN | LV_STATE_DEFAULT);
            status |= 1 << 0;
        }
        break;
    case BSP_BUTTON_PLAY: // Play
        if(screen == ScreenButton) {
            lv_obj_set_style_bg_color(ui_PanelPlay, color, LV_PART_MAIN | LV_STATE_DEFAULT);
            status |= 1 << 1;
        }
        break;
    case BSP_BUTTON_DOWN: // Down
        if (screen == ScreenButton) {
            lv_obj_set_style_bg_color(ui_PanelDown, color, LV_PART_MAIN | LV_STATE_DEFAULT);
            status |= 1 << 2;
        } else if (screen == ScreenCamera) {
            status = 0;
            s_example_write_file(result, "Camera Pass\n", true);
            _ui_screen_change(&ui_ScreenIMU, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_ScreenIMU_screen_init);
            app_button_change_screen(ScreenIMU);
        }
        break;
    case BSP_BUTTON_UP: // Up
        if(screen == ScreenButton) {
            lv_obj_set_style_bg_color(ui_PanelUp, color, LV_PART_MAIN | LV_STATE_DEFAULT);
            status |= 1 << 3;
        }
        break;
    case BSP_BUTTON_BOOT: { // The BOOT button
        return; // Attention: we return here directly
    }
    default:

        break;
    }

    if(status == 0x0F) {
        _ui_screen_change(&ui_ScreenCamera, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_ScreenCamera_screen_init);
        app_button_change_screen(ScreenCamera);

        if(sd_card_mounted == true) {
            s_example_write_file(result, "SD Card Pass\n", true);
        } else {
            s_example_write_file(result, "SD Card Fail\n", true);
        }
        s_example_write_file(result, "Microphone Pass\n", true);
        s_example_write_file(result, "Screen Pass\n", true);
        s_example_write_file(result, "Button Pass\n", true);
    }
}

esp_err_t app_button_change_screen(ScreenType new_screen)
{
    screen = new_screen;
    return ESP_OK;
}

ScreenType app_button_get_screen(void)
{
    return screen;
}

esp_err_t app_sdcard_write_result(void)
{
    s_example_write_file(result, "IMU Pass\n", true);

    return ESP_OK;
}

esp_err_t app_sdcard_init(void)
{
    esp_err_t ret = ESP_OK;
    uint8_t base_mac_addr[6] = {0};
    char mac_str[18];

    if (bsp_sdcard_mount() != ESP_OK) {
        ESP_LOGE(TAG, "SD card mount failed");
        lv_label_set_text(ui_LabelSpeech, "      SD card \n   mount failed");
        lv_obj_set_style_text_color(ui_LabelSpeech, lv_color_hex(0xff0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        sd_card_mounted = false;
        return ESP_FAIL;
    } else {
        sd_card_mounted = true;
    }

    ret = esp_read_mac(base_mac_addr, ESP_MAC_EFUSE_FACTORY);
    snprintf(mac_str, sizeof(mac_str), "%02X-%02X-%02X-%02X-%02X-%02X",
             base_mac_addr[0], base_mac_addr[1], base_mac_addr[2],
             base_mac_addr[3], base_mac_addr[4], base_mac_addr[5]);

    const char *path = BSP_SD_MOUNT_POINT;

    snprintf(result, result_len, "%s/%s.txt", path, mac_str);
    ESP_LOGI(TAG, "MAC: %s", mac_str);

    ret = s_example_write_file(result, "", false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write file");
    }
    return ESP_OK;
}

esp_err_t app_button_init(void)
{
    bsp_iot_button_create(buttons, NULL, BSP_BUTTON_NUM);
    for (int i = 0; i < BSP_BUTTON_NUM; i++) {
        iot_button_register_cb(buttons[i], BUTTON_PRESS_DOWN, button_cb, (void *) i);
    }
    return ESP_OK;
}