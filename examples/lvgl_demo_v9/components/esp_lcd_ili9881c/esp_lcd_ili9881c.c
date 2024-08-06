/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "soc/soc_caps.h"

#if SOC_MIPI_DSI_SUPPORTED
#include "esp_check.h"
#include "esp_cache.h"
#include "esp_dma_utils.h"
#include "esp_private/esp_cache_private.h"
#include "esp_log.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ppa.h"
#include "esp_lcd_ili9881c.h"

#define ILI9881C_CMD_CNDBKxSEL                (0xFF)
#define ILI9881C_CMD_BKxSEL_BYTE0             (0x98)
#define ILI9881C_CMD_BKxSEL_BYTE1             (0x81)
#define ILI9881C_CMD_BKxSEL_BYTE2_PAGE0       (0x00)
#define ILI9881C_CMD_BKxSEL_BYTE2_PAGE1       (0x01)
#define ILI9881C_CMD_BKxSEL_BYTE2_PAGE2       (0x02)
#define ILI9881C_CMD_BKxSEL_BYTE2_PAGE3       (0x03)
#define ILI9881C_CMD_BKxSEL_BYTE2_PAGE4       (0x04)

#define ILI9881C_CMD_GS_BIT       (1 << 0)
#define ILI9881C_CMD_SS_BIT       (1 << 1)

#define ALIGN_UP_BY(num, align) (((num) + ((align) - 1)) & ~((align) - 1))

typedef enum {    
    ROTATION_NONE = 0,
    ROTATION_90,            
} RotationAngle;

