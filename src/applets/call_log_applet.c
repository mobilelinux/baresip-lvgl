#include "applet.h"
#include "applet_manager.h"
#include "baresip_manager.h"
#include "history_manager.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>

// Event handler for back button
static void back_btn_clicked(lv_event_t *e) { applet_manager_back(); }

// External reference to call applet
extern applet_t call_applet;

// Event handler for call back button
static void call_back_clicked(lv_event_t *e) {
  const call_log_entry_t *entry =
      (const call_log_entry_t *)lv_event_get_user_data(e);
  if (entry) {
    log_info("CallLogApplet", "Calling back %s at %s", entry->name,
             entry->number);
    if (baresip_manager_call(entry->number) == 0) {
      // Switch to call applet if call initiated successfully
      applet_manager_launch_applet(&call_applet);
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

static int call_log_init(applet_t *applet) {
  log_info("CallLogApplet", "Initializing");

  // Create header with back button
  lv_obj_t *header = lv_obj_create(applet->screen);
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
  lv_label_set_text(title, "Call Log");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_pad_left(title, 20, 0);

  // Create scrollable call log list
  lv_obj_t *list = lv_obj_create(applet->screen);
  lv_obj_set_size(list, LV_PCT(95), LV_PCT(80));
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);

  // Add call log entries
  history_manager_init();
  int count = history_get_count();

  for (int i = 0; i < count; i++) {
    const call_log_entry_t *entry = history_get_at(i);
    if (!entry)
      continue;

    lv_obj_t *log_item = lv_obj_create(list);
    lv_obj_set_size(log_item, LV_PCT(95), 70);
    lv_obj_set_flex_flow(log_item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(log_item, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

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

    log_debug("CallLogApplet", "Entry %d cleaned URI: '%s' (Original: '%s')", i,
              uri_buf, entry->number);

    lv_obj_t *number_label = lv_label_create(details_row);
    char number_display[150];
    if (strncmp(uri_buf, "sip:", 4) == 0) {
      snprintf(number_display, sizeof(number_display), "%s", uri_buf);
    } else {
      snprintf(number_display, sizeof(number_display), "sip:%s", uri_buf);
    }

    lv_label_set_text(number_label, number_display);
    lv_obj_set_style_text_color(number_label, lv_color_hex(0x808080),
                                0); // Keep Grey as per requirements
    lv_obj_set_style_text_font(number_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_right(number_label, 10,
                               0); // Spacing between number and time

    // Time
    lv_obj_t *time_label = lv_label_create(details_row);
    lv_label_set_text(time_label, entry->time);
    lv_obj_set_style_text_color(time_label, lv_color_black(),
                                0); // Black for visibility
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_14, 0);

    // Call back button
    lv_obj_t *call_btn = lv_btn_create(log_item);
    lv_obj_set_size(call_btn, 60, 40);
    lv_obj_set_style_bg_color(call_btn, lv_color_hex(0x00AA00), 0);
    lv_obj_t *call_label = lv_label_create(call_btn);
    lv_label_set_text(call_label, LV_SYMBOL_CALL);
    lv_obj_center(call_label);
    lv_obj_add_event_cb(call_btn, call_back_clicked, LV_EVENT_CLICKED,
                        (void *)entry);
  }

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
