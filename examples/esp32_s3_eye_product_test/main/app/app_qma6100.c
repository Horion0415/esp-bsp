#include <stdio.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "app_qma6100.h"
#include "app_button.h"
#include "app_camera.h"

#include "ui.h"

#define I2C_MASTER_SCL_IO           CONFIG_I2C_MASTER_SCL      /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO           CONFIG_I2C_MASTER_SDA      /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM              1           /*!< I2C master i2c port number, the number of i2c peripheral interfaces available will depend on the chip */
#define I2C_MASTER_FREQ_HZ          400000                     /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE   0                          /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0                          /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS       100

static const char *TAG = "QMA7981";

/**@brief Read a sequence of bytes from a QMA7981 sensor registers*/
static esp_err_t QMA7981_register_read(uint8_t reg_addr, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(I2C_MASTER_NUM, QMA7981_ADDR, &reg_addr, 1, data, len, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}


/**@brief Write a byte to a QMA7981 sensor register*/
static esp_err_t QMA7981_register_write_byte(uint8_t reg_addr, uint8_t data)
{
    int ret;
    uint8_t write_buf[2] = {reg_addr, data};
    ret = i2c_master_write_to_device(I2C_MASTER_NUM, QMA7981_ADDR, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    return ret;
}

esp_err_t QMA7981_init(void)
{
    uint8_t id;
    ESP_ERROR_CHECK(QMA7981_register_read(QMA7981_WHO_AM_I_REG_ADDR, &id, 1));		
    if(id == QMA7981_ID) {
        ESP_ERROR_CHECK(QMA7981_register_write_byte(QMA7981_PWR_MGMT_1_REG_ADDR, QMA7981_start_convert_cmd));
        ESP_LOGI(TAG, "QMA7981 init success");
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t QMA7981_get_data(float *data_x, float *data_y, float * data_z, float *data_g) 
{
    uint8_t data[2];

    int16_t XA;
    int16_t YA;
    int16_t ZA;
    
    float X_AXIS_A;
    float Y_AXIS_A;
    float Z_AXIS_A;

    ESP_ERROR_CHECK(QMA7981_register_read(QMA7981_DXM, data, 2));
    XA = (data[0] & 0xFC) | (data[1]<< 8);
    ESP_ERROR_CHECK(QMA7981_register_read(QMA7981_DYM, data, 2));
    YA = (data[0] & 0xFC) | (data[1]<< 8);
    ESP_ERROR_CHECK(QMA7981_register_read(QMA7981_DZM, data, 2));
    ZA = (data[0] & 0xFC) | (data[1]<< 8);

    XA /= 4;
    YA /= 4;
    ZA /= 4;
    
    X_AXIS_A = (float) XA / QMA7981_MAX_VALUE * 2;
    Y_AXIS_A = (float) YA / QMA7981_MAX_VALUE * 2;
    Z_AXIS_A = (float) ZA / QMA7981_MAX_VALUE * 2;

    float g = sqrt(X_AXIS_A * X_AXIS_A + Y_AXIS_A * Y_AXIS_A + Z_AXIS_A * Z_AXIS_A);

    *data_x = X_AXIS_A;
    *data_y = Y_AXIS_A;
    *data_z = Z_AXIS_A;
    *data_g = g;

    return ESP_OK;
}

static void QMA7981_data_task(void *arg)
{
    float data_x, data_y, data_z;
    float data_g;
    uint8_t time = 0;
    while (1) {
        QMA7981_get_data(&data_x, &data_y, &data_z, &data_g);
        ESP_LOGD(TAG, "G: %.2f", data_g);

        if(data_g < 1.1 && data_g > 0.9) {
            lv_label_set_text(ui_LabelPass, "PASS");
            lv_obj_set_style_text_color(ui_LabelPass, lv_color_hex(0x2FF6AA), LV_PART_MAIN | LV_STATE_DEFAULT);
            if(app_button_get_screen() == ScreenIMU) {
                time++;
            }
        } else {
            time = 0;
            lv_label_set_text(ui_LabelPass, "FAIL");
            lv_obj_set_style_text_color(ui_LabelPass, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);   
        }

        if(time > 3 && app_button_get_screen() == ScreenIMU) {
            app_camera_stop();
            if(app_camera_is_finished() == true) {
                _ui_screen_change(&ui_ScreenSuccess, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_ScreenSuccess_screen_init);
                app_button_change_screen(ScreenSuccess);
                app_sdcard_write_result("IMU Pass\n");
            }
            time = 0;
        }

        vTaskDelay(300 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}

esp_err_t QMA7981_begin(void)
{
    xTaskCreate(QMA7981_data_task, "QMA7981 data task", 1024 * 4, NULL, 5, NULL);
    return ESP_OK;
}