typedef struct {
    esp_lcd_panel_io_handle_t io;
    ppa_client_handle_t ppa_client_srm_handle;
    int reset_gpio_num;
    uint8_t madctl_val; // save current value of LCD_CMD_MADCTL register
    uint8_t colmod_val; // save surrent value of LCD_CMD_COLMOD register
    const ili9881c_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
    void *lcd_buffer[3];
    esp_lcd_dpi_panel_config_t dpi_config;
    struct {
        unsigned int reset_level: 1;
        RotationAngle rotation;
    } flags;
    // To save the original functions of MIPI DPI panel
    esp_err_t (*del)(esp_lcd_panel_t *panel);
    esp_err_t (*init)(esp_lcd_panel_t *panel);
    esp_err_t (*draw_bitmap)(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
} ili9881c_panel_t;

static const char *TAG = "ili9881c";

static esp_err_t panel_ili9881c_del(esp_lcd_panel_t *panel);
static esp_err_t panel_ili9881c_init(esp_lcd_panel_t *panel);
static esp_err_t panel_ili9881c_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
static esp_err_t panel_ili9881c_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_ili9881c_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_ili9881c_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_ili9881c_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t panel_ili9881c_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_ili9881c_disp_on_off(esp_lcd_panel_t *panel, bool on_off);
static esp_err_t panel_ili9881c_sleep(esp_lcd_panel_t *panel, bool sleep);

esp_err_t esp_lcd_new_panel_ili9881c(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config,
                                   esp_lcd_panel_handle_t *ret_panel)
{
    ESP_RETURN_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, TAG, "invalid arguments");
    ili9881c_vendor_config_t *vendor_config = (ili9881c_vendor_config_t *)panel_dev_config->vendor_config;
    ESP_RETURN_ON_FALSE(vendor_config && vendor_config->mipi_config.dpi_config && vendor_config->mipi_config.dsi_bus, ESP_ERR_INVALID_ARG, TAG,
                        "invalid vendor config");

    esp_err_t ret = ESP_OK;
    ili9881c_panel_t *ili9881c = (ili9881c_panel_t *)calloc(1, sizeof(ili9881c_panel_t));
    ESP_RETURN_ON_FALSE(ili9881c, ESP_ERR_NO_MEM, TAG, "no mem for ili9881c panel");

    if (panel_dev_config->reset_gpio_num >= 0) {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
    }

    switch (panel_dev_config->color_space) {
    case LCD_RGB_ELEMENT_ORDER_RGB:
        ili9881c->madctl_val = 0;
        break;
    case LCD_RGB_ELEMENT_ORDER_BGR:
        ili9881c->madctl_val |= LCD_CMD_BGR_BIT;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported color space");
        break;
    }

    switch (panel_dev_config->bits_per_pixel) {
    case 16: // RGB565
        ili9881c->colmod_val = 0x55;
        break;
    case 18: // RGB666
        ili9881c->colmod_val = 0x66;
        break;
    case 24: // RGB888
        ili9881c->colmod_val = 0x77;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported pixel width");
        break;
    }

    // The ID register is on the CMD_Page 1
    uint8_t ID1, ID2, ID3;
    esp_lcd_panel_io_tx_param(io, ILI9881C_CMD_CNDBKxSEL, (uint8_t[]) {
        ILI9881C_CMD_BKxSEL_BYTE0, ILI9881C_CMD_BKxSEL_BYTE1, ILI9881C_CMD_BKxSEL_BYTE2_PAGE1
    }, 3);
    esp_lcd_panel_io_rx_param(io, 0x00, &ID1, 1);
    esp_lcd_panel_io_rx_param(io, 0x01, &ID2, 1);
    esp_lcd_panel_io_rx_param(io, 0x02, &ID3, 1);
    ESP_LOGI(TAG, "ID1: 0x%x, ID2: 0x%x, ID3: 0x%x", ID1, ID2, ID3);

    ili9881c->io = io;
    ili9881c->init_cmds = vendor_config->init_cmds;
    ili9881c->init_cmds_size = vendor_config->init_cmds_size;
    ili9881c->reset_gpio_num = panel_dev_config->reset_gpio_num;
    ili9881c->flags.reset_level = panel_dev_config->flags.reset_active_high;
    ili9881c->dpi_config = *(vendor_config->mipi_config.dpi_config);

    // Create MIPI DPI panel
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_dpi(vendor_config->mipi_config.dsi_bus, vendor_config->mipi_config.dpi_config, ret_panel), err, TAG,
                      "create MIPI DPI panel failed");
    ESP_LOGD(TAG, "new MIPI DPI panel @%p", *ret_panel);

    ppa_client_config_t ppa_client_config = {
        .oper_type = PPA_OPERATION_SRM,
    };
    ESP_ERROR_CHECK(ppa_register_client(&ppa_client_config, &ili9881c->ppa_client_srm_handle));

    int lcd_buffer_num = (ili9881c->dpi_config).num_fbs;
    if (lcd_buffer_num == 1) {
        ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(*ret_panel, 1, &(ili9881c->lcd_buffer[0])));
    } else if (lcd_buffer_num == 2) {
        ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(*ret_panel, 2, &(ili9881c->lcd_buffer[0]), &(ili9881c->lcd_buffer[1])));
    } else if (lcd_buffer_num == 3) {
        ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(*ret_panel, 3, &(ili9881c->lcd_buffer[0]), &(ili9881c->lcd_buffer[1]), &(ili9881c->lcd_buffer[2])));
    } else {
        ESP_LOGE(TAG, "invalid number of frame buffers: %d", lcd_buffer_num);
        ret = ESP_ERR_INVALID_ARG;
        goto err;
    }

    // Save the original functions of MIPI DPI panel
    ili9881c->del = (*ret_panel)->del;
    ili9881c->init = (*ret_panel)->init;
    ili9881c->draw_bitmap = (*ret_panel)->draw_bitmap;
    // Overwrite the functions of MIPI DPI panel
    (*ret_panel)->del = panel_ili9881c_del;
    (*ret_panel)->init = panel_ili9881c_init;
    (*ret_panel)->draw_bitmap = panel_ili9881c_draw_bitmap;
    (*ret_panel)->reset = panel_ili9881c_reset;
    (*ret_panel)->mirror = panel_ili9881c_mirror;
    (*ret_panel)->swap_xy = panel_ili9881c_swap_xy;
    (*ret_panel)->set_gap = panel_ili9881c_set_gap;
    (*ret_panel)->invert_color = panel_ili9881c_invert_color;
    (*ret_panel)->disp_on_off = panel_ili9881c_disp_on_off;
    (*ret_panel)->disp_sleep = panel_ili9881c_sleep;
    (*ret_panel)->user_data = ili9881c;
    ESP_LOGD(TAG, "new ili9881c panel @%p", ili9881c);

    return ESP_OK;

