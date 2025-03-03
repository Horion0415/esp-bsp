/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APP_USB_HID_H
#define APP_USB_HID_H

esp_err_t app_usb_hid_init(void);

bool app_usb_hid_stage(void);

#endif // APP_USB_HID_H