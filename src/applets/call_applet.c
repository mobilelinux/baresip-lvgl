#include "applet.h"
#include "applet_manager.h"
#include "logger.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_ACCOUNTS 10
#define CONFIG_DIR ".baresip-lvgl"

#include "baresip_manager.h"
#include "call_applet.h"
#include "config_manager.h"

// Audio codec definitions if not in config_manager.h
// Assuming config_manager.h defines audio_codec_t and voip_account_t
// If not, we might need them here, but previously they seemed to cause errors
// if repeated. Based on previous file content, they were commented out as
// "defined in config_manager.h"

static const char *codec_names[] = {"G.711 PCMU", "G.711 PCMA", "Opus", "G.722",
                                    "GSM"};

// Call applet data
typedef struct {
  // Screens
  lv_obj_t *dialer_screen;
  lv_obj_t *active_call_screen;

  // Dialer widgets
  lv_obj_t *number_label;
  char number_buffer[64];

  // Active Call widgets
  lv_obj_t *call_name_label;
  lv_obj_t *call_number_label;
  lv_obj_t *call_duration_label;
  lv_obj_t *call_status_label;
  lv_obj_t *mute_btn;
  lv_obj_t *speaker_btn;
  lv_obj_t *hold_btn;
  lv_obj_t *hangup_btn;
  lv_obj_t *answer_btn;       /* Audio Answer */
  lv_obj_t *video_answer_btn; /* Video Answer */

  // Number Pad
  lv_obj_t *numpad_area;
  lv_obj_t *dtmf_modal; // DTMF Overlay
  lv_obj_t *dtmf_label; // Display for pressed keys

  // Multi-Call UI
  lv_obj_t *call_main_area;
  lv_obj_t *call_list_cont;
  lv_obj_t *call_list_title;

  // Video Call UI
  lv_obj_t *video_cont;
  lv_obj_t *video_remote;
  lv_obj_t *video_local;

  // Call State
  bool is_muted;
  bool is_speaker;
  bool is_hold;
  uint32_t call_start_time; // Timers
  lv_timer_t *call_timer;   // For active Call Duration
  // lv_timer_t *status_timer; // Removed duplicate
  // lv_timer_t *video_timer;  // Removed duplicate

  // Settings data
  app_config_t config;
  voip_account_t accounts[MAX_ACCOUNTS];
  int account_count;
  reg_status_t account_status[MAX_ACCOUNTS]; // Registration status per account

  // Dialer widgets
  lv_obj_t *dialer_account_dropdown;
  lv_obj_t *dialer_status_icon;

  // Thread safety for callbacks
  volatile bool status_update_pending;
  // lv_timer_t *status_timer; // Duplicate, removed
  applet_t *applet; // Back reference for switching

  // State tracking
  // State tracking
  enum call_state current_state;
  char current_peer_uri[256];

  // Navigation
  // Navigation
  bool request_active_view;

  // Pending Call State (for Account Picker)
  char pending_number[64]; // State
  bool pending_video;      /* If true, next active screen shows video */
  lv_timer_t *status_timer;
  lv_timer_t *video_timer;

  // Thread-Safe UI Update Mechanism
  lv_timer_t *ui_poller;
  bool ui_update_needed;
  enum call_state pending_state;
  char pending_peer_uri[256];
  bool pending_incoming;

  lv_obj_t *account_picker_modal;
  lv_timer_t *exit_timer; // Timer for delayed exit on call termination
} call_data_t;

// Global pointer to current applet data for callback
static call_data_t *g_call_data = NULL;
// View Modes: 0=None, 1=Active(Connected/Out), 2=Incoming
#define VIEW_MODE_NONE 0
#define VIEW_MODE_ACTIVE 1
#define VIEW_MODE_INCOMING 2
static int g_req_view_mode = VIEW_MODE_NONE;

// Forward declarations
// Forward declarations
static void query_answer_action(lv_event_t *e);
static void query_reject_action(lv_event_t *e);
static void show_active_call_screen(call_data_t *data, const char *number,
                                    bool incoming);
static void show_dialer_screen(call_data_t *data);
static void show_account_picker(call_data_t *data);
static void account_picker_event_cb(lv_event_t *e);
static void update_call_duration(lv_timer_t *timer);
static void update_call_list(call_data_t *data, void *ignore_id);
static void load_settings(call_data_t *data);
static void dropdown_event_cb(lv_event_t *e);

static void update_account_status(call_data_t *data, int account_idx,
                                  reg_status_t status);
static void reg_status_callback(const char *aor, reg_status_t status);
static void update_account_dropdowns(call_data_t *data);
static void menu_quit_action(call_data_t *data);
static void update_video_geometry(lv_timer_t *t);

// Helper to copy file
static int copy_file(const char *src_path, const char *dst_path) {
  FILE *src = fopen(src_path, "rb");
  if (!src)
    return -1;

  FILE *dst = fopen(dst_path, "wb");
  if (!dst) {
    fclose(src);
    return -1;
  }

  char buffer[1024];
  size_t n;
  while ((n = fread(buffer, 1, sizeof(buffer), src)) > 0) {
    fwrite(buffer, 1, n, dst);
  }

  fclose(src);
  fclose(dst);
  return 0;
}

// Load settings from files
static void load_settings(call_data_t *data) {
  log_info("CallApplet", "Loading settings via ConfigManager");
  if (config_load_app_settings(&data->config) != 0) {
    data->config.preferred_codec = CODEC_OPUS;
    data->config.default_account_index = -1; // Default to Always Ask on error
  }
  data->account_count = config_load_accounts(data->accounts, MAX_ACCOUNTS);
  log_info("CallApplet",
           "Settings loaded: Codec=%s, "
           "AccCount=%d",
           config_get_codec_name(data->config.preferred_codec),
           data->account_count);
}

// Save settings to files

// Status update timer callback (runs in
// main thread)
static void status_timer_cb(lv_timer_t *timer) {
  call_data_t *data = (call_data_t *)timer->user_data;
  if (!data || !data->status_update_pending)
    return;

  data->status_update_pending = false;

  // Refresh UI for all accounts
  // Update dialer status icon
  // Refresh UI for all accounts
  // Update dialer status icon
  if (data->config.default_account_index >= 0 &&
      data->config.default_account_index < data->account_count) {
    update_account_status(
        data, data->config.default_account_index,
        data->account_status[data->config.default_account_index]);
  }
}

// Registration status callback (runs in
// background thread)
static void reg_status_callback(const char *aor, reg_status_t status) {
  if (!g_call_data || !aor)
    return;

  log_debug("CallApplet",
            "Registration status update: "
            "%s -> %d",
            aor, status);

  for (int i = 0; i < g_call_data->account_count; i++) {
    if (strstr(aor, g_call_data->accounts[i].server) &&
        strstr(aor, g_call_data->accounts[i].username)) {
      g_call_data->account_status[i] = status;
      g_call_data->status_update_pending = true;
      break;
    }
  }
}

// Update account registration status and UI
static void update_account_status(call_data_t *data, int account_idx,
                                  reg_status_t status) {
  if (account_idx < 0 || account_idx >= data->account_count)
    return;

  data->account_status[account_idx] = status;

  // Update dialer status icon if showing
  // default account
  if (account_idx == data->config.default_account_index &&
      data->dialer_status_icon) {
    lv_color_t color;
    switch (status) {
    case REG_STATUS_REGISTERED:
      color = lv_color_hex(0x00AA00);
      break; // Green
    case REG_STATUS_FAILED:
      color = lv_color_hex(0xFF0000);
      break; // Red
    case REG_STATUS_REGISTERING:
      color = lv_color_hex(0x808080);
      break; // Gray
    default:
      color = lv_color_hex(0x404040);
      break; // Dark gray
    }
    lv_obj_set_style_bg_color(data->dialer_status_icon, color, 0);
  }
}

// Event handlers
static void back_to_home(lv_event_t *e) {
  (void)e;
  applet_manager_back();
}

static void status_icon_clicked(lv_event_t *e) {
  call_data_t *data = (call_data_t *)lv_event_get_user_data(e);
  if (!data)
    return;

  int idx = data->config.default_account_index;
  if (idx < 0 || idx >= data->account_count) {
    applet_manager_show_toast("No default account selected.");
    return;
  }

  // Allow re-register attempt if failed OR registering (stuck)
  if (data->account_status[idx] != REG_STATUS_REGISTERED) {
    voip_account_t *acc = &data->accounts[idx];
    char aor[256];
    snprintf(aor, sizeof(aor), "sip:%s@%s", acc->username, acc->server);

    applet_manager_show_toast("Retrying registration...");

    // Immediate visual feedback
    update_account_status(data, idx, REG_STATUS_REGISTERING);

    baresip_manager_account_register(aor);
    // Ignore return value as requested to avoid popup
  } else {
    applet_manager_show_toast("Account is already registered.");
  }
}

static void call_key_handler(lv_event_t *e) {
  (void)e;
  uint32_t key = lv_indev_get_key(lv_indev_get_act());
  if (key == LV_KEY_ESC) {
    applet_manager_back();
    return;
  }

  if (!g_call_data)
    return;

  call_info_t calls[8];
  int count = baresip_manager_get_active_calls(calls, 8);
  if (count <= 1)
    return;

  int current_idx = -1;
  for (int i = 0; i < count; i++) {
    if (calls[i].is_current) {
      current_idx = i;
      break;
    }
  }
  if (current_idx == -1)
    current_idx = 0;

  int next_idx = current_idx;
  if (key == LV_KEY_UP) {
    next_idx = (current_idx - 1 + count) % count;
  } else if (key == LV_KEY_DOWN) {
    next_idx = (current_idx + 1) % count;
  }

  if (next_idx != current_idx) {
    baresip_manager_switch_to(calls[next_idx].id);
    update_call_list(g_call_data, NULL);
  }
}

extern applet_t about_applet;

static void menu_about_action(call_data_t *data) {
  (void)data;
  applet_manager_launch_applet(&about_applet);
}

static void menu_quit_action(call_data_t *data) {
  (void)data;
  exit(0);
}

static void menu_close_clicked(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  // When attached to modal_bg, obj IS the modal_bg layout
  lv_obj_del(obj);
}

static void menu_event_cb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  call_data_t *data = (call_data_t *)lv_event_get_user_data(e);
  const char *txt = lv_list_get_btn_text(lv_obj_get_parent(obj), obj);

  extern void settings_open_accounts(void);      // Defined in settings_applet.c
  extern void settings_open_call_settings(void); // Defined in settings_applet.c

  if (strcmp(txt, "About") == 0) {
    menu_about_action(data);
  } else if (strcmp(txt, "Settings") == 0) {
    settings_open_call_settings();
    applet_manager_launch("Settings");
  } else if (strcmp(txt, "Accounts") == 0) {
    settings_open_accounts();
    applet_manager_launch("Settings");

  } else if (strcmp(txt, "Quit") == 0) {
    menu_quit_action(data);
  }

  lv_obj_t *list = lv_obj_get_parent(obj);
  lv_obj_t *menu_container = lv_obj_get_parent(list);
  lv_obj_t *modal_bg = lv_obj_get_parent(menu_container);
  lv_obj_del(modal_bg);
}

