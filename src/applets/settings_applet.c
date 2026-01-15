#include "applet.h"
#include "applet_manager.h"
#include "baresip_manager.h"
#include "config_manager.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include "../ui/ui_helpers.h"

// Settings applet data
typedef struct {
  lv_obj_t *main_screen;
  lv_obj_t *account_settings_screen;
  lv_obj_t *call_settings_screen;
  lv_obj_t *account_form_screen;
  lv_obj_t *account_form_content;

  // Account management
  // voip_account_t from config_manager.h
  voip_account_t accounts[MAX_ACCOUNTS];
  int account_count;
  int editing_account_index;   // -1 for new, >= 0 for edit
  voip_account_t temp_account; // Temporary storage for editing

  // App Configuration
  app_config_t config;

  // Widgets
  lv_obj_t *codec_dropdown;
  lv_obj_t *default_account_dropdown;
  lv_obj_t *account_list;

  // Call Settings Widgets
  lv_obj_t *call_start_auto_sw;
  lv_obj_t *call_show_fav_sw; // [NEW]
  lv_obj_t *call_log_level_dd;
  lv_obj_t *call_listen_addr_ta;
  lv_obj_t *call_addr_fam_dd;
  lv_obj_t *call_dns_ta;

  lv_obj_t *call_video_size_dd;
  // Account form widgets
  lv_obj_t *form_name_ta;
  lv_obj_t *form_user_ta;
  lv_obj_t *form_pass_ta;
  lv_obj_t *form_server_ta;
  lv_obj_t *form_port_ta;
  lv_obj_t *form_realm_ta;
  lv_obj_t *form_proxy_ta;
  lv_obj_t *form_active_sw;

  // Extended fields
  lv_obj_t *form_nick_ta;
  lv_obj_t *form_auth_user_ta;
  lv_obj_t *form_proxy2_ta;
  lv_obj_t *form_reg_int_ta;
  // Duplicate removed
  lv_obj_t *form_media_enc_dd;
  // lv_obj_t *form_media_nat_dd; // Replaced
  lv_obj_t *form_transport_dd;
  lv_obj_t *form_ice_sw;
  lv_obj_t *form_stun_ta;

  // Codec Priority Widgets
  lv_obj_t *codec_priority_screen;
  lv_obj_t *codec_list;
  bool editing_audio_codecs; // true = audio, false = video
  lv_obj_t *form_rtcp_mux_sw;
  lv_obj_t *form_prack_sw;
  lv_obj_t *form_dtmf_dd;
  lv_obj_t *form_ans_dd;
  lv_obj_t *form_vm_ta;
  lv_obj_t *form_country_ta;
  
  // State
  bool is_deep_linked;
} settings_data_t;

// Forward declarations
static void show_main_screen(settings_data_t *data);
static void show_account_settings(settings_data_t *data);
static void show_call_settings(settings_data_t *data);
static void show_account_form(settings_data_t *data);
static void save_call_settings(settings_data_t *data);

static void refresh_account_list(settings_data_t *data);
static void update_account_dropdowns(settings_data_t *data);

// Navigation State
// Standard Codec Definitions
static const char *STANDARD_AUDIO_CODECS[] = {
    "opus/48000/2",   "PCMU/8000/1",   "PCMA/8000/1",
    "G722/16000/1",   "G7221/16000/1", "AMR/8000/1",
    "AMR-WB/16000/1", "GSM/8000/1",    NULL};

static const char *STANDARD_VIDEO_CODECS[] = {"H264", "VP8", "VP9", "AV1",
                                              NULL};

static enum {
  SETTINGS_SCREEN_MAIN,
  SETTINGS_SCREEN_ACCOUNTS,
  SETTINGS_SCREEN_CALL
} target_screen = SETTINGS_SCREEN_MAIN;

// Forward decl
static void show_codec_priority_screen(settings_data_t *data, bool is_audio);
static void show_account_form(settings_data_t *data);
static void capture_form_data(settings_data_t *data);
static void init_account_edit(settings_data_t *data, int index);
static void update_account_dropdowns(settings_data_t *data);

static enum {
  NAV_SOURCE_MAIN,
  NAV_SOURCE_DEEP_LINK
} nav_source = NAV_SOURCE_MAIN;

void settings_applet_open_accounts(void) {
    target_screen = SETTINGS_SCREEN_ACCOUNTS;
    nav_source = NAV_SOURCE_DEEP_LINK;
}

// Helper to sanitize input (replace | with _)
static void sanitize_input(char *str) {
  for (int i = 0; str[i]; i++) {
    if (str[i] == '|')
      str[i] = '_';
  }
}

static void handle_swipe_back(lv_event_t *e) {
    (void)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_RIGHT || dir == LV_DIR_LEFT) {
        log_info("SettingsApplet", "Swipe detected (Dir: %d), going back", dir);
        applet_manager_back();
    }
}

// Event handler for back button
static void back_btn_clicked(lv_event_t *e) { (void)e; applet_manager_back(); }

/* Orphaned Block Removed */

static void call_settings_clicked(lv_event_t *e) {
  (void)e;
  applet_t *applet = applet_manager_get_current();
  settings_data_t *data = (settings_data_t *)applet->user_data;
  show_call_settings(data);
  show_call_settings(data);
}

static void back_to_main(lv_event_t *e) {
  (void)e;
  applet_t *applet = applet_manager_get_current();
  settings_data_t *data = (settings_data_t *)applet->user_data;

  if (data->is_deep_linked) {
      // If deep linked, 'Back' means returning to the calling applet (e.g. Dialer)
      applet_manager_back();
      // We don't need to reset is_deep_linked here because next launch/resume will reset it based on global flags
  } else {
      // Normal navigation within Settings
      show_main_screen(data);
  }
}

static void add_account_btn_clicked(lv_event_t *e) {
  (void)e;
  applet_t *applet = applet_manager_get_current();
  settings_data_t *data = (settings_data_t *)applet->user_data;
  init_account_edit(data, -1);
  show_account_form(data);
}

static void load_settings(settings_data_t *data) {
  // Load general settings
  if (config_load_app_settings(&data->config) != 0) {
    // Defaults handled in config_manager, but ensured here just in case
    data->config.preferred_codec = CODEC_OPUS;
    data->config.default_account_index = 0;
  }

  // Load accounts
  data->account_count = config_load_accounts(data->accounts, MAX_ACCOUNTS);

  if (data->config.default_account_index >= data->account_count) {
    data->config.default_account_index = -1; // Default to None
  }
}

