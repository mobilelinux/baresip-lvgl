#include "applet_manager.h"
#include "baresip_manager.h"
#include "config_manager.h" // Added for config_manager_init and config_load_app_settings
#include "history_manager.h"
#include "logger.h"
#include "lv_drivers/sdl/sdl.h"
#include "lvgl.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h> // Added for memset
#include <sys/time.h>
#include <unistd.h>
#ifdef APP_USE_FBDEV

#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#endif
#include <stdlib.h>

// Forward declarations for applet registration functions
extern void home_applet_register(void);
extern void settings_applet_register(void);
extern void calculator_applet_register(void);
extern void call_applet_register(void);
extern void contacts_applet_register(void);
extern void call_log_applet_register(void);
extern void chat_applet_register(void);
extern void about_applet_register(void);

// Get current time in milliseconds
static uint32_t get_tick_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

// Global for loop callback
static uint32_t last_tick = 0;
#ifndef USE_FBDEV
extern volatile bool sdl_quit_qry;
#endif

static void ui_loop_cb(void) {
#ifndef USE_FBDEV
  if (sdl_quit_qry) {
    re_cancel();
    return;
  }
#endif

  // Update LVGL tick
  uint32_t current_tick = get_tick_ms();
  if (last_tick == 0)
    last_tick = current_tick;

  uint32_t elapsed = current_tick - last_tick;
  if (elapsed > 0) {
    lv_tick_inc(elapsed);
    last_tick = current_tick;
  }

  // Handle LVGL tasks
  lv_timer_handler();
}

// Initialize LVGL display
static int init_display(void) {
  lv_init();

#ifdef APP_USE_FBDEV

  // FBDEV Initialization
  fbdev_init();

  // Create display buffer (Double buffering or partial)
  static lv_disp_draw_buf_t disp_buf;
  // Use small buffer for FBDEV relative to resolution? Or full? 
  // Existing code used 100 lines for SDL. Let's stick to it.
  const size_t buf_size = 800 * 480 * sizeof(lv_color_t) / 10; // Approx 1/10th screen?
  // Hardcoded for now or use macro if available. SDL_HOR_RES might be SDL specific.
  // Re-use SDL_HOR_RES if defined, or define defaults.
  #ifndef SDL_HOR_RES
  #define SDL_HOR_RES 800
  #endif
  #ifndef SDL_VER_RES
  #define SDL_VER_RES 480
  #endif

  lv_color_t *buf1 = malloc(SDL_HOR_RES * 100 * sizeof(lv_color_t));
  lv_color_t *buf2 = NULL; // Single buffer for FBDEV usually enough unless double buffered
  
  if (!buf1) {
      log_error("Main", "Failed to allocate display buffer");
      return -1;
  }
  
  lv_disp_draw_buf_init(&disp_buf, buf1, buf2, SDL_HOR_RES * 100);

  // Initialize and register display driver
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.draw_buf = &disp_buf;
  disp_drv.flush_cb = fbdev_flush;
  disp_drv.hor_res = SDL_HOR_RES;
  disp_drv.ver_res = SDL_VER_RES;
  // disp_drv.screen_transp = 1; // Not supported on standard FBDEV usually

  lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
  if (!disp) {
    log_error("Main", "Failed to register display driver");
    return -1;
  }

  // Initialize EVDEV input
  evdev_init();
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = evdev_read;
  lv_indev_drv_register(&indev_drv);

#else
  // SDL Initialization
  sdl_init();

  // Create display buffer
  static lv_disp_draw_buf_t disp_buf;
  const size_t buf_size = SDL_HOR_RES * 100 * sizeof(lv_color_t);
  
  lv_color_t *buf1 = malloc(buf_size);
  lv_color_t *buf2 = malloc(buf_size);
  
  if (!buf1 || !buf2) {
      log_error("Main", "Failed to allocate display buffers");
      return -1;
  }
  
  lv_disp_draw_buf_init(&disp_buf, buf1, buf2, SDL_HOR_RES * 100);

  // Initialize and register display driver
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.draw_buf = &disp_buf;
  disp_drv.flush_cb = sdl_display_flush;
  disp_drv.hor_res = SDL_HOR_RES;
  disp_drv.ver_res = SDL_VER_RES;
  disp_drv.screen_transp = 1;

  lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
  if (!disp) {
    log_error("Main", "Failed to register display driver");
    return -1;
  }

  // Initialize mouse input device
  static lv_indev_drv_t indev_drv_mouse;
  lv_indev_drv_init(&indev_drv_mouse);
  indev_drv_mouse.type = LV_INDEV_TYPE_POINTER;
  indev_drv_mouse.read_cb = sdl_mouse_read;
  lv_indev_drv_register(&indev_drv_mouse);
#endif

#ifndef USE_FBDEV
  // Initialize keyboard input device
  static lv_indev_drv_t indev_drv_kb;
  lv_indev_drv_init(&indev_drv_kb);
  indev_drv_kb.type = LV_INDEV_TYPE_KEYPAD;
  indev_drv_kb.read_cb = sdl_keyboard_read;
  lv_indev_t *kb_indev = lv_indev_drv_register(&indev_drv_kb);

  // Create a group for keyboard input and assign it to the device
  lv_group_t *g = lv_group_create();
  lv_group_set_default(g);
  lv_indev_set_group(kb_indev, g);

  // Initialize mousewheel input device
  static lv_indev_drv_t indev_drv_wheel;
  lv_indev_drv_init(&indev_drv_wheel);
  indev_drv_wheel.type = LV_INDEV_TYPE_ENCODER;
  indev_drv_wheel.read_cb = sdl_mousewheel_read;
  lv_indev_drv_register(&indev_drv_wheel);
#endif

  log_info("Main", "LVGL display initialized with SDL2 (%dx%d)", SDL_HOR_RES,
           SDL_VER_RES);
  return 0;
}

