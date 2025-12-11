#include "applet.h"
#include "applet_manager.h"
#include "baresip_manager.h"
#include "contact_manager.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// UI State
static bool is_editor_mode = false;
static contact_t current_edit_contact;
static bool is_new_contact = false;
static applet_t *g_applet = NULL;

// Forward declarations
static void refresh_ui(void);
static void draw_list(void);
static void draw_editor(void);

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
  if (is_editor_mode) {
    is_editor_mode = false;
    refresh_ui();
  } else {
    applet_manager_back();
  }
}

static void add_btn_clicked(lv_event_t *e) {
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

static void delete_btn_clicked(lv_event_t *e) {
  if (!is_new_contact) {
    cm_remove(current_edit_contact.id);
  }
  is_editor_mode = false;
  refresh_ui();
}

// External reference to call applet
extern applet_t call_applet;

static void contact_item_clicked(lv_event_t *e) {
  const contact_t *c = (const contact_t *)lv_event_get_user_data(e);
  if (c) {
    if (baresip_manager_call(c->number) == 0) {
      // Switch to call applet if call initiated successfully
      applet_manager_launch_applet(&call_applet);
    }
  }
}

// ------------------- UI DRAWING -------------------

static void draw_list(void) {
  lv_obj_t *header = lv_obj_create(g_applet->screen);
  lv_obj_set_size(header, LV_PCT(100), 60);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(header, lv_palette_main(LV_PALETTE_LIGHT_BLUE), 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *back_btn = lv_btn_create(header);
  lv_obj_set_size(back_btn, 40, 40);
  lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_opa(back_btn, 0, 0);
  lv_obj_set_style_shadow_width(back_btn, 0, 0);

  lv_obj_t *back_icon = lv_label_create(back_btn);
  lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
  lv_obj_center(back_icon);
  lv_obj_add_event_cb(back_btn, back_btn_clicked, LV_EVENT_CLICKED, NULL);

  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text(title, "Contacts");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 50, 0);

  lv_obj_t *list = lv_obj_create(g_applet->screen);
  lv_obj_set_pos(list, 0, 60);
  lv_obj_set_size(list, LV_PCT(100), lv_pct(100) - 60);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(list, 10, 0);

  cm_init();
  int count = cm_get_count();

  for (int i = 0; i < count; i++) {
    const contact_t *c = cm_get_at(i);
    if (!c)
      continue;

    lv_obj_t *item = lv_obj_create(list);
    lv_obj_set_size(item, LV_PCT(100), 70);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(item, contact_item_clicked, LV_EVENT_CLICKED,
                        (void *)c);

    lv_obj_t *avatar = create_avatar(item, c->name, 50);
    lv_obj_align(avatar, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *name_lbl = lv_label_create(item);
    lv_label_set_text(name_lbl, c->name);
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(name_lbl, LV_ALIGN_LEFT_MID, 60, 0);

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

    lv_obj_add_event_cb(edit_btn, edit_btn_clicked, LV_EVENT_CLICKED,
                        (void *)c);
  }

  lv_obj_t *fab = lv_btn_create(g_applet->screen);
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

static void draw_editor(void) {
  lv_obj_t *header = lv_obj_create(g_applet->screen);
  lv_obj_set_size(header, LV_PCT(100), 60);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(header, lv_palette_main(LV_PALETTE_LIGHT_BLUE), 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *back_btn = lv_btn_create(header);
  lv_obj_set_size(back_btn, 40, 40);
  lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_opa(back_btn, 0, 0);
  lv_obj_set_style_shadow_width(back_btn, 0, 0);
  lv_obj_t *back_icon = lv_label_create(back_btn);
  lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
  lv_obj_center(back_icon);
  lv_obj_add_event_cb(back_btn, back_btn_clicked, LV_EVENT_CLICKED, NULL);

  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text(title,
                    is_new_contact ? "New Contact" : current_edit_contact.name);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 50, 0);

  lv_obj_t *save_btn = lv_btn_create(header);
  lv_obj_set_size(save_btn, 40, 40);
  lv_obj_align(save_btn, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_opa(save_btn, 0, 0);
  lv_obj_set_style_shadow_width(save_btn, 0, 0);
  lv_obj_t *save_icon = lv_label_create(save_btn);
  lv_label_set_text(save_icon, LV_SYMBOL_OK);
  lv_obj_center(save_icon);

  lv_obj_t *content = lv_obj_create(g_applet->screen);
  lv_obj_set_pos(content, 0, 60);
  lv_obj_set_size(content, LV_PCT(100), lv_pct(100) - 60);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(content, 20, 0); // Add spacing between elements
  lv_obj_set_style_pad_all(content, 20, 0);

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
  lv_textarea_set_text(name_ta,
                       is_new_contact ? "" : current_edit_contact.name);
  lv_textarea_set_placeholder_text(name_ta, "Name");

  lv_obj_t *num_lbl = lv_label_create(content);
  lv_label_set_text(num_lbl, "SIP or tel URI");
  lv_obj_set_width(num_lbl, LV_PCT(90));
  lv_obj_set_style_text_color(num_lbl, lv_palette_main(LV_PALETTE_GREY), 0);

  lv_obj_t *num_ta = lv_textarea_create(content);
  lv_textarea_set_one_line(num_ta, true);
  lv_obj_set_width(num_ta, LV_PCT(90));
  lv_textarea_set_text(num_ta,
                       is_new_contact ? "" : current_edit_contact.number);
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
    lv_obj_t *call_btn = lv_btn_create(content);
    lv_obj_set_size(call_btn, LV_PCT(50), 50);
    lv_obj_set_style_bg_color(call_btn, lv_palette_main(LV_PALETTE_GREEN), 0);

    lv_obj_t *call_icon = lv_label_create(call_btn);
    lv_label_set_text(call_icon, LV_SYMBOL_CALL " Call");
    lv_obj_center(call_icon);

    lv_obj_add_event_cb(call_btn, contact_item_clicked, LV_EVENT_CLICKED,
                        (void *)&current_edit_contact);
  }

  if (!is_new_contact) {
    lv_obj_t *del_btn = lv_btn_create(content);
    lv_obj_set_size(del_btn, LV_PCT(50), 40);
    lv_obj_set_style_bg_color(del_btn, lv_palette_main(LV_PALETTE_RED), 0);

    lv_obj_t *del_lbl = lv_label_create(del_btn);
    lv_label_set_text(del_lbl, "Delete Contact");
    lv_obj_center(del_lbl);

    lv_obj_add_event_cb(del_btn, delete_btn_clicked, LV_EVENT_CLICKED, NULL);
  }

  lv_obj_t **inputs = malloc(3 * sizeof(lv_obj_t *));
  inputs[0] = name_ta;
  inputs[1] = num_ta;
  inputs[2] = fav_sw;

  lv_obj_add_event_cb(save_btn, save_btn_clicked, LV_EVENT_CLICKED, inputs);
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

static int contacts_init(applet_t *applet) {
  g_applet = applet;
  is_editor_mode = false;
  refresh_ui();
  return 0;
}

static void contacts_start(applet_t *applet) { (void)applet; }
static void contacts_pause(applet_t *applet) { (void)applet; }
static void contacts_resume(applet_t *applet) { (void)applet; }
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
