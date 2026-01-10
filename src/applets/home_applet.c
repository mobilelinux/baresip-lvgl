#include "applet.h"
#include "applet_manager.h"
#include "baresip_manager.h"
#include "config_manager.h"
#include "database_manager.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Home Applet Data
typedef struct {
  lv_obj_t *tileview;
  lv_obj_t *clock_label;
  lv_timer_t *clock_timer;

  // Favorites
  lv_obj_t *favorites_dock;

  // Status Indicators
  lv_obj_t *incoming_call_btn;
  lv_obj_t *incoming_call_label;
  lv_obj_t *in_call_btn;
  lv_obj_t *in_call_label;
} home_data_t;

// Forward decl


static void update_clock(lv_timer_t *timer) {
  home_data_t *data = (home_data_t *)timer->user_data;
  if (!data || !data->clock_label)
    return;

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  if (!t)
    return;

  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", t->tm_hour, t->tm_min);
  lv_label_set_text(data->clock_label, buf);
}

static void applet_tile_clicked(lv_event_t *e) {
  applet_t *applet = (applet_t *)lv_event_get_user_data(e);
  if (applet) {
    applet_manager_launch_applet(applet);
  }
}

/* Unused function removed */
/* Unused function removed */

static void incoming_call_clicked(lv_event_t *e) {
  (void)e;
  // Launch Call Applet to handle incoming
  applet_manager_launch("Call");
}

static void in_call_clicked(lv_event_t *e) {
  (void)e;
  // Launch Call Applet to return to active call
  applet_manager_launch("Call");
}

// Helper to free user_data (string)
static void free_user_data(lv_event_t * e) {
    void * data = lv_event_get_user_data(e);
    if(data) {
        free(data);
    }
}

// Externs
extern void call_applet_open(const char *number);
extern void call_applet_video_open(const char *number);

static void fav_audio_clicked(lv_event_t *e) {
    const char *number = (const char *)lv_event_get_user_data(e);
    if (number) {
        call_applet_open(number);
    }
}

static void fav_video_clicked(lv_event_t *e) {
    const char *number = (const char *)lv_event_get_user_data(e);
    if (number) {
        call_applet_video_open(number);
    }
}

// Externs for Contacts
extern applet_t contacts_applet;
extern void contacts_applet_open_new(const char *number);

static void add_contact_clicked(lv_event_t *e) {
    (void)e;
    contacts_applet_open_new("");
    applet_manager_launch_applet(&contacts_applet);
}

static void populate_favorites(home_data_t *data) {
  if (!data->favorites_dock)
    return;
  lv_obj_clean(data->favorites_dock);

  // Load Contacts (API)
  db_contact_t contacts[4];
  int count = db_get_favorite_contacts(contacts, 4);

  if (count > 0) {
      for (int i = 0; i < count; i++) {
        // Container
        lv_obj_t *cont = lv_obj_create(data->favorites_dock);
        lv_obj_set_size(cont, 230, 70); // Wider for buttons
        lv_obj_set_style_pad_all(cont, 5, 0);
        lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cont, 0, 0);

        // Name/Initials
        lv_obj_t *lbl = lv_label_create(cont);
        if (strlen(contacts[i].name) > 0) {
            lv_label_set_text(lbl, contacts[i].name); // Full Name
        } else {
            lv_label_set_text(lbl, contacts[i].number);
        }
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 5, 0);
        lv_obj_set_width(lbl, 110); // Limit width
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);

        // Video Button (Rightmost)
        lv_obj_t *btn_vid = lv_btn_create(cont);
        lv_obj_set_size(btn_vid, 40, 40);
        lv_obj_align(btn_vid, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(btn_vid, lv_palette_main(LV_PALETTE_TEAL), 0);
        
        lv_obj_t *lbl_vid = lv_label_create(btn_vid);
        lv_label_set_text(lbl_vid, LV_SYMBOL_VIDEO);
        lv_obj_center(lbl_vid);

        char *num_copy_v = strdup(contacts[i].number);
        lv_obj_add_event_cb(btn_vid, fav_video_clicked, LV_EVENT_CLICKED, num_copy_v);
        lv_obj_add_event_cb(btn_vid, free_user_data, LV_EVENT_DELETE, num_copy_v);

        // Audio Button (Left of Video)
        lv_obj_t *btn_aud = lv_btn_create(cont);
        lv_obj_set_size(btn_aud, 40, 40);
        lv_obj_align(btn_aud, LV_ALIGN_RIGHT_MID, -45, 0); // Gap
        lv_obj_set_style_bg_color(btn_aud, lv_palette_main(LV_PALETTE_GREEN), 0);
        
        lv_obj_t *lbl_aud = lv_label_create(btn_aud);
        lv_label_set_text(lbl_aud, LV_SYMBOL_CALL);
        lv_obj_center(lbl_aud);

        char *num_copy_a = strdup(contacts[i].number);
        lv_obj_add_event_cb(btn_aud, fav_audio_clicked, LV_EVENT_CLICKED, num_copy_a);
        lv_obj_add_event_cb(btn_aud, free_user_data, LV_EVENT_DELETE, num_copy_a);
      }
  } else {
      // "Add Contact" placeholder
      lv_obj_t *btn = lv_btn_create(data->favorites_dock);
      lv_obj_set_size(btn, 60, 60);
      lv_obj_set_style_radius(btn, 20, 0);
      lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_GREY), 0);
      lv_obj_add_event_cb(btn, add_contact_clicked, LV_EVENT_CLICKED, NULL);
      
      lv_obj_t *lbl = lv_label_create(btn);
      lv_label_set_text(lbl, LV_SYMBOL_PLUS);
      lv_obj_center(lbl);
  }
}

