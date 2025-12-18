#include "applet.h"
#include "applet_manager.h"
#include "baresip_manager.h"
#include "config_manager.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
  lv_obj_t *call_log_level_dd;
  lv_obj_t *call_listen_addr_ta;
  lv_obj_t *call_addr_fam_dd;
  lv_obj_t *call_dns_ta;
  lv_obj_t *call_tls_cert_sw;
  lv_obj_t *call_verify_cert_sw;
  lv_obj_t *call_tls_ca_sw;
  lv_obj_t *call_ua_ta;
  lv_obj_t *call_contacts_dd;
  lv_obj_t *call_video_size_dd;
  lv_obj_t *call_ringtone_btn;

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
  lv_obj_t *form_media_enc_dd;
  lv_obj_t *form_media_nat_dd;

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

// Helper: Check if codec is in the comma-separated list
static bool is_codec_enabled(const char *list, const char *codec) {
  if (!list || !codec)
    return false;
  char *dup = strdup(list);
  char *saveptr;
  char *tok = strtok_r(dup, ",", &saveptr);
  bool found = false;
  while (tok) {
    if (strcmp(tok, codec) == 0) {
      found = true;
      break;
    }
    tok = strtok_r(NULL, ",", &saveptr);
  }
  free(dup);
  return found;
}

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

// Helper to sanitize input (replace | with _)
static void sanitize_input(char *str) {
  for (int i = 0; str[i]; i++) {
    if (str[i] == '|')
      str[i] = '_';
  }
}

// Event handler for back button
static void back_btn_clicked(lv_event_t *e) { applet_manager_back(); }

static void back_to_main(lv_event_t *e) {
  applet_t *applet = applet_manager_get_current();
  settings_data_t *data = (settings_data_t *)applet->user_data;

  if (nav_source == NAV_SOURCE_DEEP_LINK) {
    // Consume the deep link state and go back to previous applet
    target_screen = SETTINGS_SCREEN_MAIN;
    nav_source = NAV_SOURCE_MAIN;
    show_main_screen(data); // Reset UI to main so next entry is clean
    applet_manager_back();
  } else {
    // Normal navigation within Settings
    show_main_screen(data);
  }
}

// Event handler for settings items
static void setting_item_clicked(lv_event_t *e) {
  const char *text = lv_event_get_user_data(e);
  log_info("SettingsApplet", "Clicked: %s", text);
}

static void call_settings_clicked(lv_event_t *e) {
  applet_t *applet = applet_manager_get_current();
  settings_data_t *data = (settings_data_t *)applet->user_data;
  show_call_settings(data);
}

