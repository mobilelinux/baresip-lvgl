#include "applet_manager.h"
#include "logger.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

// Global applet manager instance
static applet_manager_t g_manager = {
    .applet_count = 0, .nav_depth = 0, .current_applet = NULL};

int applet_manager_init(void) {
  memset(&g_manager, 0, sizeof(applet_manager_t));
  log_info("AppletManager", "Initialized");
  return 0;
}

int applet_manager_register(applet_t *applet) {
  if (!applet) {
    log_warn("AppletManager", "Error: NULL applet");
    return -1;
  }

  if (g_manager.applet_count >= MAX_APPLETS) {
    log_warn("AppletManager", "Error: Maximum applets reached");
    return -2;
  }

  g_manager.applets[g_manager.applet_count++] = applet;
  log_info("AppletManager", "Registered applet: %s", applet->name);
  return 0;
}

static int applet_init_if_needed(applet_t *applet) {
  if (!applet)
    return -1;

  // Only initialize if not already initialized
  if (applet->state == APPLET_STATE_STOPPED && !applet->screen) {
    // Create screen for this applet
    applet->screen = lv_obj_create(NULL);
    if (!applet->screen) {
      log_error("AppletManager", "Error: Failed to create screen for %s",
                applet->name);
      return -1;
    }

    // Call init callback if provided
    if (applet->callbacks.init) {
      int ret = applet->callbacks.init(applet);
      if (ret != 0) {
        log_error("AppletManager", "Error: Init failed for %s", applet->name);
        lv_obj_del(applet->screen);
        applet->screen = NULL;
        return ret;
      }
    }

    log_info("AppletManager", "Initialized applet: %s", applet->name);
  }

  return 0;
}

int applet_manager_launch_applet(applet_t *applet) {
  if (!applet) {
    log_warn("AppletManager", "Error: NULL applet");
    return -1;
  }

  // Initialize applet if needed
  if (applet_init_if_needed(applet) != 0) {
    return -2;
  }

  // Pause current applet if exists
  if (g_manager.current_applet && g_manager.current_applet != applet) {
    if (g_manager.current_applet->callbacks.pause) {
      g_manager.current_applet->callbacks.pause(g_manager.current_applet);
    }
    g_manager.current_applet->state = APPLET_STATE_PAUSED;

    // Add to navigation stack
    if (g_manager.nav_depth < MAX_NAV_STACK) {
      g_manager.nav_stack[g_manager.nav_depth++] = g_manager.current_applet;
    }
  }

  // Start or resume the new applet
  if (applet->state == APPLET_STATE_PAUSED) {
    if (applet->callbacks.resume) {
      applet->callbacks.resume(applet);
    }
  } else {
    if (applet->callbacks.start) {
      applet->callbacks.start(applet);
    }
  }

  applet->state = APPLET_STATE_RUNNING;
  g_manager.current_applet = applet;

  // Load the screen with animation
  lv_scr_load_anim(applet->screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);

  log_info("AppletManager", "Launched applet: %s", applet->name);
  return 0;
}

int applet_manager_launch(const char *name) {
  if (!name)
    return -1;

  // Find applet by name
  for (int i = 0; i < g_manager.applet_count; i++) {
    if (strcmp(g_manager.applets[i]->name, name) == 0) {
      return applet_manager_launch_applet(g_manager.applets[i]);
    }
  }

  log_error("AppletManager", "Error: Applet not found: %s", name);
  return -3;
}

int applet_manager_back(void) {
  if (g_manager.nav_depth == 0) {
    log_debug("AppletManager", "No previous applet in stack");
    return -1;
  }

  // Get previous applet from stack
  applet_t *prev_applet = g_manager.nav_stack[--g_manager.nav_depth];

  // Pause current applet
  if (g_manager.current_applet) {
    if (g_manager.current_applet->callbacks.pause) {
      g_manager.current_applet->callbacks.pause(g_manager.current_applet);
    }
    g_manager.current_applet->state = APPLET_STATE_PAUSED;
  }

  // Resume previous applet
  if (prev_applet->callbacks.resume) {
    prev_applet->callbacks.resume(prev_applet);
  }
  prev_applet->state = APPLET_STATE_RUNNING;
  g_manager.current_applet = prev_applet;

  // Load the screen with animation
  lv_scr_load_anim(prev_applet->screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0,
                   false);

  log_debug("AppletManager", "Back to applet: %s", prev_applet->name);
  return 0;
}

int applet_manager_close_current(void) {
  if (!g_manager.current_applet) {
    return -1;
  }

  applet_t *applet = g_manager.current_applet;

  // Stop the applet
  if (applet->callbacks.stop) {
    applet->callbacks.stop(applet);
  }

  // Destroy the applet
  if (applet->callbacks.destroy) {
    applet->callbacks.destroy(applet);
  }

  // Delete screen
  if (applet->screen) {
    lv_obj_del(applet->screen);
    applet->screen = NULL;
  }

  applet->state = APPLET_STATE_STOPPED;
  log_info("AppletManager", "Closed applet: %s", applet->name);

  // Go back to previous applet
  return applet_manager_back();
}

applet_t *applet_manager_get_current(void) { return g_manager.current_applet; }

applet_t **applet_manager_get_all(int *count) {
  if (count) {
    *count = g_manager.applet_count;
  }
  return g_manager.applets;
}

void applet_manager_destroy(void) {
  // Stop and destroy all applets
  for (int i = 0; i < g_manager.applet_count; i++) {
    applet_t *applet = g_manager.applets[i];

    if (applet->state != APPLET_STATE_STOPPED) {
      if (applet->callbacks.stop) {
        applet->callbacks.stop(applet);
      }
    }

    if (applet->callbacks.destroy) {
      applet->callbacks.destroy(applet);
    }

    if (applet->screen) {
      lv_obj_del(applet->screen);
      applet->screen = NULL;
    }

    applet->state = APPLET_STATE_STOPPED;
  }

  memset(&g_manager, 0, sizeof(applet_manager_t));
  log_info("AppletManager", "Destroyed");
}

static void toast_del_cb(lv_timer_t *t) {
  lv_obj_t *toast = (lv_obj_t *)t->user_data;
  if (lv_obj_is_valid(toast)) {
    lv_obj_del(toast);
  }
}

void applet_manager_show_toast(const char *msg) {
  if (!msg)
    return;

  lv_obj_t *toast = lv_obj_create(lv_layer_top());
  lv_obj_set_size(toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(toast, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(toast, LV_OPA_80, 0);
  lv_obj_set_style_radius(toast, 20, 0);
  lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -100);
  lv_obj_clear_flag(toast, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *label = lv_label_create(toast);
  lv_label_set_text(label, msg);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_center(label);

  // Auto-delete timer
  lv_timer_t *timer = lv_timer_create(toast_del_cb, 2000, toast);
  lv_timer_set_repeat_count(timer, 1);
}