static void menu_btn_clicked(lv_event_t *e) {
  (void)e;
  applet_t *applet = applet_manager_get_current();
  call_data_t *data = (call_data_t *)applet->user_data;

  lv_obj_t *modal_bg = lv_obj_create(lv_scr_act());
  lv_obj_set_size(modal_bg, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(modal_bg, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(modal_bg, LV_OPA_50, 0);
  lv_obj_set_style_border_width(modal_bg, 0, 0);
  lv_obj_clear_flag(modal_bg, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(modal_bg, menu_close_clicked, LV_EVENT_CLICKED, NULL);

  lv_obj_t *menu_container = lv_obj_create(modal_bg);
  lv_obj_set_size(menu_container, 220, LV_SIZE_CONTENT);
  lv_obj_align(menu_container, LV_ALIGN_TOP_RIGHT, -10, 60);
  lv_obj_set_flex_flow(menu_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(menu_container, 0, 0);
  lv_obj_set_style_radius(menu_container, 10, 0);

  // Header removed as requested

  lv_obj_t *list = lv_list_create(menu_container);
  lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
  // lv_obj_set_flex_grow(list, 1); // Removed to auto-fit
  lv_obj_set_style_border_width(list, 0, 0);

  lv_obj_t *btn;
  btn = lv_list_add_btn(list, LV_SYMBOL_WARNING, "About");
  lv_obj_add_event_cb(btn, menu_event_cb, LV_EVENT_CLICKED, data);

  btn = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, "Settings");
  lv_obj_add_event_cb(btn, menu_event_cb, LV_EVENT_CLICKED, data);

  btn = lv_list_add_btn(list, LV_SYMBOL_DIRECTORY, "Accounts");
  lv_obj_add_event_cb(btn, menu_event_cb, LV_EVENT_CLICKED, data);

  btn = lv_list_add_btn(list, LV_SYMBOL_POWER, "Quit");
  lv_obj_add_event_cb(btn, menu_event_cb, LV_EVENT_CLICKED, data);
}

// DTMF Keypad Actions
static void dtmf_key_clicked(lv_event_t *e) {
  const char *key = (const char *)lv_event_get_user_data(e);
  if (!key)
    return;

  // Send generic DTMF
  baresip_manager_send_dtmf(key[0]);

  // Update display if available
  if (g_call_data && g_call_data->dtmf_label) {
    const char *old = lv_label_get_text(g_call_data->dtmf_label);
    lv_label_set_text_fmt(g_call_data->dtmf_label, "%s%c", old, key[0]);
  }
}

static void dtmf_close_clicked(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  // Walk up to modal bg
  lv_obj_t *parent = obj;
  while (parent) {
    if (parent == g_call_data->dtmf_modal) {
      lv_obj_del(parent);
      g_call_data->dtmf_modal = NULL;
      g_call_data->dtmf_label = NULL;
      return;
    }
    parent = lv_obj_get_parent(parent);
  }
  // Fallback if structure changes
  if (g_call_data->dtmf_modal) {
    lv_obj_del(g_call_data->dtmf_modal);
    g_call_data->dtmf_modal = NULL;
    g_call_data->dtmf_label = NULL;
  }
}

static void show_dtmf_keypad(call_data_t *data) {
  if (data->dtmf_modal)
    return; // Already showing

  lv_obj_t *modal = lv_obj_create(lv_scr_act());
  lv_obj_set_size(modal, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(modal, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(modal, LV_OPA_60, 0);
  lv_obj_set_style_border_width(modal, 0, 0);

  data->dtmf_modal = modal;

  lv_obj_t *panel = lv_obj_create(modal);
  lv_obj_set_size(panel, 280, 380);
  lv_obj_center(panel);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x202020), 0);
  lv_obj_set_style_radius(panel, 15, 0);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(panel, 10, 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(0x444444), 0);
  lv_obj_set_style_border_width(panel, 1, 0);

  // Header with Display and Close X
  lv_obj_t *header = lv_obj_create(panel);
  lv_obj_set_size(header, LV_PCT(100), 50);
  lv_obj_set_style_bg_opa(header, 0, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(header, 0, 0);

  data->dtmf_label = lv_label_create(header);
  lv_label_set_text(data->dtmf_label, "");
  lv_obj_set_style_text_font(data->dtmf_label, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(data->dtmf_label, lv_color_white(), 0);

  lv_obj_t *close_btn = lv_btn_create(header);
  lv_obj_set_size(close_btn, 30, 30);
  lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x555555), 0);
  lv_obj_set_style_radius(close_btn, 5, 0);
  lv_obj_t *close_txt = lv_label_create(close_btn);
  lv_label_set_text(close_txt, LV_SYMBOL_CLOSE);
  lv_obj_center(close_txt);
  lv_obj_add_event_cb(close_btn, dtmf_close_clicked, LV_EVENT_CLICKED, NULL);

  // Grid
  lv_obj_t *grid = lv_obj_create(panel);
  lv_obj_set_size(grid, LV_PCT(100), 240);
  lv_obj_set_style_bg_opa(grid, 0, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 0, 0); // Remove default padding
                                        // to fit 3 cols
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER); // Center
                                               // items, use
                                               // fixed gap
  lv_obj_set_style_pad_column(grid, 15, 0);
  lv_obj_set_style_pad_row(grid, 10, 0);

  const char *keys[] = {"1", "2", "3", "4", "5", "6",
                        "7", "8", "9", "*", "0", "#"};
  for (int i = 0; i < 12; i++) {
    lv_obj_t *btn = lv_btn_create(grid);
    lv_obj_set_size(btn, 70, 50);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x3A3A3A), 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, keys[i]);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, dtmf_key_clicked, LV_EVENT_CLICKED,
                        (void *)keys[i]);
  }

  // Bottom OK button
  lv_obj_t *ok_btn = lv_btn_create(panel);
  lv_obj_set_size(ok_btn, LV_PCT(100), 40);
  lv_obj_set_style_bg_color(ok_btn, lv_color_hex(0x007AFF),
                            0); // iOS Blue
  lv_obj_t *ok_lbl = lv_label_create(ok_btn);
  lv_label_set_text(ok_lbl, "OK");
  lv_obj_center(ok_lbl);
  lv_obj_add_event_cb(ok_btn, dtmf_close_clicked, LV_EVENT_CLICKED, NULL);
}

// Wrapper for button click
static void show_dtmf_clicked(lv_event_t *e) {
  (void)e;
  if (g_call_data) {
    show_dtmf_keypad(g_call_data);
  }
}

static void dropdown_event_cb(lv_event_t *e) {
  lv_obj_t *dropdown = lv_event_get_target(e);
  call_data_t *data = (call_data_t *)lv_event_get_user_data(e);

  uint16_t selected_idx = lv_dropdown_get_selected(dropdown);
  log_info("CallApplet", "Dropdown selected: %d", selected_idx);

  // Update config
  data->config.default_account_index = selected_idx;
  config_save_app_settings(&data->config);

  // Update status icon
  if (selected_idx < data->account_count) {
    update_account_status(data, selected_idx,
                          data->account_status[selected_idx]);
  } else {
    // "None/Always Ask" selected - set to gray
    if (data->dialer_status_icon)
      lv_obj_set_style_bg_color(data->dialer_status_icon,
                                lv_color_hex(0x404040), 0);
  }
}

static void show_dialer_screen(call_data_t *data) {
  lv_obj_clear_flag(data->dialer_screen, LV_OBJ_FLAG_HIDDEN);
  if (data->active_call_screen)
    lv_obj_add_flag(data->active_call_screen, LV_OBJ_FLAG_HIDDEN);

  lv_group_t *g = lv_group_get_default();
  if (g) {
    lv_group_remove_all_objs(g);
    lv_group_add_obj(g, data->dialer_screen);
    lv_group_focus_obj(data->dialer_screen);
  }

  // Reload settings (Issue 1 & 2 fix)
  load_settings(data);

  // Force update status icon when showing screen
  // Force update status icon based on current dropdown selection
  if (data->dialer_account_dropdown) {
    uint16_t selected = lv_dropdown_get_selected(data->dialer_account_dropdown);
    if (selected < data->account_count) {
      update_account_status(data, selected, data->account_status[selected]);
    } else {
      // "None/Always Ask" selected - set to gray
      if (data->dialer_status_icon)
        lv_obj_set_style_bg_color(data->dialer_status_icon,
                                  lv_color_hex(0x404040), 0);
    }
  }
}

// Helper to clean SIP URI for display
static void format_sip_uri(const char *in, char *out, size_t out_size) {
  if (!in || !out || out_size == 0) {
    if (out && out_size > 0)
      out[0] = '\0';
    return;
  }

  const char *start = in;

  // Handle "Display Name" <sip:user@domain>
  // format
  const char *bracket_open = strchr(in, '<');
  if (bracket_open) {
    start = bracket_open + 1;
  }

  // Skip scheme
  if (strncmp(start, "sip:", 4) == 0) {
    start += 4;
  } else if (strncmp(start, "sips:", 5) == 0) {
    start += 5;
  }

  // Copy until end, semicolon, or closing
  // bracket
  size_t i = 0;
  while (*start && *start != ';' && *start != '>' && i < out_size - 1) {
    out[i++] = *start++;
  }
  out[i] = '\0';
}

