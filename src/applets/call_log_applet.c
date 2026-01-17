#include "applet.h"
#include "applet_manager.h"
#include "baresip_manager.h"
#include "call_applet.h"
#include "config_manager.h"
#include "history_manager.h"
#include "database_manager.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include "../ui/ui_helpers.h"

// Picker State
static char g_pending_number[128] = {0};
static bool g_pending_is_video = false;
static lv_obj_t *g_account_picker_modal = NULL;
static lv_obj_t *g_detail_screen = NULL;

// Helpers
static void create_avatar(lv_obj_t *parent, const char *text, int size, int font_size);
static void get_date_header(long timestamp, char *buf, size_t size);
static void show_detail_screen(const char *number, const char *name);
static void hide_detail_screen(void);


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
extern void chat_applet_open_peer(const char *peer);
extern applet_t chat_applet;
static lv_obj_t *g_context_menu_modal = NULL;
static call_log_entry_t g_context_menu_entry;
static int g_context_menu_index = -1;
static bool g_long_press_handled = false;
static void show_call_log_context_menu(const call_log_entry_t *entry,
                                       int index);

// Filter & Selection State
static bool g_filter_missed_only = false;
static bool g_selection_mode = false;
static bool g_selected_items[100]; // Matches MAX_HISTORY behavior
static lv_obj_t *g_trash_fab = NULL; // Trash FAB
static lv_obj_t *g_edit_btn = NULL; // Header Edit Btn
static lv_obj_t *g_filter_btn_all = NULL;
static lv_obj_t *g_filter_btn_missed = NULL;

extern void history_delete_mask(const bool *selection, int count);

// Picker Implementation
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

  snprintf(g_pending_number, sizeof(g_pending_number), "%s", number);
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

static void context_menu_cancel(lv_event_t *e) { (void)e; close_context_menu(); }

// Need reference to self for refresh
extern applet_t call_log_applet;

// Helper to refresh the list content
static void populate_log_list(void);

static void context_menu_delete_log_refresh(lv_event_t *e) {
  (void)e;
  if (g_context_menu_index >= 0) {
    history_remove(g_context_menu_index);
    close_context_menu();

    // Refresh list preserving scroll position
    populate_log_list();
  }
}

static void context_menu_add_contact(lv_event_t *e) {
  (void)e;
  close_context_menu();
  const char *number = g_context_menu_entry.number;
  // Open Contacts Applet in New Mode
  contacts_applet_open_new(number);
  applet_manager_launch_applet(&contacts_applet);
}