err:
    if (ili9881c) {
        if (panel_dev_config->reset_gpio_num >= 0) {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(ili9881c);
    }
    return ret;
}

static const ili9881c_lcd_init_cmd_t vendor_specific_init_default[] = {
    // {cmd, { data }, data_size, delay_ms}
    /**** CMD_Page 3 ****/
    {ILI9881C_CMD_CNDBKxSEL, (uint8_t []){ILI9881C_CMD_BKxSEL_BYTE0, ILI9881C_CMD_BKxSEL_BYTE1, ILI9881C_CMD_BKxSEL_BYTE2_PAGE3}, 3, 0},
    {0x01, (uint8_t []){0x00}, 1, 0},
    {0x02, (uint8_t []){0x00}, 1, 0},
    {0x03, (uint8_t []){0x53}, 1, 0},
    {0x04, (uint8_t []){0x53}, 1, 0},
    {0x05, (uint8_t []){0x13}, 1, 0},
    {0x06, (uint8_t []){0x04}, 1, 0},
    {0x07, (uint8_t []){0x02}, 1, 0},
    {0x08, (uint8_t []){0x02}, 1, 0},
    {0x09, (uint8_t []){0x00}, 1, 0},
    {0x0a, (uint8_t []){0x00}, 1, 0},
    {0x0b, (uint8_t []){0x00}, 1, 0},
    {0x0c, (uint8_t []){0x00}, 1, 0},
    {0x0d, (uint8_t []){0x00}, 1, 0},
    {0x0e, (uint8_t []){0x00}, 1, 0},
    {0x0f, (uint8_t []){0x00}, 1, 0},
    {0x10, (uint8_t []){0x00}, 1, 0},
    {0x11, (uint8_t []){0x00}, 1, 0},
    {0x12, (uint8_t []){0x00}, 1, 0},
    {0x13, (uint8_t []){0x00}, 1, 0},
    {0x14, (uint8_t []){0x00}, 1, 0},
    {0x15, (uint8_t []){0x00}, 1, 0},
    {0x16, (uint8_t []){0x00}, 1, 0},
    {0x17, (uint8_t []){0x00}, 1, 0},
    {0x18, (uint8_t []){0x00}, 1, 0},
    {0x19, (uint8_t []){0x00}, 1, 0},
    {0x1a, (uint8_t []){0x00}, 1, 0},
    {0x1b, (uint8_t []){0x00}, 1, 0},
    {0x1c, (uint8_t []){0x00}, 1, 0},
    {0x1d, (uint8_t []){0x00}, 1, 0},
    {0x1e, (uint8_t []){0xc0}, 1, 0},
    {0x1f, (uint8_t []){0x80}, 1, 0},
    {0x20, (uint8_t []){0x02}, 1, 0},
    {0x21, (uint8_t []){0x09}, 1, 0},
    {0x22, (uint8_t []){0x00}, 1, 0},
    {0x23, (uint8_t []){0x00}, 1, 0},
    {0x24, (uint8_t []){0x00}, 1, 0},
    {0x25, (uint8_t []){0x00}, 1, 0},
    {0x26, (uint8_t []){0x00}, 1, 0},
    {0x27, (uint8_t []){0x00}, 1, 0},
    {0x28, (uint8_t []){0x55}, 1, 0},
    {0x29, (uint8_t []){0x03}, 1, 0},
    {0x2a, (uint8_t []){0x00}, 1, 0},
    {0x2b, (uint8_t []){0x00}, 1, 0},
    {0x2c, (uint8_t []){0x00}, 1, 0},
    {0x2d, (uint8_t []){0x00}, 1, 0},
    {0x2e, (uint8_t []){0x00}, 1, 0},
    {0x2f, (uint8_t []){0x00}, 1, 0},
    {0x30, (uint8_t []){0x00}, 1, 0},
    {0x31, (uint8_t []){0x00}, 1, 0},
    {0x32, (uint8_t []){0x00}, 1, 0},
    {0x33, (uint8_t []){0x00}, 1, 0},
    {0x34, (uint8_t []){0x00}, 1, 0},
    {0x35, (uint8_t []){0x00}, 1, 0},
    {0x36, (uint8_t []){0x00}, 1, 0},
    {0x37, (uint8_t []){0x00}, 1, 0},
    {0x38, (uint8_t []){0x3C}, 1, 0},
    {0x39, (uint8_t []){0x00}, 1, 0},
    {0x3a, (uint8_t []){0x00}, 1, 0},
    {0x3b, (uint8_t []){0x00}, 1, 0},
    {0x3c, (uint8_t []){0x00}, 1, 0},
    {0x3d, (uint8_t []){0x00}, 1, 0},
    {0x3e, (uint8_t []){0x00}, 1, 0},
    {0x3f, (uint8_t []){0x00}, 1, 0},
    {0x40, (uint8_t []){0x00}, 1, 0},
    {0x41, (uint8_t []){0x00}, 1, 0},
    {0x42, (uint8_t []){0x00}, 1, 0},
    {0x43, (uint8_t []){0x00}, 1, 0},
    {0x44, (uint8_t []){0x00}, 1, 0},
    {0x50, (uint8_t []){0x01}, 1, 0},
    {0x51, (uint8_t []){0x23}, 1, 0},
    {0x52, (uint8_t []){0x45}, 1, 0},
    {0x53, (uint8_t []){0x67}, 1, 0},
    {0x54, (uint8_t []){0x89}, 1, 0},
    {0x55, (uint8_t []){0xab}, 1, 0},
    {0x56, (uint8_t []){0x01}, 1, 0},
    {0x57, (uint8_t []){0x23}, 1, 0},
    {0x58, (uint8_t []){0x45}, 1, 0},
    {0x59, (uint8_t []){0x67}, 1, 0},
    {0x5a, (uint8_t []){0x89}, 1, 0},
    {0x5b, (uint8_t []){0xab}, 1, 0},
    {0x5c, (uint8_t []){0xcd}, 1, 0},
    {0x5d, (uint8_t []){0xef}, 1, 0},
    {0x5e, (uint8_t []){0x01}, 1, 0},
    {0x5f, (uint8_t []){0x08}, 1, 0},
    {0x60, (uint8_t []){0x02}, 1, 0},
    {0x61, (uint8_t []){0x02}, 1, 0},
    {0x62, (uint8_t []){0x0A}, 1, 0},
    {0x63, (uint8_t []){0x15}, 1, 0},
    {0x64, (uint8_t []){0x14}, 1, 0},
    {0x65, (uint8_t []){0x02}, 1, 0},
    {0x66, (uint8_t []){0x11}, 1, 0},
    {0x67, (uint8_t []){0x10}, 1, 0},
    {0x68, (uint8_t []){0x02}, 1, 0},
    {0x69, (uint8_t []){0x0F}, 1, 0},
    {0x6a, (uint8_t []){0x0E}, 1, 0},
    {0x6b, (uint8_t []){0x02}, 1, 0},
    {0x6c, (uint8_t []){0x0D}, 1, 0},
    {0x6d, (uint8_t []){0x0C}, 1, 0},
    {0x6e, (uint8_t []){0x06}, 1, 0},
    {0x6f, (uint8_t []){0x02}, 1, 0},
    {0x70, (uint8_t []){0x02}, 1, 0},
    {0x71, (uint8_t []){0x02}, 1, 0},
    {0x72, (uint8_t []){0x02}, 1, 0},
    {0x73, (uint8_t []){0x02}, 1, 0},
    {0x74, (uint8_t []){0x02}, 1, 0},
    {0x75, (uint8_t []){0x06}, 1, 0},
    {0x76, (uint8_t []){0x02}, 1, 0},
    {0x77, (uint8_t []){0x02}, 1, 0},
    {0x78, (uint8_t []){0x0A}, 1, 0},
    {0x79, (uint8_t []){0x15}, 1, 0},
    {0x7a, (uint8_t []){0x14}, 1, 0},
    {0x7b, (uint8_t []){0x02}, 1, 0},
    {0x7c, (uint8_t []){0x10}, 1, 0},
    {0x7d, (uint8_t []){0x11}, 1, 0},
    {0x7e, (uint8_t []){0x02}, 1, 0},
    {0x7f, (uint8_t []){0x0C}, 1, 0},
    {0x80, (uint8_t []){0x0D}, 1, 0},
    {0x81, (uint8_t []){0x02}, 1, 0},
    {0x82, (uint8_t []){0x0E}, 1, 0},
    {0x83, (uint8_t []){0x0F}, 1, 0},
    {0x84, (uint8_t []){0x08}, 1, 0},
    {0x85, (uint8_t []){0x02}, 1, 0},
    {0x86, (uint8_t []){0x02}, 1, 0},
    {0x87, (uint8_t []){0x02}, 1, 0},
    {0x88, (uint8_t []){0x02}, 1, 0},
    {0x89, (uint8_t []){0x02}, 1, 0},
    {0x8A, (uint8_t []){0x02}, 1, 0},
    {ILI9881C_CMD_CNDBKxSEL, (uint8_t []){ILI9881C_CMD_BKxSEL_BYTE0, ILI9881C_CMD_BKxSEL_BYTE1, ILI9881C_CMD_BKxSEL_BYTE2_PAGE4}, 3, 0},
    {0x6C, (uint8_t []){0x15}, 1, 0},
    {0x6E, (uint8_t []){0x30}, 1, 0},
    {0x6F, (uint8_t []){0x33}, 1, 0},
    {0x8D, (uint8_t []){0x1F}, 1, 0},
    {0x87, (uint8_t []){0xBA}, 1, 0},
    {0x26, (uint8_t []){0x76}, 1, 0},
    {0xB2, (uint8_t []){0xD1}, 1, 0},
    {0x35, (uint8_t []){0x1F}, 1, 0},
    {0x33, (uint8_t []){0x14}, 1, 0},
    {0x3A, (uint8_t []){0xA9}, 1, 0},
    {0x3B, (uint8_t []){0x3D}, 1, 0},
    {0x38, (uint8_t []){0x01}, 1, 0},
    {0x39, (uint8_t []){0x00}, 1, 0},
    {ILI9881C_CMD_CNDBKxSEL, (uint8_t []){ILI9881C_CMD_BKxSEL_BYTE0, ILI9881C_CMD_BKxSEL_BYTE1, ILI9881C_CMD_BKxSEL_BYTE2_PAGE1}, 3, 0},
    {0x22, (uint8_t []){0x09}, 1, 0},
    {0x31, (uint8_t []){0x00}, 1, 0},
    {0x40, (uint8_t []){0x53}, 1, 0},
    {0x50, (uint8_t []){0xC0}, 1, 0},
    {0x51, (uint8_t []){0xC0}, 1, 0},
    {0x53, (uint8_t []){0x47}, 1, 0},
    {0x55, (uint8_t []){0x46}, 1, 0},
    {0x60, (uint8_t []){0x28}, 1, 0},
    {0x2E, (uint8_t []){0xC8}, 1, 0},
    {0xA0, (uint8_t []){0x01}, 1, 0},
    {0xA1, (uint8_t []){0x10}, 1, 0},
    {0xA2, (uint8_t []){0x1B}, 1, 0},
    {0xA3, (uint8_t []){0x0C}, 1, 0},
    {0xA4, (uint8_t []){0x14}, 1, 0},
    {0xA5, (uint8_t []){0x25}, 1, 0},
    {0xA6, (uint8_t []){0x1A}, 1, 0},
    {0xA7, (uint8_t []){0x1D}, 1, 0},
    {0xA8, (uint8_t []){0x68}, 1, 0},
    {0xA9, (uint8_t []){0x1B}, 1, 0},
    {0xAA, (uint8_t []){0x26}, 1, 0},
    {0xAB, (uint8_t []){0x5B}, 1, 0},
    {0xAC, (uint8_t []){0x1B}, 1, 0},
    {0xAD, (uint8_t []){0x17}, 1, 0},
    {0xAE, (uint8_t []){0x4F}, 1, 0},
    {0xAF, (uint8_t []){0x24}, 1, 0},
    {0xB0, (uint8_t []){0x2A}, 1, 0},
    {0xB1, (uint8_t []){0x4E}, 1, 0},
    {0xB2, (uint8_t []){0x5F}, 1, 0},
    {0xB3, (uint8_t []){0x39}, 1, 0},
    {0xB7, (uint8_t []){0x03}, 1, 0},
    {0xC0, (uint8_t []){0x0F}, 1, 0},
    {0xC1, (uint8_t []){0x1B}, 1, 0},
    {0xC2, (uint8_t []){0x27}, 1, 0},
    {0xC3, (uint8_t []){0x16}, 1, 0},
    {0xC4, (uint8_t []){0x14}, 1, 0},
    {0xC5, (uint8_t []){0x28}, 1, 0},
    {0xC6, (uint8_t []){0x1D}, 1, 0},
    {0xC7, (uint8_t []){0x21}, 1, 0},
    {0xC8, (uint8_t []){0x6C}, 1, 0},
    {0xC9, (uint8_t []){0x1B}, 1, 0},
    {0xCA, (uint8_t []){0x26}, 1, 0},
    {0xCB, (uint8_t []){0x5B}, 1, 0},
    {0xCC, (uint8_t []){0x1B}, 1, 0},
    {0xCD, (uint8_t []){0x1B}, 1, 0},
    {0xCE, (uint8_t []){0x4F}, 1, 0},
    {0xCF, (uint8_t []){0x24}, 1, 0},
    {0xD0, (uint8_t []){0x2A}, 1, 0},
    {0xD1, (uint8_t []){0x4E}, 1, 0},
    {0xD2, (uint8_t []){0x5F}, 1, 0},
    {0xD3, (uint8_t []){0x39}, 1, 0},
    {ILI9881C_CMD_CNDBKxSEL, (uint8_t []){ILI9881C_CMD_BKxSEL_BYTE0, ILI9881C_CMD_BKxSEL_BYTE1, ILI9881C_CMD_BKxSEL_BYTE2_PAGE0}, 3, 0},
    {0x35, (uint8_t []){0x00}, 1, 0},
    {0x29, (uint8_t []){0x00}, 0, 0},

    //============ Gamma END===========
};

