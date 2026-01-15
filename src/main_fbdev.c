#include "applet_manager.h"
#include "baresip_manager.h"
#include "config_manager.h"
#include "history_manager.h"
#include "logger.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#include "lv_drivers/indev/evdev.h"
#include "lvgl.h"
#include <re.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/time.h>
#include <unistd.h>
#include <linux/input.h>
#include <fcntl.h>
#include <errno.h>

extern void home_applet_register(void);
extern void settings_applet_register(void);
//extern void calculator_applet_register(void);
extern void call_applet_register(void);
extern void contacts_applet_register(void);
extern void call_log_applet_register(void);
extern void about_applet_register(void);
extern void chat_applet_register(void);

static uint32_t get_tick_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

static uint32_t last_tick = 0;

static void ui_loop_cb(void) {
  uint32_t current_tick = get_tick_ms();
  if (last_tick == 0)
    last_tick = current_tick;

  uint32_t elapsed = current_tick - last_tick;
  if (elapsed > 0) {
    lv_tick_inc(elapsed);
    last_tick = current_tick;
  }
  
  // Process Video Frames
  baresip_manager_process_video();
  
  lv_timer_handler();
}

static int kbd_fd = -1;
static uint32_t last_key = 0;
static uint32_t last_key_state = LV_INDEV_STATE_REL;

static void keyboard_init(void) {
    const char * dev_name = "/dev/input/event0";
    kbd_fd = open(dev_name, O_RDWR | O_NOCTTY | O_NDELAY);
    if(kbd_fd == -1) {
        log_error("Main", "unable to open keyboard interface: %s", dev_name);
        return;
    }
    fcntl(kbd_fd, F_SETFL, O_ASYNC | O_NONBLOCK);
}

static bool shift_pressed = false;

// Helper to map keycode to ASCII
static char evdev_key_to_ascii(uint16_t code, bool shift) {
    if (code >= KEY_1 && code <= KEY_9) {
        char base = '1' + (code - KEY_1);
        char shifted[] = {'!', '@', '#', '$', '%', '^', '&', '*', '('};
        return shift ? shifted[code - KEY_1] : base;
    }
    if (code == KEY_0) return shift ? ')' : '0';

    // Alphabet Mapping - Explicit Switch Case
    // Linux Input Event Codes are NOT alphabetical.
    if (code >= KEY_Q && code <= KEY_M) { // Broad range check optimization optional, but switch is safest
         // Proceed to switch
    }

    char ch = 0;
    switch(code) {
        case KEY_A: ch = 'a'; break;
        case KEY_B: ch = 'b'; break;
        case KEY_C: ch = 'c'; break;
        case KEY_D: ch = 'd'; break;
        case KEY_E: ch = 'e'; break;
        case KEY_F: ch = 'f'; break;
        case KEY_G: ch = 'g'; break;
        case KEY_H: ch = 'h'; break;
        case KEY_I: ch = 'i'; break;
        case KEY_J: ch = 'j'; break;
        case KEY_K: ch = 'k'; break;
        case KEY_L: ch = 'l'; break;
        case KEY_M: ch = 'm'; break;
        case KEY_N: ch = 'n'; break;
        case KEY_O: ch = 'o'; break;
        case KEY_P: ch = 'p'; break;
        case KEY_Q: ch = 'q'; break;
        case KEY_R: ch = 'r'; break;
        case KEY_S: ch = 's'; break;
        case KEY_T: ch = 't'; break;
        case KEY_U: ch = 'u'; break;
        case KEY_V: ch = 'v'; break;
        case KEY_W: ch = 'w'; break;
        case KEY_X: ch = 'x'; break;
        case KEY_Y: ch = 'y'; break;
        case KEY_Z: ch = 'z'; break;
        
        case KEY_GRAVE:      ch = shift ? '~' : '`'; break;
        case KEY_MINUS:      ch = shift ? '_' : '-'; break;
        case KEY_EQUAL:      ch = shift ? '+' : '='; break;
        case KEY_LEFTBRACE:  ch = shift ? '{' : '['; break;
        case KEY_RIGHTBRACE: ch = shift ? '}' : ']'; break;
        case KEY_BACKSLASH:  ch = shift ? '|' : '\\'; break;
        case KEY_SEMICOLON:  ch = shift ? ':' : ';'; break;
        case KEY_APOSTROPHE: ch = shift ? '"' : '\''; break;
        case KEY_COMMA:      ch = shift ? '<' : ','; break;
        case KEY_DOT:        ch = shift ? '>' : '.'; break;
        case KEY_SLASH:      ch = shift ? '?' : '/'; break;
        case KEY_SPACE:      ch = ' '; break;
        case KEY_KPASTERISK: ch = '*'; break;
        case KEY_KPDOT:      ch = '.'; break;
        case KEY_KPMINUS:    ch = '-'; break;
        case KEY_KPPLUS:     ch = '+'; break;
        case KEY_KPEQUAL:    ch = '='; break;
        default: return 0;
    }
    
    // Handle shift for letters
    if (ch >= 'a' && ch <= 'z' && shift) {
        return ch - 32;
    }
    
    // For non-letters already handled by switch
    return ch;
}