static void show_active_call_screen(call_data_t *data, const char *number,
                                    bool incoming) {
  lv_obj_add_flag(data->dialer_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(data->active_call_screen, LV_OBJ_FLAG_HIDDEN);

  lv_group_t *g = lv_group_get_default();
  if (g) {
    lv_group_remove_all_objs(g);
    lv_group_add_obj(g, data->active_call_screen);
    lv_group_focus_obj(data->active_call_screen);
  }

  data->is_muted = false;
  data->is_speaker = false;
  data->is_hold = false;

  // Update Main Area Background: Opaque for Incoming (to cover video),
  // Transparent for Active
  if (data->call_main_area) {
    if (incoming) {
      lv_obj_set_style_bg_opa(data->call_main_area, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(data->call_main_area, lv_color_black(), 0);
    } else {
      lv_obj_set_style_bg_opa(data->call_main_area, LV_OPA_TRANSP, 0);
    }
  }

  if (data->call_number_label) {
    char formatted_number[256];
    format_sip_uri(number, formatted_number, sizeof(formatted_number));
    lv_label_set_text(data->call_number_label, formatted_number);
  }
  if (data->call_name_label)
    lv_label_set_text(data->call_name_label, "Unknown");

  if (data->call_status_label) {
    lv_label_set_text(data->call_status_label,
                      incoming ? "Incoming Call" : "Calling...");
    lv_obj_set_style_text_color(
        data->call_status_label,
        incoming ? lv_color_hex(0xFF0000) : lv_color_hex(0x00AA00), 0);
  }

  if (data->call_duration_label)
    lv_label_set_text(data->call_duration_label, "00:00");

  if (data->mute_btn)
    lv_obj_clear_state(data->mute_btn, LV_STATE_CHECKED);
  if (data->speaker_btn)
    lv_obj_clear_state(data->speaker_btn, LV_STATE_CHECKED);
  if (data->hold_btn)
    lv_obj_clear_state(data->hold_btn, LV_STATE_CHECKED);

  // Button visibility logic
  if (incoming) {
    // Hide in-call controls
    if (data->mute_btn)
      lv_obj_add_flag(data->mute_btn, LV_OBJ_FLAG_HIDDEN);
    if (data->speaker_btn)
      lv_obj_add_flag(data->speaker_btn, LV_OBJ_FLAG_HIDDEN);
    if (data->hold_btn)
      lv_obj_add_flag(data->hold_btn, LV_OBJ_FLAG_HIDDEN);

    // Show Answer button
    if (data->answer_btn)
      lv_obj_clear_flag(data->answer_btn, LV_OBJ_FLAG_HIDDEN);

  } else {
    // Show in-call controls
    if (data->mute_btn)
      lv_obj_clear_flag(data->mute_btn, LV_OBJ_FLAG_HIDDEN);
    if (data->speaker_btn)
      lv_obj_clear_flag(data->speaker_btn, LV_OBJ_FLAG_HIDDEN);
    if (data->hold_btn)
      lv_obj_clear_flag(data->hold_btn, LV_OBJ_FLAG_HIDDEN);

    // Hide Answer button
    if (data->answer_btn)
      lv_obj_add_flag(data->answer_btn, LV_OBJ_FLAG_HIDDEN);
  }

  // Video Visibility Logic
  bool show_video = data->pending_video;
  if (data->video_cont) {
    if (show_video) {
      lv_obj_clear_flag(data->video_cont, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(data->video_cont, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (data->video_local) {
    if (show_video) {
      lv_obj_clear_flag(data->video_local, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(data->video_local, LV_OBJ_FLAG_HIDDEN);
    }
  }

  data->call_start_time = lv_tick_get();
}

static void number_btn_clicked(lv_event_t *e) {
  applet_t *applet = applet_manager_get_current();
  call_data_t *data = (call_data_t *)applet->user_data;
  const char *digit = lv_event_get_user_data(e);

  if (data->active_call_screen &&
      !lv_obj_has_flag(data->active_call_screen, LV_OBJ_FLAG_HIDDEN)) {
    log_debug("CallApplet", "DTMF: %s", digit);
    return;
  }

  size_t len = strlen(data->number_buffer);
  if (len < sizeof(data->number_buffer) - 1) {
    data->number_buffer[len] = digit[0];
    data->number_buffer[len + 1] = '\0';
    lv_label_set_text(data->number_label, data->number_buffer);
  }
}

static void backspace_btn_clicked(lv_event_t *e) {
  (void)e;
  applet_t *applet = applet_manager_get_current();
  call_data_t *data = (call_data_t *)applet->user_data;

  size_t len = strlen(data->number_buffer);
  if (len > 0) {
    data->number_buffer[len - 1] = '\0';
    lv_label_set_text(data->number_label, data->number_buffer[0]
                                              ? data->number_buffer
                                              : "Enter number");
  }
}

static void call_btn_clicked(lv_event_t *e) {
  (void)e;
  applet_t *applet = applet_manager_get_current();
  call_data_t *data = (call_data_t *)applet->user_data;

  if (strlen(data->number_buffer) > 0) {
    app_config_t config;
    config_load_app_settings(&config);

    int acc_idx = config.default_account_index;

    // Check if "None" (-1) or invalid
    if (acc_idx < 0) {
      // Show Picker
      strncpy(data->pending_number, data->number_buffer,
              sizeof(data->pending_number) - 1);
      data->pending_video = false;
      show_account_picker(data);
      return;
    }

    // Check range
    voip_account_t accounts[MAX_ACCOUNTS];
    int count = config_load_accounts(accounts, MAX_ACCOUNTS);
    if (acc_idx >= count) {
      // Fallback to Picker
      strncpy(data->pending_number, data->number_buffer,
              sizeof(data->pending_number) - 1);
      data->pending_video = false;
      show_account_picker(data);
      return;
    }

    int ret = -1;
    voip_account_t *acc = &accounts[acc_idx];
    char aor[256];
    snprintf(aor, sizeof(aor), "sip:%s@%s", acc->username, acc->server);
    log_info("CallApplet", "Calling with default account: %s", aor);
    ret = baresip_manager_call_with_account(data->number_buffer, aor);

    if (ret == 0) {
      // Hide Video if previously shown
      if (data->video_cont)
        lv_obj_add_flag(data->video_cont, LV_OBJ_FLAG_HIDDEN);

      show_active_call_screen(data, data->number_buffer, false);

      // Clear dialer number
      data->number_buffer[0] = '\0';
      if (data->number_label)
        lv_label_set_text(data->number_label, "Enter number");
    } else {
      applet_manager_show_toast("Account not available.");
    }
  }
}

static void video_call_btn_clicked(lv_event_t *e) {
  (void)e;
  applet_t *applet = applet_manager_get_current();
  call_data_t *data = (call_data_t *)applet->user_data;

  if (strlen(data->number_buffer) > 0) {
    app_config_t config;
    config_load_app_settings(&config);

    int acc_idx = config.default_account_index;

    // Check if "None" (-1) or invalid
    if (acc_idx < 0) {
      log_info("CallApplet", "No default account, showing picker for VIDEO");
      strncpy(data->pending_number, data->number_buffer,
              sizeof(data->pending_number) - 1);
      data->pending_video = true;
      show_account_picker(data);
      return;
    }

    // Check range
    voip_account_t accounts[MAX_ACCOUNTS];
    int count = config_load_accounts(accounts, MAX_ACCOUNTS);
    if (acc_idx >= count) {
      log_info("CallApplet",
               "Video calling with implicit default account (fallback)");
      baresip_manager_videocall_with_account(data->number_buffer, NULL);
      show_active_call_screen(data, data->number_buffer, false);
      data->number_buffer[0] = '\0';
      if (data->number_label)
        lv_label_set_text(data->number_label, "Enter number");
      return;
    }

    // Valid Default Account
    int ret = -1;
    voip_account_t *acc = &accounts[acc_idx];
    char aor[256];
    snprintf(aor, sizeof(aor), "sip:%s@%s", acc->username, acc->server);
    log_info("CallApplet",
             "Video calling with default "
             "account: %s",
             aor);
    ret = baresip_manager_videocall_with_account(data->number_buffer, aor);

    if (ret == 0) {
      // Set Video Visible
      if (data->video_cont)
        lv_obj_clear_flag(data->video_cont, LV_OBJ_FLAG_HIDDEN);
      // Ensure overlays are correct (will
      // be handled in
      // show_active_call_screen updates)

      show_active_call_screen(data, data->number_buffer, false);

      // Clear dialer number
      data->number_buffer[0] = '\0';
      if (data->number_label)
        lv_label_set_text(data->number_label, "Enter number");
    } else {
      applet_manager_show_toast("Account not available.");
    }
  }
}

// Account Picker Implementation
static void account_picker_event_cb(lv_event_t *e) {
  call_data_t *data = (call_data_t *)lv_event_get_user_data(e);
  lv_obj_t *obj = lv_event_get_target(e);

  // Get Account Index from object user data
  // (stored as set_user_data per item) But
  // wait, LVGL user data on object? We can
  // convert pointer to display_d Simpler:
  // Store index in event user_data? No, we
  // need data pointer. Store index in
  // object's user_data (void* options).

  // Actually, let's use the list button's
  // user data. We need to implement the
  // picker population carefully.

  intptr_t acc_idx = (intptr_t)lv_obj_get_user_data(obj); // 0 to N-1

  // Close Modal
  if (data->account_picker_modal) {
    lv_obj_del(data->account_picker_modal);
    data->account_picker_modal = NULL;
  }

  log_info("CallApplet", "Account picked: %d", (int)acc_idx);

  voip_account_t accounts[MAX_ACCOUNTS];
  config_load_accounts(accounts, MAX_ACCOUNTS);

  int ret = -1;
  voip_account_t *acc = &accounts[acc_idx];
  char aor[256];
  snprintf(aor, sizeof(aor), "sip:%s@%s", acc->username, acc->server);

  if (data->pending_video) {
    ret = baresip_manager_videocall_with_account(data->pending_number, aor);
  } else {
    ret = baresip_manager_call_with_account(data->pending_number, aor);
  }

  if (ret == 0) {
    if (data->pending_video && data->video_cont) {
      lv_obj_clear_flag(data->video_cont, LV_OBJ_FLAG_HIDDEN);
    } else if (!data->pending_video && data->video_cont) {
      lv_obj_add_flag(data->video_cont, LV_OBJ_FLAG_HIDDEN);
    }

    show_active_call_screen(data, data->pending_number, false);
    data->number_buffer[0] = '\0';
    if (data->number_label)
      lv_label_set_text(data->number_label, "Enter number");
  } else {
    log_warn("CallApplet", "Failed to initiate call from "
                           "picker");
    applet_manager_show_toast("Account not available.");
  }
}

static void close_picker(lv_event_t *e);

static void show_account_picker(call_data_t *data) {
  if (data->account_picker_modal)
    return;

  data->account_picker_modal = lv_obj_create(lv_scr_act()); // Full screen
                                                            // modal
  lv_obj_set_size(data->account_picker_modal, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(data->account_picker_modal, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(data->account_picker_modal, LV_OPA_50, 0);
  lv_obj_set_flex_flow(data->account_picker_modal, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(data->account_picker_modal, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *panel = lv_obj_create(data->account_picker_modal);
  lv_obj_set_size(panel, 300, 400);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(panel, 10, 0);

  lv_obj_t *lbl = lv_label_create(panel);
  lv_label_set_text(lbl, "Select Account");
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);

  // List
  lv_obj_t *list = lv_list_create(panel);
  lv_obj_set_width(list, LV_PCT(100));
  lv_obj_set_flex_grow(list, 1);

  voip_account_t accounts[MAX_ACCOUNTS];
  int count = config_load_accounts(accounts, MAX_ACCOUNTS);

  for (int i = 0; i < count; i++) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s (%s)", accounts[i].display_name,
             accounts[i].server);
    lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_DIRECTORY, buf);
    lv_obj_set_user_data(btn, (void *)(intptr_t)i);
    lv_obj_add_event_cb(btn, account_picker_event_cb, LV_EVENT_CLICKED, data);
  }

  // Cancel
  lv_obj_t *cancel_btn = lv_btn_create(panel);
  lv_obj_set_size(cancel_btn, LV_PCT(100), 40);
  lv_label_set_text(lv_label_create(cancel_btn), "Cancel");
  lv_obj_add_event_cb(cancel_btn,
                      // Lambda-like inline? No, simple
                      // close helper or use user_data I'll
                      // create a simple modal close
                      close_picker, LV_EVENT_CLICKED, data);
  // Quick hack: separate close handler or
  // reuse with index -1? Let's rely on
  // click-outside? Or clean handler. I'll
  // define static close_picker
}
static void close_picker(lv_event_t *e) {
  call_data_t *d = (call_data_t *)lv_event_get_user_data(e);
  if (d && d->account_picker_modal) {
    lv_obj_del(d->account_picker_modal);
    d->account_picker_modal = NULL;
  }
}

static void answer_btn_clicked(lv_event_t *e) {
  (void)e;
  log_info("CallApplet", "Answer clicked");
  baresip_manager_answer_call(false);
  baresip_manager_answer_call(false);
}

static void video_answer_btn_clicked(lv_event_t *e) {
  (void)e;
  log_info("CallApplet", "Video Answer clicked");
  baresip_manager_answer_call(true);
}

static void hangup_btn_clicked(lv_event_t *e) {
  (void)e;
  applet_t *applet = applet_manager_get_current();
  call_data_t *data = (call_data_t *)applet->user_data;

  // Prevent accidental hangup causing
  // immediate CANCEL due to UI
  // overlap/bounce
  if (lv_tick_get() - data->call_start_time < 1000) {
    log_warn("CallApplet", "Ignoring hangup click (debounce "
                           "protection, <1000ms)");
    return;
  }

  log_info("CallApplet", "Hangup clicked");
  baresip_manager_hangup();
  // Do NOT force show_dialer_screen here.
  // on_call_state_change will handle
  // navigation (Back if 0 calls,
  // Stay/Switch if >0 calls).
}

static void mute_btn_clicked(lv_event_t *e) {
  (void)e;
  applet_t *applet = applet_manager_get_current();
  call_data_t *data = (call_data_t *)applet->user_data;
  data->is_muted = !data->is_muted;
  if (data->is_muted)
    lv_obj_add_state(data->mute_btn, LV_STATE_CHECKED);
  else
    lv_obj_clear_state(data->mute_btn, LV_STATE_CHECKED);

  baresip_manager_mute(data->is_muted);
  log_info("CallApplet", "Mute: %d", data->is_muted);
}

static void speaker_btn_clicked(lv_event_t *e) {
  (void)e;
  applet_t *applet = applet_manager_get_current();
  call_data_t *data = (call_data_t *)applet->user_data;
  data->is_speaker = !data->is_speaker;
  if (data->is_speaker)
    lv_obj_add_state(data->speaker_btn, LV_STATE_CHECKED);
  else
    lv_obj_clear_state(data->speaker_btn, LV_STATE_CHECKED);

  // TODO: backend speaker toggle if
  // available
  log_info("CallApplet", "Speaker: %d", data->is_speaker);
}

static void hold_btn_clicked(lv_event_t *e) {
  (void)e;
  applet_t *applet = applet_manager_get_current();
  call_data_t *data = (call_data_t *)applet->user_data;
  data->is_hold = !data->is_hold;

  // Call backend using current context
  // (implicit in baresip_manager) But we
  // need to pass the ID? actually
  // baresip_manager_switch_to sets
  // current_call. We can pass NULL to use
  // current, or we should fetch current
  // call ID from data/state.
  // baresip_manager_hold_call takes void*
  // call_id. If we pass NULL (or if I
  // update baresip_manager_hold_call to
  // handle NULL as current), it works. The
  // header says `int
  // baresip_manager_hold_call(void
  // *call_id);` Let's use
  // `baresip_manager_hold_call(NULL)`
  // assuming implementation handles it, OR
  // better: use the `switch_to` logic which
  // sets `g_call_state.current_call`. Wait,
  // I should pass the `current_call`? Call
  // applet doesn't have the pointer easily
  // unless we iterate. Actually, I'll
  // update baresip_manager_hold_call in
  // next step to default to current if
  // NULL. For now let's assume
  // `baresip_manager_hold_call` might fail
  // with NULL. BUT:
  // `baresip_manager_hangup` uses current
  // if nothing passed. `hold_call`
  // implementation (I saw earlier):
  /*
  int baresip_manager_hold_call(void
  *call_id) { struct call *call = (struct
  call *)call_id; if (!call) return -1;
  */
  // It FAILS if NULL.
  // So I must find the opaque pointer.
  // `update_call_list` has the pointers
  // (`calls[i].id`). But inside
  // `hold_btn_clicked` I don't know which
  // one is current easily without iterating
  // `g_call_state`. Wait, I can expose a
  // getter
  // `baresip_manager_get_current_call()`?
  // Or I can change
  // `baresip_manager_hold_call` to use
  // current if NULL. I will change
  // `baresip_manager_hold_call` to use
  // Current Call if NULL is passed.

  if (data->is_hold) {
    lv_obj_add_state(data->hold_btn, LV_STATE_CHECKED);
    lv_label_set_text(data->call_status_label, "On Hold");
    baresip_manager_hold_call(NULL); // Will update backend to
                                     // handle NULL
  } else {
    lv_obj_clear_state(data->hold_btn, LV_STATE_CHECKED);
    lv_label_set_text(data->call_status_label, "Connected");
    baresip_manager_resume_call(NULL); // Will update backend to
                                       // handle NULL
  }
  log_info("CallApplet", "Hold: %d", data->is_hold);
}

static void update_call_duration(lv_timer_t *timer) {
  call_data_t *data = (call_data_t *)timer->user_data;
  if (!data || !data->active_call_screen ||
      lv_obj_has_flag(data->active_call_screen, LV_OBJ_FLAG_HIDDEN))
    return;

  // Poll active calls to detect silent
  // termination
  call_info_t calls[4];
  int raw_count = baresip_manager_get_active_calls(calls, 4);
  int valid_count = 0;
  for (int i = 0; i < raw_count; i++) {
    if (calls[i].state != CALL_STATE_TERMINATED &&
        calls[i].state != CALL_STATE_IDLE &&
        calls[i].state != CALL_STATE_UNKNOWN &&
        calls[i].state < CALL_STATE_TERMINATED) {
      valid_count++;
    }
  }

  if (valid_count == 0) {
    log_warn("CallApplet", "Watchdog: No valid calls found in "
                           "timer! Forcing exit.");
    if (applet_manager_back() != 0) {
      log_warn("CallApplet", "Watchdog: Back navigation "
                             "failed. Showing Dialer.");
      show_dialer_screen(data);
      update_account_dropdowns(data);
    }
    return;
  }

  // Ensure list is refreshed periodically
  // (handles silent zombie cleanup)
  update_call_list(data, NULL);

  uint32_t elapsed = (lv_tick_get() - data->call_start_time) / 1000;
  uint32_t min = elapsed / 60;
  uint32_t sec = elapsed % 60;
  char time_str[16];
  snprintf(time_str, sizeof(time_str), "%02u:%02u", min, sec);
  lv_label_set_text(data->call_duration_label, time_str);
}

static void update_account_dropdowns(call_data_t *data) {
  char options[1024] = "";
  for (int i = 0; i < data->account_count; i++) {
    if (i > 0)
      strcat(options, "\n");
    char entry[256];
    snprintf(entry, sizeof(entry), "%s (%s)", data->accounts[i].display_name,
             data->accounts[i].server);
    strcat(options, entry);
  }
  if (strlen(options) == 0)
    strcpy(options, "No Accounts");
  if (data->dialer_account_dropdown) {
    lv_dropdown_set_options(data->dialer_account_dropdown, options);
    if (data->account_count > 0 &&
        data->config.default_account_index < data->account_count) {
      lv_dropdown_set_selected(data->dialer_account_dropdown,
                               data->config.default_account_index);
    }
  }
}

// Helper to draw an incoming call card
static void draw_incoming_card(lv_obj_t *parent, const call_info_t *call) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_width(card, LV_PCT(90));
  lv_obj_set_height(card, 140); // Fixed height for card
  lv_obj_set_style_bg_color(card, lv_color_hex(0x2C3E50),
                            0); // Dark Blue-Grey
  lv_obj_set_style_radius(card, 20, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  // Avatar (Left)
  lv_obj_t *avatar = lv_obj_create(card);
  lv_obj_set_size(avatar, 60, 60);
  lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(avatar, lv_palette_main(LV_PALETTE_BLUE), 0);
  lv_obj_align(avatar, LV_ALIGN_TOP_LEFT, 10, 10);

  lv_obj_t *avatar_icon = lv_label_create(avatar);
  lv_label_set_text(avatar_icon, LV_SYMBOL_EYE_OPEN);
  lv_obj_set_style_text_font(avatar_icon, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(avatar_icon, lv_color_white(), 0);
  lv_obj_center(avatar_icon);

  // Name (Right of Avatar)
  lv_obj_t *name = lv_label_create(card);
  lv_label_set_text(name, call->peer_uri);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(name, lv_color_white(), 0);
  lv_obj_align(name, LV_ALIGN_TOP_LEFT, 80, 20);
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  lv_obj_set_width(name, 200);

  // Status (Below Name)
  lv_obj_t *status = lv_label_create(card);
  lv_label_set_text(status, LV_SYMBOL_BELL " Incoming...");
  lv_obj_set_style_text_color(status, lv_color_white(), 0);
  lv_obj_align(status, LV_ALIGN_TOP_LEFT, 80, 50);

  // Buttons Row (Bottom)
  lv_obj_t *btn_row = lv_obj_create(card);
  lv_obj_set_size(btn_row, LV_PCT(100), 60);
  lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(btn_row, 0, 0);
  lv_obj_set_style_border_width(btn_row, 0, 0);
  lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_AROUND,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(btn_row, 0, 0);

  // Reject Button (Red)
  lv_obj_t *reject_btn = lv_btn_create(btn_row);
  lv_obj_set_size(reject_btn, 50, 50);
  lv_obj_set_style_radius(reject_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(reject_btn, lv_color_hex(0xFF3B30),
                            0); // Red

  lv_obj_t *reject_icon = lv_label_create(reject_btn);
  lv_label_set_text(reject_icon,
                    LV_SYMBOL_CALL);                      // Rotate?
  lv_obj_set_style_transform_angle(reject_icon, 1350, 0); // Down-left
  lv_obj_center(reject_icon);
  lv_obj_add_event_cb(reject_btn, query_reject_action, LV_EVENT_CLICKED,
                      call->id); // Use new handler

  // Answer Button (Green)
  lv_obj_t *answer_btn = lv_btn_create(btn_row);
  lv_obj_set_size(answer_btn, 50, 50);
  lv_obj_set_style_radius(answer_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(answer_btn, lv_color_hex(0x4CD964),
                            0); // Green

  lv_obj_t *answer_icon = lv_label_create(answer_btn);
  lv_label_set_text(answer_icon, LV_SYMBOL_CALL);
  lv_obj_center(answer_icon);
  lv_obj_add_event_cb(answer_btn, query_answer_action, LV_EVENT_CLICKED,
                      call->id); // Use new handler

  // Placeholder buttons for Video/Message
  lv_obj_t *video_btn = lv_btn_create(btn_row);
  lv_obj_set_size(video_btn, 50, 50);
  lv_obj_set_style_radius(video_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(video_btn, lv_color_hex(0x808080),
                            0); // Grey
  lv_obj_t *vid_lbl = lv_label_create(video_btn);
  lv_label_set_text(vid_lbl, LV_SYMBOL_VIDEO);
  lv_obj_center(vid_lbl);
}

static void call_list_item_clicked(lv_event_t *e) {
  void *call_id = lv_event_get_user_data(e);
  if (call_id) {
    baresip_manager_switch_to(call_id);
    update_call_list(g_call_data, NULL);
  }
}

// Action Handlers for Cards
static void query_answer_action(lv_event_t *e) {
  void *call_id = lv_event_get_user_data(e);
  if (call_id) {
    log_info("CallApplet", "Card Answer: Switching to %p", call_id);
    baresip_manager_switch_to(call_id);
    baresip_manager_answer_call(false);
  }
}

static void query_reject_action(lv_event_t *e) {
  void *call_id = lv_event_get_user_data(e);
  if (call_id) {
    log_info("CallApplet", "Card Reject: Switching to %p", call_id);
    baresip_manager_switch_to(call_id);
    baresip_manager_hangup();
  }
}

// Wrapper for existing single-button (might
// be unused now if we card-ify everything,
// but keep for safety)
// ...

static void on_card_answer_video(lv_event_t *e) {
  void *call_id = lv_event_get_user_data(e);
  if (g_call_data)
    g_call_data->pending_video = true;
  if (call_id) {
    baresip_manager_switch_to(call_id);
    baresip_manager_answer_call(true);
  }
}

static void on_card_answer_audio(lv_event_t *e) {
  void *call_id = lv_event_get_user_data(e);
  if (g_call_data)
    g_call_data->pending_video = false;
  if (call_id) {
    baresip_manager_switch_to(call_id);
    baresip_manager_answer_call(false);
  }
}

static void on_card_reject(lv_event_t *e) {
  void *call_id = lv_event_get_user_data(e);
  if (call_id)
    baresip_manager_reject_call(call_id);
}

static void on_card_forward(lv_event_t *e) {
  void *call_id = lv_event_get_user_data(e);
  log_info("CallApplet", "Forward clicked for %p (Not Implemented)", call_id);
}

static void update_call_list(call_data_t *data, void *ignore_id) {
  if (!data || !data->active_call_screen)
    return;

  call_info_t calls[8];
  int raw_count = baresip_manager_get_active_calls(calls, 8);

  // Filter out ignored call and calls not matching the current View Mode
  int count = 0;
  call_info_t valid_calls[8];

  bool view_incoming = (data->current_state == CALL_STATE_INCOMING);

  for (int i = 0; i < raw_count; i++) {
    if (ignore_id && calls[i].id == ignore_id)
      continue;

    // Global filter: terminated/idle
    if (calls[i].state == CALL_STATE_TERMINATED ||
        calls[i].state == CALL_STATE_IDLE ||
        calls[i].state == CALL_STATE_UNKNOWN ||
        calls[i].state >= CALL_STATE_TERMINATED)
      continue;

    // View Mode Filter
    // In Incoming Mode: Show ONLY Incoming calls
    // In Active Mode: Show ONLY Non-Incoming calls (Established, Outgoing,
    // Held, etc)
    bool is_incoming_call = (calls[i].state == CALL_STATE_INCOMING);
    if (view_incoming && !is_incoming_call)
      continue;
    if (!view_incoming && is_incoming_call)
      continue;

    valid_calls[count++] = calls[i];
  }

  // Copy back for simplicity
  for (int i = 0; i < count; i++)
    calls[i] = valid_calls[i];

  // Find current call (the one sending
  // events or focused)

  // baresip_manager_get_active_calls
  // returns calls with 'is_current' flag.
  // We should rely on that or the first
  // call if none is current.
  int current_idx = -1;
  for (int i = 0; i < count; i++) {
    if (calls[i].is_current) {
      current_idx = i;
      break;
    }
  }
  // If no call is marked current but we
  // have calls, default to first?
  if (current_idx == -1 && count > 0) {
    current_idx = 0;
    // Critical: If we defaulted to a new call because the previous current
    // was filtered out (view switch), we MUST sync the backend focus
    // otherwise buttons (Answer/Hangup) will act on the hidden call.
    log_info("CallApplet", "View switch detected, syncing focus to %p",
             calls[current_idx].id);
    baresip_manager_switch_to(calls[current_idx].id);
    // Update local state to match
    data->current_state = calls[current_idx].state;
    strncpy(data->current_peer_uri, calls[current_idx].peer_uri,
            sizeof(data->current_peer_uri) - 1);
  }

  // 1. Update Main Area (Current
  // Active/Incoming Call)
  if (current_idx >= 0) {
    call_info_t *current = &calls[current_idx];
    bool is_incoming = (current->state == CALL_STATE_INCOMING);

    // Update labels
    // Prepare formatted URI
    char fmt[256];
    format_sip_uri(current->peer_uri, fmt, sizeof(fmt));

    if (data->call_number_label) {
      lv_label_set_text(data->call_number_label, fmt);
    }
    if (data->call_name_label)
      lv_label_set_text(data->call_name_label,
                        fmt); // Display formatted URI as name/info

    if (data->call_status_label) {
      const char *status_text = "Unknown";
      lv_color_t color = lv_color_hex(0xFFFFFF);
      switch (current->state) {
      case CALL_STATE_INCOMING:
        status_text = "Incoming Call";
        color = lv_color_hex(0xFF0000);
        break;
      case CALL_STATE_ESTABLISHED:
        if (current->is_held) {
          status_text = "On Hold";
          color = lv_color_hex(0xFF8800); // Orange for Hold
        } else {
          status_text = "Connected";
          color = lv_color_hex(0x00AA00);
        }
        break;
      case CALL_STATE_OUTGOING:
        status_text = "Calling...";
        color = lv_color_hex(0xAAAA00);
        break;
      case CALL_STATE_RINGING:
        status_text = "Ringing...";
        color = lv_color_hex(0xAAAA00);
        break;
      case CALL_STATE_EARLY:
        status_text = "Connecting...";
        color = lv_color_hex(0xAAAA00);
        break;
      default:
        status_text = "Ended";
        break;
      }
      lv_label_set_text(data->call_status_label, status_text);
      lv_obj_set_style_text_color(data->call_status_label, color, 0);
    }

    // Update Buttons Visibility
    if (is_incoming) {
      if (data->mute_btn)
        lv_obj_add_flag(data->mute_btn, LV_OBJ_FLAG_HIDDEN);
      if (data->speaker_btn)
        lv_obj_add_flag(data->speaker_btn, LV_OBJ_FLAG_HIDDEN);
      if (data->hold_btn)
        lv_obj_add_flag(data->hold_btn, LV_OBJ_FLAG_HIDDEN);
      if (data->answer_btn)
        lv_obj_clear_flag(data->answer_btn, LV_OBJ_FLAG_HIDDEN);
      if (data->video_answer_btn)
        lv_obj_clear_flag(data->video_answer_btn, LV_OBJ_FLAG_HIDDEN);
      // Ensure hangup/reject is visible
      // (hangup_btn usually is)
    } else {
      if (data->mute_btn)
        lv_obj_clear_flag(data->mute_btn, LV_OBJ_FLAG_HIDDEN);
      if (data->speaker_btn)
        lv_obj_clear_flag(data->speaker_btn, LV_OBJ_FLAG_HIDDEN);
      if (data->hold_btn)
        lv_obj_clear_flag(data->hold_btn, LV_OBJ_FLAG_HIDDEN);
      if (data->answer_btn)
        lv_obj_add_flag(data->answer_btn, LV_OBJ_FLAG_HIDDEN);
      if (data->video_answer_btn)
        lv_obj_add_flag(data->video_answer_btn, LV_OBJ_FLAG_HIDDEN);

      // Update Hold State
      if (data->hold_btn) {
        if (current->is_held)
          lv_obj_add_state(data->hold_btn, LV_STATE_CHECKED);
        else
          lv_obj_clear_state(data->hold_btn, LV_STATE_CHECKED);
      }
    }
  }

  // Show/Hide Video Container based on state
  if (current_idx >= 0 && calls[current_idx].state == CALL_STATE_ESTABLISHED) {
    if (data->video_cont)
      lv_obj_clear_flag(data->video_cont, LV_OBJ_FLAG_HIDDEN);
    if (data->video_remote)
      lv_obj_clear_flag(data->video_remote, LV_OBJ_FLAG_HIDDEN);
  } else {
    // Optional: Hide if not established?
    // Keep hidden by default so ok.
  }

  // 2. Update Side List (Other Calls)
  if (!data->call_list_cont)
    return;
  lv_obj_clean(data->call_list_cont);

  // Show call list only if more than 1 call (as requested)
  if (count > 1) {
    lv_obj_clear_flag(data->call_list_cont, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(data->call_list_cont, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_group_t *g = lv_group_get_default();

  for (int i = 0; i < count; i++) {
    // Container for the card
    lv_obj_t *card = lv_obj_create(data->call_list_cont);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_style_pad_all(card, 5, 0);
    lv_obj_set_style_radius(card, 15, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    if (g)
      lv_group_add_obj(g, card);

    if (view_incoming) {
      // === INCOMING CALL CARD STYLE ===
      lv_obj_set_height(card, 130); // Taller for buttons
      lv_obj_set_style_bg_color(card, lv_color_hex(0x333333), 0);

      // Header: Icon + Name
      lv_obj_t *header_row = lv_obj_create(card);
      lv_obj_set_size(header_row, LV_PCT(100), 40);
      lv_obj_set_style_bg_opa(header_row, 0, 0);
      lv_obj_set_style_border_width(header_row, 0, 0);
      lv_obj_set_style_pad_all(header_row, 0, 0);
      lv_obj_set_flex_flow(header_row, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(header_row, LV_FLEX_ALIGN_START,
                            LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

      lv_obj_t *icon_bg = lv_obj_create(header_row);
      lv_obj_set_size(icon_bg, 30, 30);
      lv_obj_set_style_radius(icon_bg, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(icon_bg, lv_palette_main(LV_PALETTE_BLUE), 0);
      lv_obj_clear_flag(icon_bg, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_t *icon = lv_label_create(icon_bg);
      lv_label_set_text(icon, LV_SYMBOL_EYE_OPEN);
      lv_obj_center(icon);

      lv_obj_t *name = lv_label_create(header_row);
      char fmt[256];
      format_sip_uri(calls[i].peer_uri, fmt, sizeof(fmt));
      lv_label_set_text(name, fmt);
      lv_obj_set_style_text_color(name, lv_color_white(), 0);
      lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
      lv_obj_set_width(name, 120);

      // Action Buttons Row
      lv_obj_t *btn_row = lv_obj_create(card);
      lv_obj_set_size(btn_row, LV_PCT(100), 70);
      lv_obj_set_style_bg_opa(btn_row, 0, 0);
      lv_obj_set_style_border_width(btn_row, 0, 0);
      lv_obj_set_style_pad_all(btn_row, 0, 0);
      lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                            LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

      // 1. Forward (Stub)
      lv_obj_t *btn_fwd = lv_btn_create(btn_row);
      lv_obj_set_size(btn_fwd, 40, 40);
      lv_obj_set_style_radius(btn_fwd, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(btn_fwd, lv_color_hex(0x555555), 0);
      lv_obj_t *lbl_fwd = lv_label_create(btn_fwd);
      lv_label_set_text(lbl_fwd,
                        LV_SYMBOL_SHUFFLE); // Shuffle as Forward? Or Next
      lv_obj_center(lbl_fwd);
      lv_obj_add_event_cb(btn_fwd, on_card_forward, LV_EVENT_CLICKED,
                          calls[i].id);
      if (g)
        lv_group_add_obj(g, btn_fwd);

      // 2. Video
      lv_obj_t *btn_vid = lv_btn_create(btn_row);
      lv_obj_set_size(btn_vid, 40, 40);
      lv_obj_set_style_radius(btn_vid, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(btn_vid, lv_color_hex(0x555555), 0);
      lv_obj_t *lbl_vid = lv_label_create(btn_vid);
      lv_label_set_text(lbl_vid, LV_SYMBOL_VIDEO);
      lv_obj_center(lbl_vid);
      lv_obj_add_event_cb(btn_vid, on_card_answer_video, LV_EVENT_CLICKED,
                          calls[i].id);
      if (g)
        lv_group_add_obj(g, btn_vid);

      // 3. Answer (Green)
      lv_obj_t *btn_ans = lv_btn_create(btn_row);
      lv_obj_set_size(btn_ans, 40, 40);
      lv_obj_set_style_radius(btn_ans, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(btn_ans, lv_color_hex(0x00AA00), 0);
      lv_obj_t *lbl_ans = lv_label_create(btn_ans);
      lv_label_set_text(lbl_ans, LV_SYMBOL_CALL);
      lv_obj_center(lbl_ans);
      lv_obj_add_event_cb(btn_ans, on_card_answer_audio, LV_EVENT_CLICKED,
                          calls[i].id);
      if (g)
        lv_group_add_obj(g, btn_ans);

      // 4. Reject (Red)
      lv_obj_t *btn_rej = lv_btn_create(btn_row);
      lv_obj_set_size(btn_rej, 40, 40);
      lv_obj_set_style_radius(btn_rej, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(btn_rej, lv_color_hex(0xFF0000), 0);
      lv_obj_t *lbl_rej = lv_label_create(btn_rej);
      lv_label_set_text(lbl_rej, LV_SYMBOL_CLOSE);
      lv_obj_center(lbl_rej);
      lv_obj_add_event_cb(btn_rej, on_card_reject, LV_EVENT_CLICKED,
                          calls[i].id);
      if (g)
        lv_group_add_obj(g, btn_rej);

    } else {
      // === ACTIVE CALL CARD STYLE (Simple Switch) ===
      lv_obj_set_height(card, 80);

      if (i == current_idx) {
        lv_obj_set_style_bg_color(card, lv_color_hex(0x444444), 0); // Highlight
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_border_color(card, lv_palette_main(LV_PALETTE_BLUE),
                                      0);

        // Update status labels on Main Screen logic is kept above,
        // no need to repeat here unless we want visual sync.
        // Note: Main screen update logic in previous implementation was INSIDE
        // loop. I MUST preserve it if I removed it? YES, I removed lines
        // 1476-1504 which updated the Main Screen Labels!
      } else {
        lv_obj_set_style_bg_color(card, lv_color_hex(0x222222), 0);
        lv_obj_set_style_border_width(card, 0, 0);
      }

      // Just Name + Status
      lv_obj_t *lbl_name = lv_label_create(card);
      char fmt[256];
      format_sip_uri(calls[i].peer_uri, fmt, sizeof(fmt));
      lv_label_set_text(lbl_name, fmt);
      lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 10, 10);

      lv_obj_t *lbl_status = lv_label_create(card);
      lv_label_set_text(lbl_status, calls[i].is_held ? "Hold" : "Active");
      lv_obj_set_style_text_color(lbl_status,
                                  calls[i].is_held ? lv_color_hex(0xFFAA00)
                                                   : lv_color_hex(0x00AA00),
                                  0);
      lv_obj_align(lbl_status, LV_ALIGN_BOTTOM_LEFT, 10, -10);

      // Click to switch
      lv_obj_add_event_cb(card, call_list_item_clicked, LV_EVENT_CLICKED,
                          calls[i].id);
    }
  }
}

static void update_video_geometry(lv_timer_t *t) {
  call_data_t *data = (call_data_t *)t->user_data;
  if (!data || !data->video_remote) {
    return;
  }

  // Force unhide to ensure layout calculates size
  if (lv_obj_has_flag(data->video_cont, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_clear_flag(data->video_cont, LV_OBJ_FLAG_HIDDEN);
    // We might need to wait for layout update? LVGL handles it next pass.
    // But we proceed to get coords anyway.
  }

  lv_area_t coords;
  lv_obj_get_coords(data->video_remote, &coords);

  // Convert to SDL generic rect
  int w = coords.x2 - coords.x1 + 1;
  int h = coords.y2 - coords.y1 + 1;
  static int log_div_geom = 0;
  if (log_div_geom++ % 10 == 0) {
    log_info("CallApplet", "Video Geometry: %d,%d %dx%d (Hidden: %d)",
             coords.x1, coords.y1, w, h,
             lv_obj_has_flag(data->video_cont, LV_OBJ_FLAG_HIDDEN));
  }
  // Only update if changed or periodically to be safe
  baresip_manager_set_video_rect(coords.x1, coords.y1, w, h);

  // Local Video
  if (data->video_local) {
    lv_obj_get_coords(data->video_local, &coords);
    int lw = coords.x2 - coords.x1 + 1;
    int lh = coords.y2 - coords.y1 + 1;
    static int log_div_loc = 0;
    if (log_div_loc++ % 10 == 0) {
      log_info("CallApplet", "Local Video Geometry: %d,%d %dx%d", coords.x1,
               coords.y1, lw, lh);
    }
    baresip_manager_set_local_video_rect(coords.x1, coords.y1, lw, lh);
  }
}

static void exit_timer_cb(lv_timer_t *t) {
  call_data_t *data = (call_data_t *)t->user_data;
  data->exit_timer = NULL;
  show_dialer_screen(data);
}

// --- Thread-Safe UI Update Logic ---
static void check_ui_updates(lv_timer_t *t) {
  call_data_t *data = (call_data_t *)t->user_data;
  if (!data || !data->ui_update_needed)
    return;

  data->ui_update_needed = false;
  enum call_state state = data->pending_state;
  const char *peer = data->pending_peer_uri;
  bool incoming = data->pending_incoming;

  log_info("CallApplet",
           "Processing UI Update: State=%d, Peer='%s', Incoming=%d", state,
           peer, incoming);

  // Cancel exit timer if we moved out of TERMINATED state (e.g. new call)
  if (state != CALL_STATE_TERMINATED && data->exit_timer) {
    lv_timer_del(data->exit_timer);
    data->exit_timer = NULL;
  }

  if (state == CALL_STATE_INCOMING ||
      (incoming && state != CALL_STATE_ESTABLISHED &&
       state != CALL_STATE_TERMINATED)) {
    if (data->call_status_label)
      lv_label_set_text(data->call_status_label, "Incoming Call...");
    show_active_call_screen(data, peer, true);
  } else if (state == CALL_STATE_ESTABLISHED) {
    if (data->call_status_label) {
      lv_label_set_text(data->call_status_label, "Connected");
      lv_obj_set_style_text_color(data->call_status_label,
                                  lv_color_hex(0x00FF00), 0);
    }

    // Ensure Status Timer
    if (!data->status_timer) {
      data->status_timer = lv_timer_create(update_call_duration, 1000, data);
    }
    // Ensure Video Timer (Geometry)
    if (!data->video_timer) {
      data->video_timer = lv_timer_create(update_video_geometry, 100, data);
    }

    show_active_call_screen(data, peer, false);

    // Update buttons based on state
    if (data->mute_btn && data->speaker_btn) {
      // (Optional) Update button states if needed
    }

  } else if (state == CALL_STATE_TERMINATED) {
    if (data->call_status_label)
      lv_label_set_text(data->call_status_label, "Ended");

    // Stop timers
    if (data->status_timer) {
      lv_timer_del(data->status_timer);
      data->status_timer = NULL;
    }
    if (data->video_timer) {
      lv_timer_del(data->video_timer);
      data->video_timer = NULL;
    }

    // Reset video geometry
    baresip_manager_set_video_rect(0, 0, 0, 0);
    baresip_manager_set_local_video_rect(0, 0, 0, 0);

    // Return to dialer after delay
    if (!data->exit_timer) {
      log_info("CallApplet", "Starting exit timer (2s delay)");
      data->exit_timer = lv_timer_create(exit_timer_cb, 2000, data);
      lv_timer_set_repeat_count(data->exit_timer, 1);
    }
  } else if (state == CALL_STATE_RINGING || state == CALL_STATE_EARLY) {
    // Outgoing Ringing/Progress
    if (data->call_status_label)
      lv_label_set_text(data->call_status_label, (state == CALL_STATE_RINGING)
                                                     ? "Ringing..."
                                                     : "Connecting...");
    show_active_call_screen(data, peer, false);
  }
}

// Callback for call state changes from
// baresip_manager (Runs in Baresip Thread or arbitrary thread)
// Async callback to switch applet on main thread
static void launch_call_applet_async(void *data) {
  (void)data;
  log_info("CallApplet", "Async: Launching Call Applet");
  applet_manager_launch("Call");
}

static void on_call_state_change(enum call_state state, const char *peer_uri,
                                 void *call) {
  (void)call;
  if (!g_call_data) {
    log_error("CallApplet", "g_call_data is NULL, cannot handle state change");
    return;
  }

  log_info("CallApplet", "State Change Detected: %d (Peer: %s)", state,
           peer_uri);

  g_call_data->current_state = state;
  strncpy(g_call_data->current_peer_uri, peer_uri ? peer_uri : "Unknown",
          sizeof(g_call_data->current_peer_uri) - 1);
  g_call_data->current_peer_uri[sizeof(g_call_data->current_peer_uri) - 1] =
      '\0';

  // Queue UI Update
  g_call_data->pending_state = state;
  strncpy(g_call_data->pending_peer_uri, g_call_data->current_peer_uri,
          sizeof(g_call_data->pending_peer_uri) - 1);

  // Determine if incoming
  // Check direction if call object is available
  if (state == CALL_STATE_INCOMING) {
    g_call_data->pending_incoming = true;
  } else if (call) {
    // If not explicitly INCOMING state, check if it's an incoming call that
    // isn't established yet (e.g. Early Media or Ringing on incoming)
    bool is_outgoing = call_is_outgoing((struct call *)call);
    if (!is_outgoing && state != CALL_STATE_ESTABLISHED &&
        state != CALL_STATE_TERMINATED) {
      g_call_data->pending_incoming = true;
    } else {
      g_call_data->pending_incoming = false;
    }
  } else {
    // Fallback if no call obj (shouldn't happen for active calls)
    g_call_data->pending_incoming = false;
  }

  g_call_data->ui_update_needed = true;

  // Force applet switch if Incoming or Established
  if (state == CALL_STATE_INCOMING) {
    call_applet_request_incoming_view();
    lv_async_call(launch_call_applet_async, NULL);
  } else if (state == CALL_STATE_ESTABLISHED) {
    call_applet_request_active_view();
    lv_async_call(launch_call_applet_async, NULL);
  }
}

static int call_init(applet_t *applet) {
  log_info("CallApplet", "Initializing");

  call_data_t *data = lv_mem_alloc(sizeof(call_data_t));
  memset(data, 0, sizeof(call_data_t));
  applet->user_data = data;
  g_call_data = data; // Global reference
                      // for callbacks

  data->config.default_account_index = 0;
  data->config.preferred_codec = CODEC_OPUS;
  data->current_state = CALL_STATE_IDLE;
  data->current_state = baresip_manager_get_state();
  data->current_state = baresip_manager_get_state();
  log_debug("CallApplet", "CallInit: Fetched State=%d", data->current_state);

  const char *current_peer = baresip_manager_get_peer();
  if (current_peer) {
    strncpy(data->current_peer_uri, current_peer,
            sizeof(data->current_peer_uri) - 1);
  } else {
    strcpy(data->current_peer_uri, "Unknown");
  }

  // Update global pointer
  g_call_data = data;
  static int baresip_initialized = 0;
  if (!baresip_initialized) {
    log_info("CallApplet", "Initializing baresip manager");
    if (baresip_manager_init() != 0) {
      log_error("CallApplet", "Failed to initialize "
                              "baresip manager");
      lv_mem_free(data);
      return -1;
    }
    baresip_initialized = 1;
  }

  g_call_data = data;
  data->status_update_pending = false;
  data->status_timer = lv_timer_create(status_timer_cb, 200, data);

  data->call_timer = lv_timer_create(update_call_duration, 1000, data);
  data->video_timer = lv_timer_create(update_video_geometry, 100, data);
  baresip_manager_set_reg_callback(reg_status_callback);
  baresip_manager_set_callback(on_call_state_change);
  load_settings(data);

  log_info("CallApplet", "Auto-registering enabled "
                         "accounts...");
  for (int i = 0; i < data->account_count; i++) {
    if (data->accounts[i].enabled) {
      log_info("CallApplet", "Registering account %d: %s@%s", i,
               data->accounts[i].username, data->accounts[i].server);
      baresip_manager_add_account(&data->accounts[i]);
      data->account_status[i] = REG_STATUS_REGISTERING;
    } else {
      data->account_status[i] = REG_STATUS_NONE;
    }
  }

  data->dialer_screen = lv_obj_create(applet->screen);
  data->active_call_screen = lv_obj_create(applet->screen);
  lv_obj_set_size(data->dialer_screen, LV_PCT(100), LV_PCT(100));
  lv_obj_set_size(data->active_call_screen, LV_PCT(100), LV_PCT(100));
  lv_obj_add_flag(data->active_call_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_bg_opa(data->active_call_screen, 0,
                          0); // Transparent background

  lv_obj_add_event_cb(data->dialer_screen, call_key_handler, LV_EVENT_KEY,
                      NULL);
  lv_obj_add_event_cb(data->active_call_screen, call_key_handler, LV_EVENT_KEY,
                      NULL);

  // === ACTIVE CALL SCREEN ===
  lv_obj_t *active_call_cont = lv_obj_create(data->active_call_screen);
  lv_obj_set_size(active_call_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(active_call_cont,
                       LV_FLEX_FLOW_ROW); // CHANGED to ROW
  lv_obj_set_style_pad_all(active_call_cont, 0, 0);
  lv_obj_set_style_bg_opa(active_call_cont, LV_OPA_TRANSP,
                          0); // Transparent background
  lv_obj_set_style_border_width(active_call_cont, 0, 0); // No border
  lv_obj_clear_flag(active_call_cont, LV_OBJ_FLAG_SCROLLABLE);

  // Ensure root applet screen is transparent for video
  lv_obj_set_style_bg_opa(applet->screen, 0, 0);

  // Side List for multiple calls (Created FIRST to be on LEFT)
  data->call_list_cont = lv_obj_create(active_call_cont);
  lv_obj_set_width(data->call_list_cont, 220);
  lv_obj_set_height(data->call_list_cont, LV_PCT(100));
  lv_obj_set_style_bg_color(data->call_list_cont, lv_color_hex(0x222222), 0);
  lv_obj_set_flex_flow(data->call_list_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(data->call_list_cont, 5, 0);
  lv_obj_set_style_border_width(data->call_list_cont, 0, 0);
  lv_obj_add_flag(data->call_list_cont,
                  LV_OBJ_FLAG_HIDDEN); // Hidden by default

  // Create Main Area (Created SECOND to be on RIGHT)
  data->call_main_area = lv_obj_create(active_call_cont);
  lv_obj_set_flex_grow(data->call_main_area, 1);
  lv_obj_set_height(data->call_main_area, LV_PCT(100));
  // Make main area transparent so
  // background video shows (if we put video
  // behind main area) Or put video INSIDE
  // main area as layer 0.
  lv_obj_set_style_bg_opa(data->call_main_area, 0, 0);
  lv_obj_set_style_border_width(data->call_main_area, 0, 0);
  lv_obj_clear_flag(data->call_main_area, LV_OBJ_FLAG_SCROLLABLE);

  //  // Video Container - Reparented to ROOT SCREEN to avoid layout issues
  data->video_cont = lv_obj_create(data->active_call_screen);
  lv_obj_remove_style_all(data->video_cont);
  lv_obj_set_size(data->video_cont, 800, 480);
  lv_obj_set_pos(data->video_cont, 0, 0);
  lv_obj_move_background(data->video_cont); // Ensure it is behind UI

  // Remote Video (Full container)
  data->video_remote = lv_obj_create(data->video_cont);
  lv_obj_remove_style_all(data->video_remote);
  lv_obj_set_size(data->video_remote, 800, 480);
  lv_obj_set_pos(data->video_remote, 0, 0);
  lv_obj_add_flag(data->video_remote, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_clear_flag(data->video_remote, LV_OBJ_FLAG_SCROLLABLE);

  // Local Video (PiP)
  data->video_local =
      lv_obj_create(data->active_call_screen); // Move PiP to Top Layer as well
  lv_obj_remove_style_all(data->video_local);
  lv_obj_set_style_bg_opa(data->video_local, 0, 0);
  lv_obj_set_size(data->video_local, 160, 120);
  lv_obj_align(data->video_local, LV_ALIGN_TOP_RIGHT, -20, 20);
  lv_obj_set_style_border_width(data->video_local, 0, 0); // No Border
  lv_obj_move_foreground(
      data->video_local); // Ensure PiP is on top of everything

  // --- UI Layer (Layer 1, Overlays) ---
  lv_obj_t *ui_layer = lv_obj_create(data->call_main_area);
  lv_obj_set_size(ui_layer, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(ui_layer, 0,
                          0); // Transparent
  lv_obj_set_style_border_width(ui_layer, 0, 0);
  lv_obj_set_style_pad_all(ui_layer, 0, 0); // Remove default padding
  lv_obj_set_flex_flow(ui_layer, LV_FLEX_FLOW_COLUMN);
  lv_obj_align(ui_layer, LV_ALIGN_CENTER, 0, 0);

  // Top Info (Call Status/Name)
  lv_obj_t *top_info = lv_obj_create(ui_layer);
  lv_obj_set_size(top_info, LV_PCT(100),
                  LV_SIZE_CONTENT); // Auto height to fit content
  lv_obj_set_style_bg_opa(top_info, 0, 0);
  lv_obj_set_style_border_width(top_info, 0, 0);
  lv_obj_set_style_pad_all(top_info, 10, 0); // Add padding to avoid clipping
  lv_obj_clear_flag(top_info, LV_OBJ_FLAG_SCROLLABLE); // Fix scrollbar
  lv_obj_set_flex_flow(top_info, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(top_info, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  // ... (Labels inside top_info) ...
  data->call_name_label = lv_label_create(top_info);
  lv_label_set_text(data->call_name_label, ""); // Default empty
  lv_obj_set_style_text_font(data->call_name_label, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(data->call_name_label, lv_color_white(),
                              0); // Always white? Or dark if white
                                  // bg?
  // If video is background, white is good.
  // If no video, we usually have white bg.
  // We need dynamic text color or shadow.
  // For now, let's keep it simple. If we
  // have video, we might need text shadow.

  data->call_number_label = lv_label_create(top_info);
  lv_label_set_text(data->call_number_label,
                    "0000"); // Changed from number to
                             // "0000" to match original
                             // init
  lv_obj_set_style_text_font(data->call_number_label, &lv_font_montserrat_16,
                             0);
  lv_obj_set_style_text_color(data->call_number_label,
                              lv_palette_main(LV_PALETTE_GREY), 0);

  data->call_duration_label = lv_label_create(top_info);
  lv_label_set_text(data->call_duration_label, "00:00");

  data->call_status_label = lv_label_create(top_info);
  lv_label_set_text(data->call_status_label, "Dialing...");
  lv_obj_set_style_text_color(data->call_status_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(data->call_status_label, &lv_font_montserrat_16,
                             0);

  lv_obj_set_style_text_font(data->call_status_label, &lv_font_montserrat_16,
                             0);

  // Video container and children removed from here (pushed to background layer)

  // Action Grid
  lv_obj_t *action_grid = lv_obj_create(ui_layer);
  lv_obj_set_size(action_grid, LV_PCT(90), 120);
  lv_obj_set_flex_flow(action_grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(action_grid, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(action_grid, 0, 0);
  lv_obj_set_style_border_width(action_grid, 0, 0);

  data->mute_btn = lv_btn_create(action_grid);
  lv_obj_set_size(data->mute_btn, 60, 60);
  lv_obj_add_flag(data->mute_btn, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_t *mute_lbl = lv_label_create(data->mute_btn);
  lv_label_set_text(mute_lbl, LV_SYMBOL_MUTE);
  lv_obj_center(mute_lbl);
  lv_obj_add_event_cb(data->mute_btn, mute_btn_clicked, LV_EVENT_CLICKED, NULL);

  data->speaker_btn = lv_btn_create(action_grid);
  lv_obj_set_size(data->speaker_btn, 60, 60);
  lv_obj_add_flag(data->speaker_btn, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_t *spkr_lbl = lv_label_create(data->speaker_btn);
  lv_label_set_text(spkr_lbl, LV_SYMBOL_VOLUME_MAX);
  lv_obj_center(spkr_lbl);
  lv_obj_add_event_cb(data->speaker_btn, speaker_btn_clicked, LV_EVENT_CLICKED,
                      NULL);

  lv_obj_t *keypad_btn = lv_btn_create(action_grid);
  lv_obj_set_size(keypad_btn, 60, 60);
  lv_obj_t *kp_lbl = lv_label_create(keypad_btn);
  lv_label_set_text(kp_lbl, LV_SYMBOL_KEYBOARD);
  lv_obj_center(kp_lbl);
  lv_obj_add_event_cb(keypad_btn, show_dtmf_clicked, LV_EVENT_CLICKED, NULL);

  data->hold_btn = lv_btn_create(action_grid);
  lv_obj_set_size(data->hold_btn, 60, 60);
  lv_obj_add_flag(data->hold_btn, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_t *hold_lbl = lv_label_create(data->hold_btn);
  lv_label_set_text(hold_lbl, LV_SYMBOL_PAUSE);
  lv_obj_center(hold_lbl);
  lv_obj_add_event_cb(data->hold_btn, hold_btn_clicked, LV_EVENT_CLICKED, NULL);

  lv_obj_t *bottom_area = lv_obj_create(ui_layer); // Parent changed
  lv_obj_set_size(bottom_area, LV_PCT(100), 80);
  lv_obj_set_flex_flow(bottom_area, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bottom_area, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_border_width(bottom_area, 0, 0);
  lv_obj_set_style_bg_opa(bottom_area, 0, 0); // Transparent
  lv_obj_clear_flag(bottom_area, LV_OBJ_FLAG_SCROLLABLE);

  data->hangup_btn = lv_btn_create(bottom_area);
  lv_obj_set_size(data->hangup_btn, 70, 70);
  lv_obj_set_style_radius(data->hangup_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(data->hangup_btn, lv_color_hex(0xFF0000), 0);
  lv_obj_set_style_border_width(data->hangup_btn, 0, 0);
  lv_obj_set_style_shadow_width(data->hangup_btn, 0, 0);
  lv_obj_set_style_outline_width(data->hangup_btn, 0, 0);
  // Remove default padding to ensure true centering
  lv_obj_set_style_pad_all(data->hangup_btn, 0, 0);

  lv_obj_t *hangup_lbl = lv_label_create(data->hangup_btn);
  lv_label_set_text(hangup_lbl, LV_SYMBOL_CALL);
  // Rotate to look like hangup
  lv_obj_set_style_transform_angle(hangup_lbl, 1350, 0);
  // Ensure font size is appropriate
  lv_obj_set_style_text_font(hangup_lbl, &lv_font_montserrat_24, 0);
  lv_obj_align(hangup_lbl, LV_ALIGN_CENTER, 10, 10);
  lv_obj_add_event_cb(data->hangup_btn, hangup_btn_clicked, LV_EVENT_CLICKED,
                      NULL);

  // === ANSWER BUTTON (Initially hidden)
  // ===
  data->answer_btn = lv_btn_create(bottom_area);
  lv_obj_set_size(data->answer_btn, 70, 70);
  lv_obj_set_style_radius(data->answer_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(data->answer_btn, lv_color_hex(0x00AA00), 0);
  lv_obj_set_style_border_width(data->answer_btn, 0, 0);
  lv_obj_set_style_shadow_width(data->answer_btn, 0, 0);
  lv_obj_set_style_outline_width(data->answer_btn, 0, 0);
  lv_obj_t *answer_lbl = lv_label_create(data->answer_btn);
  lv_label_set_text(answer_lbl, LV_SYMBOL_CALL);
  lv_obj_center(answer_lbl);
  lv_obj_add_event_cb(data->answer_btn, answer_btn_clicked, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_add_flag(data->answer_btn, LV_OBJ_FLAG_HIDDEN);

  // === VIDEO ANSWER BUTTON (Initially hidden) ===
  data->video_answer_btn = lv_btn_create(bottom_area);
  lv_obj_set_size(data->video_answer_btn, 70, 70);
  lv_obj_set_style_radius(data->video_answer_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(data->video_answer_btn, lv_color_hex(0x555555), 0);
  lv_obj_set_style_border_width(data->video_answer_btn, 0, 0);
  lv_obj_set_style_shadow_width(data->video_answer_btn, 0, 0);
  lv_obj_set_style_outline_width(data->video_answer_btn, 0, 0);
  lv_obj_t *vid_ans_lbl = lv_label_create(data->video_answer_btn);
  lv_label_set_text(vid_ans_lbl, LV_SYMBOL_VIDEO);
  lv_obj_center(vid_ans_lbl);
  lv_obj_add_event_cb(data->video_answer_btn, video_answer_btn_clicked,
                      LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(data->video_answer_btn, LV_OBJ_FLAG_HIDDEN);

  // === DIALER SCREEN ===
  lv_obj_t *dialer_container = lv_obj_create(data->dialer_screen);
  lv_obj_set_size(dialer_container, LV_PCT(100), LV_PCT(100));
  lv_obj_set_scrollbar_mode(dialer_container, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_flex_flow(dialer_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(dialer_container, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(dialer_container, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *dialer_header = lv_obj_create(dialer_container);
  lv_obj_set_size(dialer_header, LV_PCT(100), 60);
  lv_obj_set_scrollbar_mode(dialer_header, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(dialer_header, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(dialer_header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(dialer_header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *back_btn = lv_btn_create(dialer_header);
  lv_obj_set_size(back_btn, 50, 50);
  lv_obj_t *back_label = lv_label_create(back_btn);
  lv_label_set_text(back_label, LV_SYMBOL_LEFT);
  lv_obj_center(back_label);
  lv_obj_add_event_cb(back_btn, back_to_home, LV_EVENT_CLICKED, NULL);

  lv_obj_t *title_label = lv_label_create(dialer_header);
  lv_label_set_text(title_label, "Call");
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_24, 0);
  lv_obj_set_flex_grow(title_label, 1);
  lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *menu_btn = lv_btn_create(dialer_header);
  lv_obj_set_size(menu_btn, 50, 50);
  lv_obj_t *menu_label = lv_label_create(menu_btn);
  lv_label_set_text(menu_label, LV_SYMBOL_LIST);
  lv_obj_center(menu_label);
  lv_obj_add_event_cb(menu_btn, menu_btn_clicked, LV_EVENT_CLICKED, NULL);

  lv_obj_t *account_row = lv_obj_create(dialer_container);
  lv_obj_set_size(account_row, LV_PCT(90), 50);
  lv_obj_set_scrollbar_mode(account_row, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(account_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(account_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(account_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  data->dialer_status_icon = lv_obj_create(account_row);
  lv_obj_set_size(data->dialer_status_icon, 16, 16);
  lv_obj_set_style_radius(data->dialer_status_icon, 8, 0);
  lv_obj_set_style_bg_color(data->dialer_status_icon, lv_color_hex(0x404040),
                            0);
  lv_obj_set_style_border_width(data->dialer_status_icon, 0, 0);
  lv_obj_add_flag(data->dialer_status_icon, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(data->dialer_status_icon, status_icon_clicked,
                      LV_EVENT_CLICKED, data);

  data->dialer_account_dropdown = lv_dropdown_create(account_row);
  lv_obj_set_flex_grow(data->dialer_account_dropdown, 1);
  lv_dropdown_set_options(data->dialer_account_dropdown, "No Accounts");

  data->number_label = lv_label_create(dialer_container);
  lv_label_set_text(data->number_label, "Enter number");
  lv_obj_set_style_text_font(data->number_label, &lv_font_montserrat_24, 0);
  lv_obj_align(data->number_label, LV_ALIGN_TOP_MID, 0, 45);

  lv_obj_t *numpad = lv_obj_create(dialer_container);
  lv_obj_set_size(numpad, LV_PCT(80), 240);
  lv_obj_set_scrollbar_mode(numpad, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(numpad, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(numpad, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_flex_flow(numpad, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(numpad, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_border_width(numpad, 0, 0);

  const char *digits[] = {"1", "2", "3", "4", "5", "6",
                          "7", "8", "9", "*", "0", "#"};
  for (int i = 0; i < 12; i++) {
    lv_obj_t *btn = lv_btn_create(numpad);
    lv_obj_set_size(btn, 60, 60);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, digits[i]);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, number_btn_clicked, LV_EVENT_CLICKED,
                        (void *)digits[i]);
  }

  lv_obj_t *btn_row = lv_obj_create(dialer_container);
  lv_obj_set_size(btn_row, LV_PCT(90), 80);
  lv_obj_set_scrollbar_mode(btn_row, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_gap(btn_row, 30, 0);
  lv_obj_set_style_border_width(btn_row, 0, 0);

  lv_obj_t *call_btn = lv_btn_create(btn_row);
  lv_obj_set_size(call_btn, 70, 70);
  lv_obj_set_style_radius(call_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(call_btn, lv_color_hex(0x00AA00), 0);
  lv_obj_t *call_label = lv_label_create(call_btn);
  lv_label_set_text(call_label, LV_SYMBOL_CALL);
  lv_obj_set_style_text_font(call_label, &lv_font_montserrat_24, 0);
  lv_obj_center(call_label);
  lv_obj_add_event_cb(call_btn, call_btn_clicked, LV_EVENT_CLICKED, NULL);

  lv_obj_t *video_call_btn = lv_btn_create(btn_row);
  lv_obj_set_size(video_call_btn, 60, 60);
  lv_obj_set_style_radius(video_call_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(video_call_btn, lv_color_hex(0x0055AA), 0);
  lv_obj_t *video_label = lv_label_create(video_call_btn);
  lv_label_set_text(video_label, LV_SYMBOL_VIDEO);
  lv_obj_center(video_label);
  lv_obj_add_event_cb(video_call_btn, video_call_btn_clicked, LV_EVENT_CLICKED,
                      NULL);

  lv_obj_t *backspace_btn = lv_btn_create(btn_row);
  lv_obj_set_size(backspace_btn, 50, 50);
  lv_obj_set_style_radius(backspace_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(backspace_btn, lv_color_hex(0x606060), 0);
  lv_obj_t *bs_label = lv_label_create(backspace_btn);
  lv_label_set_text(bs_label, LV_SYMBOL_BACKSPACE);
  lv_obj_center(bs_label);
  lv_obj_add_event_cb(backspace_btn, backspace_btn_clicked, LV_EVENT_CLICKED,
                      NULL);

  strcpy(data->number_buffer, "");
  update_account_dropdowns(data);

  return 0;
}

static void call_start(applet_t *applet) {
  call_data_t *data = (call_data_t *)applet->user_data;
  log_info("CallApplet", "Started");

  // Create thread-safe UI poller if not exists
  if (!data->ui_poller) {
    data->ui_poller = lv_timer_create(check_ui_updates, 50, data);
  }

  // Sync state from Baresip Manager
  data->current_state = baresip_manager_get_state();
  const char *peer = baresip_manager_get_peer();
  if (peer) {
    strncpy(data->current_peer_uri, peer, sizeof(data->current_peer_uri) - 1);
  } else {
    data->current_peer_uri[0] = '\0';
  }

  if (g_req_view_mode != VIEW_MODE_NONE) {
    // Re-use logic for view mode processing (simplified duplication for now)
    // To ensure consistency, we should ideally call a helper, but for quick
    // fix: Process View Mode
    call_info_t calls[8];
    int count = baresip_manager_get_active_calls(calls, 8);
    int target_idx = -1;

    // Priority 1: Find Current Call matching the requested View Mode
    for (int i = 0; i < count; i++) {
      bool mode_match = (g_req_view_mode == VIEW_MODE_ACTIVE &&
                         calls[i].state != CALL_STATE_INCOMING) ||
                        (g_req_view_mode == VIEW_MODE_INCOMING &&
                         calls[i].state == CALL_STATE_INCOMING);

      if (mode_match && calls[i].is_current) {
        target_idx = i;
        break;
      }
    }

    // Priority 2: Fallback to first matching call if no current call found
    if (target_idx == -1) {
      for (int i = 0; i < count; i++) {
        bool mode_match = (g_req_view_mode == VIEW_MODE_ACTIVE &&
                           calls[i].state != CALL_STATE_INCOMING) ||
                          (g_req_view_mode == VIEW_MODE_INCOMING &&
                           calls[i].state == CALL_STATE_INCOMING);

        if (mode_match) {
          target_idx = i;
          break;
        }
      }
    }

    if (target_idx >= 0) {
      log_info("CallApplet", "Start: Switching to call %p for mode %d",
               calls[target_idx].id, g_req_view_mode);
      baresip_manager_switch_to(calls[target_idx].id);
      data->current_state = calls[target_idx].state;
      strncpy(data->current_peer_uri, calls[target_idx].peer_uri,
              sizeof(data->current_peer_uri) - 1);

      bool is_incoming_screen = (g_req_view_mode == VIEW_MODE_INCOMING);
      show_active_call_screen(data, data->current_peer_uri, is_incoming_screen);
      if (!is_incoming_screen)
        update_call_list(data, NULL);
    } else {
      log_warn("CallApplet", "Start: No matching call found for mode %d",
               g_req_view_mode);
      show_dialer_screen(data);
    }
    g_req_view_mode = VIEW_MODE_NONE;
  } else {
    // Always show dialer screen on manual
    // start as requested
    show_dialer_screen(data);
  }

  // Note: if there ARE active calls, the
  // user can still use the dialer to start
  // a NEW call. The 'return to call' logic
  // is handled via notifications or if the
  // user manually navigates back (which we
  // might need to add a button for in
  // Dialer if calls exist). For now,
  // adhering strictly to "always show
  // dialer screen" on launch.
}

static void call_pause(applet_t *applet) {
  call_data_t *data = (call_data_t *)applet->user_data;
  log_info("CallApplet", "Paused");
  if (data->call_timer)
    lv_timer_pause(data->call_timer);
  if (data->status_timer)
    lv_timer_pause(data->status_timer);
  if (data->video_timer)
    lv_timer_pause(data->video_timer);
}

static void call_resume(applet_t *applet) {
  call_data_t *data = (call_data_t *)applet->user_data;
  if (data->call_timer)
    lv_timer_resume(data->call_timer);
  if (data->status_timer)
    lv_timer_resume(data->status_timer);
  if (data->video_timer)
    lv_timer_resume(data->video_timer);

  // Sync state from Baresip Manager
  data->current_state = baresip_manager_get_state();
  const char *peer = baresip_manager_get_peer();
  if (peer) {
    strncpy(data->current_peer_uri, peer, sizeof(data->current_peer_uri) - 1);
  } else {
    data->current_peer_uri[0] = '\0';
  }

  log_info("CallApplet", "Resuming. State=%d, RequestActive=%d, ViewMode=%d",
           data->current_state, data->request_active_view, g_req_view_mode);

  // Always refresh active call screen data
  // if we are in a call Check explicit
  // intent first
  // Handle View Requests
  if (g_req_view_mode != VIEW_MODE_NONE) {
    log_info("CallApplet", "Resume: Processing View Mode %d", g_req_view_mode);

    // Find appropriate call
    call_info_t calls[8];
    int count = baresip_manager_get_active_calls(calls, 8);
    int target_idx = -1;

    for (int i = 0; i < count; i++) {
      if (g_req_view_mode == VIEW_MODE_ACTIVE) {
        // Look for non-incoming (Established, Outgoing, Ringing?)
        // User said Incoming Screen is for Ringing/Early(Incoming side).
        // In Call is for Outgoing/Connected.
        if (calls[i].state != CALL_STATE_INCOMING) {
          target_idx = i;
          break;
        }
      } else if (g_req_view_mode == VIEW_MODE_INCOMING) {
        if (calls[i].state == CALL_STATE_INCOMING) {
          target_idx = i;
          break;
        }
      }
    }

    if (target_idx >= 0) {
      log_info("CallApplet", "Switching to call %p for mode %d",
               calls[target_idx].id, g_req_view_mode);
      baresip_manager_switch_to(calls[target_idx].id);
      // Force state update locally to ensure screen matches
      data->current_state = calls[target_idx].state;
      strncpy(data->current_peer_uri, calls[target_idx].peer_uri,
              sizeof(data->current_peer_uri) - 1);

      bool is_incoming_screen = (g_req_view_mode == VIEW_MODE_INCOMING);
      show_active_call_screen(data, data->current_peer_uri, is_incoming_screen);

      // If Active mode, we might need to explicit update list/buttons
      if (!is_incoming_screen) {
        update_call_list(data, NULL);
      }
    } else {
      log_warn("CallApplet",
               "No matching call found for mode %d, defaulting to Dialer",
               g_req_view_mode);
      show_dialer_screen(data);
    }

    g_req_view_mode = VIEW_MODE_NONE;
  } else {
    // Default: Show dialer if IDLE
    log_info("CallApplet", "Resume: Showing Dialer (No forced active view)");
    show_dialer_screen(data);
    load_settings(data);            // Re-load settings
    update_account_dropdowns(data); // Update dropdowns
  }
}

// Global Intent Setter - Active (Connected/Outgoing)
void call_applet_request_active_view(void) {
  log_info("CallApplet", "Requesting Active View (Mode 1)");
  g_req_view_mode = VIEW_MODE_ACTIVE;
}

// Global Intent Setter - Incoming
void call_applet_request_incoming_view(void) {
  log_info("CallApplet", "Requesting Incoming View (Mode 2)");
  g_req_view_mode = VIEW_MODE_INCOMING;
}

static void call_stop(applet_t *applet) {
  (void)applet;
  log_info("CallApplet", "Stopped");
  call_data_t *data = (call_data_t *)applet->user_data;
  if (data && data->ui_poller) {
    lv_timer_del(data->ui_poller);
    data->ui_poller = NULL;
  }
  if (data && data->exit_timer) {
    lv_timer_del(data->exit_timer);
    data->exit_timer = NULL;
  }
}

static void call_destroy(applet_t *applet) {
  log_info("CallApplet", "Destroying");
  g_call_data = NULL; // Prevent dangling pointer
  if (applet->user_data) {
    lv_mem_free(applet->user_data);
    applet->user_data = NULL;
  }
}

APPLET_DEFINE(call_applet, "Call", "Make a call", LV_SYMBOL_CALL);

void call_applet_register(void) {
  call_applet.callbacks.init = call_init;
  call_applet.callbacks.start = call_start;
  call_applet.callbacks.pause = call_pause;
  call_applet.callbacks.resume = call_resume;
  call_applet.callbacks.stop = call_stop;
  call_applet.callbacks.destroy = call_destroy;

  applet_manager_register(&call_applet);
}