int main(void) {
  log_info("Main", "=== LVGL Applet Manager with SDL2 ===");

  // Initialize LVGL display
  if (init_display() != 0) {
    log_error("Main", "Failed to initialize display");
    return 1;
  }

  // Initialize config manager
  config_manager_init();

  // Load config to set log level early
  app_config_t config;
  memset(&config, 0, sizeof(app_config_t));
  if (config_load_app_settings(&config) == 0) {
    logger_init(config.log_level);
    baresip_manager_set_log_level(config.log_level);
  } else {
    logger_init(LOG_LEVEL_INFO);
    baresip_manager_set_log_level(LOG_LEVEL_INFO);
  }

  // Initialize Baresip Manager EARLY (to load modules before applets use them)
  if (baresip_manager_init() != 0) {
    log_error("Main", "Failed to initialize Baresip Manager");
    return 1;
  }

  // Initialize applet manager
  if (applet_manager_init() != 0) {
    log_error("Main", "Failed to initialize applet manager");
    goto cleanup;
  }

  // Register all applets
  log_info("Main", "Registering applets...");
  home_applet_register();
  settings_applet_register();
  calculator_applet_register();
  call_applet_register();
  contacts_applet_register();
  call_log_applet_register();
  chat_applet_register();
  about_applet_register();

  // Force initialization of Call applet to start background SIP services
  log_info("Main", "Initializing background services...");
  int count = 0;
  applet_t **applets = applet_manager_get_all(&count);
  if (applets) {
    for (int i = 0; i < count; i++) {
      if (applets[i] && applets[i]->name &&
          strcmp(applets[i]->name, "Call") == 0) {
        if (applets[i]->callbacks.init) {
          // Manually create screen to satisfy applet_manager requirements
          if (!applets[i]->screen) {
            applets[i]->screen = lv_obj_create(NULL);
          }

          if (applets[i]->callbacks.init(applets[i]) != 0) {
            log_error("Main",
                      "Failed to initialize Call applet background services");
            // Cleanup on failure
            if (applets[i]->screen) {
              lv_obj_del(applets[i]->screen);
              applets[i]->screen = NULL;
            }
          } else {
            // Mark as PAUSED so applet_manager knows it's initialized but
            // background
            applets[i]->state = APPLET_STATE_PAUSED;
            log_info("Main", "Call applet background services initialized");
          }
        }
        break;
      }
    }
  }

  // Launch home screen
  log_info("Main", "Launching home screen...");
  if (applet_manager_launch("Home") != 0) {
    log_error("Main", "Failed to launch home screen");
    return 1;
  }

  log_info("Main", "=== Applet Manager Running ===");
  log_info("Main", "Use mouse to interact with the UI");
  log_info("Main", "Press ESC or close window to exit");

  // Start Baresip main loop with UI callback
  last_tick = get_tick_ms();
  baresip_manager_loop(ui_loop_cb, 5); // 5ms interval for UI updates

cleanup:
  log_info("Main", "=== Shutting down ===");

  // Cleanup
  applet_manager_destroy();

  log_info("Main", "Applet Manager exited successfully!");
  return 0;
}