static void home_key_handler(lv_event_t *e) {
  home_data_t *data = (home_data_t *)lv_event_get_user_data(e);
  uint32_t key = lv_indev_get_key(lv_indev_get_act());
  
  if (key == LV_KEY_RIGHT) {
      // Go to Apps (1,0)
      if (data && data->tileview)
          lv_obj_set_tile_id(data->tileview, 1, 0, LV_ANIM_ON);
  } else if (key == LV_KEY_LEFT) {
      // Go to Home (0,0)
      if (data && data->tileview)
          lv_obj_set_tile_id(data->tileview, 0, 0, LV_ANIM_ON);
  }
}

static int home_init(applet_t *applet) {
  log_info("HomeApplet", "Initializing");
  home_data_t *data = lv_mem_alloc(sizeof(home_data_t));
  if (!data)
    return -1;
  memset(data, 0, sizeof(home_data_t));
  applet->user_data = data;

  // Root Tileview
  data->tileview = lv_tileview_create(applet->screen);
  lv_obj_set_size(data->tileview, LV_PCT(100), LV_PCT(100));
  // Remove scrollbars
  lv_obj_set_scrollbar_mode(data->tileview, LV_SCROLLBAR_MODE_OFF);
  
  // NOTE: Swapped Coords per User Request (Swipe Direction Change)
  // PAGE 1: Home (Clock/Favs) at (0,0) - LEFT (DEFAULT)
  // Neighbor is (1,0) [Right] -> Apps.
  // Allow Scroll Right to go to Apps.
  
  // PAGE 1: HOME (0,0)
  lv_obj_t *page_home = lv_tileview_add_tile(data->tileview, 0, 0, LV_DIR_RIGHT);

  // PAGE 2: APPS (1,0)
  lv_obj_t *page_apps = lv_tileview_add_tile(data->tileview, 1, 0, LV_DIR_LEFT);

  // --- Populate APPS PAGE (page_apps) ---
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


  // --- Populate HOME PAGE (page_home) ---

  // In-Call Button (Hidden by default)
  // Incoming Call Button (Hidden by default)
  data->incoming_call_btn = lv_btn_create(page_home);
  lv_obj_set_size(data->incoming_call_btn, 120, 60);
  lv_obj_align(data->incoming_call_btn, LV_ALIGN_TOP_LEFT, 20,
               20); // Top slot for urgent
  lv_obj_set_style_bg_color(data->incoming_call_btn,
                            lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_set_style_radius(data->incoming_call_btn, 30, 0);
  lv_obj_add_flag(data->incoming_call_btn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(data->incoming_call_btn, LV_OBJ_FLAG_EVENT_BUBBLE);

  data->incoming_call_label = lv_label_create(data->incoming_call_btn);
  lv_label_set_text(data->incoming_call_label, LV_SYMBOL_CALL " Incoming");
  lv_obj_set_style_text_font(data->incoming_call_label, &lv_font_montserrat_16,
                             0);
  lv_obj_center(data->incoming_call_label);

  lv_obj_add_event_cb(data->incoming_call_btn, incoming_call_clicked,
                      LV_EVENT_CLICKED, NULL);

  // Active Call Button (Hidden by default)
  data->in_call_btn = lv_btn_create(page_home);
  lv_obj_set_size(data->in_call_btn, 120, 60);
  lv_obj_align(data->in_call_btn, LV_ALIGN_TOP_LEFT, 20, 90); // Second slot
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
  lv_obj_align(data->clock_label, LV_ALIGN_CENTER, -120,
               -20); // Move left slightly

  // Favorites Dock
  data->favorites_dock = lv_obj_create(page_home);
  lv_obj_set_size(data->favorites_dock, 270, LV_PCT(90)); // Wider for buttons
  lv_obj_align(data->favorites_dock, LV_ALIGN_RIGHT_MID, -10, 0);
  lv_obj_set_flex_flow(data->favorites_dock, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(data->favorites_dock, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_color(data->favorites_dock,
                            lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_style_bg_opa(data->favorites_dock, LV_OPA_20, 0);
  lv_obj_set_style_radius(data->favorites_dock, 20, 0);
  lv_obj_set_style_pad_all(data->favorites_dock, 5, 0);
  lv_obj_set_style_pad_gap(data->favorites_dock, 10, 0);

  // All Apps Button (on Home Page) - Updated to Left
  // Since Apps is on Right (1,0) wait...
  // User wanted Home(0,0), Apps(1,0). (Per "Swipe Left to go to Apps").
  // So Apps is to the RIGHT of Home.
  // Swipe Left (finger moves left) pulls content from Right.
  // Yes.
  
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
