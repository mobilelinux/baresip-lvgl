#include "applet.h"
#include "applet_manager.h"
#include "logger.h"
#include <stdio.h>

// Home applet data
typedef struct {
  lv_obj_t *grid;
} home_data_t;

// Event handler for applet tile clicks
static void applet_tile_clicked(lv_event_t *e) {
  applet_t *applet = (applet_t *)lv_event_get_user_data(e);
  if (applet) {
    applet_manager_launch_applet(applet);
  }
}

static int home_init(applet_t *applet) {
  log_info("HomeApplet", "Initializing");

  // Allocate applet data
  home_data_t *data = lv_mem_alloc(sizeof(home_data_t));
  if (!data)
    return -1;
  applet->user_data = data;

  // Create title bar
  lv_obj_t *title = lv_label_create(applet->screen);
  lv_label_set_text(title, "Applet Launcher");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

  // Create grid container for applet tiles
  data->grid = lv_obj_create(applet->screen);
  lv_obj_set_size(data->grid, LV_PCT(95), LV_PCT(80));
  lv_obj_align(data->grid, LV_ALIGN_CENTER, 0, 20);
  lv_obj_set_flex_flow(data->grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(data->grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(data->grid, 10, 0);
  lv_obj_set_style_pad_gap(data->grid, 15, 0);

  // Get all registered applets
  int count;
  applet_t **applets = applet_manager_get_all(&count);

  // Create tiles for each applet (except home itself)
  for (int i = 0; i < count; i++) {
    if (applets[i] == applet)
      continue; // Skip self

    // Create tile container
    lv_obj_t *tile = lv_btn_create(data->grid);
    lv_obj_set_size(tile, 120, 120);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(tile, 10, 0);

    // Create vertical layout for icon and label
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    // Add icon
    lv_obj_t *icon = lv_label_create(tile);
    lv_label_set_text(icon,
                      applets[i]->icon ? applets[i]->icon : LV_SYMBOL_FILE);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_32, 0);

    // Add label
    lv_obj_t *label = lv_label_create(tile);
    lv_label_set_text(label, applets[i]->name);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    // Add click event
    lv_obj_add_event_cb(tile, applet_tile_clicked, LV_EVENT_CLICKED,
                        applets[i]);
  }

  return 0;
}

static void home_start(applet_t *applet) { log_info("HomeApplet", "Started"); }

static void home_pause(applet_t *applet) { log_debug("HomeApplet", "Paused"); }

static void home_resume(applet_t *applet) {
  log_debug("HomeApplet", "Resumed");
}

static void home_stop(applet_t *applet) { log_info("HomeApplet", "Stopped"); }

static void home_destroy(applet_t *applet) {
  log_info("HomeApplet", "Destroying");
  if (applet->user_data) {
    lv_mem_free(applet->user_data);
    applet->user_data = NULL;
  }
}

// Define the home applet
APPLET_DEFINE(home_applet, "Home", "Applet Launcher", LV_SYMBOL_HOME);

// Initialize callbacks
void home_applet_register(void) {
  home_applet.callbacks.init = home_init;
  home_applet.callbacks.start = home_start;
  home_applet.callbacks.pause = home_pause;
  home_applet.callbacks.resume = home_resume;
  home_applet.callbacks.stop = home_stop;
  home_applet.callbacks.destroy = home_destroy;

  applet_manager_register(&home_applet);
}