static void context_menu_call_action(lv_event_t *e) {
  (void)e;
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
  create_menu_btn(menu, LV_SYMBOL_CALL, "Audio Call", false,
                  context_menu_call_action);
  create_menu_btn(menu, LV_SYMBOL_VIDEO, "Video Call", false,
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
/* Unused function removed */

// Event handler for video call back button
/* Unused function removed */

// Make applet pointer global so we can refresh
static applet_t *g_log_applet = NULL;

// Global list object
static lv_obj_t *g_call_log_list = NULL;

// --- Helpers ---
static void get_date_header(long timestamp, char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm *t_now = localtime(&now);
    int day_curr = t_now->tm_yday;
    int year_curr = t_now->tm_year;

    struct tm *t_ts = localtime(&timestamp);
    int day_ts = t_ts->tm_yday;
    int year_ts = t_ts->tm_year;

    if (year_curr == year_ts && day_curr == day_ts) {
        snprintf(buf, size, "Today");
    } else if (year_curr == year_ts && day_curr == day_ts + 1) {
         snprintf(buf, size, "Yesterday");
    } else {
         strftime(buf, size, "%b %d %A", t_ts); // e.g. Jan 4 Sunday
    }
}

static void create_avatar(lv_obj_t *parent, const char *text, int size, int font_size) {
    lv_obj_t *avatar = lv_obj_create(parent);
    lv_obj_set_size(avatar, size, size);
    lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(avatar, lv_color_hex(0x333333), 0); // Dark Grey/Black
    lv_obj_set_style_border_width(avatar, 0, 0);
    lv_obj_clear_flag(avatar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(avatar, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lbl = lv_label_create(avatar);
    // Initials logic? Or just number?
    // If text is number, just show it? if name, show initial?
    // Image shows "8" for 808086. "9" for 99. "G" for George?
    // Let's take first char.
    char initial[4] = {0};
    if (text && strlen(text) > 0) {
        // Skip sip: if present (should be cleaned before passed)
        initial[0] = text[0]; 
    }
    lv_label_set_text(lbl, initial);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    if(font_size == 0) font_size = 24;
    // Hacky font selection based on size
    if (font_size >= 32) lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0);
    else lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    
    lv_obj_center(lbl);
}

// --- Detail Screen ---
static void detail_call_clicked(lv_event_t *e) {
    const char *number = lv_event_get_user_data(e);
    // Call Logic
    if (number) {
         baresip_manager_call(number); // Simplified for now, or use picker logic
         call_applet_request_active_view();
         applet_manager_launch_applet(&call_applet);
    }
}

static void detail_msg_clicked(lv_event_t *e) {
     const char *number = (const char *)lv_event_get_user_data(e);
     if (number) {
         log_info("CallLogApplet", "Opening Chat with %s", number);
         chat_applet_open_peer(number);
         applet_manager_launch_applet(&chat_applet);
     }
}

static void hide_detail_screen(void) {
    if (g_detail_screen) {
        lv_obj_del(g_detail_screen);
        g_detail_screen = NULL;
    }
}

static void show_detail_screen(const char *number, const char *name) {
    if (g_detail_screen) return;
    
    g_detail_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_detail_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_detail_screen, lv_color_white(), 0);
    lv_obj_set_flex_flow(g_detail_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(g_detail_screen, 0, 0);

    // Header (Custom for Detail View to overlay main header)
    // Header (Custom for Detail View to overlay main header)
    lv_obj_t *header = ui_create_title_bar(g_detail_screen, 
                                          (name && strlen(name)>0) ? name : number, 
                                          true, (lv_event_cb_t)hide_detail_screen, NULL);
    // Align? ui_create_title_bar aligns to top.
    
    // Note: ORIGINAL used custom alignment and radius. Common widget enforces standard.
    // If we want "floating card" look for detail screen, we might need to override styles.
    // But "common title bar" implies standard look. We stick to that.
    
    // We don't need to manually create back button or title.
    // But we might need to hide the title passed to helper if we want custom title logic?
    // The helper sets text. We passed the name/number.
    
    /* 
    lv_obj_t *header = lv_obj_create(g_detail_screen);
    ...
    */
    
    // Fix: ui_create_title_bar creates the label. We can get it if we need to style it?
    // For now, standard is fine. "name" or "number" is displayed.
    
    // Subtitle logic (URI) was in original...
    // Original lines 542-550 created title.
    // Lines 583-596 created subtitle in content area.
    // So standard title is fine.

    lv_obj_t *title_lbl = lv_label_create(header);
    lv_label_set_text(title_lbl, (name && strlen(name)>0) ? name : number); // Show Name or Number
    lv_obj_set_style_text_color(title_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(title_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title_lbl, 200); // Limit width to avoid overlap
    lv_obj_set_style_text_align(title_lbl, LV_TEXT_ALIGN_CENTER, 0);

    // Content Container (Centered info)
    lv_obj_t *info_cont = lv_obj_create(g_detail_screen);
    lv_obj_set_size(info_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(info_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(info_cont, 0, 0);
    lv_obj_set_style_border_width(info_cont, 0, 0);
    lv_obj_set_style_pad_all(info_cont, 20, 0);
    
    // Clean URI for display
    char clean_uri[128];
    strncpy(clean_uri, number, sizeof(clean_uri)-1);
    clean_uri[sizeof(clean_uri)-1] = '\0';
    
    // Strip params
    char *p = strchr(clean_uri, ';');
    if (p) *p = '\0';
    
    // Strip prefix
    char *start = clean_uri;
    if (strncmp(start, "sip:", 4) == 0) start += 4;
    else if (strncmp(start, "sips:", 5) == 0) start += 5;

    // Large Avatar
    create_avatar(info_cont, (name && strlen(name)>0) ? name : start, 100, 48);
    
    // Number/Name
    lv_obj_t *main_lbl = lv_label_create(info_cont);
    lv_label_set_text(main_lbl, (name && strlen(name)>0) ? name : start);
    lv_obj_set_style_text_font(main_lbl, &lv_font_montserrat_32, 0);
    lv_obj_set_style_pad_top(main_lbl, 10, 0);
    
    // SIP URI (Subtitle) - Only show if Name is present, otherwise redundant (or show full if needed?)
    // User said: "show sip uri as username@domain".
    // If name is present, Main=Name, Sub=URI.
    // If name absent, Main=URI, Sub=Hidden?
    lv_obj_t *sub_lbl = lv_label_create(info_cont);
    lv_label_set_text(sub_lbl, start); 
    if (name && strlen(name)>0) {
        lv_obj_clear_flag(sub_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Main label already shows URI. Hide subtitle to avoid duplicate.
        // Or show nothing?
        // Let's hide it if redundant.
        lv_obj_add_flag(sub_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_text_color(sub_lbl, lv_palette_main(LV_PALETTE_ORANGE), 0);
    
    // Actions Row
    lv_obj_t *actions = lv_obj_create(info_cont);
    lv_obj_set_size(actions, LV_PCT(80), 100);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(actions, 0, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_gap(actions, 40, 0);

    // Call FAB
    lv_obj_t *btn_call = lv_btn_create(actions);
    lv_obj_set_size(btn_call, 60, 60);
    lv_obj_set_style_radius(btn_call, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn_call, lv_palette_main(LV_PALETTE_DEEP_ORANGE), 0); // Red/Orange per image
    lv_obj_t *lbl_call = lv_label_create(btn_call);
    lv_label_set_text(lbl_call, LV_SYMBOL_CALL);
    lv_obj_center(lbl_call);
    char *num_copy = lv_mem_alloc(128);
    strcpy(num_copy, number);
    lv_obj_add_event_cb(btn_call, detail_call_clicked, LV_EVENT_CLICKED, num_copy);

    // Msg FAB
    lv_obj_t *btn_msg = lv_btn_create(actions);
    lv_obj_set_size(btn_msg, 60, 60);
    lv_obj_set_style_radius(btn_msg, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn_msg, lv_palette_main(LV_PALETTE_DEEP_ORANGE), 0);
    lv_obj_t *lbl_msg = lv_label_create(btn_msg);
    lv_label_set_text(lbl_msg, LV_SYMBOL_EDIT); // Message icon fallback
    lv_obj_center(lbl_msg);
    
    char *num_copy_msg = lv_mem_alloc(128);
    strcpy(num_copy_msg, number);
    lv_obj_add_event_cb(btn_msg, detail_msg_clicked, LV_EVENT_CLICKED, num_copy_msg);
    
    // History List for this contact
    lv_obj_t *hist_list = lv_obj_create(g_detail_screen);
    lv_obj_set_size(hist_list, LV_PCT(100), LV_PCT(40)); // Fill rest
    lv_obj_set_flex_flow(hist_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(hist_list, 0, 0);
    lv_obj_set_style_bg_opa(hist_list, 0, 0);
    
    // Populate History for this contact
    // We iterate global history. 
    int count = history_get_count();
    for(int i=0; i<count; i++) {
        const call_log_entry_t *e = history_get_at(i);
        // Simple match: check if number is same
        if (strstr(e->number, number) || (strlen(name)>0 && strstr(e->name, name))) {
             lv_obj_t *item = lv_obj_create(hist_list);
             lv_obj_set_size(item, LV_PCT(100), 50);
             lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
             lv_obj_set_flex_align(item, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
             
             // Icon + Status
             lv_obj_t *left = lv_obj_create(item);
             lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
             lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
             lv_obj_set_style_bg_opa(left, 0, 0);
             lv_obj_set_style_border_width(left, 0, 0);
             
             lv_obj_t *icon = lv_label_create(left);
             // Use tiny arrow
             if (e->type == CALL_TYPE_INCOMING) {
                 lv_label_set_text(icon, LV_SYMBOL_DOWN); 
                 lv_obj_set_style_text_color(icon, lv_palette_main(LV_PALETTE_GREEN), 0);
             } else if (e->type == CALL_TYPE_OUTGOING) {
                 lv_label_set_text(icon, LV_SYMBOL_UP);
                 lv_obj_set_style_text_color(icon, lv_palette_main(LV_PALETTE_BLUE), 0);
             } else {
                 lv_label_set_text(icon, "!");
                 lv_obj_set_style_text_color(icon, lv_palette_main(LV_PALETTE_RED), 0);
             }
             
             lv_obj_t *lbl_st = lv_label_create(left);
             lv_label_set_text(lbl_st, (e->type == CALL_TYPE_INCOMING) ? "Incoming" : "Outgoing");
             
             // Time
             lv_obj_t *time = lv_label_create(item);
             char time_short[64];
             // Extract HH:MM from YYYY-MM-DD HH:MM
             if (strlen(e->time) > 11) {
                 strcpy(time_short, e->time + 11);
             } else {
                 strcpy(time_short, e->time);
             }
             lv_label_set_text(time, time_short);
        }
    }
}

static void log_item_clicked(lv_event_t *e) {
    if (g_long_press_handled) {
        g_long_press_handled = false;
        return;
    }
    call_log_entry_t *entry = (call_log_entry_t *)lv_event_get_user_data(e);
    if (!entry) return;
    
    // Resolve name
    char name_disp[256] = {0};
    if (db_contact_find(entry->number, name_disp, sizeof(name_disp)) != 0) {
        if (strlen(entry->name) > 0) strcpy(name_disp, entry->name);
    }
    
    show_detail_screen(entry->number, name_disp);
}

// Checkbox handler
static void list_checkbox_cb(lv_event_t *e) {
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *cb = lv_event_get_target(e);
    if (index >= 0 && index < 100) {
        g_selected_items[index] = lv_obj_has_state(cb, LV_STATE_CHECKED);
        
        // Update Trash Visibility
        int count = 0;
        for(int i=0; i<100; i++) if(g_selected_items[i]) count++;
        
        if (g_trash_fab) {
            if (count > 0) lv_obj_clear_flag(g_trash_fab, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(g_trash_fab, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void log_item_toggle_select(lv_event_t *e) {
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index >= 0 && index < 100) {
        g_selected_items[index] = !g_selected_items[index];
        populate_log_list(); // Refresh
    }
}

static void toggle_edit_mode_clicked(lv_event_t *e) {
    (void)e;
    g_selection_mode = !g_selection_mode;
    if (g_selection_mode) {
        memset(g_selected_items, 0, sizeof(g_selected_items));
    }
    populate_log_list();
}

static void delete_selected_logs_clicked(lv_event_t *e) {
    (void)e;
    // History delete mask
    history_delete_mask(g_selected_items, 100);
    g_selection_mode = false;
    populate_log_list();
}

static void populate_log_list(void) {
  if (!g_call_log_list) return;

  lv_coord_t scroll_y = lv_obj_get_scroll_y(g_call_log_list);
  lv_obj_clean(g_call_log_list);

  int total = history_get_count();
  char prev_date_str[64] = {0};
  
  for (int i = 0; i < total; i++) {
    const call_log_entry_t *entry = history_get_at(i);
    
    // Filter
    if (g_filter_missed_only && entry->type != CALL_TYPE_MISSED) continue;
    
    // 1. Date Header
    char date_header[64];
    get_date_header(entry->timestamp, date_header, sizeof(date_header));
    
    if (strcmp(date_header, prev_date_str) != 0) {
        // Add Header
        lv_obj_t *hdr = lv_obj_create(g_call_log_list);
        lv_obj_set_size(hdr, LV_PCT(100), 30);
        lv_obj_set_style_bg_color(hdr, lv_palette_lighten(LV_PALETTE_GREY, 4), 0);
        lv_obj_set_style_border_width(hdr, 0, 0);
        lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(hdr, 10, 0);
        
        lv_obj_t *lbl = lv_label_create(hdr);
        lv_label_set_text(lbl, date_header);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        strcpy(prev_date_str, date_header);
    }
    
    // 3. Render Item
    lv_obj_t *item = lv_obj_create(g_call_log_list);
    lv_obj_set_size(item, LV_PCT(100), 70);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(item, lv_color_white(), 0);
    lv_obj_set_style_border_width(item, 0, 0);
    lv_obj_set_style_border_side(item, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(item, 1, 0);
    lv_obj_set_style_border_color(item, lv_palette_lighten(LV_PALETTE_GREY, 3), 0);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(item, 10, 0);
    lv_obj_set_style_pad_gap(item, 15, 0);

    // SELECTION MODE
    if (g_selection_mode) {
        lv_obj_t *cb = lv_checkbox_create(item);
        lv_checkbox_set_text(cb, "");
        lv_obj_align(cb, LV_ALIGN_LEFT_MID, 0, 0);
        if (g_selected_items[i]) lv_obj_add_state(cb, LV_STATE_CHECKED);
        lv_obj_add_event_cb(cb, list_checkbox_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)i);
        
        // Allow row click to toggle
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(item, log_item_toggle_select, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    } else {
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        call_log_entry_t *entry_copy = lv_mem_alloc(sizeof(call_log_entry_t));
        *entry_copy = *entry;
        lv_obj_add_event_cb(item, log_item_clicked, LV_EVENT_CLICKED, entry_copy);
        lv_obj_add_event_cb(item, log_long_press_handler, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)i);
    }

    // Adjust left padding for avatar if checkbox present
    int x_offset = g_selection_mode ? 40 : 0;

    // Avatar
    // Reuse cleaning
    char uri_clean[128];
    strncpy(uri_clean, entry->number, sizeof(uri_clean)-1); uri_clean[127] = 0;
    char *p = strchr(uri_clean, ';'); if(p) *p='\0';
    if(strncmp(uri_clean, "sip:", 4)==0) { memmove(uri_clean, uri_clean+4, strlen(uri_clean+4)+1); }
    
    char number_disp[128], name_disp[128];
    if (db_contact_find(entry->number, name_disp, sizeof(name_disp)) == 0) {
        snprintf(number_disp, sizeof(number_disp), "%s", name_disp);
    } else {
        snprintf(number_disp, sizeof(number_disp), "%s", uri_clean);
    }
    
    lv_obj_t *av = lv_obj_create(item);
    lv_obj_set_size(av, 40, 40);
    lv_obj_set_style_radius(av, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(av, lv_palette_main(LV_PALETTE_BLUE_GREY), 0);
    lv_obj_align(av, LV_ALIGN_LEFT_MID, x_offset, 0);
    
    lv_obj_t *lbl = lv_label_create(av);
    lv_label_set_text(lbl, (strlen(number_disp)>0) ? number_disp : "?"); 
    lv_obj_center(lbl);
    // Let's use create_avatar but we need to modify it to accept parent?
    // Existing create_avatar attaches to parent center. We want to align it manually?
    // The previous code called create_avatar(item...).
    // Let's just create it manually here to control alignment easily.
    // Or wrapper.
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);

    // ... Rest of item drawing ...
    // Since I replaced the block ending at create_avatar, I must ensure I didn't break functionality.
    // The original code: 
    // create_avatar(item, number_disp, 40, 0);
    //
    // I need to replicate create_avatar OR modify create_avatar to support offset.
    // create_avatar centers the label. It doesn't algin the avatar obj itself?
    // Checking create_avatar implementation in original file (Line 460):
    // lv_obj_create(parent) ...
    // It doesn't set alignment on the avatar object itself relative to parent!
    // So it flows in flex layout?
    // Yes, `item` has `LV_FLEX_FLOW_ROW`.
    // So just adding elements adds them to flow.
    //
    // So if I add checkbox first, then avatar, it flows correctly!
    // I don't need manual `x_offset` and `lv_obj_align` if flex is used!
    // Let's check `item` creation:
    // lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    //
    // YES! Flex flow handles it. 
    // If g_selection_mode, I added checkbox. It appears first.
    // Then create_avatar(item...) adds avatar. It appears second.
    // So NO manual offset needed!
    //
    // I should revert the manual offset logic and just rely on flex order.
    //
    // BUT, `create_avatar` was called at the end of the original snippet I replaced.
    // So I should just call `create_avatar` normally.
    
    create_avatar(item, number_disp, 40, 0);
    
    // Icon
    lv_obj_t *icon = lv_label_create(item);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    if (entry->type == CALL_TYPE_INCOMING) {
        lv_label_set_text(icon, LV_SYMBOL_DOWN); 
        lv_obj_set_style_text_color(icon, lv_palette_main(LV_PALETTE_GREEN), 0);
    } else if (entry->type == CALL_TYPE_OUTGOING) {
        lv_label_set_text(icon, LV_SYMBOL_UP);
        lv_obj_set_style_text_color(icon, lv_palette_main(LV_PALETTE_BLUE), 0);
    } else {
        lv_label_set_text(icon, "!"); 
        lv_obj_set_style_text_color(icon, lv_palette_main(LV_PALETTE_RED), 0);
    }
    
    // Info
    lv_obj_t *info = lv_obj_create(item);
    lv_obj_clear_flag(info, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_grow(info, 1);
    lv_obj_set_width(info, 0); // Important: 0 to allow Flex Grow without forcing full width
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START); // Center Vertically
    lv_obj_set_style_bg_opa(info, 0, 0);
    lv_obj_set_style_border_width(info, 0, 0);
    lv_obj_set_style_pad_all(info, 0, 0);
    lv_obj_set_style_pad_gap(info, 0, 0);
    
    // Restore Name Label
    lv_obj_t *r1 = lv_label_create(info);
    lv_label_set_text(r1, number_disp);
    lv_obj_set_style_text_font(r1, LV_FONT_DEFAULT, 0); 
    lv_obj_set_style_text_color(r1, lv_color_black(), 0);
    lv_label_set_long_mode(r1, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(r1, LV_PCT(100));

    // Debug Info Container (Removed)
    lv_obj_set_style_bg_opa(info, 0, 0);

    log_info("CallLog", "Added row: '%s'", number_disp);
    
    // Time (Subtitle)
    char time_short[64];
    if (strlen(entry->time) > 11) strcpy(time_short, entry->time + 11);
    else strcpy(time_short, entry->time);
    
    lv_obj_t *r2 = lv_label_create(info);
    lv_label_set_text(r2, time_short);
    lv_obj_set_style_text_color(r2, lv_palette_main(LV_PALETTE_GREY), 0);
  }
  
  lv_obj_scroll_to_y(g_call_log_list, scroll_y, LV_ANIM_OFF);
}

static void filter_all_clicked(lv_event_t *e) {
    (void)e;
    g_filter_missed_only = false;
    // Update button styles
    lv_obj_set_style_bg_color(g_filter_btn_all, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_bg_color(g_filter_btn_missed, lv_palette_main(LV_PALETTE_GREY), 0);
    populate_log_list();
}

static void filter_missed_clicked(lv_event_t *e) {
    (void)e;
    g_filter_missed_only = true;
    lv_obj_set_style_bg_color(g_filter_btn_all, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_bg_color(g_filter_btn_missed, lv_palette_main(LV_PALETTE_BLUE), 0);
    populate_log_list();
}

// Old mode_toggle_clicked removed

static int call_log_init(applet_t *applet) {
  log_info("CallLogApplet", "Initializing");
  g_log_applet = applet;
  lv_obj_clean(applet->screen);

  // Main Layout
  lv_obj_set_flex_flow(applet->screen, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(applet->screen, 0, 0);
  lv_obj_set_style_pad_gap(applet->screen, 0, 0);

  // 1. Header (Manual Layout for precise centering)
  // 1. Header
  lv_obj_t *header = ui_create_title_bar(applet->screen, "Call Log", true, back_btn_clicked, NULL);
  
  // Edit Button (Right)
  // Edit Button (Right)
  g_edit_btn = ui_header_add_action_btn(header, LV_SYMBOL_EDIT, toggle_edit_mode_clicked, NULL);

  // 2. Filter Row (Below Header)
  lv_obj_t *filter_row = lv_obj_create(applet->screen);
  lv_obj_set_size(filter_row, LV_PCT(100), 50);
  lv_obj_set_style_bg_opa(filter_row, 0, 0);
  lv_obj_set_style_border_width(filter_row, 0, 0);
  lv_obj_set_flex_flow(filter_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(filter_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(filter_row, 5, 0);
  lv_obj_set_style_pad_gap(filter_row, 20, 0);

  // Filter Buttons
  g_filter_btn_all = lv_btn_create(filter_row);
  lv_obj_set_size(g_filter_btn_all, 80, 36);
  lv_obj_set_style_bg_color(g_filter_btn_all, lv_palette_main(LV_PALETTE_BLUE), 0); 
  lv_obj_t *lbl_all = lv_label_create(g_filter_btn_all);
  lv_label_set_text(lbl_all, "All");
  lv_obj_center(lbl_all); // Center text
  lv_obj_add_event_cb(g_filter_btn_all, filter_all_clicked, LV_EVENT_CLICKED, NULL);

  g_filter_btn_missed = lv_btn_create(filter_row);
  lv_obj_set_size(g_filter_btn_missed, 80, 36);
  lv_obj_set_style_bg_color(g_filter_btn_missed, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_t *lbl_missed = lv_label_create(g_filter_btn_missed);
  lv_label_set_text(lbl_missed, "Missed");
  lv_obj_center(lbl_missed); // Center text
  lv_obj_add_event_cb(g_filter_btn_missed, filter_missed_clicked, LV_EVENT_CLICKED, NULL);

  // 3. List
  g_call_log_list = lv_obj_create(applet->screen);
  lv_obj_set_width(g_call_log_list, LV_PCT(100)); // Full width
  lv_obj_set_flex_grow(g_call_log_list, 1); 
  
  // Match Padding with Header (approx)
  lv_obj_set_style_pad_left(g_call_log_list, 15, 0);
  lv_obj_set_style_pad_right(g_call_log_list, 15, 0);
  
  lv_obj_align(g_call_log_list, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_flex_flow(g_call_log_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(g_call_log_list, LV_DIR_VER);
  
  // Add top padding to list so it doesn't touch the header immediately?
  lv_obj_set_style_pad_top(g_call_log_list, 10, 0);

  populate_log_list();

  // Trash FAB (Bottom Center) - Hidden by default
  g_trash_fab = lv_btn_create(applet->screen);
  lv_obj_add_flag(g_trash_fab, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_size(g_trash_fab, 56, 56);
  lv_obj_align(g_trash_fab, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_style_radius(g_trash_fab, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_trash_fab, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_add_flag(g_trash_fab, LV_OBJ_FLAG_HIDDEN);
  
  lv_obj_t *t_icon = lv_label_create(g_trash_fab);
  lv_label_set_text(t_icon, LV_SYMBOL_TRASH);
  lv_obj_center(t_icon);
  lv_obj_add_event_cb(g_trash_fab, delete_selected_logs_clicked, LV_EVENT_CLICKED, NULL);

  return 0;
}

static void call_log_start(applet_t *applet) {
  (void)applet;
  log_info("CallLogApplet", "Started");
  populate_log_list();
  db_mark_missed_calls_read();
}

static void call_log_pause(applet_t *applet) {
  (void)applet;
  log_debug("CallLogApplet", "Paused");
}

static void call_log_resume(applet_t *applet) {
  (void)applet;
  log_debug("CallLogApplet", "Resumed");
  populate_log_list();
  db_mark_missed_calls_read();

  // Refresh list to show new entries
  populate_log_list();

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