static esp_err_t panel_ili9881c_del(esp_lcd_panel_t *panel)
{
    ili9881c_panel_t *ili9881c = (ili9881c_panel_t *)panel->user_data;

    if (ili9881c->reset_gpio_num >= 0) {
        gpio_reset_pin(ili9881c->reset_gpio_num);
    }
    // Delete MIPI DPI panel
    ili9881c->del(panel);
    free(ili9881c);
    ESP_LOGD(TAG, "del ili9881c panel @%p", ili9881c);

    return ESP_OK;
}

static esp_err_t panel_ili9881c_init(esp_lcd_panel_t *panel)
{
    ili9881c_panel_t *ili9881c = (ili9881c_panel_t *)panel->user_data;
    esp_lcd_panel_io_handle_t io = ili9881c->io;
    const ili9881c_lcd_init_cmd_t *init_cmds = NULL;
    uint16_t init_cmds_size = 0;
    bool is_command0_enable = false;
    bool is_cmd_overwritten = false;

    // back to CMD_Page 0
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, ILI9881C_CMD_CNDBKxSEL, (uint8_t[]) {
        ILI9881C_CMD_BKxSEL_BYTE0, ILI9881C_CMD_BKxSEL_BYTE1, ILI9881C_CMD_BKxSEL_BYTE2_PAGE0
    }, 3), TAG, "send command failed");
    // exit sleep mode
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SLPOUT, NULL, 0), TAG,
                        "io tx param failed");
    vTaskDelay(pdMS_TO_TICKS(120));

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]) {
        ili9881c->madctl_val,
    }, 1), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_COLMOD, (uint8_t[]) {
        ili9881c->colmod_val,
    }, 1), TAG, "send command failed");

    // vendor specific initialization, it can be different between manufacturers
    // should consult the LCD supplier for initialization sequence code
    if (ili9881c->init_cmds) {
        init_cmds = ili9881c->init_cmds;
        init_cmds_size = ili9881c->init_cmds_size;
    } else {
        init_cmds = vendor_specific_init_default;
        init_cmds_size = sizeof(vendor_specific_init_default) / sizeof(ili9881c_lcd_init_cmd_t);
    }

    for (int i = 0; i < init_cmds_size; i++) {
        // Check if the command has been used or conflicts with the internal
        if (is_command0_enable && init_cmds[i].data_bytes > 0) {
            switch (init_cmds[i].cmd) {
            case LCD_CMD_MADCTL:
                is_cmd_overwritten = true;
                ili9881c->madctl_val = ((uint8_t *)init_cmds[i].data)[0];
                break;
            case LCD_CMD_COLMOD:
                is_cmd_overwritten = true;
                ili9881c->colmod_val = ((uint8_t *)init_cmds[i].data)[0];
                break;
            default:
                is_cmd_overwritten = false;
                break;
            }

            if (is_cmd_overwritten) {
                is_cmd_overwritten = false;
                ESP_LOGW(TAG, "The %02Xh command has been used and will be overwritten by external initialization sequence",
                         init_cmds[i].cmd);
            }
        }

        // Send command
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes), TAG, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));

        if((init_cmds[i].cmd == ILI9881C_CMD_CNDBKxSEL) && (((uint8_t *)init_cmds[i].data)[2] == ILI9881C_CMD_BKxSEL_BYTE2_PAGE0)) {
            is_command0_enable = true;
        } else if((init_cmds[i].cmd == ILI9881C_CMD_CNDBKxSEL) && (((uint8_t *)init_cmds[i].data)[2] != ILI9881C_CMD_BKxSEL_BYTE2_PAGE0)) {
            is_command0_enable = false;
        }
    }
    ESP_LOGD(TAG, "send init commands success");

    ESP_RETURN_ON_ERROR(ili9881c->init(panel), TAG, "init MIPI DPI panel failed");

    return ESP_OK;
}

