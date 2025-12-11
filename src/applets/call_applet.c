#include "applet.h"
#include "applet_manager.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_ACCOUNTS 10
#define CONFIG_DIR ".baresip-lvgl"

#include "baresip_manager.h"
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
  lv_obj_t *answer_btn;
  lv_obj_t *dtmf_btn;

  // Call State
  bool is_muted;
  bool is_speaker;
  bool is_hold;
  uint32_t call_start_time;
  lv_timer_t *call_timer;

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
  lv_timer_t *status_timer;
  applet_t *applet; // Back reference for switching

  // State tracking
  enum call_state current_state;
  char current_peer_uri[256];
} call_data_t;

// Global pointer to current applet data for callback
static call_data_t *g_call_data = NULL;

// Forward declarations
static void show_active_call_screen(call_data_t *data, const char *number,
                                    bool incoming);
static void show_dialer_screen(call_data_t *data);
static void update_call_duration(lv_timer_t *timer);
static void load_settings(call_data_t *data);
static void save_settings(call_data_t *data);
static void update_account_status(call_data_t *data, int account_idx,
                                  reg_status_t status);
static void reg_status_callback(const char *aor, reg_status_t status);
static void update_account_dropdowns(call_data_t *data);
static void menu_quit_action(call_data_t *data);

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
    data->config.default_account_index = 0;
  }
  data->account_count = config_load_accounts(data->accounts, MAX_ACCOUNTS);
  log_info("CallApplet", "Settings loaded: Codec=%s, AccCount=%d",
           config_get_codec_name(data->config.preferred_codec),
           data->account_count);
}

// Save settings to files
static void save_settings(call_data_t *data) {
  config_save_app_settings(&data->config);
}

// Status update timer callback (runs in main thread)
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

