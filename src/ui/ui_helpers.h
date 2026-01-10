#ifndef UI_HELPERS_H
#define UI_HELPERS_H

#include "lvgl.h"

// Standard Header Height
#define UI_HEADER_HEIGHT 60

/**
 * @brief Create a standardized title bar.
 *        Style: Light Blue background, White text, Back button on left (optional).
 * 
 * @param parent The parent object (usually the screen).
 * @param title The title text.
 * @param has_back_btn If true, adds a back button.
 * @param back_cb Callback for the back button (can be NULL).
 * @param back_user_data User data for the back callback.
 * @return The title bar object (header).
 */
lv_obj_t *ui_create_title_bar(lv_obj_t *parent, const char *title, bool has_back_btn, lv_event_cb_t back_cb, void *back_user_data);

/**
 * @brief Add an action button to the right side of the title bar.
 * 
 * @param header The title bar object.
 * @param icon_symbol The LVGL symbol string (e.g. LV_SYMBOL_OK).
 * @param cb Callback function.
 * @param user_data User data for the callback.
 * @return The button object.
 */
lv_obj_t *ui_header_add_action_btn(lv_obj_t *header, const char *icon_symbol, lv_event_cb_t cb, void *user_data);

#endif // UI_HELPERS_H
