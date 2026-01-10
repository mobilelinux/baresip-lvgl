#include "applet.h"
#include "applet_manager.h"
#include "../ui/ui_helpers.h"
#include "lvgl.h"

static lv_obj_t *about_screen;

static void handle_swipe_back(lv_event_t *e) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_RIGHT || dir == LV_DIR_LEFT) {
        applet_manager_back();
    }
}

static void back_btn_clicked(lv_event_t *e) { applet_manager_back(); }

static void add_header_text(lv_obj_t *parent, const char *text) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0x444444), 0); // Dark grey
  lv_obj_set_width(label, LV_PCT(95));
  lv_obj_set_style_pad_top(label, 20, 0);
  lv_obj_set_style_pad_bottom(label, 5, 0);
}

static void add_body_text(lv_obj_t *parent, const char *text) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(label, lv_color_black(), 0);
  lv_obj_set_width(label, LV_PCT(95));
  lv_obj_set_style_pad_bottom(label, 10, 0);
}

static void add_link_text(lv_obj_t *parent, const char *text,
                          const char *url_text) {
  // Simplified link text visualization as LVGL doesn't support HTML links
  // natively easily
  lv_obj_t *cont = lv_obj_create(parent);
  lv_obj_set_size(cont, LV_PCT(95), LV_SIZE_CONTENT);
  lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_bg_opa(cont, 0, 0);

  lv_obj_t *l1 = lv_label_create(cont);
  lv_label_set_text(l1, text);
  lv_obj_set_style_text_font(l1, &lv_font_montserrat_16, 0);

  if (url_text) {
    lv_obj_t *l2 = lv_label_create(cont);
    lv_label_set_text(l2, url_text);
    lv_obj_set_style_text_font(l2, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l2, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_obj_add_flag(l2, LV_OBJ_FLAG_CLICKABLE); // Just to hint interaction if we added click logic
  }
}

static void about_applet_start(void) {
  if (about_screen)
    return;

  about_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(about_screen, lv_color_white(), 0);
  
  // Swipe Support
  lv_obj_add_event_cb(about_screen, handle_swipe_back, LV_EVENT_GESTURE, NULL);
  lv_obj_clear_flag(about_screen, LV_OBJ_FLAG_GESTURE_BUBBLE); 
  lv_obj_add_flag(about_screen, LV_OBJ_FLAG_CLICKABLE); 

  // Header
  // Use common title bar
  ui_create_title_bar(about_screen, "About baresip+", true, back_btn_clicked, NULL);

  // Content
  lv_obj_t *content = lv_obj_create(about_screen);
  lv_obj_set_size(content, LV_PCT(100),
                  LV_PCT(100)); // Will be constrained by header
  // Content position managed by Flex container

  // Re-do layout: Main screen column
  lv_obj_set_flex_flow(about_screen, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(about_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(about_screen, 0, 0);
  //lv_obj_set_layout(header, LV_LAYOUT_FLEX); // Header is flex row

  // Content is scrolalble
  lv_obj_set_flex_grow(content, 1);
  lv_obj_set_width(content, LV_PCT(100));
  lv_obj_set_style_pad_all(content, 15, 0);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  
  // Swipe Bubble for Content
  lv_obj_add_flag(content, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(content, LV_OBJ_FLAG_CLICKABLE);

  // -- Content Items --
  add_header_text(content, "Baresip LVGL with video calls");
  add_body_text(content, "Steven<mlinux@gmail.com>.\n");
  add_header_text(content, "Usage Hints");
  add_body_text(content, "\xE2\x80\xA2 Add/Remove Account in Account Settings "
                         "and set default account.");
  add_body_text(content, "\xE2\x80\xA2 Long touches can also be used to remove "
                         "calls and contacts.");
  add_body_text(content, "\xE2\x80\xA2 Peers of calls and messages can be "
                         "added to contacts by long touches.");

  add_header_text(content, "Known Issues");
  add_body_text(content, "\xE2\x80\xA2 Only tested on my macbook m4 pro.");
  add_body_text(content, "\xE2\x80\xA2 NO i18n support.");

  add_header_text(content, "Source code");
  add_link_text(content, "Source code is available at ",
                "GitHub, where also issues can be reported.");

  add_header_text(content, "Licenses");
  add_body_text(content, "\xE2\x80\xA2 BSD-3-Clause except the following:");
  add_body_text(content, "\xE2\x80\xA2 Apache 2.0 AMR codecs and TLS security");
  add_body_text(
      content,
      "\xE2\x80\xA2 AGPLv4 ZRTP media encryption"); 
  add_body_text(content,
                "\xE2\x80\xA2 GNU LGPL 2.1 G.722, G.726, and Codec2 codecs");
  add_body_text(content, "\xE2\x80\xA2 GNU GPLv3 G.729 codec");
  add_body_text(content, "\xE2\x80\xA2 GNU GPLv2 H.264 and H.265 codecs");
  add_body_text(content, "\xE2\x80\xA2 AOMedia AV1 codec");

  lv_scr_load(about_screen);
}

static void about_applet_stop(void) {
  if (about_screen) {
     // Nothing to stop specifically
  }
}

static void about_applet_destroy(void) {
  if (about_screen && lv_obj_is_valid(about_screen)) {
    lv_obj_del(about_screen);
    about_screen = NULL;
  }
}

static void about_applet_start_wrapper(applet_t *self) {
  about_applet_start();
  self->screen = about_screen;
}

static void about_applet_stop_wrapper(applet_t *self) {
  (void)self;
  about_applet_stop();
}

static void about_applet_destroy_wrapper(applet_t *self) {
  about_applet_destroy();
  self->screen = NULL;
}

applet_t about_applet = {
    .name = "About",
    .callbacks = {.start = about_applet_start_wrapper,
                  .stop = about_applet_stop_wrapper,
                  .destroy = about_applet_destroy_wrapper}};

void about_applet_register(void) { applet_manager_register(&about_applet); }
