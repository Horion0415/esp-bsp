/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APP_SMTP_H
#define APP_SMTP_H

#include "esp_log.h"

void app_smtp_set_config(char *server, char *port, char *sender, char *password, char *recipient);
esp_err_t app_smtp_tls_init(void);
esp_err_t app_smtp_connect_server(void);
esp_err_t app_smtp_perform_authentication(void);
esp_err_t app_smtp_compose_email(uint8_t *pic_buf, uint32_t pic_size, char* filename);
esp_err_t app_smtp_close_connection(void);

#endif // APP_SMTP_H