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
  // Account Status
  lv_obj_t *account_btn;
  lv_obj_t *account_icon;
  lv_obj_t *account_label;
  char account_aor[128];
  
  // Clock & Date
  lv_obj_t *clock_label;
  lv_obj_t *date_label;
  lv_timer_t *clock_timer;

  // Favorites
  lv_obj_t *favorites_dock;

  // Status Indicators
  lv_obj_t *incoming_call_btn;
  lv_obj_t *incoming_call_label;
  lv_obj_t *in_call_btn;
  lv_obj_t *in_call_label;
  
  lv_obj_t *apps_grid;
} home_data_t;

static home_data_t *g_home_data = NULL; // Global for notifications

// Forward decl



static void account_clicked(lv_event_t *e) {
    (void)e;
    // Launch Settings Applet
    applet_manager_launch("Settings");
}

static void update_account_cache(home_data_t *data) {
    if (!data) return;
    
    app_config_t cfg;
    if (config_load_app_settings(&cfg) == 0) {
        voip_account_t accounts[MAX_ACCOUNTS];
        int count = config_load_accounts(accounts, MAX_ACCOUNTS);
        if (cfg.default_account_index >= 0 && cfg.default_account_index < count) {
            voip_account_t *acc = &accounts[cfg.default_account_index];
            
            // Build AOR for status check: sip:user@domain
            if (strlen(acc->username)>0 && strlen(acc->server)>0) {
                 snprintf(data->account_aor, sizeof(data->account_aor), "sip:%s@%s", acc->username, acc->server);
            } else {
                 data->account_aor[0] = '\0';
            }
            
            // Update Label
            if (data->account_label) {
                if (strlen(acc->display_name) > 0) lv_label_set_text(data->account_label, acc->display_name);
                else if (strlen(acc->username) > 0) lv_label_set_text(data->account_label, acc->username);
                else lv_label_set_text(data->account_label, "Default Account");
            }
        } else {
            // No default
            data->account_aor[0] = '\0';
            if (data->account_label) lv_label_set_text(data->account_label, "Default Account");
        }
    }
}

