#ifndef ESP_QMA6100_H
#define ESP_QMA6100_H

#include "esp_err.h"
#include "driver/i2c.h"
#include "driver/gpio.h"

#define QMA7981_ADDR                        0x12        /*!< Slave address of the QMA7981 */
#define QMA7981_WHO_AM_I_REG_ADDR           0x00        /*!< Register addresses of the "who am I" register */
#define QMA7981_PWR_MGMT_1_REG_ADDR         0x11        /*!< Register addresses of the power managment register */

#define QMA7981_ID                          0x90											//QMA7981的ID

#define QMA7981_DXM 0x01											//QMA7981寄存器X轴加速度地址
#define QMA7981_DYM 0x03											//QMA7981寄存器Y轴加速度地址
#define QMA7981_DZM 0x05											//QMA7981寄存器Z轴加速度地址

#define QMA7981_start_convert_cmd 0xC0					//设置QMA7981为active模式的指令

#define QMA7981_MAX_VALUE 0x1FFF							//满量程读数

#ifdef __cplusplus
extern "C" {
#endif


esp_err_t QMA7981_init(void);
esp_err_t QMA7981_begin(void);

#ifdef __cplusplus
}
#endif
#endif