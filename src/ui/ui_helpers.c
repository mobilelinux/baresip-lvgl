#include "ui_helpers.h"

lv_obj_t *ui_create_title_bar(lv_obj_t *parent, const char *title, bool has_back_btn, lv_event_cb_t back_cb, void *back_user_data) {
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_set_size(header, LV_PCT(100), UI_HEADER_HEIGHT);
    lv_obj_set_style_bg_color(header, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0); // Ensure coverage
    lv_obj_set_style_radius(header, 0, 0); // No radius
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, lv_palette_lighten(LV_PALETTE_GREY, 3), 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    // Explicitly disabling padding to ensure alignment works as expected?
    // contacts_applet didn't enable/disable padding on header explicitly but let's be safe or minimal.
    // Actually contacts_applet didn't touch padding on header, just layout.

    if (has_back_btn) {
        lv_obj_t *back_btn = lv_btn_create(header);
        lv_obj_set_size(back_btn, 40, 40);
        lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_opa(back_btn, 0, 0);
        lv_obj_set_style_shadow_width(back_btn, 0, 0);
        
        lv_obj_t *back_icon = lv_label_create(back_btn);
        lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(back_icon, lv_color_black(), 0);
        lv_obj_center(back_icon);
        
        if (back_cb) {
            lv_obj_add_event_cb(back_btn, back_cb, LV_EVENT_CLICKED, back_user_data);
        }
    }

    lv_obj_t *title_lbl = lv_label_create(header);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, lv_color_black(), 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_20, 0);
    
    // Alignment logic
    if (has_back_btn) {
        lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 50, 0);
    } else {
        // If no back button, maybe small padding/offset
        lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 20, 0); 
    }

    return header;
}

lv_obj_t *ui_header_add_action_btn(lv_obj_t *header, const char *icon_symbol, lv_event_cb_t cb, void *user_data) {
    lv_obj_t *action_btn = lv_btn_create(header);
    lv_obj_set_size(action_btn, 40, 40);
    lv_obj_align(action_btn, LV_ALIGN_RIGHT_MID, 0, 0); // Will stack if multiple? No, user should manage offsets if multiple.
    // Or we could use LEFT_OF last child? 
    // Simplify: Just align RIGHT_MID. If caller adds multiple, they need manual adjustment or we improve this helper later.
    // contacts_applet only had one action at a time (Save or Edit/Video/Audio logic was in list items/editor).
    // Editor had one Save button.
    
    // To support multiple, we might want to check children?
    // For now, assume single action button or check current count.
    // Let's just do RIGHT_MID, but allow margin if needed.
    // Actually, let's keep it simple: Just RIGHT_MID.
    
    lv_obj_set_style_bg_opa(action_btn, 0, 0);
    lv_obj_set_style_shadow_width(action_btn, 0, 0);
    
    lv_obj_t *icon_lbl = lv_label_create(action_btn);
    lv_label_set_text(icon_lbl, icon_symbol);
    lv_obj_set_style_text_color(icon_lbl, lv_color_black(), 0);
    lv_obj_center(icon_lbl);
    
    if (cb) {
        lv_obj_add_event_cb(action_btn, cb, LV_EVENT_CLICKED, user_data);
    }
    
    return action_btn;
}
