#include "applet_manager.h"
#include "baresip_manager.h"
#include "lv_drivers/sdl/sdl.h"
#include "lvgl.h"
#include <SDL.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

// Forward declarations for applet registration functions
extern void home_applet_register(void);
extern void settings_applet_register(void);
extern void calculator_applet_register(void);
extern void call_applet_register(void);
extern void contacts_applet_register(void);
extern void call_log_applet_register(void);

// Get current time in milliseconds
static uint32_t get_tick_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

// Global for loop callback
static uint32_t last_tick = 0;
extern volatile bool sdl_quit_qry;

static void ui_loop_cb(void) {
  if (sdl_quit_qry) {
    re_cancel();
    return;
  }

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

// Initialize LVGL display with SDL2
static int init_display(void) {
  lv_init();

  // Initialize SDL
  sdl_init();

  // Create display buffer
  static lv_disp_draw_buf_t disp_buf;
  static lv_color_t buf1[SDL_HOR_RES * 100];
  static lv_color_t buf2[SDL_HOR_RES * 100];
  lv_disp_draw_buf_init(&disp_buf, buf1, buf2, SDL_HOR_RES * 100);

  // Initialize and register display driver
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.draw_buf = &disp_buf;
  disp_drv.flush_cb = sdl_display_flush;
  disp_drv.hor_res = SDL_HOR_RES;
  disp_drv.ver_res = SDL_VER_RES;

  lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
  if (!disp) {
    printf("Failed to register display driver\n");
    return -1;
  }

  // Initialize mouse input device
  static lv_indev_drv_t indev_drv_mouse;
  lv_indev_drv_init(&indev_drv_mouse);
  indev_drv_mouse.type = LV_INDEV_TYPE_POINTER;
  indev_drv_mouse.read_cb = sdl_mouse_read;
  lv_indev_drv_register(&indev_drv_mouse);

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

  printf("LVGL display initialized with SDL2 (%dx%d)\n", SDL_HOR_RES,
         SDL_VER_RES);
  return 0;
}

int main(void) {
  printf("=== LVGL Applet Manager with SDL2 ===\n");

  // Initialize LVGL display
  if (init_display() != 0) {
    printf("Failed to initialize display\n");
    return 1;
  }

  // Initialize applet manager
  if (applet_manager_init() != 0) {
    fprintf(stderr, "Failed to initialize applet manager\n");
    goto cleanup;
  }

  // Register all applets
  printf("\nRegistering applets...\n");
  home_applet_register();
  settings_applet_register();
  calculator_applet_register();
  call_applet_register();
  contacts_applet_register();
  call_log_applet_register();

  // Force initialization of Call applet to start background SIP services
  printf("\nInitializing background services...\n");
  int count = 0;
  applet_t **applets = applet_manager_get_all(&count);
  if (applets) {
    for (int i = 0; i < count; i++) {
      if (applets[i] && applets[i]->name &&
          strcmp(applets[i]->name, "Call") == 0) {
        if (applets[i]->callbacks.init) {
          if (applets[i]->callbacks.init(applets[i]) != 0) {
            printf("Failed to initialize Call applet background services\n");
          } else {
            printf("Call applet background services initialized\n");
          }
        }
        break;
      }
    }
  }

  // Launch home screen
  printf("\nLaunching home screen...\n");
  if (applet_manager_launch("Home") != 0) {
    printf("Failed to launch home screen\n");
    return 1;
  }

  printf("\n=== Applet Manager Running ===\n");
  printf("Use mouse to interact with the UI\n");
  printf("Press ESC or close window to exit\n\n");

  // Start Baresip main loop with UI callback
  last_tick = get_tick_ms();
  baresip_manager_loop(ui_loop_cb, 5); // 5ms interval for UI updates

cleanup:
  printf("\n=== Shutting down ===\n");

  // Cleanup
  applet_manager_destroy();

  printf("Applet Manager exited successfully!\n");
  return 0;
}
