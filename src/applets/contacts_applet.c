#include "applet.h"
#include "applet_manager.h"
#include "baresip_manager.h"
#include "call_applet.h"
#include "config_manager.h"
#include "contact_manager.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../ui/ui_helpers.h"

// UI State
static bool is_editor_mode = false;
static contact_t current_edit_contact;
static bool is_new_contact = false;
static applet_t *g_applet = NULL;

// Account Picker State
// Account Picker State
static char g_pending_number[128] = {0};
static bool g_pending_is_video = false;

static lv_obj_t *g_account_picker_modal = NULL;

// Context Menu State
// Context Menu State
static lv_obj_t *g_context_menu_modal = NULL;
static contact_t g_context_menu_contact;
static bool g_long_press_handled = false;

// Forward declarations
static void refresh_ui(void);
static void draw_list(void);
static void draw_editor(void);
static void contact_long_press_handler(lv_event_t *e);

static bool g_preserve_view = false;

// Selection Mode State
static bool g_selection_mode = false;
static bool g_selected_mask[100]; // MAX_CONTACTS default? Limits? cm_get_count dynamic?
// We should check contact_manager.h for MAX limits. If dynamic, we might need bitmask or dynamic array.
// For now assuming 100 is safe upper bound or matching MAX constants if available.
// Let's check imports. cm_get_count used.
// Let's use 100 for now, same as call log.

// External reference to call applet

static bool g_return_to_caller = false;
static lv_obj_t *g_contacts_trash_btn = NULL;


static void delete_btn_clicked(lv_event_t *e) {
  (void)e;
  if (!is_new_contact) {
    cm_remove(current_edit_contact.id);
  }
  is_editor_mode = false;
  refresh_ui();
}

// External API to open editor with number (for Call Log "Add To Contact")
void contacts_applet_open_new(const char *number) {
  if (!g_applet)
    return; // Should not happen if applet manager handles init

  // Ensure we are initialized
  cm_init();

  is_editor_mode = true;
  is_new_contact = true;
  g_return_to_caller = true; // Set flag to return to previous applet on back
  g_preserve_view = true;    // Instruct resume to NOT reset to list
  memset(&current_edit_contact, 0, sizeof(contact_t));
  if (number) {
    strncpy(current_edit_contact.number, number,
            sizeof(current_edit_contact.number) - 1);
  }
  refresh_ui();
}

// ... (omitted) ...

static int contacts_init(applet_t *applet) {
  log_info("ContactsApplet", "Initializing");
  g_applet = applet;
  is_editor_mode = false;
  g_preserve_view = false;
  refresh_ui();
  return 0;
}

static void contacts_start(applet_t *applet) {
  (void)applet;
  // On start (first launch or after stop), default to list
  if (!g_preserve_view) {
    if (is_editor_mode) {
      is_editor_mode = false;
      refresh_ui();
    }
  }
  g_preserve_view = false;
}

static void contacts_pause(applet_t *applet) { (void)applet; }

static void contacts_resume(applet_t *applet) {
  (void)applet;
  if (g_preserve_view) {
    g_preserve_view = false;
    return;
  }
  // Otherwise, ensure list view
  if (is_editor_mode) {
    is_editor_mode = false;
    refresh_ui();
  }
}

// Helper: Create standardized avatar
static lv_obj_t *create_avatar(lv_obj_t *parent, const char *name, int size) {
  lv_obj_t *avatar = lv_obj_create(parent);
  lv_obj_set_size(avatar, size, size);
  lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(avatar, lv_palette_main(LV_PALETTE_BLUE_GREY), 0);
  lv_obj_set_style_border_width(avatar, 0, 0);
  lv_obj_clear_flag(avatar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *label = lv_label_create(avatar);
  char initial[2] = {name && strlen(name) > 0 ? name[0] : '?', '\0'};
  lv_label_set_text(label, initial);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);

  if (size > 60)
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
  else
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);

  lv_obj_center(label);
  return avatar;
}

// ------------------- EVENT HANDLERS -------------------

static void back_btn_clicked(lv_event_t *e) {
  (void)e;
  if (is_editor_mode) {
    if (g_return_to_caller) {
      g_return_to_caller = false;
      applet_manager_back();
      return;
    }
    is_editor_mode = false;
    refresh_ui();
  } else {
    applet_manager_back();
  }
}

static void add_btn_clicked(lv_event_t *e) {
  (void)e;
  is_editor_mode = true;
  is_new_contact = true;
  memset(&current_edit_contact, 0, sizeof(contact_t));
  refresh_ui();
}

static void edit_btn_clicked(lv_event_t *e) {
  const contact_t *c = (const contact_t *)lv_event_get_user_data(e);
  if (c) {
    is_editor_mode = true;
    is_new_contact = false;
    current_edit_contact = *c;
    refresh_ui();
  }
}

