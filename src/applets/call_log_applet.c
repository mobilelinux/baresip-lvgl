#include "applet.h"
#include "applet_manager.h"
#include "baresip_manager.h"
#include "call_applet.h"
#include "config_manager.h"
#include "history_manager.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>

// Picker State
static char g_pending_number[128] = {0};
static bool g_pending_is_video = false;
static lv_obj_t *g_account_picker_modal = NULL;

static void close_picker_modal(void);
static void show_account_picker(const char *number, bool is_video);
// Event handler for back button
static void back_btn_clicked(lv_event_t *e) { applet_manager_back(); }

// External reference to call applet
// External reference to call applet
extern applet_t call_applet;
// External reference to leads to contacts applet
extern void contacts_applet_open_new(const char *number);
extern applet_t contacts_applet; // Need to launch it

// Context Menu State
// Context Menu State
static lv_obj_t *g_context_menu_modal = NULL;
static call_log_entry_t g_context_menu_entry;
static int g_context_menu_index = -1;
static bool g_long_press_handled = false;
static void show_call_log_context_menu(const call_log_entry_t *entry,
                                       int index);

// Picker Implementation
static void close_picker_modal(void) {
  if (g_account_picker_modal) {
    lv_obj_del(g_account_picker_modal);
    g_account_picker_modal = NULL;
  }
}

static void account_picker_cancel(lv_event_t *e) { close_picker_modal(); }

