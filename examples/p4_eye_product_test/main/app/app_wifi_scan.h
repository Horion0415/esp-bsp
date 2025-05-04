#ifndef __APP_WIFI_SCAN_H__
#define __APP_WIFI_SCAN_H__

void app_wifi_scan(void);

uint16_t app_wifi_scan_get_ap_count(void);

int8_t app_wifi_scan_get_rssi_by_ssid(const char* target_ssid);

#endif