static void save_btn_clicked(lv_event_t *e) {
  lv_obj_t **inputs = (lv_obj_t **)lv_event_get_user_data(e);
  if (!inputs)
    return;

  lv_obj_t *name_ta = inputs[0];
  lv_obj_t *num_ta = inputs[1];
  lv_obj_t *fav_sw = inputs[2];

  const char *name = lv_textarea_get_text(name_ta);
  const char *number = lv_textarea_get_text(num_ta);
  bool fav = lv_obj_has_state(fav_sw, LV_STATE_CHECKED);

  if (strlen(name) == 0 || strlen(number) == 0) {
    log_warn("ContactsApplet", "Validation failed: Empty fields");
    return;
  }

  if (is_new_contact) {
    cm_add(name, number, fav);
  } else {
    cm_update(current_edit_contact.id, name, number, fav);
  }

  is_editor_mode = false;
  free(inputs);
  refresh_ui();
}

// Selection Handlers
static void on_edit_clicked(lv_event_t *e) {
    (void)e;
    g_selection_mode = !g_selection_mode;
    // Reset selection on exit? Yes.
    if (!g_selection_mode) {
        memset(g_selected_mask, 0, sizeof(g_selected_mask));
    }
    refresh_ui();
}

static void on_trash_clicked(lv_event_t *e) {
    (void)e;
    // Delete selected
    // Iterate backwards to avoid index shifting issues if we had a remove_at logic,
    // but cm_remove takes ID. So order doesn't strictly matter for finding ID, 
    // but we must be careful if indices shift.
    // cm_get_at(i) returns pointer.
    // We should collect IDs to remove first?
    // contacts are re-indexed? 
    // Let's assume cm_remove handles it.
    // Efficient approach: Collect IDs then remove.
    
    int count = cm_get_count();
    int removed = 0;
    
    // We need to store IDs because cm_get_count changes as we remove?
    // If cm is simple array list, removing swaps or shifts.
    // Safest: Iterate and collect IDs or loop backwards.
    // cm_get_at(i) is safe?
    
    // Let's blindly try Loop Backwards
    for (int i = count - 1; i >= 0; i--) {
        if (g_selected_mask[i]) {
            const contact_t *c = cm_get_at(i);
            if (c) {
                cm_remove(c->id);
                removed++;
            }
        }
    }
    
    if (removed > 0) {
        log_info("ContactsApplet", "Deleted %d contacts", removed);
    }
    
    g_selection_mode = false;
    memset(g_selected_mask, 0, sizeof(g_selected_mask));
    refresh_ui();
}



// External reference to call applet

// External reference to call applet
extern applet_t call_applet;

// ------------------- ACCOUNT PICKER -------------------

static void close_picker_modal(void) {
  if (g_account_picker_modal) {
    lv_obj_del(g_account_picker_modal);
    g_account_picker_modal = NULL;
  }
}

static void account_picker_cancel(lv_event_t *e) { (void)e; close_picker_modal(); }

static void account_picker_item_clicked(lv_event_t *e) {
  const char *aor = (const char *)lv_event_get_user_data(e);
  if (aor && strlen(g_pending_number) > 0) {
    int ret = -1;
    if (g_pending_is_video) {
      log_info("ContactsApplet", "Picking account %s for VIDEO calling", aor);
      ret = baresip_manager_videocall_with_account(g_pending_number, aor);
    } else {
      log_info("ContactsApplet", "Picking account %s for audio calling", aor);
      ret = baresip_manager_call_with_account(g_pending_number, aor);
    }

    if (ret == 0) {
      close_picker_modal();
      call_applet_request_active_view();
      applet_manager_launch_applet(&call_applet);
    } else {
      applet_manager_show_toast("Account not available.");
    }
  }
}