static void save_settings(settings_data_t *data) {
  config_save_app_settings(&data->config);
  config_save_accounts(data->accounts, data->account_count);
}

static void edit_account_clicked(lv_event_t *e) {
  applet_t *applet = applet_manager_get_current();
  settings_data_t *data = (settings_data_t *)applet->user_data;
  int index = (int)(intptr_t)lv_event_get_user_data(e);
  init_account_edit(data, index);
  show_account_form(data);
}

static void delete_account_clicked(lv_event_t *e) {
  applet_t *applet = applet_manager_get_current();
  settings_data_t *data = (settings_data_t *)applet->user_data;
  int index = (int)(intptr_t)lv_event_get_user_data(e);

  // Shift accounts down
  for (int i = index; i < data->account_count - 1; i++) {
    data->accounts[i] = data->accounts[i + 1];
  }
  data->account_count--;

  data->account_count--;

  save_settings(data);
  refresh_account_list(data);
  update_account_dropdowns(data);
  log_info("SettingsApplet", "Account deleted");
}

static void save_account_clicked(lv_event_t *e) {
  (void)e;
  applet_t *applet = applet_manager_get_current();
  settings_data_t *data = (settings_data_t *)applet->user_data;

  capture_form_data(data);
  voip_account_t *acc;

  if (data->editing_account_index == -1) {
    if (data->account_count >= MAX_ACCOUNTS) {
      log_warn("SettingsApplet", "Maximum accounts reached");
      return;
    }
    acc = &data->accounts[data->account_count++];
  } else {
    acc = &data->accounts[data->editing_account_index];
  }

  // Copy temp to perm
  *acc = data->temp_account;

  // Sanitize fields (already done in capture but good to ensure uniqueness if
  // needed)
  sanitize_input(acc->display_name);
  sanitize_input(acc->username);
  sanitize_input(acc->password);
  sanitize_input(acc->server);

  strncpy(acc->realm, lv_textarea_get_text(data->form_realm_ta),
          sizeof(acc->realm) - 1);
  sanitize_input(acc->realm);

  strncpy(acc->outbound_proxy, lv_textarea_get_text(data->form_proxy_ta),
          sizeof(acc->outbound_proxy) - 1);
  sanitize_input(acc->outbound_proxy);

  // Extended fields


  strncpy(acc->auth_user, lv_textarea_get_text(data->form_auth_user_ta),
          sizeof(acc->auth_user) - 1);
  sanitize_input(acc->auth_user);

  strncpy(acc->outbound_proxy2, lv_textarea_get_text(data->form_proxy2_ta),
          sizeof(acc->outbound_proxy2) - 1);
  sanitize_input(acc->outbound_proxy2);

  acc->reg_interval = atoi(lv_textarea_get_text(data->form_reg_int_ta));

  lv_dropdown_get_selected_str(data->form_media_enc_dd, acc->media_enc,
                               sizeof(acc->media_enc));
  lv_dropdown_get_selected_str(data->form_media_enc_dd, acc->media_enc,
                               sizeof(acc->media_enc));
  
  lv_dropdown_get_selected_str(data->form_transport_dd, acc->transport,
                               sizeof(acc->transport));
  
  acc->use_ice = lv_obj_has_state(data->form_ice_sw, LV_STATE_CHECKED);

  strncpy(acc->stun_server, lv_textarea_get_text(data->form_stun_ta),
          sizeof(acc->stun_server) - 1);
  sanitize_input(acc->stun_server);
  lv_dropdown_get_selected_str(data->form_dtmf_dd, acc->dtmf_mode,
                               sizeof(acc->dtmf_mode));
  lv_dropdown_get_selected_str(data->form_ans_dd, acc->answer_mode,
                               sizeof(acc->answer_mode));

  acc->rtcp_mux = lv_obj_has_state(data->form_rtcp_mux_sw, LV_STATE_CHECKED);
  acc->prack = lv_obj_has_state(data->form_prack_sw, LV_STATE_CHECKED);

  strncpy(acc->vm_uri, lv_textarea_get_text(data->form_vm_ta),
          sizeof(acc->vm_uri) - 1);
  sanitize_input(acc->vm_uri);

  acc->port = atoi(lv_textarea_get_text(data->form_port_ta));
  acc->enabled = lv_obj_has_state(data->form_active_sw, LV_STATE_CHECKED);

  save_settings(data);

  if (acc->enabled) {
    baresip_manager_add_account(acc);
  }

  show_account_settings(data);
  log_info("SettingsApplet", "Account saved: %s@%s", acc->username,
           acc->server);
}

static void default_account_changed(lv_event_t *e) {
  applet_t *applet = applet_manager_get_current();
  settings_data_t *data = (settings_data_t *)applet->user_data;
  lv_obj_t *dropdown = lv_event_get_target(e);

  int selected = lv_dropdown_get_selected(dropdown);
  if (selected >= data->account_count) {
    data->config.default_account_index = -1; // None
  } else {
    data->config.default_account_index = selected;
  }

  save_settings(data);
  log_debug("SettingsApplet", "Default account changed to index: %d",
            data->config.default_account_index);
}

