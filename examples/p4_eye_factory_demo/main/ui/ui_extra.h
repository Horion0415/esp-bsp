#ifndef UI_EXTRA_H
#define UI_EXTRA_H

#include "ui.h"
#include <stdbool.h>

typedef enum {
    UI_PAGE_MAIN,           // Main page
    UI_PAGE_CAMERA,         // Camera page
    UI_PAGE_INTERVAL_CAM,   // Interval camera page
    UI_PAGE_VIDEO_MODE,     // Video mode page
    UI_PAGE_ALBUM,          // Album page
    UI_PAGE_USB_DISK,       // USB disk page
    UI_PAGE_SETTINGS,       // Settings page
    UI_PAGE_MAX             // Page count
} ui_page_t;

typedef struct {
    const char* language;
    const char* resolution;
    const char* flash;
} settings_info_t;

/**
 * @brief Initialize UI extra functionality
 */
void ui_extra_init(void);

/**
 * @brief Menu button handler
 */
void ui_extra_btn_menu(void);

/**
 * @brief Up button handler
 */
void ui_extra_btn_up(void);

/**
 * @brief Down button handler
 */
void ui_extra_btn_down(void);

/**
 * @brief Encoder button handler
 */
void ui_extra_btn_encoder(void);

/**
 * @brief Get current page
 * @return Current page enum value
 */
ui_page_t ui_extra_get_current_page(void);

/**
 * @brief Get chosen page
 * @return Chosen page enum value
 */
ui_page_t ui_extra_get_choosed_page(void);

/**
 * @brief Navigate to specified page
 * @param page Target page enum value
 */
void ui_extra_goto_page(ui_page_t page);

/**
 * @brief Get current settings information
 * @return Pointer to settings information structure
 */
settings_info_t* ui_extra_get_settings(void);

/**
 * @brief Set magnification factor
 * @param factor Magnification factor value
 */
void app_extra_set_magnification_factor(uint16_t factor);

/**
 * @brief Get magnification factor
 * @return Current magnification factor value
 */
uint16_t app_extra_get_magnification_factor(void);

/**
 * @brief Set interval time
 * @param time Interval time in minutes
 */
void app_extra_set_interval_time(uint16_t time);

/**
 * @brief Get interval time
 * @return Current interval time in minutes
 */
uint16_t app_extra_get_interval_time(void);

/**
 * @brief Set SD card mount status
 * @param mounted Whether SD card is mounted
 */
void ui_extra_set_sd_card_mounted(bool mounted);

/**
 * @brief Get SD card mount status
 * @return Whether SD card is mounted
 */
bool ui_extra_get_sd_card_mounted(void);

/**
 * @brief Set saved photo count
 * @param count Number of saved photos
 */
void app_extra_set_saved_photo_count(uint16_t count);

/**
 * @brief Get saved photo count
 * @return Current saved photo count
 */
uint16_t app_extra_get_saved_photo_count(void);

/**
 * @brief Get popup window visible status
 * @return Whether popup window is visible
 */
bool ui_extra_get_popup_window_visible(void);

/**
 * @brief Set USB disk mount status
 * @param mounted Whether USB disk is mounted
 */
void ui_extra_set_usb_disk_mounted(bool mounted);

/**
 * @brief Get USB disk mount status
 * @return Whether USB disk is mounted
 */
bool ui_extra_get_usb_disk_mounted(void);

/**
 * @brief Show interval timer warning popup
 */
void ui_extra_popup_interval_timer_warning(void);

/**
 * @brief Clear page
 */
void ui_extra_clear_page(void);

/**
 * @brief Start interval timer
 */
void ui_extra_start_interval_timer(void);

#endif