static void keyboard_read(lv_indev_drv_t * drv, lv_indev_data_t * data) {
    (void)drv;
    if (kbd_fd == -1) return;

    struct input_event in;
    while(read(kbd_fd, &in, sizeof(struct input_event)) > 0) {
        if(in.type == EV_KEY) {
            // Track Shift State
            if (in.code == KEY_LEFTSHIFT || in.code == KEY_RIGHTSHIFT) {
                shift_pressed = (in.value != 0);
            }
            
            uint32_t key = 0;
            // Handle Control Keys
            switch(in.code) {
                case KEY_BACKSPACE: key = LV_KEY_BACKSPACE; break;
                case KEY_ENTER:     key = LV_KEY_ENTER; break;
                case KEY_UP:        key = LV_KEY_UP; break;
                case KEY_DOWN:      key = LV_KEY_DOWN; break;
                case KEY_LEFT:      key = LV_KEY_LEFT; break;
                case KEY_RIGHT:     key = LV_KEY_RIGHT; break;
                case KEY_TAB:       
                    key = shift_pressed ? LV_KEY_PREV : LV_KEY_NEXT; 
                    break;
                case KEY_ESC:       key = LV_KEY_ESC; break; // Useful for cancelling
                case KEY_DELETE:    key = LV_KEY_DEL; break;
                default: 
                    // ASCII Mapping
                    key = (uint32_t)evdev_key_to_ascii(in.code, shift_pressed);
                    break;
            }
            
            if(key != 0) {
                 last_key = key;
                 if (in.value) { // Log on press only
                    printf("KBD: Code=%d Shift=%d Char='%c' (%d)\n", in.code, shift_pressed, (char)key, key);
                 }
            }
            // Update state
            last_key_state = (in.value) ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
        }
    }
    
    data->key = last_key;
    data->state = last_key_state;
}

static int init_display(void) {
  lv_init();

  // Initialize Framebuffer
  // system("fbset"); // DEBUG: Print actual resolution
  fbdev_init();

  // Initialize Input (Event Device)
  evdev_init();

  // Create display buffer
  // Use 800x600 as verified by fbset
  #define DISPLAY_WIDTH 800
  #define DISPLAY_HEIGHT 600
  
  static lv_disp_draw_buf_t disp_buf;
  static lv_color_t buf1[DISPLAY_WIDTH * 100];
  static lv_color_t buf2[DISPLAY_WIDTH * 100];
  lv_disp_draw_buf_init(&disp_buf, buf1, buf2, DISPLAY_WIDTH * 100);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.draw_buf = &disp_buf;
  disp_drv.flush_cb = fbdev_flush;
  disp_drv.hor_res = DISPLAY_WIDTH;
  disp_drv.ver_res = DISPLAY_HEIGHT;
  
  lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
  if (!disp) {
    log_error("Main", "Failed to register display driver");
    return -1;
  }

  // Initialize Mouse/Touch (first event device configured in lv_drv_conf.h - event1)
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = evdev_read;
  lv_indev_t *mouse_indev = lv_indev_drv_register(&indev_drv);

  // Create a cursor object on the system layer so it's always on top
  // Create a cursor object
  lv_obj_t *cursor_obj = lv_label_create(lv_layer_sys());
  lv_label_set_text(cursor_obj, "+"); 
  
  // CRITICAL: Explicitly align to TOP_LEFT to ensure (0,0) coordinates map to screen origin
  lv_obj_align(cursor_obj, LV_ALIGN_TOP_LEFT, 0, 0);

  // Center the cursor
  lv_obj_set_style_translate_x(cursor_obj, -5, 0);
  lv_obj_set_style_translate_y(cursor_obj, -8, 0);

  lv_indev_set_cursor(mouse_indev, cursor_obj);

  // Initialize Keyboard (manually handling event0)
  keyboard_init();
  static lv_indev_drv_t kbd_drv;
  lv_indev_drv_init(&kbd_drv);
  kbd_drv.type = LV_INDEV_TYPE_KEYPAD;
  kbd_drv.read_cb = keyboard_read;
  lv_indev_t * kbd_indev = lv_indev_drv_register(&kbd_drv);

  // Create a default group for keyboard focus
  lv_group_t * g = lv_group_create();
  lv_group_set_default(g);
  lv_indev_set_group(kbd_indev, g);
  
  // printf("Main: LVGL display initialized with FBDEV (%dx%d) - KBD Fix v1\n", DISPLAY_WIDTH, DISPLAY_HEIGHT);
  return 0;
}