// Screen builders
static void show_main_screen(settings_data_t *data) {
  lv_obj_clear_flag(data->main_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(data->account_settings_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(data->call_settings_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(data->account_form_screen, LV_OBJ_FLAG_HIDDEN);
}

static void show_account_settings(settings_data_t *data) {
  lv_obj_add_flag(data->main_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(data->account_settings_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(data->call_settings_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(data->account_form_screen, LV_OBJ_FLAG_HIDDEN);

  refresh_account_list(data);
}

static void update_account_dropdowns(settings_data_t *data) {
  char options[1024] = "";
  for (int i = 0; i < data->account_count; i++) {
    if (i > 0)
      strcat(options, "\n");
    char entry[256];
    snprintf(entry, sizeof(entry), "%s@%s", data->accounts[i].username,
             data->accounts[i].server);
    strcat(options, entry);
  }

  // Add "Always Ask" option
  if (data->account_count > 0)
    strcat(options, "\n");
  strcat(options, "Always Ask");

  if (strlen(options) == 0)
    strcpy(options, "Always Ask"); // Should at least have Always Ask

  if (data->default_account_dropdown)
    lv_dropdown_set_options(data->default_account_dropdown, options);

  // Preserve selection if possible, otherwise clamp
  if (data->codec_dropdown)
    lv_dropdown_set_selected(data->codec_dropdown,
                             data->config.preferred_codec);

  if (data->default_account_dropdown) {
    if (data->config.default_account_index >= 0 &&
        data->config.default_account_index < data->account_count) {
      lv_dropdown_set_selected(data->default_account_dropdown,
                               data->config.default_account_index);
    } else {
      // Select "None" (last item)
      lv_dropdown_set_selected(data->default_account_dropdown,
                               data->account_count);
    }
  }
}

static void refresh_account_list(settings_data_t *data) {
  lv_obj_clean(data->account_list);
  log_info("SettingsApplet", "Refreshing account list. Count: %d",
           data->account_count);

  for (int i = 0; i < data->account_count; i++) {
    voip_account_t *acc = &data->accounts[i];
    log_debug("SettingsApplet", "Rendering account %d: %s (User: %s)", i,
              acc->display_name, acc->username);

    lv_obj_t *item = lv_obj_create(data->account_list);
    lv_obj_set_size(item, LV_PCT(100), 70);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *info = lv_obj_create(item);
    lv_obj_set_size(info, LV_PCT(60), LV_PCT(90));
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(info, 0, 0);
    lv_obj_set_style_bg_opa(info, 0, 0);  // Transparent background
    lv_obj_set_style_pad_all(info, 0, 0); // remove padding to avoid clipping

    lv_obj_t *lbl = lv_label_create(info);
    char buf[128];
    // Title: username@server
    snprintf(buf, sizeof(buf), "%s@%s", acc->username, acc->server);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_black(), 0); // Black text

    // Subtitle: Display Name (if exists) -> no, user removed unnecessary info.
    // Let's just remove the sub-label if it was just repeating sip info.
    // Or maybe keep Display Name if it's different?
    // User said "remove OTHER unnecessary info".
    // I will show Display Name if set, otherwise nothing.
    if (strlen(acc->display_name) > 0) {
      lv_obj_t *sub = lv_label_create(info);
      lv_label_set_text(sub, acc->display_name);
      lv_obj_set_style_text_color(sub, lv_color_hex(0x808080), 0);
      lv_obj_set_style_text_font(sub, &lv_font_montserrat_16, 0);
    }

    // Buttons
    lv_obj_t *btns = lv_obj_create(item);
    lv_obj_set_size(btns, 100,
                    LV_PCT(100)); // Increased width to fit buttons comfortably
    lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_border_width(btns, 0, 0);
    lv_obj_set_style_bg_opa(btns, 0, 0); // Transparent
    lv_obj_set_style_pad_all(btns, 0, 0);
    lv_obj_set_flex_align(btns, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *edit_btn = lv_btn_create(btns);
    lv_obj_set_size(edit_btn, 40, 40);
    lv_obj_set_style_bg_opa(edit_btn, 0, 0);       // Transparent
    lv_obj_set_style_shadow_width(edit_btn, 0, 0); // No shadow
    lv_obj_t *edit_lbl = lv_label_create(edit_btn);
    lv_label_set_text(edit_lbl, LV_SYMBOL_EDIT);
    lv_obj_center(edit_lbl);
    lv_obj_set_style_text_font(edit_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(edit_lbl, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_add_event_cb(edit_btn, edit_account_clicked, LV_EVENT_CLICKED,
                        (void *)(intptr_t)i);

    lv_obj_t *del_btn = lv_btn_create(btns);
    lv_obj_set_size(del_btn, 40, 40);
    lv_obj_set_style_bg_opa(del_btn, 0, 0);       // Transparent
    lv_obj_set_style_shadow_width(del_btn, 0, 0); // No shadow
    lv_obj_set_style_pad_left(del_btn, 10, 0);
    lv_obj_t *del_lbl = lv_label_create(del_btn);
    lv_label_set_text(del_lbl, LV_SYMBOL_TRASH);
    lv_obj_center(del_lbl);
    lv_obj_set_style_text_font(del_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(del_lbl, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_add_event_cb(del_btn, delete_account_clicked, LV_EVENT_CLICKED,
                        (void *)(intptr_t)i);
  }
  update_account_dropdowns(data);
}

// UI Helper: Create a row with a label and a switch
static lv_obj_t *create_switch_row(lv_obj_t *parent, const char *label_text,
                                   bool checked) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), 50);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(row, 5, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_bg_opa(row, 0, 0);

  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, label_text);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);

  lv_obj_t *sw = lv_switch_create(row);
  if (checked)
    lv_obj_add_state(sw, LV_STATE_CHECKED);
  return sw;
}

// UI Helper: Create a row with a label and a dropdown
static lv_obj_t *create_dropdown_row(lv_obj_t *parent, const char *label_text,
                                     const char *options, int selected) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), 50);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(row, 5, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_bg_opa(row, 0, 0);

  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, label_text);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);

  lv_obj_t *dd = lv_dropdown_create(row);
  lv_dropdown_set_options(dd, options);
  lv_dropdown_set_selected(dd, selected);
  return dd;
}

static void save_settings_clicked(lv_event_t *e) {
  settings_data_t *data = (settings_data_t *)lv_event_get_user_data(e);
  if (data) {
    save_call_settings(data);
    show_main_screen(data);
  }
}

// Save Codec List from UI back to account struct
static void save_codec_priority(settings_data_t *data) {
  // Save priority back to temp_account string
  voip_account_t *acc = &data->temp_account;
  char *target_str =
      data->editing_audio_codecs ? acc->audio_codecs : acc->video_codecs;
  char buffer[256] = {0};

  // Iterate list items
  uint32_t cnt = lv_obj_get_child_cnt(data->codec_list);
  for (uint32_t i = 0; i < cnt; i++) {
    lv_obj_t *item = lv_obj_get_child(data->codec_list, i);
    // Item structure: [Checkbox] [Label] ...

    lv_obj_t *cb = lv_obj_get_child(item, 0); // Checkbox
    if (lv_obj_has_state(cb, LV_STATE_CHECKED)) {
      lv_obj_t *l = lv_obj_get_child(item, 1); // Label
      const char *txt = lv_label_get_text(l);

      if (strlen(buffer) > 0)
        strcat(buffer, ",");
      strcat(buffer, txt);
    }
  }

  strncpy(target_str, buffer, 255);
  target_str[255] = '\0'; // Ensure null termination
}

static void codec_back_clicked(lv_event_t *e) {
  settings_data_t *data = (settings_data_t *)lv_event_get_user_data(e);
  save_codec_priority(data);
  show_account_form(data);
}

static void codec_move_up(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target(e);
  lv_obj_t *item = lv_obj_get_parent(btn); // The row

  uint32_t idx = lv_obj_get_index(item);
  if (idx > 0) {
    lv_obj_move_to_index(item, idx - 1);
  }
}

static void codec_move_down(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target(e);
  lv_obj_t *item = lv_obj_get_parent(btn);

  uint32_t idx = lv_obj_get_index(item);
  if (idx < lv_obj_get_child_cnt(lv_obj_get_parent(item)) - 1) {
    lv_obj_move_to_index(item, idx + 1);
  }
}

static void show_codec_priority_screen(settings_data_t *data, bool is_audio) {
  data->editing_audio_codecs = is_audio;
  lv_obj_add_flag(data->account_form_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(data->codec_priority_screen, LV_OBJ_FLAG_HIDDEN);

  lv_obj_clean(data->codec_priority_screen);

  // Header
  lv_obj_t *header = ui_create_title_bar(data->codec_priority_screen, 
                                        is_audio ? "Audio Codecs" : "Video Codecs", 
                                        true, codec_back_clicked, data);
  lv_obj_t *dummy = lv_obj_create(header); // Spacer replaced by auto-layout or ignored
  lv_obj_set_size(dummy, 50, 40);
  lv_obj_set_style_bg_opa(dummy, 0, 0);
  lv_obj_set_style_border_width(dummy, 0, 0);

  // List
  data->codec_list = lv_obj_create(data->codec_priority_screen);
  lv_obj_set_size(data->codec_list, LV_PCT(100), LV_PCT(85));
  lv_obj_align(data->codec_list, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_flex_flow(data->codec_list, LV_FLEX_FLOW_COLUMN);

  voip_account_t *acc = &data->temp_account;
  const char *current_str = is_audio ? acc->audio_codecs : acc->video_codecs;

  // Logic:
  // 1. Add enabled codecs from current_str in order.
  // 2. Add remaining standard codecs (unchecked) at the end.

  // We need to keep track of added standard codecs
  bool added_std[32] = {false};
  const char **std_codecs =
      is_audio ? STANDARD_AUDIO_CODECS : STANDARD_VIDEO_CODECS;

  if (strlen(current_str) > 0) {
    char *dup = strdup(current_str);
    char *tok = strtok(dup, ",");
    while (tok) {
      // Find if this is a standard codec to mark it as added
      for (int i = 0; std_codecs[i]; i++) {
        if (strcmp(tok, std_codecs[i]) == 0) {
          added_std[i] = true;
          break;
        }
      }

      // Add Item (Checked)
      lv_obj_t *row = lv_obj_create(data->codec_list);
      lv_obj_set_size(row, LV_PCT(100), 50);
      lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);

      lv_obj_t *cb = lv_checkbox_create(row);
      lv_checkbox_set_text(cb, "");
      lv_obj_add_state(cb, LV_STATE_CHECKED);

      lv_obj_t *l = lv_label_create(row);
      lv_label_set_text(l, tok);
      lv_obj_set_flex_grow(l, 1);

      lv_obj_t *up = lv_btn_create(row);
      lv_obj_set_size(up, 30, 30);
      lv_label_set_text(lv_label_create(up), LV_SYMBOL_UP);
      lv_obj_add_event_cb(up, codec_move_up, LV_EVENT_CLICKED, NULL);

      lv_obj_t *down = lv_btn_create(row);
      lv_obj_set_size(down, 30, 30);
      lv_label_set_text(lv_label_create(down), LV_SYMBOL_DOWN);
      lv_obj_add_event_cb(down, codec_move_down, LV_EVENT_CLICKED, NULL);

      tok = strtok(NULL, ",");
    }
    free(dup);
  }

  // Add remaining standard codecs
  for (int i = 0; std_codecs[i]; i++) {
    if (!added_std[i]) {
      lv_obj_t *row = lv_obj_create(data->codec_list);
      lv_obj_set_size(row, LV_PCT(100), 50);
      lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);

      lv_obj_t *cb = lv_checkbox_create(row);
      lv_checkbox_set_text(cb, "");
      // Unchecked by default

      lv_obj_t *l = lv_label_create(row);
      lv_label_set_text(l, std_codecs[i]);
      lv_obj_set_flex_grow(l, 1);

      lv_obj_t *up = lv_btn_create(row);
      lv_obj_set_size(up, 30, 30);
      lv_label_set_text(lv_label_create(up), LV_SYMBOL_UP);
      lv_obj_add_event_cb(up, codec_move_up, LV_EVENT_CLICKED, NULL);

      lv_obj_t *down = lv_btn_create(row);
      lv_obj_set_size(down, 30, 30);
      lv_label_set_text(lv_label_create(down), LV_SYMBOL_DOWN);
      lv_obj_add_event_cb(down, codec_move_down, LV_EVENT_CLICKED, NULL);
    }
  }
}

static void capture_form_data(settings_data_t *data) {
  voip_account_t *acc = &data->temp_account;


  strncpy(acc->display_name, lv_textarea_get_text(data->form_name_ta),
          sizeof(acc->display_name) - 1);
  strncpy(acc->auth_user, lv_textarea_get_text(data->form_auth_user_ta),
          sizeof(acc->auth_user) - 1);
  strncpy(acc->password, lv_textarea_get_text(data->form_pass_ta),
          sizeof(acc->password) - 1);
  strncpy(acc->server, lv_textarea_get_text(data->form_server_ta),
          sizeof(acc->server) - 1);
  strncpy(acc->username, lv_textarea_get_text(data->form_user_ta),
          sizeof(acc->username) - 1);

  const char *port_str = lv_textarea_get_text(data->form_port_ta);
  acc->port = atoi(port_str);

  strncpy(acc->outbound_proxy, lv_textarea_get_text(data->form_proxy_ta),
          sizeof(acc->outbound_proxy) - 1);
  strncpy(acc->outbound_proxy2, lv_textarea_get_text(data->form_proxy2_ta),
          sizeof(acc->outbound_proxy2) - 1);

  acc->enabled = lv_obj_has_state(data->form_active_sw, LV_STATE_CHECKED);

  const char *reg_int_str = lv_textarea_get_text(data->form_reg_int_ta);
  acc->reg_interval = atoi(reg_int_str);

  // Note: Dropdowns logic for capture
  char buf[64];
  lv_dropdown_get_selected_str(data->form_media_enc_dd, buf, sizeof(buf));
  snprintf(acc->media_enc, sizeof(acc->media_enc), "%.*s", (int)sizeof(acc->media_enc)-1, buf);

  lv_dropdown_get_selected_str(data->form_media_enc_dd, buf, sizeof(buf));
  snprintf(acc->media_enc, sizeof(acc->media_enc), "%.*s", (int)sizeof(acc->media_enc)-1, buf);

  lv_dropdown_get_selected_str(data->form_transport_dd, buf, sizeof(buf));
  // Convert to lowercase just in case
  for(int i=0; buf[i]; i++) buf[i] = tolower(buf[i]);
  snprintf(acc->transport, sizeof(acc->transport), "%.*s", (int)sizeof(acc->transport)-1, buf);

  acc->use_ice = lv_obj_has_state(data->form_ice_sw, LV_STATE_CHECKED);

  snprintf(acc->stun_server, sizeof(acc->stun_server), "%.*s", (int)sizeof(acc->stun_server)-1, lv_textarea_get_text(data->form_stun_ta));

  acc->rtcp_mux = lv_obj_has_state(data->form_rtcp_mux_sw, LV_STATE_CHECKED);
  acc->prack = lv_obj_has_state(data->form_prack_sw, LV_STATE_CHECKED);

  lv_dropdown_get_selected_str(data->form_dtmf_dd, buf, sizeof(buf));
  snprintf(acc->dtmf_mode, sizeof(acc->dtmf_mode), "%.*s", (int)sizeof(acc->dtmf_mode)-1, buf);

  lv_dropdown_get_selected_str(data->form_ans_dd, buf, sizeof(buf));
  snprintf(acc->answer_mode, sizeof(acc->answer_mode), "%.*s", (int)sizeof(acc->answer_mode)-1, buf);

  snprintf(acc->vm_uri, sizeof(acc->vm_uri), "%.*s", (int)sizeof(acc->vm_uri)-1, lv_textarea_get_text(data->form_vm_ta));
}

static void open_audio_codecs_clicked(lv_event_t *e) {
  settings_data_t *data = (settings_data_t *)lv_event_get_user_data(e);
  capture_form_data(data);
  show_codec_priority_screen(data, true);
}

static void open_video_codecs_clicked(lv_event_t *e) {
  settings_data_t *data = (settings_data_t *)lv_event_get_user_data(e);
  capture_form_data(data);
  show_codec_priority_screen(data, false);
}

static void account_settings_clicked(lv_event_t *e) {
  (void)e;
  applet_t *applet = applet_manager_get_current();
  settings_data_t *data = (settings_data_t *)applet->user_data;
  show_account_settings(data);
}

static void show_call_settings(settings_data_t *data) {
  lv_obj_add_flag(data->main_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(data->account_settings_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(data->call_settings_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(data->account_form_screen, LV_OBJ_FLAG_HIDDEN);

  lv_obj_clean(data->call_settings_screen);

  // Header
  // Header
  lv_obj_t *header = ui_create_title_bar(data->call_settings_screen, "System Settings", true, back_to_main, data);
  ui_header_add_action_btn(header, LV_SYMBOL_OK, save_settings_clicked, data);

  // Content Container (Scrollable)
  lv_obj_t *content = lv_obj_create(data->call_settings_screen);
  lv_obj_set_size(content, LV_PCT(100), LV_PCT(85)); // Leave space for header
  lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(content, 10, 0);
  lv_obj_set_style_pad_row(content, 10, 0); // Spacing between rows

  // Form Fields
  data->call_start_auto_sw = create_switch_row(
      content, "Start Automatically", data->config.start_automatically);

  data->call_show_fav_sw = create_switch_row(
      content, "Favorite Contact Show", data->config.show_favorites);

  lv_obj_t *listen_ta = lv_textarea_create(content);
  lv_obj_set_width(listen_ta, LV_PCT(100));
  lv_textarea_set_one_line(listen_ta, true);
  lv_obj_set_scrollbar_mode(listen_ta, LV_SCROLLBAR_MODE_OFF);
  lv_textarea_set_placeholder_text(listen_ta, "Listen Address (0.0.0.0:5060)");
  if (strlen(data->config.listen_address) > 0)
    lv_textarea_set_text(listen_ta, data->config.listen_address);
  data->call_listen_addr_ta = listen_ta;

  data->call_addr_fam_dd =
      create_dropdown_row(content, "Address Family", "IPv4\nIPv6",
                          data->config.address_family == 2 ? 1 : 0);

  lv_obj_t *dns_ta = lv_textarea_create(content);
  lv_obj_set_width(dns_ta, LV_PCT(100));
  lv_textarea_set_one_line(dns_ta, true);
  lv_obj_set_scrollbar_mode(dns_ta, LV_SCROLLBAR_MODE_OFF);
  lv_textarea_set_placeholder_text(dns_ta, "DNS Servers");
  if (strlen(data->config.dns_servers) > 0)
    lv_textarea_set_text(dns_ta, data->config.dns_servers);
  data->call_dns_ta = dns_ta;

  data->call_video_size_dd = create_dropdown_row(
      content, "Video Frame Size", "1920x1080\n1280x720\n640x480\n320x240",
      data->config.video_frame_size);

  // Log Level
  data->call_log_level_dd = create_dropdown_row(
      content, "Log Level", "TRACE\nDEBUG\nINFO\nWARN\nERROR\nFATAL",
      data->config.log_level);
}

static void save_call_settings(settings_data_t *data) {
  if (!data)
    return;

  data->config.start_automatically =
      lv_obj_has_state(data->call_start_auto_sw, LV_STATE_CHECKED);

  data->config.show_favorites =
      lv_obj_has_state(data->call_show_fav_sw, LV_STATE_CHECKED);

  data->config.log_level = lv_dropdown_get_selected(data->call_log_level_dd);
  logger_set_level((log_level_t)data->config.log_level);
  baresip_manager_set_log_level((log_level_t)data->config.log_level);

  const char *addr_input = lv_textarea_get_text(data->call_listen_addr_ta);
  char validated_addr[64];
  if (strchr(addr_input, ':')) {
    // Has port, copy as is
    strncpy(validated_addr, addr_input, sizeof(validated_addr) - 1);
  } else if (strlen(addr_input) > 0) {
    // No port, append :5060
    snprintf(validated_addr, sizeof(validated_addr), "%s:5060", addr_input);
  } else {
    // Empty, default
    strcpy(validated_addr, "0.0.0.0:5060");
  }
  validated_addr[sizeof(validated_addr) - 1] = '\0';
  snprintf(data->config.listen_address, sizeof(data->config.listen_address), "%.*s", (int)sizeof(data->config.listen_address)-1, validated_addr);

  int fam_idx = lv_dropdown_get_selected(data->call_addr_fam_dd);
  data->config.address_family =
      (fam_idx == 0) ? 1 : 2; // 0->IPv4(1), 1->IPv6(2)

  // Basic DNS validation (ensure not empty / comma format check if needed)
  snprintf(data->config.dns_servers, sizeof(data->config.dns_servers), "%.*s", (int)sizeof(data->config.dns_servers)-1, lv_textarea_get_text(data->call_dns_ta));

  data->config.video_frame_size =
      lv_dropdown_get_selected(data->call_video_size_dd);

  // Save Default Account
  if (data->default_account_dropdown) {
    int sel_idx = lv_dropdown_get_selected(data->default_account_dropdown);
    if (sel_idx >= data->account_count) {
      data->config.default_account_index = -1; // "None" selected
    } else {
      data->config.default_account_index = sel_idx;
    }
  }

  config_save_app_settings(&data->config);
}

static void init_account_edit(settings_data_t *data, int index) {
  data->editing_account_index = index;
  if (index >= 0 && index < data->account_count) {
    data->temp_account = data->accounts[index];
  } else {
    // defaults
    memset(&data->temp_account, 0, sizeof(voip_account_t));
    data->temp_account.port = 5060;
    data->temp_account.reg_interval = 900;
    strcpy(data->temp_account.dtmf_mode, "rtp");
    strcpy(data->temp_account.answer_mode, "manual");
    strcpy(data->temp_account.audio_codecs,
           "opus/48000/2,PCMU/8000/1,PCMA/8000/1,G722/16000/1");
    strcpy(data->temp_account.video_codecs, "H264");
    strcpy(data->temp_account.transport, "udp");
    data->temp_account.use_ice = false;
    data->temp_account.enabled = true; // Default to enabled
  }
}

static void show_account_form(settings_data_t *data) {
  lv_obj_add_flag(data->main_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(data->account_settings_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(data->call_settings_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(data->codec_priority_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(data->account_form_screen, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *content = data->account_form_content;
  lv_obj_clean(content);

  // Use temp_account
  voip_account_t *acc = &data->temp_account;

  // SIP URI Display
  lv_obj_t *uri_lbl = lv_label_create(content);
  lv_label_set_text(uri_lbl, "SIP URI");
  lv_obj_set_style_text_font(uri_lbl, &lv_font_montserrat_16, 0);

  lv_obj_t *uri_ta = lv_textarea_create(content);
  lv_obj_set_width(uri_ta, LV_PCT(95));
  lv_textarea_set_one_line(uri_ta, true);
  lv_obj_add_state(uri_ta, LV_STATE_DISABLED);

  char buf[256];
  if (data->editing_account_index >= 0) {
    snprintf(buf, sizeof(buf), "sip:%s@%s", acc->username, acc->server);
  } else {
    strcpy(buf, "sip:user@domain");
  }
  lv_textarea_set_text(uri_ta, buf);

// Helper macro to add labeled TA
#define ADD_TA(label_txt, ta_ptr, placeholder)                                 \
  do {                                                                         \
    lv_label_create(content);                                                  \
    lv_label_set_text(lv_obj_get_child(content, -1), label_txt);               \
    ta_ptr = lv_textarea_create(content);                                      \
    lv_obj_set_width(ta_ptr, LV_PCT(95));                                      \
    lv_textarea_set_one_line(ta_ptr, true);                                    \
    if (placeholder)                                                           \
      lv_textarea_set_placeholder_text(ta_ptr, placeholder);                   \
  } while (0)

  ADD_TA("SIP Username", data->form_user_ta, "user123");
  ADD_TA("Display Name", data->form_name_ta, "John Doe");
  ADD_TA("Authentication Username", data->form_auth_user_ta, "user123");

  ADD_TA("Authentication Password", data->form_pass_ta, "secret");
  lv_textarea_set_password_mode(data->form_pass_ta, true);

  ADD_TA("SIP Domain (Server)", data->form_server_ta, "sip.example.com");
  ADD_TA("Port", data->form_port_ta, "5060");

  lv_label_create(content);
  lv_label_set_text(lv_obj_get_child(content, -1), "Outbound Proxies");

  ADD_TA("SIP URI of Proxy Server", data->form_proxy_ta,
         "sip:proxy.example.com");
  ADD_TA("SIP URI of another Proxy Server", data->form_proxy2_ta,
         "sip:backup.example.com");

  // Register Switch
  lv_obj_t *reg_cont = lv_obj_create(content);
  lv_obj_set_size(reg_cont, LV_PCT(95), 50);
  lv_obj_clear_flag(reg_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(reg_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(reg_cont, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *lbl = lv_label_create(reg_cont);
  lv_label_set_text(lbl, "Register");
  data->form_active_sw = lv_switch_create(reg_cont);

  ADD_TA("Registration Interval", data->form_reg_int_ta, "900");

  lv_label_create(content);
  lv_label_set_text(lv_obj_get_child(content, -1), "Media Encryption");
  data->form_media_enc_dd = lv_dropdown_create(content);
  lv_dropdown_set_options(data->form_media_enc_dd, "none\nsrtp\nzrtp");
  lv_obj_set_width(data->form_media_enc_dd, LV_PCT(95));

  lv_label_create(content);
  lv_label_set_text(lv_obj_get_child(content, -1), "Transport Protocol");
  data->form_transport_dd = lv_dropdown_create(content);
  lv_dropdown_set_options(data->form_transport_dd, "UDP\nTCP\nTLS");
  lv_obj_set_width(data->form_transport_dd, LV_PCT(95));

  // ICE Switch
  lv_obj_t *ice_cont = lv_obj_create(content);
  lv_obj_set_size(ice_cont, LV_PCT(95), 50);
  lv_obj_clear_flag(ice_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(ice_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ice_cont, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lbl = lv_label_create(ice_cont);
  lv_label_set_text(lbl, "Use ICE");
  data->form_ice_sw = lv_switch_create(ice_cont);

  ADD_TA("STUN/TURN Server (Optional)", data->form_stun_ta, "stun.fs.com:3478");

  // RTCP Mux
  lv_obj_t *rtcp_cont = lv_obj_create(content);
  lv_obj_set_size(rtcp_cont, LV_PCT(95), 50);
  lv_obj_clear_flag(rtcp_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(rtcp_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(rtcp_cont, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lbl = lv_label_create(rtcp_cont);
  lv_label_set_text(lbl, "RTCP Multiplexing");
  data->form_rtcp_mux_sw = lv_switch_create(rtcp_cont);

  // Prack
  lv_obj_t *prack_cont = lv_obj_create(content);
  lv_obj_set_size(prack_cont, LV_PCT(95), 50);
  lv_obj_clear_flag(prack_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(prack_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(prack_cont, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lbl = lv_label_create(prack_cont);
  lv_label_set_text(lbl, "Reliable Provisional Responses");
  data->form_prack_sw = lv_switch_create(prack_cont);

  lv_label_create(content);
  lv_label_set_text(lv_obj_get_child(content, -1), "DTMF Mode");
  data->form_dtmf_dd = lv_dropdown_create(content);
  lv_dropdown_set_options(data->form_dtmf_dd, "rtp\nsdp\ninfo\nauto");
  lv_obj_set_width(data->form_dtmf_dd, LV_PCT(95));

  lv_label_create(content);
  lv_label_set_text(lv_obj_get_child(content, -1), "Answer Mode");
  data->form_ans_dd = lv_dropdown_create(content);
  lv_dropdown_set_options(data->form_ans_dd, "manual\nauto");
  lv_obj_set_width(data->form_ans_dd, LV_PCT(95));

  ADD_TA("Voicemail URI", data->form_vm_ta, "");

  // Realm (Hidden)
  data->form_realm_ta = lv_textarea_create(content);
  lv_obj_add_flag(data->form_realm_ta, LV_OBJ_FLAG_HIDDEN);

  // Codecs
  lv_obj_t *codec_label = lv_label_create(content);
  lv_label_set_text(codec_label, "Codecs Priority:");
  lv_obj_set_style_pad_top(codec_label, 10, 0);

  lv_obj_t *audio_btn = lv_btn_create(content);
  lv_obj_set_width(audio_btn, LV_PCT(95));
  lv_label_set_text(lv_label_create(audio_btn), "Audio Codecs >");
  lv_obj_add_event_cb(audio_btn, open_audio_codecs_clicked, LV_EVENT_CLICKED,
                      data);

  lv_obj_t *video_btn = lv_btn_create(content);
  lv_obj_set_width(video_btn, LV_PCT(95));
  lv_label_set_text(lv_label_create(video_btn), "Video Codecs >");
  lv_obj_add_event_cb(video_btn, open_video_codecs_clicked, LV_EVENT_CLICKED,
                      data);

  // Pre-populate logic (Always from temp_account)

  lv_textarea_set_text(data->form_name_ta, acc->display_name);
  lv_textarea_set_text(data->form_auth_user_ta, acc->auth_user);
  lv_textarea_set_text(data->form_pass_ta, acc->password);
  lv_textarea_set_text(data->form_server_ta, acc->server);
  lv_textarea_set_text(data->form_user_ta, acc->username);
  char pbuf[32];
  snprintf(pbuf, sizeof(pbuf), "%d", acc->port);
  lv_textarea_set_text(data->form_port_ta, pbuf);
  lv_textarea_set_text(data->form_proxy_ta, acc->outbound_proxy);
  lv_textarea_set_text(data->form_proxy2_ta, acc->outbound_proxy2);
  if (acc->enabled)
    lv_obj_add_state(data->form_active_sw, LV_STATE_CHECKED);

  snprintf(pbuf, sizeof(pbuf), "%d", acc->reg_interval);
  lv_textarea_set_text(data->form_reg_int_ta, pbuf);
  
  // Set Transport Selection (Case insensitive match or default to 0)
  if (strcasecmp(acc->transport, "tcp") == 0) lv_dropdown_set_selected(data->form_transport_dd, 1);
  else if (strcasecmp(acc->transport, "tls") == 0) lv_dropdown_set_selected(data->form_transport_dd, 2);
  else lv_dropdown_set_selected(data->form_transport_dd, 0); // UDP

  if (acc->use_ice) lv_obj_add_state(data->form_ice_sw, LV_STATE_CHECKED);
  else lv_obj_clear_state(data->form_ice_sw, LV_STATE_CHECKED);

  lv_textarea_set_text(data->form_stun_ta, acc->stun_server);
}

static int settings_init(applet_t *applet) {
  log_info("SettingsApplet", "Initializing");
  settings_data_t *data = lv_mem_alloc(sizeof(settings_data_t));
  if (!data)
    return -1;
  memset(data, 0, sizeof(settings_data_t));
  applet->user_data = data;

  config_manager_init();
  load_settings(data);

  // Create Screens
  lv_obj_add_event_cb(applet->screen, handle_swipe_back, LV_EVENT_GESTURE, NULL);
  data->main_screen = lv_obj_create(applet->screen);
  data->account_settings_screen = lv_obj_create(applet->screen);
  data->call_settings_screen = lv_obj_create(applet->screen);
  data->account_form_screen = lv_obj_create(applet->screen);

  lv_obj_set_size(data->main_screen, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(data->main_screen, 0, 0);
  lv_obj_set_style_border_width(data->main_screen, 0, 0);
  lv_obj_set_style_radius(data->main_screen, 0, 0);

  lv_obj_set_size(data->account_settings_screen, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(data->account_settings_screen, 0, 0);
  lv_obj_set_style_border_width(data->account_settings_screen, 0, 0);
  lv_obj_set_style_radius(data->account_settings_screen, 0, 0);

  lv_obj_set_size(data->call_settings_screen, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(data->call_settings_screen, 0, 0);
  lv_obj_set_style_border_width(data->call_settings_screen, 0, 0);
  lv_obj_set_style_radius(data->call_settings_screen, 0, 0);

  lv_obj_set_size(data->account_form_screen, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(data->account_form_screen, 0, 0);
  lv_obj_set_style_border_width(data->account_form_screen, 0, 0);
  lv_obj_set_style_radius(data->account_form_screen, 0, 0);

  data->codec_priority_screen = lv_obj_create(applet->screen);
  lv_obj_set_size(data->codec_priority_screen, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(data->codec_priority_screen, 0, 0);
  lv_obj_set_style_border_width(data->codec_priority_screen, 0, 0);
  lv_obj_set_style_radius(data->codec_priority_screen, 0, 0);
  lv_obj_add_flag(data->codec_priority_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(data->codec_priority_screen, LV_OBJ_FLAG_HIDDEN);

  lv_obj_add_flag(data->account_settings_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(data->call_settings_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(data->account_form_screen, LV_OBJ_FLAG_HIDDEN);

  // --- MAIN SCREEN ---
  lv_obj_add_flag(data->main_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(data->account_settings_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(data->call_settings_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(data->account_form_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);

  ui_create_title_bar(data->main_screen, "Settings", true, back_btn_clicked, NULL);

  lv_obj_t *main_list = lv_list_create(data->main_screen);
  lv_obj_set_size(main_list, LV_PCT(90), LV_PCT(80));
  lv_obj_align(main_list, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_add_flag(main_list, LV_OBJ_FLAG_GESTURE_BUBBLE);

  lv_obj_t *btn_call =
      lv_list_add_btn(main_list, LV_SYMBOL_SETTINGS, "System Settings");
  lv_obj_add_event_cb(btn_call, call_settings_clicked, LV_EVENT_CLICKED, NULL);

  lv_obj_t *btn_acc =
      lv_list_add_btn(main_list, LV_SYMBOL_LIST, "Account Settings");
  lv_obj_add_event_cb(btn_acc, account_settings_clicked, LV_EVENT_CLICKED,
                      NULL);

  // --- ACCOUNT SETTINGS SCREEN (ACCOUNTS LIST) ---
  // --- ACCOUNT SETTINGS SCREEN (ACCOUNTS LIST) ---
  lv_obj_t *call_header = ui_create_title_bar(data->account_settings_screen, "Account Settings", true, back_to_main, NULL);
  ui_header_add_action_btn(call_header, LV_SYMBOL_PLUS, add_account_btn_clicked, NULL);

  lv_obj_t *call_content = lv_obj_create(data->account_settings_screen);
  lv_obj_set_size(call_content, LV_PCT(95), LV_PCT(80));
  lv_obj_align(call_content, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_flex_flow(call_content, LV_FLEX_FLOW_COLUMN);

  // Default Account
  lv_obj_t *def_acc_label = lv_label_create(call_content);
  lv_label_set_text(def_acc_label, "Default Account:");
  lv_obj_set_style_pad_top(def_acc_label, 10, 0);
  data->default_account_dropdown = lv_dropdown_create(call_content);
  lv_obj_set_width(data->default_account_dropdown, LV_PCT(90));
  lv_obj_add_event_cb(data->default_account_dropdown, default_account_changed,
                      LV_EVENT_VALUE_CHANGED, NULL);

  // Account List Header
  lv_obj_t *acc_list_label = lv_label_create(call_content);
  lv_label_set_text(acc_list_label, "Defined Accounts:");
  lv_obj_set_style_pad_top(acc_list_label, 20, 0);

  // Account List Container
  data->account_list = lv_obj_create(call_content);
  lv_obj_set_size(data->account_list, LV_PCT(95), 200);
  lv_obj_set_flex_flow(data->account_list, LV_FLEX_FLOW_COLUMN);

  // --- ACCOUNT FORM SCREEN ---
  lv_obj_t *form_header = ui_create_title_bar(data->account_form_screen, "Account", true, account_settings_clicked, NULL);
  // Note: ORIGINAL used form_save_btn (green). Helper makes transparent.
  // We'll stick to helper for consistency, or we could set style on the returned button?
  // User asked for "common widget". Common implies same style.
  // So we accept the transparent/white icon style.
  ui_header_add_action_btn(form_header, LV_SYMBOL_SAVE, save_account_clicked, NULL);

  data->account_form_content = lv_obj_create(data->account_form_screen);
  lv_obj_set_size(data->account_form_content, LV_PCT(95), LV_PCT(80));
  lv_obj_align(data->account_form_content, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_flex_flow(data->account_form_content, LV_FLEX_FLOW_COLUMN);

  return 0;
}

// Enum definitions moved to top of file

void settings_open_accounts(void) {
  target_screen = SETTINGS_SCREEN_ACCOUNTS;
  nav_source = NAV_SOURCE_DEEP_LINK;
}

void settings_open_call_settings(void) {
  target_screen = SETTINGS_SCREEN_CALL;
  nav_source = NAV_SOURCE_DEEP_LINK;
}

static void apply_launch_request(settings_data_t *data) {
    if (nav_source == NAV_SOURCE_DEEP_LINK) {
        data->is_deep_linked = true;
        if (target_screen == SETTINGS_SCREEN_ACCOUNTS) show_account_settings(data);
        else if (target_screen == SETTINGS_SCREEN_CALL) show_call_settings(data);
        else show_main_screen(data);
        
        // consume the request
        nav_source = NAV_SOURCE_MAIN;
        target_screen = SETTINGS_SCREEN_MAIN;
    } else {
        // Normal Launch or Resume -> Force Main Screen
        data->is_deep_linked = false;
        show_main_screen(data);
    }
}

static void settings_start(applet_t *applet) {
  log_info("SettingsApplet", "Started");
  settings_data_t *data = (settings_data_t *)applet->user_data;
  apply_launch_request(data);
}

static void settings_pause(applet_t *applet) {
  (void)applet;
  log_info("SettingsApplet", "Paused");
}

static void settings_resume(applet_t *applet) {
  log_info("SettingsApplet", "Resumed");
  settings_data_t *data = (settings_data_t *)applet->user_data;
  apply_launch_request(data);
}

static void settings_stop(applet_t *applet) {
  (void)applet;
  log_info("SettingsApplet", "Stopped");
}

static void settings_destroy(applet_t *applet) {
  printf("[SettingsApplet] Destroying\n");
  if (applet->user_data) {
    lv_mem_free(applet->user_data);
    applet->user_data = NULL;
  }
}

// Define the settings applet
APPLET_DEFINE(settings_applet, "Settings", "Application Settings",
              LV_SYMBOL_SETTINGS);

// Initialize callbacks
void settings_applet_register(void) {
  settings_applet.callbacks.init = settings_init;
  settings_applet.callbacks.start = settings_start;
  settings_applet.callbacks.pause = settings_pause;
  settings_applet.callbacks.resume = settings_resume;
  settings_applet.callbacks.stop = settings_stop;
  settings_applet.callbacks.destroy = settings_destroy;

  applet_manager_register(&settings_applet);
}
