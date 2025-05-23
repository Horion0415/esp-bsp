#ifndef APP_GPIO_H
#define APP_GPIO_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Number of GPIO pins in group 1
 */
#define GROUP1_PIN_NUM 7

/**
 * @brief Number of GPIO pins in group 2  
 */
#define GROUP2_PIN_NUM 7

/**
 * @brief GPIO pins in group 1 (used as sender and receiver)
 */
extern const int group1_pins[GROUP1_PIN_NUM];

/**
 * @brief GPIO pins in group 2 (used as receiver and sender)
 */
extern const int group2_pins[GROUP2_PIN_NUM];

/**
 * @brief Initialize all GPIO pins for testing
 * 
 * This function configures all GPIO pins defined in both groups.
 * It automatically sets up the pins for bidirectional testing between
 * group1 and group2 pins.
 */
void init_gpio(void);

/**
 * @brief Test all GPIO connections
 * 
 * This function performs comprehensive connectivity tests:
 * 1. Tests group1 as output -> group2 as input
 * 2. Tests group2 as output -> group1 as input
 * 
 * @return true if all pin connections test successfully, false otherwise
 */
bool test_gpio_connection(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_GPIO_H */ 