// Registration status callback (runs in background thread)
static void reg_status_callback(const char *aor, reg_status_t status) {
  if (!g_call_data || !aor)
    return;

  log_debug("CallApplet", "Registration status update: %s -> %d", aor, status);

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

  // Update dialer status icon if showing default account
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

static void menu_about_action(call_data_t *data) {
  (void)data;
  lv_obj_t *mbox = lv_msgbox_create(
      NULL, "About",
      "Baresip LVGL Call Applet\nVersion 1.0\nBased on baresip-studio design.",
      NULL, true);
  lv_obj_center(mbox);
}

static void menu_backup_action(call_data_t *data) {
  (void)data;
  char config_dir[256];
  config_get_dir_path(config_dir, sizeof(config_dir));
  char src[256], dst[256];

  snprintf(src, sizeof(src), "%s/accounts.conf", config_dir);
  snprintf(dst, sizeof(dst), "%s/accounts.conf.bak", config_dir);
  copy_file(src, dst);

  snprintf(src, sizeof(src), "%s/settings.conf", config_dir);
  snprintf(dst, sizeof(dst), "%s/settings.conf.bak", config_dir);
  copy_file(src, dst);

  lv_obj_t *mbox = lv_msgbox_create(
      NULL, "Backup", "Settings backed up successfully!", NULL, true);
  lv_obj_center(mbox);
}

static void menu_restore_action(call_data_t *data) {
  char config_dir[256];
  config_get_dir_path(config_dir, sizeof(config_dir));
  char src[256], dst[256];

  snprintf(src, sizeof(src), "%s/accounts.conf.bak", config_dir);
  snprintf(dst, sizeof(dst), "%s/accounts.conf", config_dir);
  if (copy_file(src, dst) == 0) {
    snprintf(src, sizeof(src), "%s/settings.conf.bak", config_dir);
    snprintf(dst, sizeof(dst), "%s/settings.conf", config_dir);
    copy_file(src, dst);

    load_settings(data);
    update_account_dropdowns(data);
    lv_obj_t *mbox = lv_msgbox_create(
        NULL, "Restore", "Settings restored successfully!", NULL, true);
    lv_obj_center(mbox);
  } else {
    lv_obj_t *mbox =
        lv_msgbox_create(NULL, "Restore", "No backup found!", NULL, true);
    lv_obj_center(mbox);
  }
}

static void menu_quit_action(call_data_t *data) {
  (void)data;
  exit(0);
}

static void menu_close_clicked(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_obj_t *header = lv_obj_get_parent(obj);
  lv_obj_t *menu_container = lv_obj_get_parent(header);
  lv_obj_t *modal_bg = lv_obj_get_parent(menu_container);
  lv_obj_del(modal_bg);
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
  } else if (strcmp(txt, "Backup") == 0) {
    menu_backup_action(data);
  } else if (strcmp(txt, "Restore") == 0) {
    menu_restore_action(data);
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

  lv_obj_t *menu_container = lv_obj_create(modal_bg);
  lv_obj_set_size(menu_container, 220, 320);
  lv_obj_center(menu_container);
  lv_obj_set_flex_flow(menu_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(menu_container, 0, 0);
  lv_obj_set_style_radius(menu_container, 10, 0);

  lv_obj_t *header = lv_obj_create(menu_container);
  lv_obj_set_size(header, LV_PCT(100), 50);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(header, 10, 0);
  lv_obj_set_style_bg_color(header, lv_color_hex(0xE0E0E0), 0);
  lv_obj_set_style_border_width(header, 0, 0);

  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text(title, "Menu");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

  lv_obj_t *close_btn = lv_btn_create(header);
  lv_obj_set_size(close_btn, 30, 30);
  lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFF5555), 0);
  lv_obj_t *close_label = lv_label_create(close_btn);
  lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
  lv_obj_center(close_label);
  lv_obj_add_event_cb(close_btn, menu_close_clicked, LV_EVENT_CLICKED, NULL);

  lv_obj_t *list = lv_list_create(menu_container);
  lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(list, 1);
  lv_obj_set_style_border_width(list, 0, 0);

  lv_obj_t *btn;
  btn = lv_list_add_btn(list, LV_SYMBOL_WARNING, "About");
  lv_obj_add_event_cb(btn, menu_event_cb, LV_EVENT_CLICKED, data);

  btn = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, "Settings");
  lv_obj_add_event_cb(btn, menu_event_cb, LV_EVENT_CLICKED, data);

  btn = lv_list_add_btn(list, LV_SYMBOL_DIRECTORY, "Accounts");
  lv_obj_add_event_cb(btn, menu_event_cb, LV_EVENT_CLICKED, data);

  btn = lv_list_add_btn(list, LV_SYMBOL_UPLOAD, "Backup");
  lv_obj_add_event_cb(btn, menu_event_cb, LV_EVENT_CLICKED, data);

  btn = lv_list_add_btn(list, LV_SYMBOL_DOWNLOAD, "Restore");
  lv_obj_add_event_cb(btn, menu_event_cb, LV_EVENT_CLICKED, data);

  btn = lv_list_add_btn(list, LV_SYMBOL_POWER, "Quit");
  lv_obj_add_event_cb(btn, menu_event_cb, LV_EVENT_CLICKED, data);
}

static void show_dialer_screen(call_data_t *data) {
  lv_obj_clear_flag(data->dialer_screen, LV_OBJ_FLAG_HIDDEN);
  if (data->active_call_screen)
    lv_obj_add_flag(data->active_call_screen, LV_OBJ_FLAG_HIDDEN);
}

static void show_active_call_screen(call_data_t *data, const char *number,
                                    bool incoming) {
  lv_obj_add_flag(data->dialer_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(data->active_call_screen, LV_OBJ_FLAG_HIDDEN);

  data->is_muted = false;
  data->is_speaker = false;
  data->is_hold = false;

  if (data->call_number_label)
    lv_label_set_text(data->call_number_label, number);
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
    log_info("CallApplet", "Calling: %s", data->number_buffer);
    baresip_manager_call(data->number_buffer);
    show_active_call_screen(data, data->number_buffer, false);
  }
}

static void answer_btn_clicked(lv_event_t *e) {
  (void)e;
  log_info("CallApplet", "Answer clicked");
  baresip_manager_answer();
}