static esp_err_t panel_ili9881c_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    ili9881c_panel_t *ili9881c = (ili9881c_panel_t *)panel->user_data;
    esp_lcd_dpi_panel_config_t dpi_config = ili9881c->dpi_config;

    ppa_client_handle_t ppa_client_srm_handle = ili9881c->ppa_client_srm_handle;
    RotationAngle rotation = ili9881c->flags.rotation;
    lcd_color_rgb_pixel_format_t pixel_format = dpi_config.pixel_format;
    uint8_t lcd_bit_per_pixel = 0;
    ppa_srm_color_mode_t color_mode = PPA_SRM_COLOR_MODE_RGB565;
    if (pixel_format == LCD_COLOR_PIXEL_FORMAT_RGB565) {
        lcd_bit_per_pixel = 16;
        color_mode = PPA_SRM_COLOR_MODE_RGB565;
    } else if (pixel_format == LCD_COLOR_PIXEL_FORMAT_RGB888) {
        lcd_bit_per_pixel = 24;
        color_mode = PPA_SRM_COLOR_MODE_RGB888;
    } else {
        ESP_LOGE(TAG, "unsupported pixel format: %d", pixel_format);
        return ESP_ERR_NOT_SUPPORTED;
    }
    uint32_t data_cache_line_size = 0;
    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &data_cache_line_size));
    uint32_t hor_res = (dpi_config.video_timing).h_size;
    uint32_t ver_res = (dpi_config.video_timing).v_size;
    uint32_t buf_size = ALIGN_UP_BY(hor_res * ver_res * lcd_bit_per_pixel / 8, data_cache_line_size);

    int num_fb = dpi_config.num_fbs;
    void *lcd_buffer = NULL;
    
    if(rotation != ROTATION_NONE) {
        if(num_fb == 1 && color_data != ili9881c->lcd_buffer[0]) {
            lcd_buffer = ili9881c->lcd_buffer[0];
            if(y_end - y_start > hor_res || x_end - x_start > ver_res) {
                ESP_LOGE(TAG, "invalid height or width");
                return ESP_ERR_INVALID_ARG;
            }
        } else {
            ESP_LOGE(TAG, "rotation is not supported");
            rotation = ROTATION_NONE;
        }
    }

    if (rotation == ROTATION_90) {
        ppa_srm_oper_config_t srm_config = {
            .in.buffer = color_data,
            .in.pic_w = x_end - x_start,
            .in.pic_h = y_end - y_start,
            .in.block_w = x_end - x_start,
            .in.block_h = y_end - y_start,
            .in.block_offset_x = 0,
            .in.block_offset_y = 0,
            .in.srm_cm = color_mode,

            .out.buffer = lcd_buffer,
            .out.buffer_size = buf_size,
            .out.pic_w = hor_res,
            .out.pic_h = ver_res,
            .out.block_offset_x = y_start,
            .out.block_offset_y = ver_res - x_end,
            .out.srm_cm = color_mode,
            
            .rotation_angle = PPA_SRM_ROTATION_ANGLE_90,
            
            .scale_x = 1.0,
            .scale_y = 1.0,
            
            .rgb_swap = 0,
            .byte_swap = 0,
            
            .mode = PPA_TRANS_MODE_BLOCKING,
        };

        ESP_ERROR_CHECK(ppa_do_scale_rotate_mirror(ppa_client_srm_handle, &srm_config));

    } else {
        ESP_ERROR_CHECK(ili9881c->draw_bitmap(panel, x_start, y_start, x_end, y_end, color_data));
    }   

    return ESP_OK;
}

