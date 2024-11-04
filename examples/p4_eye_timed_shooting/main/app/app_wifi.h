/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APP_WIFI_H
#define APP_WIFI_H

void wifi_init_sta(uint8_t *wifi_ssid, uint8_t *wifi_password);
bool app_wifi_get_connected(void);

#endif // APP_WIFI_H