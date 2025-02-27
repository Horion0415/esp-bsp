/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APP_USB_MSC_H
#define APP_USB_MSC_H

esp_err_t app_usb_msc_init(const char *base_path);

bool app_usb_msc_stage(void);

#endif // APP_USB_MSC_H