static void hangup_btn_clicked(lv_event_t *e) {
  (void)e;
  applet_t *applet = applet_manager_get_current();
  call_data_t *data = (call_data_t *)applet->user_data;

  // Prevent accidental hangup causing immediate CANCEL due to UI overlap/bounce
  if (lv_tick_get() - data->call_start_time < 1000) {
    log_warn("CallApplet",
             "Ignoring hangup click (debounce protection, <1000ms)");
    return;
  }

  log_info("CallApplet", "Hangup clicked");
  baresip_manager_hangup();
  show_dialer_screen(data);
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
  log_info("CallApplet", "Speaker: %d", data->is_speaker);
}

static void hold_btn_clicked(lv_event_t *e) {
  (void)e;
  applet_t *applet = applet_manager_get_current();
  call_data_t *data = (call_data_t *)applet->user_data;
  data->is_hold = !data->is_hold;
  if (data->is_hold) {
    lv_obj_add_state(data->hold_btn, LV_STATE_CHECKED);
    lv_label_set_text(data->call_status_label, "On Hold");
  } else {
    lv_obj_clear_state(data->hold_btn, LV_STATE_CHECKED);
    lv_label_set_text(data->call_status_label, "Connected");
  }
  log_info("CallApplet", "Hold: %d", data->is_hold);
}