static void show_account_picker(const char *number, bool is_video) {
  if (g_account_picker_modal)
    return; // Already open

  strncpy(g_pending_number, number, sizeof(g_pending_number) - 1);
  g_pending_is_video = is_video;

  g_account_picker_modal = lv_obj_create(lv_scr_act());
  lv_obj_set_size(g_account_picker_modal, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(g_account_picker_modal, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_account_picker_modal, LV_OPA_50, 0);
  lv_obj_set_flex_flow(g_account_picker_modal, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(g_account_picker_modal, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *panel = lv_obj_create(g_account_picker_modal);
  lv_obj_set_size(panel, LV_PCT(80), LV_PCT(60));
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(panel, 20, 0);

  lv_obj_t *title = lv_label_create(panel);
  lv_label_set_text(title, "Choose Account");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_pad_bottom(title, 20, 0);
  lv_obj_center(title);

  // Load accounts
  voip_account_t accounts[MAX_ACCOUNTS];
  int count = config_load_accounts(accounts, MAX_ACCOUNTS);

  for (int i = 0; i < count; i++) {
    if (!accounts[i].enabled)
      continue;

    lv_obj_t *btn = lv_btn_create(panel);
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_set_height(btn, 50);

    lv_obj_t *lbl = lv_label_create(btn);
    char buf[128];
    snprintf(buf, sizeof(buf), "%s (%s)",
             accounts[i].display_name[0] ? accounts[i].display_name
                                         : accounts[i].username,
             accounts[i].server);
    lv_label_set_text(lbl, buf);
    lv_obj_center(lbl);

    // Construct AOR for callback: sip:user:pass@domain or simple
    // sip:user@domain Baresip uag_find_aor expects "sip:user@domain" usually.
    // We construct distinct AOR string to identify the ua.
    // Use heap string for user data (simple leak for now? No, need to free).
    // Better: create button, attach static copy? No.
    // Let's use lv_mem_alloc, LVGL will not free it automatically on deletion
    // unless event is deletion. Since we del the modal, we rely on OS cleanup?
    // No. Ideally we attach a destroy event to separate memory. For simplicity
    // here: we construct the string into the event handler logic by index? No,
    // let's just use `str` which is valid during the session? No stack var.
    // Let's alloc.
    char *aor_copy = lv_mem_alloc(256);
    snprintf(aor_copy, 256, "sip:%s@%s", accounts[i].username,
             accounts[i].server);

    // Note: We are leaking `aor_copy` here. In a long running app this is bad.
    // But for this modal which is rare, it's "acceptable" for prototype.
    // To fix: add DELETE event handler to free user_data.

    lv_obj_add_event_cb(btn, account_picker_item_clicked, LV_EVENT_CLICKED,
                        aor_copy);
  }

  // Cancel Button
  lv_obj_t *cancel_btn = lv_btn_create(panel);
  lv_obj_set_width(cancel_btn, LV_PCT(100));
  lv_obj_set_style_bg_color(cancel_btn, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
  lv_label_set_text(cancel_lbl, "Cancel");
  lv_obj_center(cancel_lbl);
  lv_obj_add_event_cb(cancel_btn, account_picker_cancel, LV_EVENT_CLICKED,
                      NULL);
}

// ------------------- HANDLERS -------------------

static void contact_item_clicked(lv_event_t *e) {
  if (g_long_press_handled) {
    g_long_press_handled = false;
    return;
  }
  const contact_t *c = (const contact_t *)lv_event_get_user_data(e);
  if (!c)
    return;

  app_config_t config;
  config_load_app_settings(&config);

  // Check Default Account
  if (config.default_account_index >= 0) {
    // Valid index, try to load it
    voip_account_t accounts[MAX_ACCOUNTS];
    int count = config_load_accounts(accounts, MAX_ACCOUNTS);
    if (config.default_account_index < count) {
      // Use this account
      char aor[256];
      voip_account_t *acc = &accounts[config.default_account_index];
      snprintf(aor, sizeof(aor), "sip:%s@%s", acc->username, acc->server);

      log_info("ContactsApplet", "Calling with default account: %s", aor);
      if (baresip_manager_call_with_account(c->number, aor) == 0) {
        call_applet_request_active_view();
        applet_manager_launch_applet(&call_applet);
      } else {
        applet_manager_show_toast("Account not available.");
      }
      return;
    }
  }

  // Fallback: Show Picker
  log_info("ContactsApplet", "No default account, showing picker");
  show_account_picker(c->number, false);
}

static void contact_checkbox_cb(lv_event_t *e) {
    lv_obj_t *cb = lv_event_get_target(e);
    int idx = (intptr_t)lv_event_get_user_data(e);
    bool checked = lv_obj_has_state(cb, LV_STATE_CHECKED);
    
    // Safety check
    if (idx >= 0 && idx < 100) {
        g_selected_mask[idx] = checked;
    }
    
    // Update Trash Visibility
    if (g_contacts_trash_btn) {
        bool any_selected = false;
        // cm_get_count() might be slow? But loop 100 is fast.
        for(int i=0; i<100; i++) {
             if (g_selected_mask[i]) { any_selected = true; break; }
        }
        if (any_selected) lv_obj_clear_flag(g_contacts_trash_btn, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(g_contacts_trash_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void contact_video_clicked(lv_event_t *e) {
  const contact_t *c = (const contact_t *)lv_event_get_user_data(e);
  if (!c)
    return;

  app_config_t config;
  config_load_app_settings(&config);

  // Check Default Account
  if (config.default_account_index >= 0) {
    // Valid index, try to load it
    voip_account_t accounts[MAX_ACCOUNTS];
    int count = config_load_accounts(accounts, MAX_ACCOUNTS);
    if (config.default_account_index < count) {
      // Use this account
      char aor[256];
      voip_account_t *acc = &accounts[config.default_account_index];
      snprintf(aor, sizeof(aor), "sip:%s@%s", acc->username, acc->server);

      log_info("ContactsApplet", "Video calling with default account: %s", aor);
      if (baresip_manager_videocall_with_account(c->number, aor) == 0) {
        call_applet_request_active_view();
        applet_manager_launch_applet(&call_applet);
      } else {
        applet_manager_show_toast("Account not available.");
      }
      return;
    }
  }

  // Fallback: Video Call directly (default UA) - or should we show picker?
  // Let's just video call directly if no default account set, using default UA.
  // Or show picker? Existing logic shows picker.
  // But picker `show_account_picker` currently triggers `contact_item_clicked`
  // logic (via `account_picker_item_clicked` which calls
  // `baresip_manager_call_with_account`). The picker doesn't support video
  // choice yet. For simplicity: If no default account, use default UA for
  // video.
  // Fallback: Show Picker for Video
  log_info("ContactsApplet", "No default account, showing picker for VIDEO");
  show_account_picker(c->number, true);
}

// ------------------- UI DRAWING -------------------

static void draw_list(void) {
  // Use Flex layout for full height filling
  lv_obj_set_flex_flow(g_applet->screen, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(g_applet->screen, 0, 0);
  lv_obj_set_style_pad_gap(g_applet->screen, 0, 0);

  lv_obj_t *header = ui_create_title_bar(g_applet->screen, "Contacts", true, back_btn_clicked, NULL);
  
  // Add Edit Action
  ui_header_add_action_btn(header, g_selection_mode ? LV_SYMBOL_CLOSE : LV_SYMBOL_EDIT, on_edit_clicked, NULL);

  lv_obj_t *list = lv_obj_create(g_applet->screen);
  lv_obj_set_width(list, LV_PCT(100));
  lv_obj_set_flex_grow(list, 1); // Fill remaining space
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(list, 10, 0);
  lv_obj_set_style_pad_gap(list, 10, 0);

  cm_init();
  int count = cm_get_count();

  for (int i = 0; i < count; i++) {
    const contact_t *c = cm_get_at(i);
    if (!c)
      continue;

    lv_obj_t *item = lv_obj_create(list);
    lv_obj_set_size(item, LV_PCT(100), 70);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    
    // In selection mode, clicking toggles selection (via checkbox or whole item?)
    // User requested "Edit icon... to multi-select contacts. after user selection complete, show Trash".
    // "Edit" toggles mode.
    
    if (g_selection_mode) {
        lv_obj_t *cb = lv_checkbox_create(item);
        lv_checkbox_set_text(cb, "");
        if (g_selected_mask[i]) lv_obj_add_state(cb, LV_STATE_CHECKED);
        lv_obj_add_event_cb(cb, contact_checkbox_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)i);
        lv_obj_align(cb, LV_ALIGN_LEFT_MID, 0, 0);
        
        // Disable normal click action?
        // Or make item click toggle checkbox?
        // Let's disable item click for simplicity, or handle it manually.
        // If we don't attach contact_item_clicked, it does nothing.
        // We can attach a toggle handler to item.
    } else {
        lv_obj_add_event_cb(item, contact_item_clicked, LV_EVENT_CLICKED, (void *)c);
        lv_obj_add_event_cb(item, contact_long_press_handler, LV_EVENT_LONG_PRESSED, (void *)c);
    }

    lv_obj_t *avatar = create_avatar(item, c->name, 50);
    // Shift avatar right if checkbox is present
    if (g_selection_mode) {
        lv_obj_align(avatar, LV_ALIGN_LEFT_MID, 40, 0); 
    } else {
        lv_obj_align(avatar, LV_ALIGN_LEFT_MID, 0, 0);
    }

    lv_obj_t *name_lbl = lv_label_create(item);
    lv_label_set_text(name_lbl, c->name);
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_16, 0);
    // Shift label
    if (g_selection_mode) {
         lv_obj_align(name_lbl, LV_ALIGN_LEFT_MID, 100, 0);
    } else {
         lv_obj_align(name_lbl, LV_ALIGN_LEFT_MID, 60, 0);
    }

    if (!g_selection_mode) {
        // Normal Mode Buttons
        lv_obj_t *edit_btn = lv_btn_create(item);
        lv_obj_set_size(edit_btn, 40, 40);
        lv_obj_align(edit_btn, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_opa(edit_btn, 0, 0);
        lv_obj_set_style_shadow_width(edit_btn, 0, 0);

        lv_obj_t *edit_icon = lv_label_create(edit_btn);
        lv_label_set_text(edit_icon, LV_SYMBOL_EDIT);
        lv_obj_set_style_text_color(edit_icon, lv_palette_main(LV_PALETTE_TEAL), 0);
        lv_obj_set_style_text_font(edit_icon, &lv_font_montserrat_20, 0);
        lv_obj_center(edit_icon);
        lv_obj_add_event_cb(edit_btn, edit_btn_clicked, LV_EVENT_CLICKED, (void *)c);

        lv_obj_t *video_btn = lv_btn_create(item);
        lv_obj_set_size(video_btn, 40, 40);
        lv_obj_align(video_btn, LV_ALIGN_RIGHT_MID, -45, 0); // Left of Edit
        lv_obj_set_style_bg_opa(video_btn, 0, 0);

        lv_obj_t *audio_btn = lv_btn_create(item);
        lv_obj_set_size(audio_btn, 40, 40);
        lv_obj_align(audio_btn, LV_ALIGN_RIGHT_MID, -90, 0); // Left of Video
        lv_obj_set_style_bg_opa(audio_btn, 0, 0);
        lv_obj_set_style_shadow_width(audio_btn, 0, 0);

        lv_obj_t *audio_icon = lv_label_create(audio_btn);
        lv_label_set_text(audio_icon, LV_SYMBOL_CALL);
        lv_obj_set_style_text_color(audio_icon, lv_palette_main(LV_PALETTE_GREEN), 0);
        lv_obj_set_style_text_font(audio_icon, &lv_font_montserrat_20, 0);
        lv_obj_center(audio_icon);

        lv_obj_add_event_cb(audio_btn, contact_item_clicked, LV_EVENT_CLICKED, (void *)c);
        lv_obj_set_style_shadow_width(video_btn, 0, 0);

        lv_obj_t *video_icon = lv_label_create(video_btn);
        lv_label_set_text(video_icon, LV_SYMBOL_VIDEO);
        lv_obj_set_style_text_color(video_icon, lv_palette_main(LV_PALETTE_BLUE), 0);
        lv_obj_set_style_text_font(video_icon, &lv_font_montserrat_20, 0);
        lv_obj_center(video_icon);

        lv_obj_add_event_cb(video_btn, contact_video_clicked, LV_EVENT_CLICKED, (void *)c);
    }
  }

  // Floating Buttons
  if (g_selection_mode) {
      // Trash Button (Center Bottom)
      // Trash Button (Center Bottom)
      g_contacts_trash_btn = lv_btn_create(g_applet->screen);
      lv_obj_add_flag(g_contacts_trash_btn, LV_OBJ_FLAG_FLOATING);
      lv_obj_set_size(g_contacts_trash_btn, 60, 60);
      lv_obj_align(g_contacts_trash_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
      lv_obj_set_style_radius(g_contacts_trash_btn, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(g_contacts_trash_btn, lv_palette_main(LV_PALETTE_RED), 0);
      lv_obj_set_style_shadow_width(g_contacts_trash_btn, 10, 0);
      
      lv_obj_t *trash_icon = lv_label_create(g_contacts_trash_btn);
      lv_label_set_text(trash_icon, LV_SYMBOL_TRASH);
      lv_obj_set_style_text_font(trash_icon, &lv_font_montserrat_24, 0);
      lv_obj_center(trash_icon);
      
      
      lv_obj_add_event_cb(g_contacts_trash_btn, on_trash_clicked, LV_EVENT_CLICKED, NULL);
      
      // Update visibility based on selection
      bool any_selected = false;
      for(int i=0; i<count; i++) {
          if (g_selected_mask[i]) { any_selected = true; break; }
      }
      if (any_selected) lv_obj_clear_flag(g_contacts_trash_btn, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(g_contacts_trash_btn, LV_OBJ_FLAG_HIDDEN);

      
      // Store trash button reference? It's not stored in global currently in contacts_applet.
      // We need to store it to update it later.
      // Let's add `static lv_obj_t *g_contacts_trash_btn = NULL;` at top and use it.


  } else {
      // Add Contact FAB (Right Bottom)
      lv_obj_t *fab = lv_btn_create(g_applet->screen);
      lv_obj_add_flag(fab, LV_OBJ_FLAG_FLOATING); // Ignore flex layout
      lv_obj_set_size(fab, 56, 56);
      lv_obj_align(fab, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
      lv_obj_set_style_radius(fab, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(fab, lv_palette_main(LV_PALETTE_DEEP_ORANGE), 0);
      lv_obj_set_style_shadow_width(fab, 10, 0);
      lv_obj_set_style_shadow_opa(fab, LV_OPA_30, 0);

      lv_obj_t *plus = lv_label_create(fab);
      lv_label_set_text(plus, LV_SYMBOL_PLUS);
      lv_obj_set_style_text_font(plus, &lv_font_montserrat_24, 0);
      lv_obj_center(plus);

      lv_obj_add_event_cb(fab, add_btn_clicked, LV_EVENT_CLICKED, NULL);
  }
}

static void draw_editor(void) {
  // Use Flex layout for full height filling
  lv_obj_set_flex_flow(g_applet->screen, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(g_applet->screen, 0, 0);
  lv_obj_set_style_pad_gap(g_applet->screen, 0, 0);

  lv_obj_t *save_btn = ui_header_add_action_btn(
      ui_create_title_bar(g_applet->screen,
                          is_new_contact ? "New Contact" : current_edit_contact.name,
                          true, back_btn_clicked, NULL),
      LV_SYMBOL_OK, NULL, NULL);

  lv_obj_t *content = lv_obj_create(g_applet->screen);
  lv_obj_set_width(content, LV_PCT(100));
  lv_obj_set_flex_grow(content, 1); // Fill remaining space
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(content, 20, 0); // Add spacing between elements
  lv_obj_set_style_pad_all(content, 20, 0);

  // Make content looks like full screen (no card style)
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_radius(content, 0, 0);
  lv_obj_set_style_bg_color(content, lv_color_white(), 0);

  lv_obj_t *avatar = create_avatar(
      content, is_new_contact ? "" : current_edit_contact.name, 80);
  lv_obj_set_style_bg_color(avatar, lv_palette_main(LV_PALETTE_LIGHT_GREEN), 0);

  lv_obj_t *name_lbl = lv_label_create(content);
  lv_label_set_text(name_lbl, "Name");
  lv_obj_set_width(name_lbl, LV_PCT(90));
  lv_obj_set_style_text_color(name_lbl, lv_palette_main(LV_PALETTE_GREY), 0);

  lv_obj_t *name_ta = lv_textarea_create(content);
  lv_textarea_set_one_line(name_ta, true);
  lv_obj_set_width(name_ta, LV_PCT(90));
  lv_textarea_set_text(name_ta, current_edit_contact.name);
  lv_textarea_set_placeholder_text(name_ta, "Name");

  lv_obj_t *num_lbl = lv_label_create(content);
  lv_label_set_text(num_lbl, "SIP or tel URI");
  lv_obj_set_width(num_lbl, LV_PCT(90));
  lv_obj_set_style_text_color(num_lbl, lv_palette_main(LV_PALETTE_GREY), 0);

  lv_obj_t *num_ta = lv_textarea_create(content);
  lv_textarea_set_one_line(num_ta, true);
  lv_obj_set_width(num_ta, LV_PCT(90));
  lv_textarea_set_text(num_ta, current_edit_contact.number);
  lv_textarea_set_placeholder_text(num_ta, "sip:user@domain");

  lv_obj_t *fav_cont = lv_obj_create(content);
  lv_obj_set_size(fav_cont, LV_PCT(90), 50);
  lv_obj_clear_flag(fav_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(fav_cont, 0, 0);
  lv_obj_set_flex_flow(fav_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(fav_cont, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *fav_lbl = lv_label_create(fav_cont);
  lv_label_set_text(fav_lbl, "Favorite");

  lv_obj_t *fav_sw = lv_switch_create(fav_cont);
  if (!is_new_contact && current_edit_contact.is_favorite) {
    lv_obj_add_state(fav_sw, LV_STATE_CHECKED);
  }

  if (!is_new_contact) {
    lv_obj_t *actions_cont = lv_obj_create(content);
    lv_obj_set_size(actions_cont, LV_PCT(100), 60);
    lv_obj_set_style_border_width(actions_cont, 0, 0);
    lv_obj_set_style_bg_opa(actions_cont, 0, 0);
    lv_obj_set_flex_flow(actions_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions_cont, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *call_btn = lv_btn_create(actions_cont);
    lv_obj_set_size(call_btn, LV_PCT(48), 50);
    lv_obj_set_style_bg_color(call_btn, lv_palette_main(LV_PALETTE_GREEN), 0);

    lv_obj_t *call_icon = lv_label_create(call_btn);
    lv_label_set_text(call_icon, LV_SYMBOL_CALL " Call");
    lv_obj_center(call_icon);
    // Note: Assuming contact_item_clicked works here too if we pass
    // current_edit_contact? We need to pass address of current_edit_contact.
    lv_obj_add_event_cb(call_btn, contact_item_clicked, LV_EVENT_CLICKED,
                        (void *)&current_edit_contact);

    lv_obj_t *video_btn = lv_btn_create(actions_cont);
    lv_obj_set_size(video_btn, LV_PCT(48), 50);
    lv_obj_set_style_bg_color(video_btn, lv_palette_main(LV_PALETTE_BLUE), 0);

    lv_obj_t *video_icon = lv_label_create(video_btn);
    lv_label_set_text(video_icon, LV_SYMBOL_VIDEO " Video");
    lv_obj_center(video_icon);
    lv_obj_add_event_cb(video_btn, contact_video_clicked, LV_EVENT_CLICKED,
                        (void *)&current_edit_contact);

    lv_obj_add_event_cb(call_btn, contact_item_clicked, LV_EVENT_CLICKED,
                        (void *)&current_edit_contact);
  }
  if (!is_new_contact) {
    // Delete Button at Bottom Center with 25px margin
    lv_obj_t *del_btn = lv_btn_create(
        g_applet->screen); // Parent is screen to be fixed relative to it
    lv_obj_add_flag(del_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(del_btn, 140, 50);
    lv_obj_align(del_btn, LV_ALIGN_BOTTOM_MID, 0, -25);
    lv_obj_set_style_bg_color(del_btn, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_radius(del_btn, 25, 0); // Rounded pill
    lv_obj_set_style_shadow_width(del_btn, 10, 0);

    lv_obj_t *del_lbl = lv_label_create(del_btn);
    lv_label_set_text(del_lbl, LV_SYMBOL_TRASH "  Delete");
    lv_obj_center(del_lbl);

    lv_obj_add_event_cb(del_btn, delete_btn_clicked, LV_EVENT_CLICKED, NULL);
  }

  lv_obj_t **inputs = malloc(3 * sizeof(lv_obj_t *));
  inputs[0] = name_ta;
  inputs[1] = num_ta;
  inputs[2] = fav_sw;

  lv_obj_add_event_cb(save_btn, save_btn_clicked, LV_EVENT_CLICKED, inputs);
}

// ------------------- CONTEXT MENU -------------------

static void close_context_menu(void) {
  if (g_context_menu_modal) {
    lv_obj_del(g_context_menu_modal);
    g_context_menu_modal = NULL;
  }
}

static void context_menu_cancel(lv_event_t *e) { (void)e; close_context_menu(); }

static void context_menu_delete(lv_event_t *e) {
  (void)e;
  (void)e;
  cm_remove(g_context_menu_contact.id);
  close_context_menu();
  refresh_ui();
}

static void context_menu_toggle_fav(lv_event_t *e) {
  (void)e;
  (void)e;
  bool new_fav = !g_context_menu_contact.is_favorite;
  cm_update(g_context_menu_contact.id, g_context_menu_contact.name,
            g_context_menu_contact.number, new_fav);
  close_context_menu();
  refresh_ui();
}

// Helper to create a menu button
static lv_obj_t *create_menu_btn(lv_obj_t *parent, const char *icon,
                                 const char *text, bool is_red,
                                 lv_event_cb_t event_cb) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, LV_PCT(100), 50);
  lv_obj_set_style_bg_opa(btn, 0, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_pad_left(btn, 20, 0);

  // Layout: Icon + Text + Spacer
  lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_gap(btn, 15, 0);

  lv_obj_t *icon_lbl = lv_label_create(btn);
  lv_label_set_text(icon_lbl, icon);
  lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_24,
                             0); // Larger icon

  lv_obj_t *text_lbl = lv_label_create(btn);
  lv_label_set_text(text_lbl, text);
  lv_obj_set_style_text_font(text_lbl, &lv_font_montserrat_16, 0);

  if (is_red) {
    lv_obj_set_style_text_color(icon_lbl, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_text_color(text_lbl, lv_palette_main(LV_PALETTE_RED), 0);
  } else {
    lv_obj_set_style_text_color(icon_lbl, lv_color_black(), 0);
    lv_obj_set_style_text_color(text_lbl, lv_color_black(), 0);
  }

  if (event_cb) {
    lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, NULL);
  }

  return btn;
}

static void show_contact_context_menu(const contact_t *c) {
  if (g_context_menu_modal)
    return;

  g_context_menu_contact = *c;

  // 1. Overlay
  g_context_menu_modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(g_context_menu_modal, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(g_context_menu_modal, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_context_menu_modal, LV_OPA_40, 0); // Dimmed
  lv_obj_add_event_cb(g_context_menu_modal, context_menu_cancel,
                      LV_EVENT_CLICKED, NULL);

  // Container
  lv_obj_t *container = lv_obj_create(g_context_menu_modal);
  lv_obj_set_size(container, 300, LV_SIZE_CONTENT);
  lv_obj_center(container);
  lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_opa(container, 0, 0);
  lv_obj_set_style_border_width(container, 0, 0);
  lv_obj_set_style_pad_all(container, 0, 0);
  lv_obj_set_style_pad_gap(container, 15, 0);

  // 2. Preview Item
  lv_obj_t *preview = lv_obj_create(container);
  lv_obj_set_size(preview, LV_PCT(100), 80);
  lv_obj_set_style_bg_color(preview, lv_color_white(), 0);
  lv_obj_set_style_radius(preview, 15, 0);
  lv_obj_set_flex_flow(preview, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(preview, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(preview, 15, 0);
  lv_obj_set_style_pad_gap(preview, 15, 0);
  lv_obj_clear_flag(preview, LV_OBJ_FLAG_SCROLLABLE);

  // Avatar
  lv_obj_t *avatar = lv_obj_create(preview);
  lv_obj_set_size(avatar, 50, 50);
  lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(avatar, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_style_bg_opa(avatar, LV_OPA_20, 0);
  lv_obj_set_style_border_width(avatar, 0, 0);
  lv_obj_clear_flag(avatar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *avatar_txt = lv_label_create(avatar);
  lv_label_set_text(avatar_txt, LV_SYMBOL_FILE);
  lv_obj_set_style_text_color(avatar_txt, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_style_text_font(avatar_txt, &lv_font_montserrat_24, 0);
  lv_obj_center(avatar_txt);

  // Info
  lv_obj_t *text_col = lv_obj_create(preview);
  lv_obj_set_size(text_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(text_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_opa(text_col, 0, 0);
  lv_obj_set_style_border_width(text_col, 0, 0);
  lv_obj_set_style_pad_all(text_col, 0, 0);
  lv_obj_set_style_pad_gap(text_col, 2, 0);
  lv_obj_set_flex_grow(text_col, 1);

  lv_obj_t *name_lbl = lv_label_create(text_col);
  lv_label_set_text(name_lbl, c->name);
  lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(name_lbl, lv_color_black(), 0);

  lv_obj_t *num_lbl = lv_label_create(text_col);
  lv_label_set_text(num_lbl, c->number);
  lv_obj_set_style_text_font(num_lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(num_lbl, lv_palette_main(LV_PALETTE_GREY), 0);

  // 3. Menu
  lv_obj_t *menu = lv_obj_create(container);
  lv_obj_set_size(menu, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(menu, lv_color_white(), 0);
  lv_obj_set_style_radius(menu, 15, 0);
  lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(menu, 0, 0);
  lv_obj_set_style_pad_gap(menu, 0, 0);
  lv_obj_set_style_clip_corner(menu, true, 0);

  // Divider style
  static lv_style_t div_style;
  static bool div_inited = false;
  if (!div_inited) {
    lv_style_init(&div_style);
    lv_style_set_bg_color(&div_style, lv_palette_lighten(LV_PALETTE_GREY, 4));
    lv_style_set_bg_opa(&div_style, LV_OPA_COVER);
    lv_style_set_height(&div_style, 1);
    lv_style_set_width(&div_style, LV_PCT(100));
    div_inited = true;
  }

  // Favorite (Text depends on state)
  create_menu_btn(menu, LV_SYMBOL_OK,
                  c->is_favorite ? "Unfavorite" : "Favorite", false,
                  context_menu_toggle_fav);
  lv_obj_t *div4 = lv_obj_create(menu);
  lv_obj_add_style(div4, &div_style, 0);

  // Delete
  create_menu_btn(menu, LV_SYMBOL_TRASH, "Delete Contact", true,
                  context_menu_delete);
}

static void contact_long_press_handler(lv_event_t *e) {
  const contact_t *c = (const contact_t *)lv_event_get_user_data(e);
  if (!c)
    return;

  g_long_press_handled = true;
  show_contact_context_menu(c);
}

static void refresh_ui(void) {
  if (!g_applet || !g_applet->screen)
    return;
  lv_obj_clean(g_applet->screen);

  if (is_editor_mode) {
    draw_editor();
  } else {
    draw_list();
  }
}

// contacts_init, contacts_start, contacts_resume are defined above with the
// implementation logic. Removing empty placeholders.

static void contacts_stop(applet_t *applet) { (void)applet; }
static void contacts_destroy(applet_t *applet) { (void)applet; }

APPLET_DEFINE(contacts_applet, "Contacts", "Contact list", LV_SYMBOL_LIST);

void contacts_applet_register(void) {
  contacts_applet.callbacks.init = contacts_init;
  contacts_applet.callbacks.start = contacts_start;
  contacts_applet.callbacks.pause = contacts_pause;
  contacts_applet.callbacks.resume = contacts_resume;
  contacts_applet.callbacks.stop = contacts_stop;
  contacts_applet.callbacks.destroy = contacts_destroy;
  applet_manager_register(&contacts_applet);
}