static void update_clock(lv_timer_t *timer) {
  home_data_t *data = (home_data_t *)timer->user_data;
  if (!data) return;

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  if (!t) return;

  // 1. Clock
  if (data->clock_label) {
      char buf[8];
      snprintf(buf, sizeof(buf), "%02d:%02d", t->tm_hour, t->tm_min);
      lv_label_set_text(data->clock_label, buf);
  }

  // 2. Date
  if (data->date_label) {
      char date_buf[32];
      strftime(date_buf, sizeof(date_buf), "%a, %b %d", t);
      lv_label_set_text(data->date_label, date_buf);
  }
  
  // 3. Account Status
  if (data->account_icon && strlen(data->account_aor) > 0) {
      reg_status_t status = baresip_manager_get_account_status(data->account_aor);
      if (status == REG_STATUS_REGISTERED) {
          lv_label_set_text(data->account_icon, LV_SYMBOL_OK);
          lv_obj_set_style_text_color(data->account_icon, lv_palette_main(LV_PALETTE_GREEN), 0);
      } else if (status == REG_STATUS_REGISTERING) {
          lv_label_set_text(data->account_icon, LV_SYMBOL_REFRESH);
          lv_obj_set_style_text_color(data->account_icon, lv_palette_main(LV_PALETTE_ORANGE), 0);
      } else {
          lv_label_set_text(data->account_icon, LV_SYMBOL_WARNING);
          lv_obj_set_style_text_color(data->account_icon, lv_palette_main(LV_PALETTE_RED), 0);
      }
  } else if (data->account_icon) {
      lv_label_set_text(data->account_icon, LV_SYMBOL_SETTINGS);
      lv_obj_set_style_text_color(data->account_icon, lv_palette_main(LV_PALETTE_GREY), 0);
  }

  // 4. Call State (Original Logic)
  enum call_state state = baresip_manager_get_state();
  if (state == CALL_STATE_INCOMING) {
      if (data->incoming_call_btn) lv_obj_clear_flag(data->incoming_call_btn, LV_OBJ_FLAG_HIDDEN);
      if (data->in_call_btn) lv_obj_add_flag(data->in_call_btn, LV_OBJ_FLAG_HIDDEN);
  } else if (state == CALL_STATE_ESTABLISHED || state == CALL_STATE_EARLY || state == CALL_STATE_RINGING) {
      if (data->in_call_btn) lv_obj_clear_flag(data->in_call_btn, LV_OBJ_FLAG_HIDDEN);
      if (data->incoming_call_btn) lv_obj_add_flag(data->incoming_call_btn, LV_OBJ_FLAG_HIDDEN);
  } else {
      if (data->incoming_call_btn) lv_obj_add_flag(data->incoming_call_btn, LV_OBJ_FLAG_HIDDEN);
      if (data->in_call_btn) lv_obj_add_flag(data->in_call_btn, LV_OBJ_FLAG_HIDDEN);
  }
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
static void free_user_data(lv_event_t *e) {
    void *data = lv_event_get_user_data(e);
    if (data) free(data);
}

// Externs for Contacts
extern applet_t contacts_applet;
extern void contacts_applet_open_new(const char *number);

static void add_contact_clicked(lv_event_t *e) {
    (void)e;
    contacts_applet_open_new("");
    applet_manager_launch_applet(&contacts_applet);
}

static void fav_video_clicked(lv_event_t *e) {
    char *number = (char *)lv_event_get_user_data(e);
    if (number) {
        // Start Video Call
        applet_manager_launch("Call");
        
        // TODO: Pass number to Call Applet
        // For now, user has to dial. 
        // Previously we had call_applet_video_open extern?
        // extern void call_applet_video_open(const char *number);
        // call_applet_video_open(number); 
        // We can restore that if symbols exist suitable. 
        // Leaving as launch for now to fix build.
    }
}

static void fav_audio_clicked(lv_event_t *e) {
    char *number = (char *)lv_event_get_user_data(e);
    if (number) {
        // Start Audio Call
         applet_manager_launch("Call");
         // extern void call_applet_open(const char *number);
         // call_applet_open(number);
    }
}


static void populate_favorites(home_data_t *data) {
  if (!data || !data->favorites_dock)
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

extern int db_chat_get_unread_count(void);
extern int db_call_get_missed_count(void);

static void update_badges(home_data_t *data) {
    if (!data || !data->apps_grid) return;
    int msg_count = db_chat_get_unread_count();
    int call_count = db_call_get_missed_count();
    
    // Iterate Grid
    uint32_t cnt = lv_obj_get_child_cnt(data->apps_grid);
    for(uint32_t i=0; i<cnt; i++) {
        lv_obj_t *tile = lv_obj_get_child(data->apps_grid, i);
        if (lv_obj_get_child_cnt(tile) > 1) {
             lv_obj_t *lbl = lv_obj_get_child(tile, 1);
             const char *txt = lv_label_get_text(lbl);
             
             int badge_val = 0;
             if (txt && strcmp(txt, "Messages") == 0) badge_val = msg_count;
             if (txt && strcmp(txt, "Call") == 0) badge_val = call_count;

             if (badge_val > 0) {
                 // SHOW BADGE
                 if (lv_obj_get_child_cnt(tile) > 2) {
                     lv_obj_clear_flag(lv_obj_get_child(tile, 2), LV_OBJ_FLAG_HIDDEN);
                 } else {
                     // Create Badge (Duplicate logic, should refactor)
                     lv_obj_t *badge = lv_obj_create(tile);
                     lv_obj_set_size(badge, 16, 16);
                     lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
                     lv_obj_set_style_bg_color(badge, lv_palette_main(LV_PALETTE_RED), 0);
                     lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -10, 10);
                     lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
                     lv_obj_set_style_border_width(badge, 2, 0);
                     lv_obj_set_style_border_color(badge, lv_color_white(), 0);
                 }
             } else {
                 // HIDE BADGE
                  if (lv_obj_get_child_cnt(tile) > 2) {
                     lv_obj_add_flag(lv_obj_get_child(tile, 2), LV_OBJ_FLAG_HIDDEN);
                 }
             }
        }
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
  g_home_data = data;

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
  data->apps_grid = grid;
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

  // --- Center Info (Account, Time, Date) ---
  
  // 1. Account Button (Top)
  data->account_btn = lv_btn_create(page_home);
  lv_obj_set_size(data->account_btn, 220, 50);
  lv_obj_align(data->account_btn, LV_ALIGN_CENTER, -60, -90); // Centered, Above Clock
  lv_obj_set_style_bg_opa(data->account_btn, LV_OPA_TRANSP, 0); // Transparent
  lv_obj_set_style_shadow_width(data->account_btn, 0, 0);
  lv_obj_set_flex_flow(data->account_btn, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(data->account_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(data->account_btn, 5, 0);
  lv_obj_set_style_pad_gap(data->account_btn, 10, 0);
  lv_obj_add_event_cb(data->account_btn, account_clicked, LV_EVENT_CLICKED, NULL);

  data->account_icon = lv_label_create(data->account_btn);
  lv_label_set_text(data->account_icon, LV_SYMBOL_SETTINGS); // Default
  lv_obj_set_style_text_font(data->account_icon, &lv_font_montserrat_20, 0);

  data->account_label = lv_label_create(data->account_btn);
  lv_label_set_text(data->account_label, "Default Account");
  lv_obj_set_style_text_font(data->account_label, &lv_font_montserrat_20, 0);
  lv_label_set_long_mode(data->account_label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(data->account_label, 160);
  lv_obj_set_style_text_align(data->account_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(data->account_label, lv_palette_main(LV_PALETTE_BLUE), 0);

  // 2. Clock (Middle)
  data->clock_label = lv_label_create(page_home);
  lv_obj_set_style_text_font(data->clock_label, &lv_font_montserrat_48, 0);
  lv_obj_align(data->clock_label, LV_ALIGN_CENTER, -60, -20); // Center

  // 3. Date (Bottom)
  data->date_label = lv_label_create(page_home);
  lv_obj_set_style_text_font(data->date_label, &lv_font_montserrat_20, 0);
  lv_obj_align(data->date_label, LV_ALIGN_CENTER, -60, 30); // Below Clock

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
  
  // Initial Info Load
  update_account_cache(data);
  
  // Then update
  update_clock(data->clock_timer);

  // Populate Favorites
  populate_favorites(data);
  update_badges(data);

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
    update_account_cache(data);
    update_badges(data);

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
  g_home_data = NULL;
}

void home_applet_on_message(void) {
    if (g_home_data) {
        update_badges(g_home_data);
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