static void account_picker_item_clicked(lv_event_t *e) {
  const char *aor = (const char *)lv_event_get_user_data(e);
  if (aor && strlen(g_pending_number) > 0) {
    int ret = -1;
    if (g_pending_is_video) {
      log_info("CallLogApplet", "Picking account %s for VIDEO calling", aor);
      ret = baresip_manager_videocall_with_account(g_pending_number, aor);
    } else {
      log_info("CallLogApplet", "Picking account %s for audio calling", aor);
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

    char *aor_copy = lv_mem_alloc(256);
    snprintf(aor_copy, 256, "sip:%s@%s", accounts[i].username,
             accounts[i].server);

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

// ------------------- CONTEXT MENU -------------------

static void close_context_menu(void) {
  if (g_context_menu_modal) {
    lv_obj_del(g_context_menu_modal);
    g_context_menu_modal = NULL;
  }
}

static void context_menu_cancel(lv_event_t *e) { close_context_menu(); }

// Need reference to self for refresh
extern applet_t call_log_applet;

// Helper to refresh the list content
static void populate_log_list(void);

static void context_menu_delete_log_refresh(lv_event_t *e) {
  if (g_context_menu_index >= 0) {
    history_remove(g_context_menu_index);
    close_context_menu();

    // Refresh list preserving scroll position
    populate_log_list();
  }
}

static void context_menu_add_contact(lv_event_t *e) {
  close_context_menu();
  // Open Contacts Applet in New Mode
  contacts_applet_open_new(g_context_menu_entry.number);
  applet_manager_launch_applet(&contacts_applet);
}

static void context_menu_call_action(lv_event_t *e) {
  close_context_menu();

  char number_buf[128];
  snprintf(number_buf, sizeof(number_buf), "%s", g_context_menu_entry.number);

  // Reuse logic from call_back_clicked for account selection
  if (strlen(number_buf) > 0) {
    int ret = -1;
    if (strlen(g_context_menu_entry.account_aor) > 0) {
      log_info("CallLogApplet", "Context Call: using stored account: %s",
               g_context_menu_entry.account_aor);
      ret = baresip_manager_call_with_account(number_buf,
                                              g_context_menu_entry.account_aor);
    } else {
      // No stored account, check default config
      app_config_t config;
      config_load_app_settings(&config);

      if (config.default_account_index >= 0) {
        voip_account_t accounts[MAX_ACCOUNTS];
        int count = config_load_accounts(accounts, MAX_ACCOUNTS);
        if (config.default_account_index < count) {
          char aor[256];
          voip_account_t *acc = &accounts[config.default_account_index];
          snprintf(aor, sizeof(aor), "sip:%s@%s", acc->username, acc->server);
          ret = baresip_manager_call_with_account(number_buf, aor);
        } else {
          ret = baresip_manager_call(number_buf);
        }
      } else {
        // No default account, Show Picker
        // Note: showing picker from here is tricky because we just closed
        // context menu. But show_account_picker handles its own modal. We need
        // to set g_pending_number first? show_account_picker does that.
        show_account_picker(number_buf, false);
        return;
      }
    }

    if (ret == 0) {
      call_applet_request_active_view();
      applet_manager_launch_applet(&call_applet);
    } else {
      applet_manager_show_toast("Account not available.");
    }
  }
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

static void show_call_log_context_menu(const call_log_entry_t *entry,
                                       int index) {
  if (g_context_menu_modal)
    return;

  g_context_menu_entry = *entry;
  g_context_menu_index = index;

  // 1. Overlay
  g_context_menu_modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(g_context_menu_modal, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(g_context_menu_modal, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_context_menu_modal, LV_OPA_40, 0); // Dimmed
  lv_obj_add_event_cb(g_context_menu_modal, context_menu_cancel,
                      LV_EVENT_CLICKED, NULL);

  // Container for Preview + Menu (Centered vertically/horizontally or aligned)
  // Screenshot shows it floating. We'll use a flex column centered.
  lv_obj_t *container = lv_obj_create(g_context_menu_modal);
  lv_obj_set_size(container, 300,
                  LV_SIZE_CONTENT); // Fixed width like phone menu
  lv_obj_center(container);
  lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_opa(container, 0, 0); // Transparent container
  lv_obj_set_style_border_width(container, 0, 0);
  lv_obj_set_style_pad_all(container, 0, 0);
  lv_obj_set_style_pad_gap(container, 15, 0); // Gap between Preview and Menu

  // 2. Preview Item (The "Pill")
  lv_obj_t *preview = lv_obj_create(container);
  lv_obj_set_size(preview, LV_PCT(100), 80); // Fixed height approx
  lv_obj_set_style_bg_color(preview, lv_color_white(), 0);
  lv_obj_set_style_radius(preview, 15, 0);
  lv_obj_set_flex_flow(preview, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(preview, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(preview, 15, 0);
  lv_obj_set_style_pad_gap(preview, 15, 0);
  lv_obj_clear_flag(preview, LV_OBJ_FLAG_SCROLLABLE);

  // Avatar Icon (Circle)
  lv_obj_t *avatar = lv_obj_create(preview);
  lv_obj_set_size(avatar, 50, 50);
  lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(avatar, lv_palette_main(LV_PALETTE_GREY),
                            0);                  // Default grey
  lv_obj_set_style_bg_opa(avatar, LV_OPA_20, 0); // Light grey
  lv_obj_set_style_border_width(avatar, 0, 0);
  lv_obj_clear_flag(avatar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *avatar_txt = lv_label_create(avatar);
  lv_label_set_text(avatar_txt, LV_SYMBOL_FILE);
  lv_obj_set_style_text_color(avatar_txt, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_style_text_font(avatar_txt, &lv_font_montserrat_24, 0);
  lv_obj_center(avatar_txt);

  // Text Info (Name + Number)
  lv_obj_t *text_col = lv_obj_create(preview);
  lv_obj_set_size(text_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(text_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_opa(text_col, 0, 0);
  lv_obj_set_style_border_width(text_col, 0, 0);
  lv_obj_set_style_pad_all(text_col, 0, 0);
  lv_obj_set_style_pad_gap(text_col, 2, 0); // Tight gap
  lv_obj_set_flex_grow(text_col, 1);        // Grow to fill space

  lv_obj_t *name_lbl = lv_label_create(text_col);
  lv_label_set_text(name_lbl, (entry->name[0] ? entry->name : entry->number));
  lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_16,
                             0); // Bold-ish if possible, we keep 16
  lv_obj_set_style_text_color(name_lbl, lv_color_black(), 0);

  lv_obj_t *num_lbl = lv_label_create(text_col);
  lv_label_set_text(num_lbl, entry->number);
  lv_obj_set_style_text_font(num_lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(num_lbl, lv_palette_main(LV_PALETTE_GREY), 0);

  // Trailing Info Icon
  // Since we don't have "Info" action specifically, maybe skip or just show it
  // static
  lv_obj_t *info_icon = lv_label_create(preview);
  lv_label_set_text(
      info_icon,
      LV_SYMBOL_NEW_LINE); // "info" symbol tricky, using generic or skipping.
  // LV_SYMBOL_WARNING is closest to 'i' in circle? Or just blank.
  // We'll skip it to keep it clean unless requested.

  // 3. Menu Container
  lv_obj_t *menu = lv_obj_create(container);
  lv_obj_set_size(menu, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(menu, lv_color_white(), 0);
  lv_obj_set_style_radius(menu, 15, 0);
  lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(menu, 0, 0);
  lv_obj_set_style_pad_gap(menu, 0, 0);        // Handled by buttons/separators
  lv_obj_set_style_clip_corner(menu, true, 0); // Clip children to radius

  // Menu Items
  // Call
  create_menu_btn(menu, LV_SYMBOL_CALL, "Call", false,
                  context_menu_call_action);
  // placeholder for Call action?
  // Wait, "Call" should probably call? I should split context_menu_add_contact
  // logic or make new callbacks. For now, I'll bind existing callbacks or
  // placeholders.

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

  lv_obj_t *div1 = lv_obj_create(menu);
  lv_obj_add_style(div1, &div_style, 0);

  // Create Contact (Placeholder)
  create_menu_btn(menu, LV_SYMBOL_PLUS, "Create New Contact", false,
                  context_menu_add_contact);

  lv_obj_t *div3 = lv_obj_create(menu);
  lv_obj_add_style(div3, &div_style, 0);

  // Delete
  create_menu_btn(menu, LV_SYMBOL_TRASH, "Delete", true,
                  context_menu_delete_log_refresh);
}

static void log_long_press_handler(lv_event_t *e) {
  // User data is index cast to void*? No, we need entry and index.
  // history_get_at returns pointer.
  // Let's pass the index in user_data as intptr_t
  intptr_t index = (intptr_t)lv_event_get_user_data(e);
  const call_log_entry_t *entry = history_get_at((int)index);
  if (entry) {
    g_long_press_handled = true;
    show_call_log_context_menu(entry, (int)index);
  }
}

// Event handler for call back button
static void call_back_clicked(lv_event_t *e) {
  if (g_long_press_handled) {
    g_long_press_handled = false;
    return;
  }
  const call_log_entry_t *entry =
      (const call_log_entry_t *)lv_event_get_user_data(e);
  if (entry) {
    // Make a local copy of data to ensure memory safety
    char name_buf[64];
    char number_buf[128];

    // Use snprintf for safe copying with null termination
    snprintf(name_buf, sizeof(name_buf), "%s", entry->name);
    snprintf(number_buf, sizeof(number_buf), "%s", entry->number);

    log_info("CallLogApplet", "Calling back %s at %s", name_buf, number_buf);

    if (strlen(number_buf) > 0) {
      int ret = -1;
      if (strlen(entry->account_aor) > 0) {
        log_info("CallLogApplet", "Calling back using stored account: %s",
                 entry->account_aor);
        ret = baresip_manager_call_with_account(number_buf, entry->account_aor);
      } else {
        // No stored account, check default config
        app_config_t config;
        config_load_app_settings(&config);

        if (config.default_account_index >= 0) {
          voip_account_t accounts[MAX_ACCOUNTS];
          int count = config_load_accounts(accounts, MAX_ACCOUNTS);
          if (config.default_account_index < count) {
            // Use Default
            char aor[256];
            voip_account_t *acc = &accounts[config.default_account_index];
            snprintf(aor, sizeof(aor), "sip:%s@%s", acc->username, acc->server);
            ret = baresip_manager_call_with_account(number_buf, aor);
          } else {
            ret = baresip_manager_call(number_buf); // Fallback
          }
        } else {
          // No default account, Show Picker
          log_info("CallLogApplet", "No default account, showing picker");
          show_account_picker(number_buf, false);
          return;
        }
      }

      if (ret == 0) {
        // Switch to call applet if call initiated successfully
        call_applet_request_active_view();
        applet_manager_launch_applet(&call_applet);
      }
    } else {
      log_warn("CallLogApplet", "Cannot call empty number");
    }
  }
}

// Event handler for video call back button
static void video_call_back_clicked(lv_event_t *e) {
  if (g_long_press_handled) {
    g_long_press_handled = false;
    return;
  }
  const call_log_entry_t *entry =
      (const call_log_entry_t *)lv_event_get_user_data(e);
  if (entry) {
    char number_buf[128];
    snprintf(number_buf, sizeof(number_buf), "%s", entry->number);

    log_info("CallLogApplet", "Video calling back %s", number_buf);

    if (strlen(number_buf) > 0) {
      int ret = -1;
      if (strlen(entry->account_aor) > 0) {
        ret = baresip_manager_videocall_with_account(number_buf,
                                                     entry->account_aor);
      } else {
        // No stored account, check default
        app_config_t config;
        config_load_app_settings(&config);

        if (config.default_account_index >= 0) {
          voip_account_t accounts[MAX_ACCOUNTS];
          int count = config_load_accounts(accounts, MAX_ACCOUNTS);
          if (config.default_account_index < count) {
            char aor[256];
            voip_account_t *acc = &accounts[config.default_account_index];
            snprintf(aor, sizeof(aor), "sip:%s@%s", acc->username, acc->server);
            ret = baresip_manager_videocall_with_account(number_buf, aor);
          } else {
            ret = baresip_manager_videocall(number_buf);
          }
        } else {
          // Picker
          log_info("CallLogApplet",
                   "No default account, showing picker for VIDEO");
          show_account_picker(number_buf, true);
          return;
        }
      }

      if (ret == 0) {
        call_applet_request_active_view();
        applet_manager_launch_applet(&call_applet);
      }
    }
  }
}

// Get call type icon and color
static const char *get_call_icon(call_type_t type) {
  switch (type) {
  case CALL_TYPE_INCOMING:
    return LV_SYMBOL_DOWNLOAD;
  case CALL_TYPE_OUTGOING:
    return LV_SYMBOL_UPLOAD;
  case CALL_TYPE_MISSED:
    return LV_SYMBOL_CLOSE;
  default:
    return LV_SYMBOL_CALL;
  }
}

static lv_color_t get_call_color(call_type_t type) {
  switch (type) {
  case CALL_TYPE_INCOMING:
    return lv_color_hex(0x00AA00);
  case CALL_TYPE_OUTGOING:
    return lv_color_hex(0x0088FF);
  case CALL_TYPE_MISSED:
    return lv_color_hex(0xFF0000);
  default:
    return lv_color_hex(0x808080);
  }
}

// Make applet pointer global so we can refresh
static applet_t *g_log_applet = NULL;

// Global list object
static lv_obj_t *g_call_log_list = NULL;

static void populate_log_list(void) {
  if (!g_call_log_list)
    return;

  // Save scroll position
  lv_coord_t scroll_y = lv_obj_get_scroll_y(g_call_log_list);

  lv_obj_clean(g_call_log_list);

  // Add call log entries
  history_manager_init();
  int count = history_get_count();

  for (int i = 0; i < count; i++) {
    const call_log_entry_t *entry = history_get_at(i);
    if (!entry)
      continue;

    lv_obj_t *log_item = lv_obj_create(g_call_log_list);
    lv_obj_set_size(log_item, LV_PCT(95), 70);
    lv_obj_set_scrollbar_mode(log_item, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(log_item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(log_item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(log_item, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Add Long Press for Context Menu
    lv_obj_add_flag(log_item,
                    LV_OBJ_FLAG_CLICKABLE); // Ensure item captures input
    lv_obj_add_event_cb(log_item, log_long_press_handler, LV_EVENT_LONG_PRESSED,
                        (void *)(intptr_t)i);

    // Call type icon
    lv_obj_t *icon_label = lv_label_create(log_item);
    lv_label_set_text(icon_label, get_call_icon(entry->type));
    lv_obj_set_style_text_color(icon_label, get_call_color(entry->type), 0);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_20, 0);

    // Call info container
    lv_obj_t *info_container = lv_obj_create(log_item);
    lv_obj_set_flex_grow(info_container, 1); // Use flex grow to fill space
    lv_obj_set_size(info_container, LV_SIZE_CONTENT, LV_PCT(90)); // Auto width
    lv_obj_set_flex_flow(info_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info_container, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(info_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(info_container, 0, 0);
    lv_obj_set_style_bg_opa(info_container, 0, 0); // Transparent
    lv_obj_set_style_pad_all(info_container, 0, 0);

    lv_obj_t *details_row = lv_obj_create(info_container);
    lv_obj_set_size(details_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(details_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(details_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(details_row, 0, 0);
    lv_obj_set_style_border_width(details_row, 0, 0);
    lv_obj_set_style_bg_opa(details_row, 0, 0);

    // Number (SIP URI) - Cleaned
    char uri_buf[128];
    strncpy(uri_buf, entry->number, sizeof(uri_buf) - 1);
    uri_buf[sizeof(uri_buf) - 1] = '\0'; // Ensure null termination

    char *param_start = strchr(uri_buf, ';');
    if (param_start) {
      *param_start = '\0'; // Strip parameters like ;transport=udp
    }

    // log_debug("CallLogApplet", "Entry %d cleaned URI: '%s' (Original: '%s')",
    // i,
    //           uri_buf, entry->number);

    lv_obj_t *number_label = lv_label_create(details_row);
    char number_display[150];
    // Strip sip: or sips: prefix for display
    const char *display_ptr = uri_buf;
    if (strncmp(display_ptr, "sip:", 4) == 0)
      display_ptr += 4;
    else if (strncmp(display_ptr, "sips:", 5) == 0)
      display_ptr += 5;

    snprintf(number_display, sizeof(number_display), "%s", display_ptr);

    lv_label_set_text(number_label, number_display);
    lv_obj_set_style_text_color(number_label, lv_color_hex(0x808080),
                                0); // Keep Grey as per requirements
    lv_obj_set_style_text_font(number_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_right(number_label, 10,
                               0); // Spacing between number and time

    // Time
    lv_obj_t *time_label = lv_label_create(details_row);
    lv_label_set_text(time_label, entry->time);
    lv_obj_set_style_text_color(time_label, lv_color_black(),
                                0); // Black for visibility
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_16, 0);

    // Call back button
    lv_obj_t *call_btn = lv_btn_create(log_item);
    lv_obj_set_size(call_btn, 60, 40);
    lv_obj_set_style_bg_color(call_btn, lv_color_hex(0x00AA00), 0);
    lv_obj_t *call_label = lv_label_create(call_btn);
    lv_label_set_text(call_label, LV_SYMBOL_CALL);
    lv_obj_center(call_label);
    lv_obj_add_event_cb(call_btn, call_back_clicked, LV_EVENT_CLICKED,
                        (void *)entry);

    // Video Call button
    lv_obj_t *video_btn = lv_btn_create(log_item);
    lv_obj_set_size(video_btn, 60, 40);
    lv_obj_set_style_bg_color(video_btn, lv_color_hex(0x0055AA),
                              0); // Blue for video
    lv_obj_t *video_label = lv_label_create(video_btn);
    lv_label_set_text(video_label, LV_SYMBOL_VIDEO);
    lv_obj_center(video_label);
    lv_obj_add_event_cb(video_btn, video_call_back_clicked, LV_EVENT_CLICKED,
                        (void *)entry);
  }

  // Restore scroll
  lv_obj_scroll_to_y(g_call_log_list, scroll_y, LV_ANIM_OFF);
}

static int call_log_init(applet_t *applet) {
  log_info("CallLogApplet", "Initializing");
  g_log_applet = applet;
  lv_obj_clean(applet->screen); // Ensure clean state for refresh

  // Create header with back button
  lv_obj_t *header = lv_obj_create(applet->screen);
  lv_obj_set_size(header, LV_PCT(100), 60);
  lv_obj_set_scrollbar_mode(header, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *back_btn = lv_btn_create(header);
  lv_obj_set_size(back_btn, 50, 40);
  lv_obj_t *back_label = lv_label_create(back_btn);
  lv_label_set_text(back_label, LV_SYMBOL_LEFT);
  lv_obj_center(back_label);
  lv_obj_add_event_cb(back_btn, back_btn_clicked, LV_EVENT_CLICKED, NULL);

  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text(title, "Call Log");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_pad_left(title, 20, 0);

  // Create scrollable call log list
  g_call_log_list = lv_obj_create(applet->screen);
  lv_obj_set_size(g_call_log_list, LV_PCT(95), LV_PCT(80));
  lv_obj_align(g_call_log_list, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_flex_flow(g_call_log_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(g_call_log_list, LV_DIR_VER);

  // Add call log entries
  populate_log_list();

  return 0;
}

static void call_log_start(applet_t *applet) {
  (void)applet;
  log_info("CallLogApplet", "Started");
}

static void call_log_pause(applet_t *applet) {
  (void)applet;
  log_debug("CallLogApplet", "Paused");
}

static void call_log_resume(applet_t *applet) {
  (void)applet;
  log_debug("CallLogApplet", "Resumed");

  if (g_call_log_list) {
    lv_obj_scroll_to_y(g_call_log_list, 0, LV_ANIM_OFF);
  }
}

static void call_log_stop(applet_t *applet) {
  (void)applet;
  log_info("CallLogApplet", "Stopped");
}

static void call_log_destroy(applet_t *applet) {
  (void)applet;
  log_info("CallLogApplet", "Destroying");
}

// Define the call log applet
APPLET_DEFINE(call_log_applet, "Call Log", "Recent calls", LV_SYMBOL_IMAGE);

// Initialize callbacks
void call_log_applet_register(void) {
  call_log_applet.callbacks.init = call_log_init;
  call_log_applet.callbacks.start = call_log_start;
  call_log_applet.callbacks.pause = call_log_pause;
  call_log_applet.callbacks.resume = call_log_resume;
  call_log_applet.callbacks.stop = call_log_stop;
  call_log_applet.callbacks.destroy = call_log_destroy;

  applet_manager_register(&call_log_applet);
}