static esp_err_t panel_ili9881c_reset(esp_lcd_panel_t *panel)
{
    ili9881c_panel_t *ili9881c = (ili9881c_panel_t *)panel->user_data;
    esp_lcd_panel_io_handle_t io = ili9881c->io;

    // Perform hardware reset
    if (ili9881c->reset_gpio_num >= 0) {
        gpio_set_level(ili9881c->reset_gpio_num, ili9881c->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(ili9881c->reset_gpio_num, !ili9881c->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
    } else if (io) { // Perform software reset
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET, NULL, 0), TAG, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    return ESP_OK;
}

static esp_err_t panel_ili9881c_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    ili9881c_panel_t *ili9881c = (ili9881c_panel_t *)panel->user_data;
    esp_lcd_panel_io_handle_t io = ili9881c->io;
    uint8_t command = 0;

    ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_STATE, TAG, "invalid panel IO");

    if (invert_color_data) {
        command = LCD_CMD_INVON;
    } else {
        command = LCD_CMD_INVOFF;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG, "send command failed");

    return ESP_OK;
}

static esp_err_t panel_ili9881c_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    ili9881c_panel_t *ili9881c = (ili9881c_panel_t *)panel->user_data;
    esp_lcd_panel_io_handle_t io = ili9881c->io;
    uint8_t madctl_val = ili9881c->madctl_val;

    ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_STATE, TAG, "invalid panel IO");

    // Control mirror through LCD command
    if (mirror_x) {
        madctl_val |= ILI9881C_CMD_GS_BIT;
    } else {
        madctl_val &= ~ILI9881C_CMD_GS_BIT;
    }
    if (mirror_y) {
        madctl_val |= ILI9881C_CMD_SS_BIT;
    } else {
        madctl_val &= ~ILI9881C_CMD_SS_BIT;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t []) {
        madctl_val
    }, 1), TAG, "send command failed");
    ili9881c->madctl_val = madctl_val;

    return ESP_OK;
}

static esp_err_t panel_ili9881c_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    ili9881c_panel_t *ili9881c = (ili9881c_panel_t *)panel->user_data;

    if (swap_axes) {
        ili9881c->flags.rotation = ROTATION_90;
    }

    return ESP_OK;
}

static esp_err_t panel_ili9881c_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    ESP_LOGE(TAG, "set_gap is not supported by this panel");
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t panel_ili9881c_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    ili9881c_panel_t *ili9881c = (ili9881c_panel_t *)panel->user_data;
    esp_lcd_panel_io_handle_t io = ili9881c->io;
    int command = 0;

    if (on_off) {
        command = LCD_CMD_DISPON;
    } else {
        command = LCD_CMD_DISPOFF;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG, "send command failed");
    return ESP_OK;
}

static esp_err_t panel_ili9881c_sleep(esp_lcd_panel_t *panel, bool sleep)
{
    ili9881c_panel_t *ili9881c = (ili9881c_panel_t *)panel->user_data;
    esp_lcd_panel_io_handle_t io = ili9881c->io;
    int command = 0;

    if (sleep) {
        command = LCD_CMD_SLPIN;
    } else {
        command = LCD_CMD_SLPOUT;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG, "send command failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    return ESP_OK;
}
#endif