int main(void) {
  setbuf(stdout, NULL);
  
  // Initialize re library (CRITICAL: Must be first, before logger or any modules)
  int err = libre_init();
  if (err) {
    fprintf(stderr, "Main: libre_init() failed: %d\n", err);
    return 1;
  }
  sys_coredump_set(true);

  printf("Main: Step 1 - libre_init success\n");

  // printf("Main: === LVGL Applet Manager with FBDEV (KBD Fix v1) ===\n");

  if (init_display() != 0) {
    log_error("Main", "Failed to initialize display");
    return 1;
  }
  printf("Main: Step 2 - init_display success\n");

  config_manager_init();

  app_config_t config;
  memset(&config, 0, sizeof(app_config_t));
  if (config_load_app_settings(&config) == 0) {
    logger_init(config.log_level);
    baresip_manager_set_log_level(config.log_level);
  } else {
    logger_init(LOG_LEVEL_INFO);
    baresip_manager_set_log_level(LOG_LEVEL_INFO);
  }
  printf("Main: Step 3 - Config and Logger initialized\n");

  if (baresip_manager_init() != 0) {
    printf("Main: Baresip Manager Init FAILED via printf\n");
    log_error("Main", "Failed to initialize Baresip Manager");
    return 1;
  }
  printf("Main: Step 4 - Baresip Manager initialized\n");

  if (applet_manager_init() != 0) {
    log_error("Main", "Failed to initialize applet manager");
    goto cleanup;
  }

  printf("Main: Registering applets...\n");
  home_applet_register();
  settings_applet_register();
  //calculator_applet_register();
  call_applet_register();
  contacts_applet_register();
  call_log_applet_register();
  chat_applet_register();
  about_applet_register();

  printf("Main: Initializing background services...\n");
  int count = 0;
  applet_t **applets = applet_manager_get_all(&count);
  if (applets) {
    for (int i = 0; i < count; i++) {
      if (applets[i] && applets[i]->name &&
          strcmp(applets[i]->name, "Call") == 0) {
        if (applets[i]->callbacks.init) {
          if (!applets[i]->screen) {
            applets[i]->screen = lv_obj_create(NULL);
          }
          if (applets[i]->callbacks.init(applets[i]) != 0) {
            log_error("Main", "Failed to initialize Call applet background services");
            if (applets[i]->screen) {
              lv_obj_del(applets[i]->screen);
              applets[i]->screen = NULL;
            }
          } else {
            applets[i]->state = APPLET_STATE_PAUSED;
            printf("Main: Call applet background services initialized\n");
            fflush(stdout);
          }
        }
        break;
      }
    }
  }

  printf("Main: Launching home screen...\n");
  fflush(stdout);
  if (applet_manager_launch("Home") != 0) {
    log_error("Main", "Failed to launch home screen");
    return 1;
  }

  printf("=== Applet Manager Running (FBDEV) ===\n");
  fflush(stdout);

  last_tick = get_tick_ms();
  printf("Main: Entering Baresip Manager Loop...\n");
  fflush(stdout);
  baresip_manager_loop(ui_loop_cb, 5);

cleanup:
  log_info("Main", "=== Shutting down ===");
  applet_manager_destroy();
  log_info("Main", "Applet Manager exited successfully!");
  return 0;
}