static void add_account_btn_clicked(lv_event_t *e) {
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
  strncpy(acc->nickname, lv_textarea_get_text(data->form_nick_ta),
          sizeof(acc->nickname) - 1);
  sanitize_input(acc->nickname);

  strncpy(acc->auth_user, lv_textarea_get_text(data->form_auth_user_ta),
          sizeof(acc->auth_user) - 1);
  sanitize_input(acc->auth_user);

  strncpy(acc->outbound_proxy2, lv_textarea_get_text(data->form_proxy2_ta),
          sizeof(acc->outbound_proxy2) - 1);
  sanitize_input(acc->outbound_proxy2);

  acc->reg_interval = atoi(lv_textarea_get_text(data->form_reg_int_ta));

  lv_dropdown_get_selected_str(data->form_media_enc_dd, acc->media_enc,
                               sizeof(acc->media_enc));
  lv_dropdown_get_selected_str(data->form_media_nat_dd, acc->media_nat,
                               sizeof(acc->media_nat));
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

static void codec_changed(lv_event_t *e) {
  applet_t *applet = applet_manager_get_current();
  settings_data_t *data = (settings_data_t *)applet->user_data;
  lv_obj_t *dropdown = lv_event_get_target(e);

  data->config.preferred_codec = lv_dropdown_get_selected(dropdown);
  save_settings(data);

  log_debug("SettingsApplet", "Codec changed to: %s",
            config_get_codec_name(data->config.preferred_codec));
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

// UI Helper: Create a row with a text area
static lv_obj_t *create_text_row(lv_obj_t *parent, const char *placeholder,
                                 const char *value) {
  lv_obj_t *ta = lv_textarea_create(parent);
  lv_obj_set_width(ta, LV_PCT(100));
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_placeholder_text(ta, placeholder);
  if (value)
    lv_textarea_set_text(ta, value);
  // Margin removed, handled by parent spacing
  return ta;
}

// UI Helper: Create a row with a label and a dropdown
static lv_obj_t *create_dropdown_row(lv_obj_t *parent, const char *label_text,
                                     const char *options, int selected) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), 50);
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
  lv_obj_t *header = lv_obj_create(data->codec_priority_screen);
  lv_obj_set_size(header, LV_PCT(100), 60);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *back = lv_btn_create(header);
  lv_obj_set_size(back, 50, 40);
  lv_label_set_text(lv_label_create(back), LV_SYMBOL_LEFT);
  lv_obj_add_event_cb(back, codec_back_clicked, LV_EVENT_CLICKED, data);

  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text(title, is_audio ? "Audio Codecs" : "Video Codecs");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16,
                             0); // Reduced font size to fit

  lv_obj_t *dummy = lv_obj_create(header); // Spacer
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

  strncpy(acc->nickname, lv_textarea_get_text(data->form_nick_ta),
          sizeof(acc->nickname) - 1);
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
  strncpy(acc->media_enc, buf, sizeof(acc->media_enc) - 1);

  lv_dropdown_get_selected_str(data->form_media_nat_dd, buf, sizeof(buf));
  strncpy(acc->media_nat, buf, sizeof(acc->media_nat) - 1);

  acc->rtcp_mux = lv_obj_has_state(data->form_rtcp_mux_sw, LV_STATE_CHECKED);
  acc->prack = lv_obj_has_state(data->form_prack_sw, LV_STATE_CHECKED);

  lv_dropdown_get_selected_str(data->form_dtmf_dd, buf, sizeof(buf));
  strncpy(acc->dtmf_mode, buf, sizeof(acc->dtmf_mode) - 1);

  lv_dropdown_get_selected_str(data->form_ans_dd, buf, sizeof(buf));
  strncpy(acc->answer_mode, buf, sizeof(acc->answer_mode) - 1);

  strncpy(acc->vm_uri, lv_textarea_get_text(data->form_vm_ta),
          sizeof(acc->vm_uri) - 1);
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
  lv_obj_t *header = lv_obj_create(data->call_settings_screen);
  lv_obj_set_size(header, LV_PCT(100), 60);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *back_btn = lv_btn_create(header);
  lv_obj_set_size(back_btn, 50, 40);
  lv_obj_t *back_label = lv_label_create(back_btn);
  lv_label_set_text(back_label, LV_SYMBOL_LEFT);
  lv_obj_center(back_label);
  lv_obj_add_event_cb(back_btn, back_to_main, LV_EVENT_CLICKED, data);

  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text(title, "System Settings");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);

  lv_obj_t *save_btn = lv_btn_create(header);
  lv_obj_set_size(save_btn, 50, 40);
  lv_obj_t *save_label = lv_label_create(save_btn);
  lv_label_set_text(save_label, LV_SYMBOL_OK);
  lv_obj_center(save_label);
  lv_obj_add_event_cb(save_btn, save_settings_clicked, LV_EVENT_CLICKED, data);

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

  // Log Level
  data->call_log_level_dd = create_dropdown_row(
      content, "Log Level", "TRACE\nDEBUG\nINFO\nWARN\nERROR\nFATAL",
      data->config.log_level);

  lv_obj_t *listen_ta = lv_textarea_create(content);
  lv_obj_set_width(listen_ta, LV_PCT(100));
  lv_textarea_set_one_line(listen_ta, true);
  lv_textarea_set_placeholder_text(listen_ta, "Listen Address");
  if (strlen(data->config.listen_address) > 0)
    lv_textarea_set_text(listen_ta, data->config.listen_address);
  data->call_listen_addr_ta = listen_ta;

  data->call_addr_fam_dd =
      create_dropdown_row(content, "Address Family", "IPv4\nIPv6",
                          data->config.address_family == 2 ? 1 : 0);

  lv_obj_t *dns_ta = lv_textarea_create(content);
  lv_obj_set_width(dns_ta, LV_PCT(100));
  lv_textarea_set_one_line(dns_ta, true);
  lv_textarea_set_placeholder_text(dns_ta, "DNS Servers");
  if (strlen(data->config.dns_servers) > 0)
    lv_textarea_set_text(dns_ta, data->config.dns_servers);
  data->call_dns_ta = dns_ta;

  data->call_tls_cert_sw = create_switch_row(content, "TLS Certificate File",
                                             data->config.use_tls_client_cert);
  data->call_verify_cert_sw = create_switch_row(
      content, "Verify Server Certificates", data->config.verify_server_cert);
  data->call_tls_ca_sw =
      create_switch_row(content, "TLS CA File", data->config.use_tls_ca_file);

  lv_obj_t *ua_ta = lv_textarea_create(content);
  lv_obj_set_width(ua_ta, LV_PCT(100));
  lv_textarea_set_one_line(ua_ta, true);
  lv_textarea_set_placeholder_text(ua_ta, "User Agent");
  if (strlen(data->config.user_agent) > 0)
    lv_textarea_set_text(ua_ta, data->config.user_agent);
  data->call_ua_ta = ua_ta;

  // Audio Settings Header
  lv_obj_t *audio_header = lv_label_create(content);
  lv_label_set_text(audio_header, "Audio Settings");
  lv_obj_set_style_text_font(audio_header, &lv_font_montserrat_16, 0);
  lv_obj_set_style_pad_top(audio_header, 20, 0);
  lv_obj_set_style_pad_bottom(audio_header, 10, 0);

  data->call_contacts_dd =
      create_dropdown_row(content, "Contacts", "baresip\nLocal\nLDAP",
                          data->config.contacts_source);

  // Ringtone (Placeholder)
  lv_obj_t *ringtone_row = lv_obj_create(content);
  lv_obj_set_size(ringtone_row, LV_PCT(100), 50);
  lv_obj_set_flex_flow(ringtone_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ringtone_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *rt_lbl = lv_label_create(ringtone_row);
  lv_label_set_text(rt_lbl, "Ringtone");
  lv_obj_t *rt_val = lv_label_create(ringtone_row);
  lv_label_set_text(rt_val, "Default"); // Static for now

  data->call_video_size_dd = create_dropdown_row(content, "Video Frame Size",
                                                 "1280x720\n640x480\n320x240",
                                                 data->config.video_frame_size);
}