static void update_call_duration(lv_timer_t *timer) {
  call_data_t *data = (call_data_t *)timer->user_data;
  if (!data || !data->active_call_screen ||
      lv_obj_has_flag(data->active_call_screen, LV_OBJ_FLAG_HIDDEN))
    return;

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
    snprintf(entry, sizeof(entry), "%s@%s", data->accounts[i].display_name,
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

// Callback for call state changes from Baresip Manager
static void on_call_state_change(enum call_state state, const char *peer_uri) {
  log_debug("CallApplet", "OnCallStateChange: State=%d, Peer=%s", state,
            peer_uri ? peer_uri : "unknown");

  // Always launch applet for incoming/established calls, even if not
  // authorized/initialized yet
  if (state == CALL_STATE_INCOMING || state == CALL_STATE_ESTABLISHED) {
    applet_manager_launch("Call");
  }

  if (!g_call_data) {
    log_error("CallApplet", "g_call_data is NULL, skipping valid update");
    return;
  }

  // Update internal state
  g_call_data->current_state = state;
  if (peer_uri) {
    strncpy(g_call_data->current_peer_uri, peer_uri,
            sizeof(g_call_data->current_peer_uri) - 1);
  } else {
    strcpy(g_call_data->current_peer_uri, "Unknown");
  }

  // Update UI if we are already running
  if (state == CALL_STATE_INCOMING) {
    if (peer_uri) {
      lv_label_set_text(g_call_data->call_name_label, peer_uri);
      lv_label_set_text(g_call_data->call_number_label, peer_uri);
    }
    lv_label_set_text(g_call_data->call_status_label, "Incoming Call...");

    show_active_call_screen(g_call_data, peer_uri ? peer_uri : "Unknown", true);

  } else if (state == CALL_STATE_ESTABLISHED) {
    lv_label_set_text(g_call_data->call_status_label, "Connected");
    lv_obj_set_style_text_color(g_call_data->call_status_label,
                                lv_color_hex(0x00FF00), 0);
    // Start timer for duration updates
    if (!g_call_data->status_timer) {
      g_call_data->status_timer =
          lv_timer_create(update_call_duration, 1000, g_call_data);
    }

    show_active_call_screen(g_call_data, peer_uri ? peer_uri : "Unknown",
                            false);

  } else if (state == CALL_STATE_TERMINATED || state == CALL_STATE_IDLE) {
    if (g_call_data->status_timer) {
      lv_timer_del(g_call_data->status_timer);
      g_call_data->status_timer = NULL;
    }
    show_dialer_screen(g_call_data);
  }
}

static int call_init(applet_t *applet) {
  log_info("CallApplet", "Initializing");

  call_data_t *data = lv_mem_alloc(sizeof(call_data_t));
  memset(data, 0, sizeof(call_data_t));
  applet->user_data = data;
  g_call_data = data; // Global reference for callbacks

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
      log_error("CallApplet", "Failed to initialize baresip manager");
      lv_mem_free(data);
      return -1;
    }
    baresip_initialized = 1;
  }

  g_call_data = data;
  data->status_update_pending = false;
  data->status_timer = lv_timer_create(status_timer_cb, 200, data);
  data->call_timer = lv_timer_create(update_call_duration, 1000, data);
  baresip_manager_set_reg_callback(reg_status_callback);
  baresip_manager_set_callback(on_call_state_change);
  load_settings(data);

  log_info("CallApplet", "Auto-registering enabled accounts...");
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

  // === ACTIVE CALL SCREEN ===
  lv_obj_t *active_call_cont = lv_obj_create(data->active_call_screen);
  lv_obj_set_size(active_call_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(active_call_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(active_call_cont, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(active_call_cont, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *top_info = lv_obj_create(active_call_cont);
  lv_obj_set_size(top_info, LV_PCT(100), 150);
  lv_obj_set_flex_flow(top_info, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(top_info, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_border_width(top_info, 0, 0);
  lv_obj_set_style_bg_opa(top_info, LV_OPA_TRANSP, 0);

  data->call_name_label = lv_label_create(top_info);
  lv_label_set_text(data->call_name_label, "Unknown");
  lv_obj_set_style_text_font(data->call_name_label, &lv_font_montserrat_24, 0);

  data->call_number_label = lv_label_create(top_info);
  lv_label_set_text(data->call_number_label, "0000");
  lv_obj_set_style_text_font(data->call_number_label, &lv_font_montserrat_14,
                             0);

  data->call_status_label = lv_label_create(top_info);
  lv_label_set_text(data->call_status_label, "Calling...");
  lv_obj_set_style_text_color(data->call_status_label, lv_color_hex(0x00AA00),
                              0);

  data->call_duration_label = lv_label_create(top_info);
  lv_label_set_text(data->call_duration_label, "00:00");
  lv_obj_set_style_text_font(data->call_duration_label, &lv_font_montserrat_20,
                             0);

  lv_obj_t *action_grid = lv_obj_create(active_call_cont);
  lv_obj_set_size(action_grid, LV_PCT(90), 120);
  lv_obj_set_flex_flow(action_grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(action_grid, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
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

  data->hold_btn = lv_btn_create(action_grid);
  lv_obj_set_size(data->hold_btn, 60, 60);
  lv_obj_add_flag(data->hold_btn, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_t *hold_lbl = lv_label_create(data->hold_btn);
  lv_label_set_text(hold_lbl, LV_SYMBOL_PAUSE);
  lv_obj_center(hold_lbl);
  lv_obj_add_event_cb(data->hold_btn, hold_btn_clicked, LV_EVENT_CLICKED, NULL);

  lv_obj_t *bottom_area = lv_obj_create(active_call_cont);
  lv_obj_set_size(bottom_area, LV_PCT(100), 80);
  lv_obj_set_flex_flow(bottom_area, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bottom_area, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_border_width(bottom_area, 0, 0);

  data->hangup_btn = lv_btn_create(bottom_area);
  lv_obj_set_size(data->hangup_btn, 70, 70);
  lv_obj_set_style_radius(data->hangup_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(data->hangup_btn, lv_color_hex(0xFF0000), 0);
  lv_obj_t *hangup_lbl = lv_label_create(data->hangup_btn);
  lv_label_set_text(hangup_lbl, LV_SYMBOL_CALL);
  lv_obj_set_style_transform_angle(hangup_lbl, 1350, 0);
  lv_obj_center(hangup_lbl);
  lv_obj_add_event_cb(data->hangup_btn, hangup_btn_clicked, LV_EVENT_CLICKED,
                      NULL);

  // === ANSWER BUTTON (Initially hidden) ===
  data->answer_btn = lv_btn_create(bottom_area);
  lv_obj_set_size(data->answer_btn, 70, 70);
  lv_obj_set_style_radius(data->answer_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(data->answer_btn, lv_color_hex(0x00AA00), 0);
  lv_obj_t *answer_lbl = lv_label_create(data->answer_btn);
  lv_label_set_text(answer_lbl, LV_SYMBOL_CALL);
  lv_obj_center(answer_lbl);
  lv_obj_add_event_cb(data->answer_btn, answer_btn_clicked, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_add_flag(data->answer_btn, LV_OBJ_FLAG_HIDDEN);

  // === DIALER SCREEN ===
  lv_obj_t *dialer_container = lv_obj_create(data->dialer_screen);
  lv_obj_set_size(dialer_container, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(dialer_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(dialer_container, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(dialer_container, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *dialer_header = lv_obj_create(dialer_container);
  lv_obj_set_size(dialer_header, LV_PCT(100), 60);
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
  lv_obj_set_flex_flow(account_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(account_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  data->dialer_status_icon = lv_obj_create(account_row);
  lv_obj_set_size(data->dialer_status_icon, 16, 16);
  lv_obj_set_style_radius(data->dialer_status_icon, 8, 0);
  lv_obj_set_style_bg_color(data->dialer_status_icon, lv_color_hex(0x404040),
                            0);
  lv_obj_set_style_border_width(data->dialer_status_icon, 0, 0);

  data->dialer_account_dropdown = lv_dropdown_create(account_row);
  lv_obj_set_flex_grow(data->dialer_account_dropdown, 1);
  lv_dropdown_set_options(data->dialer_account_dropdown, "No Accounts");

  data->number_label = lv_label_create(dialer_container);
  lv_label_set_text(data->number_label, "Enter number");
  lv_obj_set_style_text_font(data->number_label, &lv_font_montserrat_24, 0);
  lv_obj_align(data->number_label, LV_ALIGN_TOP_MID, 0, 45);

  lv_obj_t *numpad = lv_obj_create(dialer_container);
  lv_obj_set_size(numpad, LV_PCT(80), 240);
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
  show_dialer_screen(data);
}

static void call_pause(applet_t *applet) {
  (void)applet;
  log_info("CallApplet", "Paused");
}

static void call_resume(applet_t *applet) {
  call_data_t *data = (call_data_t *)applet->user_data;

  // Sync state from Baresip Manager
  data->current_state = baresip_manager_get_state();
  const char *peer = baresip_manager_get_peer();
  if (peer) {
    strncpy(data->current_peer_uri, peer, sizeof(data->current_peer_uri) - 1);
  } else {
    data->current_peer_uri[0] = '\0';
  }

  log_info("CallApplet", "Resuming. State=%d, ActiveCallScreen=%p, Incoming=%s",
           data->current_state, (void *)data->active_call_screen,
           (data->current_state == CALL_STATE_INCOMING) ? "YES" : "NO");

  // Always refresh active call screen data if we are in a call
  if (data->current_state == CALL_STATE_INCOMING ||
      data->current_state == CALL_STATE_EARLY ||
      data->current_state == CALL_STATE_ESTABLISHED ||
      data->current_state == CALL_STATE_RINGING ||
      data->current_state == CALL_STATE_OUTGOING) { // Added OUTGOING check

    // Safety check: ensure screen exists
    if (!data->active_call_screen) {
      log_warn("CallApplet", "WARNING: Active Call Screen is NULL in Resume! "
                             "Recreating...");
      // This shouldn't happen if init ran, but good for debug
    }

    show_active_call_screen(data, data->current_peer_uri,
                            data->current_state == CALL_STATE_INCOMING);
  } else {
    show_dialer_screen(data);
    load_settings(data);            // Re-load settings if returning to dialer
    update_account_dropdowns(data); // Update dropdowns if returning to dialer
  }
}

static void call_stop(applet_t *applet) {
  (void)applet;
  log_info("CallApplet", "Stopped");
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
