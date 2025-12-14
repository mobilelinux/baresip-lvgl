#include "applet.h"
#include "applet_manager.h"
#include "baresip_manager.h"
#include "call_applet.h"
#include "contact_manager.h"
#include "logger.h"
#include <stdio.h>
#include <time.h>

// Home applet data
typedef struct {
  lv_obj_t *tileview;
  lv_obj_t *clock_label;
  lv_obj_t *favorites_dock;
  lv_obj_t *in_call_btn;
  lv_obj_t *in_call_label;
  lv_timer_t *clock_timer;
} home_data_t;

extern applet_t call_applet;

// Update the clock label
static void update_clock(lv_timer_t *timer) {
  home_data_t *data = (home_data_t *)timer->user_data;
  if (!data || !data->clock_label)
    return;

  time_t t = time(NULL);
  struct tm tm = *localtime(&t);

  lv_label_set_text_fmt(data->clock_label, "%02d:%02d", tm.tm_hour, tm.tm_min);

  // Update In-Call Button Visibility
  if (data->in_call_btn && data->in_call_label) {
    call_info_t calls[4];
    int count = baresip_manager_get_active_calls(calls, 4);
    bool has_incoming = false;
    bool has_active = false;

    for (int i = 0; i < count; i++) {
      if (calls[i].state == CALL_STATE_INCOMING) {
        has_incoming = true;
      } else if (calls[i].state != CALL_STATE_IDLE &&
                 calls[i].state != CALL_STATE_TERMINATED) {
        has_active = true;
      }
    }

    if (has_incoming) {
      if (lv_obj_has_flag(data->in_call_btn, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(data->in_call_btn, LV_OBJ_FLAG_HIDDEN);
      }
      lv_obj_set_style_bg_color(data->in_call_btn,
                                lv_palette_main(LV_PALETTE_RED), 0);
      lv_label_set_text(data->in_call_label, LV_SYMBOL_CALL " Incoming");

      // Optional: Animate shake? For now just static red
    } else if (has_active) {
      if (lv_obj_has_flag(data->in_call_btn, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(data->in_call_btn, LV_OBJ_FLAG_HIDDEN);
      }
      lv_obj_set_style_bg_color(data->in_call_btn,
                                lv_palette_main(LV_PALETTE_GREEN), 0);
      lv_label_set_text(data->in_call_label, LV_SYMBOL_CALL " In Call");
    } else {
      if (!lv_obj_has_flag(data->in_call_btn, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(data->in_call_btn, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
}

// Handle click on In-Call button
static void in_call_clicked(lv_event_t *e) {
  (void)e;
  // User explicitly clicked the "In Call" status button, so show the active
  // call screen
  call_applet_request_active_view();
  applet_manager_launch_applet(&call_applet);
}

// Handle click on favorite contact
static void favorite_clicked(lv_event_t *e) {
  (void)e; // Unused
  const char *number = (const char *)lv_event_get_user_data(e);
  if (number) {
    if (baresip_manager_call(number) == 0) {
      call_applet_request_active_view();
      applet_manager_launch_applet(&call_applet);
    }
  }
}

// Populate the favorites dock
static void populate_favorites(home_data_t *data) {
  if (!data || !data->favorites_dock)
    return;

  lv_obj_clean(data->favorites_dock);

  cm_init();
  int count = cm_get_count();
  bool found_any = false;

  for (int i = 0; i < count; i++) {
    const contact_t *c = cm_get_at(i);
    if (!c || !c->is_favorite)
      continue;

    found_any = true;

    lv_obj_t *btn = lv_btn_create(data->favorites_dock);
    lv_obj_set_size(btn, 60, 60);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(btn, 60, 60);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(
        btn, LV_OBJ_FLAG_EVENT_BUBBLE); // Enable bubbling for key handling
    lv_obj_add_event_cb(btn, favorite_clicked, LV_EVENT_CLICKED,
                        (void *)c->number);

    lv_obj_t *initial = lv_label_create(btn);
    char init_char[2] = {c->name[0], '\0'};
    lv_label_set_text(initial, init_char);
    lv_obj_center(initial);
  }

  if (!found_any) {
    lv_obj_t *lbl = lv_label_create(data->favorites_dock);
    lv_label_set_text(lbl, "No Favorites");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl);
  }
}

static void home_key_handler(lv_event_t *e) {
  home_data_t *data = (home_data_t *)lv_event_get_user_data(e);
  if (!data || !data->tileview)
    return;

  uint32_t key = lv_indev_get_key(lv_indev_get_act());
  // Home is (0,0), Apps is (1,0)
  if (key == LV_KEY_RIGHT) {
    // Go to Apps (1,0)
    lv_obj_set_tile_id(data->tileview, 1, 0, LV_ANIM_ON);
  } else if (key == LV_KEY_LEFT || key == LV_KEY_ESC) {
    // Go to Home (0,0)
    lv_obj_set_tile_id(data->tileview, 0, 0, LV_ANIM_ON);
  }
}

static void all_apps_clicked(lv_event_t *e) {
  home_data_t *data = (home_data_t *)lv_event_get_user_data(e);
  if (data && data->tileview) {
    // Go to Apps (1,0)
    lv_obj_set_tile_id(data->tileview, 1, 0, LV_ANIM_ON);
  }
}

// Event handler for applet tile clicks (Launcher)
static void applet_tile_clicked(lv_event_t *e) {
  applet_t *applet = (applet_t *)lv_event_get_user_data(e);
  if (applet) {
    applet_manager_launch_applet(applet);
  }
}

static int home_init(applet_t *applet) {
  log_info("HomeApplet", "Initializing");

  home_data_t *data = lv_mem_alloc(sizeof(home_data_t));
  if (!data)
    return -1;
  memset(data, 0, sizeof(home_data_t));
  applet->user_data = data;

  // Create Tileview
  data->tileview = lv_tileview_create(applet->screen);
  lv_obj_set_size(data->tileview, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(data->tileview, LV_OBJ_FLAG_SCROLLABLE);

  // --- PAGE 1: Clock & Favorites (HOME) at (0,0) ---
  // Allow sliding LEFT to go to Apps at (1,0)? No, swiping Left reveals Right.
  // Neighbor is at (1,0) [Right]. So we allow moving focus RIGHT.
  // LV_DIR_RIGHT means "There is a tile to the right".
  lv_obj_t *page_home =
      lv_tileview_add_tile(data->tileview, 0, 0, LV_DIR_RIGHT);

  // In-Call Button (Hidden by default)
  data->in_call_btn = lv_btn_create(page_home);
  lv_obj_set_size(data->in_call_btn, 120, 60);
  lv_obj_align(data->in_call_btn, LV_ALIGN_TOP_LEFT, 20, 20);
  lv_obj_set_style_bg_color(data->in_call_btn,
                            lv_palette_main(LV_PALETTE_GREEN), 0);
  lv_obj_set_style_radius(data->in_call_btn, 30, 0);
  lv_obj_add_flag(data->in_call_btn, LV_OBJ_FLAG_HIDDEN); // Initially hidden
  lv_obj_add_flag(data->in_call_btn, LV_OBJ_FLAG_EVENT_BUBBLE);

  data->in_call_label = lv_label_create(data->in_call_btn);
  lv_label_set_text(data->in_call_label, LV_SYMBOL_CALL " In Call");
  lv_obj_set_style_text_font(data->in_call_label, &lv_font_montserrat_16, 0);
  lv_obj_center(data->in_call_label);

  lv_obj_add_event_cb(data->in_call_btn, in_call_clicked, LV_EVENT_CLICKED,
                      NULL);

  // Clock
  data->clock_label = lv_label_create(page_home);
  lv_obj_set_style_text_font(data->clock_label, &lv_font_montserrat_48, 0);
  lv_obj_align(data->clock_label, LV_ALIGN_CENTER, -80, -20);

  // Favorites Dock
  data->favorites_dock = lv_obj_create(page_home);
  lv_obj_set_size(data->favorites_dock, 100, LV_PCT(90));
  lv_obj_align(data->favorites_dock, LV_ALIGN_RIGHT_MID, -10, 0);
  lv_obj_set_flex_flow(data->favorites_dock, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(data->favorites_dock, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_color(data->favorites_dock,
                            lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_style_bg_opa(data->favorites_dock, LV_OPA_20, 0);
  lv_obj_set_style_radius(data->favorites_dock, 20, 0);
  lv_obj_set_style_pad_all(data->favorites_dock, 10, 0);
  lv_obj_set_style_pad_gap(data->favorites_dock, 10, 0);

  // All Apps Button (on Home Page)
  lv_obj_t *all_apps_btn = lv_btn_create(page_home);
  lv_obj_set_size(all_apps_btn, 80, 80);
  lv_obj_align(all_apps_btn, LV_ALIGN_BOTTOM_LEFT, 20, -20);
  lv_obj_set_style_radius(all_apps_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(all_apps_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
  lv_obj_add_flag(all_apps_btn, LV_OBJ_FLAG_EVENT_BUBBLE);

  lv_obj_t *all_apps_icon = lv_label_create(all_apps_btn);
  lv_label_set_text(all_apps_icon, LV_SYMBOL_LIST);
  lv_obj_set_style_text_font(all_apps_icon, &lv_font_montserrat_32, 0);
  lv_obj_center(all_apps_icon);

  lv_obj_add_event_cb(all_apps_btn, all_apps_clicked, LV_EVENT_CLICKED, data);

  // --- PAGE 2: Applet Grid (APPS) at (1,0) ---
  // Neighbor is at (0,0) [Left]. So we allow moving focus LEFT.
  lv_obj_t *page_apps = lv_tileview_add_tile(data->tileview, 1, 0, LV_DIR_LEFT);

  // Title for App Drawer
  lv_obj_t *title = lv_label_create(page_apps);
  lv_label_set_text(title, "Applets");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

  // Grid
  lv_obj_t *grid = lv_obj_create(page_apps);
  lv_obj_set_size(grid, LV_PCT(90), LV_PCT(80));
  lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(grid, 10, 0);
  lv_obj_set_style_pad_gap(grid, 20, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_bg_opa(grid, 0, 0);

  // Populate Apps
  int count;
  applet_t **applets = applet_manager_get_all(&count);

  for (int i = 0; i < count; i++) {
    if (!applets[i])
      continue;
    if (applets[i] == applet)
      continue;

    lv_obj_t *tile = lv_btn_create(grid);
    lv_obj_set_size(tile, 100, 100);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *icon = lv_label_create(tile);
    lv_label_set_text(icon,
                      applets[i]->icon ? applets[i]->icon : LV_SYMBOL_FILE);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_32, 0);

    lv_obj_t *label = lv_label_create(tile);
    lv_label_set_text(label, applets[i]->name);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_add_event_cb(tile, applet_tile_clicked, LV_EVENT_CLICKED,
                        applets[i]);
  }

  // Set Default Page to Home (0,0)
  lv_obj_set_tile_id(data->tileview, 0, 0, LV_ANIM_OFF);

  // Key Handling: Add ONLY tileview to default group
  lv_group_t *g = lv_group_get_default();
  if (g) {
    lv_group_remove_all_objs(g);
    lv_group_add_obj(g, data->tileview);
  }
  lv_obj_add_event_cb(data->tileview, home_key_handler, LV_EVENT_KEY, data);

  // Start Timer first
  data->clock_timer = lv_timer_create(update_clock, 1000, data);
  // Then update
  update_clock(data->clock_timer);

  // Populate Favorites
  populate_favorites(data);

  return 0;
}

static void home_start(applet_t *applet) {
  (void)applet;
  log_info("HomeApplet", "Started");
  home_data_t *data = (home_data_t *)applet->user_data;
  // Refocus tileview on start
  if (data && data->tileview) {
    lv_group_t *g = lv_group_get_default();
    if (g) {
      lv_group_focus_obj(data->tileview);
    }
  }
}

static void home_resume(applet_t *applet) {
  log_debug("HomeApplet", "Resumed");
  home_data_t *data = (home_data_t *)applet->user_data;
  if (data) {
    populate_favorites(data);

    // Restore focus to tileview
    if (data->tileview) {
      lv_group_t *g = lv_group_get_default();
      if (g) {
        lv_group_remove_all_objs(g);
        lv_group_add_obj(g, data->tileview);
        lv_group_focus_obj(data->tileview);
      }
    }
  }
}

static void home_pause(applet_t *applet) {
  (void)applet;
  log_debug("HomeApplet", "Paused");
}

static void home_stop(applet_t *applet) {
  (void)applet;
  log_info("HomeApplet", "Stopped");
}

static void home_destroy(applet_t *applet) {
  log_info("HomeApplet", "Destroying");
  home_data_t *data = (home_data_t *)applet->user_data;
  if (data) {
    if (data->clock_timer) {
      lv_timer_del(data->clock_timer);
      data->clock_timer = NULL;
    }
    lv_mem_free(data);
    applet->user_data = NULL;
  }
}

APPLET_DEFINE(home_applet, "Home", "Applet Launcher", LV_SYMBOL_HOME);

void home_applet_register(void) {
  home_applet.callbacks.init = home_init;
  home_applet.callbacks.start = home_start;
  home_applet.callbacks.pause = home_pause;
  home_applet.callbacks.resume = home_resume;
  home_applet.callbacks.stop = home_stop;
  home_applet.callbacks.destroy = home_destroy;

  applet_manager_register(&home_applet);
}