static void save_call_settings(settings_data_t *data) {
  if (!data)
    return;

  data->config.start_automatically =
      lv_obj_has_state(data->call_start_auto_sw, LV_STATE_CHECKED);

  data->config.log_level = lv_dropdown_get_selected(data->call_log_level_dd);
  logger_set_level((log_level_t)data->config.log_level);

  strncpy(data->config.listen_address,
          lv_textarea_get_text(data->call_listen_addr_ta),
          sizeof(data->config.listen_address) - 1);

  int fam_idx = lv_dropdown_get_selected(data->call_addr_fam_dd);
  data->config.address_family =
      (fam_idx == 0) ? 1 : 2; // 0->IPv4(1), 1->IPv6(2)

  strncpy(data->config.dns_servers, lv_textarea_get_text(data->call_dns_ta),
          sizeof(data->config.dns_servers) - 1);

  data->config.use_tls_client_cert =
      lv_obj_has_state(data->call_tls_cert_sw, LV_STATE_CHECKED);
  data->config.verify_server_cert =
      lv_obj_has_state(data->call_verify_cert_sw, LV_STATE_CHECKED);
  data->config.use_tls_ca_file =
      lv_obj_has_state(data->call_tls_ca_sw, LV_STATE_CHECKED);

  strncpy(data->config.user_agent, lv_textarea_get_text(data->call_ua_ta),
          sizeof(data->config.user_agent) - 1);

  data->config.contacts_source =
      lv_dropdown_get_selected(data->call_contacts_dd);
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

  ADD_TA("Nickname", data->form_nick_ta, "My Account");
  ADD_TA("Display Name", data->form_name_ta, "John Doe");
  ADD_TA("Authentication Username", data->form_auth_user_ta, "user123");

  ADD_TA("Authentication Password", data->form_pass_ta, "secret");
  lv_textarea_set_password_mode(data->form_pass_ta, true);

  ADD_TA("SIP Domain (Server)", data->form_server_ta, "sip.example.com");
  ADD_TA("SIP Username", data->form_user_ta, "user123");
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
  lv_label_set_text(lv_obj_get_child(content, -1), "Media NAT Traversal");
  data->form_media_nat_dd = lv_dropdown_create(content);
  lv_dropdown_set_options(data->form_media_nat_dd, "ice\nstun\nturn\nnone");
  lv_obj_set_width(data->form_media_nat_dd, LV_PCT(95));

  // RTCP Mux
  lv_obj_t *rtcp_cont = lv_obj_create(content);
  lv_obj_set_size(rtcp_cont, LV_PCT(95), 50);
  lv_obj_set_flex_flow(rtcp_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(rtcp_cont, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lbl = lv_label_create(rtcp_cont);
  lv_label_set_text(lbl, "RTCP Multiplexing");
  data->form_rtcp_mux_sw = lv_switch_create(rtcp_cont);

  // Prack
  lv_obj_t *prack_cont = lv_obj_create(content);
  lv_obj_set_size(prack_cont, LV_PCT(95), 50);
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
  lv_textarea_set_text(data->form_nick_ta, acc->nickname);
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
  data->main_screen = lv_obj_create(applet->screen);
  data->account_settings_screen = lv_obj_create(applet->screen);
  data->call_settings_screen = lv_obj_create(applet->screen);
  data->account_form_screen = lv_obj_create(applet->screen);

  lv_obj_set_size(data->main_screen, LV_PCT(100), LV_PCT(100));
  lv_obj_set_size(data->account_settings_screen, LV_PCT(100), LV_PCT(100));
  lv_obj_set_size(data->call_settings_screen, LV_PCT(100), LV_PCT(100));
  lv_obj_set_size(data->account_form_screen, LV_PCT(100), LV_PCT(100));

  data->codec_priority_screen = lv_obj_create(applet->screen);
  lv_obj_set_size(data->codec_priority_screen, LV_PCT(100), LV_PCT(100));
  lv_obj_add_flag(data->codec_priority_screen, LV_OBJ_FLAG_HIDDEN);

  lv_obj_add_flag(data->account_settings_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(data->call_settings_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(data->account_form_screen, LV_OBJ_FLAG_HIDDEN);

  // --- MAIN SCREEN ---
  lv_obj_t *header = lv_obj_create(data->main_screen);
  lv_obj_set_size(header, LV_PCT(100), 60);
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
  lv_label_set_text(title, "Settings");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_pad_left(title, 20, 0);

  lv_obj_t *main_list = lv_list_create(data->main_screen);
  lv_obj_set_size(main_list, LV_PCT(90), LV_PCT(80));
  lv_obj_align(main_list, LV_ALIGN_BOTTOM_MID, 0, -20);

  lv_obj_t *btn_call =
      lv_list_add_btn(main_list, LV_SYMBOL_SETTINGS, "System Settings");
  lv_obj_add_event_cb(btn_call, call_settings_clicked, LV_EVENT_CLICKED, NULL);

  lv_obj_t *btn_acc =
      lv_list_add_btn(main_list, LV_SYMBOL_LIST, "Account Settings");
  lv_obj_add_event_cb(btn_acc, account_settings_clicked, LV_EVENT_CLICKED,
                      NULL);

  // --- ACCOUNT SETTINGS SCREEN (ACCOUNTS LIST) ---
  lv_obj_t *call_header = lv_obj_create(data->account_settings_screen);
  lv_obj_set_size(call_header, LV_PCT(100), 60);
  lv_obj_align(call_header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_flex_flow(call_header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(call_header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *call_back_btn = lv_btn_create(call_header);
  lv_obj_set_size(call_back_btn, 50, 40);
  lv_obj_t *call_back_label = lv_label_create(call_back_btn);
  lv_label_set_text(call_back_label, LV_SYMBOL_LEFT);
  lv_obj_center(call_back_label);
  lv_obj_add_event_cb(call_back_btn, back_to_main, LV_EVENT_CLICKED, NULL);

  lv_obj_t *call_title = lv_label_create(call_header);
  lv_label_set_text(call_title, "Account Settings");
  lv_obj_set_style_text_font(call_title, &lv_font_montserrat_24, 0);

  lv_obj_t *call_add_btn = lv_btn_create(call_header);
  lv_obj_set_size(call_add_btn, 80, 40);
  lv_obj_set_style_bg_color(call_add_btn, lv_color_hex(0x00AA00), 0);
  lv_obj_t *add_label = lv_label_create(call_add_btn);
  lv_label_set_text(add_label, LV_SYMBOL_PLUS " Add");
  lv_obj_center(add_label);
  lv_obj_add_event_cb(call_add_btn, add_account_btn_clicked, LV_EVENT_CLICKED,
                      NULL);

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
  lv_obj_t *form_header = lv_obj_create(data->account_form_screen);
  lv_obj_set_size(form_header, LV_PCT(100), 60);
  lv_obj_align(form_header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_flex_flow(form_header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(form_header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  // Back Button
  lv_obj_t *form_back_btn = lv_btn_create(form_header);
  lv_obj_set_size(form_back_btn, 50, 40);
  lv_obj_t *form_back_label = lv_label_create(form_back_btn);
  lv_label_set_text(form_back_label, LV_SYMBOL_LEFT);
  lv_obj_center(form_back_label);
  lv_obj_add_event_cb(form_back_btn, account_settings_clicked, LV_EVENT_CLICKED,
                      NULL);

  // Title
  lv_obj_t *form_title = lv_label_create(form_header);
  lv_label_set_text(form_title, "Account");
  lv_obj_set_style_text_font(form_title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_pad_left(form_title, 10, 0);

  // Spacer
  lv_obj_t *spacer = lv_obj_create(form_header);
  lv_obj_set_flex_grow(spacer, 1);
  lv_obj_set_height(spacer, 0); // Invisible
  lv_obj_set_style_bg_opa(spacer, 0, 0);
  lv_obj_set_style_border_width(spacer, 0, 0);

  // Save Button (Header)
  lv_obj_t *form_save_btn = lv_btn_create(form_header);
  lv_obj_set_size(form_save_btn, 50, 40);
  lv_obj_set_style_bg_color(form_save_btn, lv_color_hex(0x00AA00), 0);
  lv_obj_t *form_save_icon = lv_label_create(form_save_btn);
  lv_label_set_text(form_save_icon, LV_SYMBOL_SAVE); // Save Icon
  lv_obj_center(form_save_icon);
  lv_obj_add_event_cb(form_save_btn, save_account_clicked, LV_EVENT_CLICKED,
                      NULL);

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

static void settings_start(applet_t *applet) {
  log_info("SettingsApplet", "Started");
  settings_data_t *data = (settings_data_t *)applet->user_data;

  if (target_screen == SETTINGS_SCREEN_ACCOUNTS) {
    show_account_settings(data);
  } else if (target_screen == SETTINGS_SCREEN_CALL) {
    show_call_settings(data);
  } else {
    show_main_screen(data);
    nav_source = NAV_SOURCE_MAIN; // Reset if starting at main
  }
}

static void settings_pause(applet_t *applet) {
  log_info("SettingsApplet", "Paused");
}

static void settings_resume(applet_t *applet) {
  log_info("SettingsApplet", "Resumed");
  settings_data_t *data = (settings_data_t *)applet->user_data;

  // Handle Deep Links on Resume
  if (target_screen == SETTINGS_SCREEN_ACCOUNTS) {
    show_account_settings(data);
  } else if (target_screen == SETTINGS_SCREEN_CALL) {
    show_call_settings(data);
  } else {
    refresh_account_list(data);
  }
}

static void settings_stop(applet_t *applet) {
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
