#include <sys/ioctl.h>
#include <errno.h>
#include <stdio.h>
/**
 * @file evdev.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "evdev.h"
#if USE_EVDEV != 0 || USE_BSD_EVDEV

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#if USE_BSD_EVDEV
#include <dev/evdev/input.h>
#else
#include <linux/input.h>
#endif

#if USE_XKB
#include "xkb.h"
#endif /* USE_XKB */

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
int map(int x, int in_min, int in_max, int out_min, int out_max);

/**********************
 *  STATIC VARIABLES
 **********************/
int evdev_fd = -1;
int evdev_root_x;
int evdev_root_y;
int evdev_button;

int evdev_key_val;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Initialize the evdev interface
 */
void evdev_init(void)
{ 
    puts("EVDEV: evdev_init called - Auto-detecting device (Priority Mode)..."); fflush(stdout);
    
    char dev_path[32];
    char best_dev_path[32];
    best_dev_path[0] = '\0';
    int best_priority = 0; /* 0=None, 1=REL, 2=ABS */
    int i;

    /* Scan /dev/input/event0 to event9 */
    for (i = 0; i < 10; i++) {
        sprintf(dev_path, "/dev/input/event%d", i);
        int fd = open(dev_path, O_RDONLY);
        if (fd >= 0) {
            unsigned long ev_bits[1] = {0};
            
            #ifndef EVIOCGBIT
            #define EVIOCGBIT(ev,len)  _IOC(_IOC_READ, 'E', 0x20 + (ev), len)
            #endif

            if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) >= 0) {
                 int has_rel = (ev_bits[0] >> 2) & 1; /* EV_REL = 0x02 */
                 int has_abs = (ev_bits[0] >> 3) & 1; /* EV_ABS = 0x03 */
                 
                 int current_priority = 0;
                 if (has_abs) current_priority = 2;
                 else if (has_rel) current_priority = 1;

                 if (current_priority > 0) {
                     printf("EVDEV: Candidate %s (REL=%d, ABS=%d, Prio=%d)\n", dev_path, has_rel, has_abs, current_priority);
                     fflush(stdout);
                 }

                 if (current_priority > best_priority) {
                     best_priority = current_priority;
                     sprintf(best_dev_path, "%s", dev_path);
                 }
            }
            close(fd);
            if (best_priority == 2) break;
        }
    }

    if (best_priority > 0) {
        printf("EVDEV: Selected best device: %s (Priority %d)\n", best_dev_path, best_priority);
        evdev_set_file(best_dev_path);
    } else {
        printf("EVDEV: No suitable device found, trying default %s\n", EVDEV_NAME);
        evdev_set_file(EVDEV_NAME);
    }

#if USE_XKB
    xkb_init();
#endif
}
// Global calibration variables (initialized to defaults)
static int evdev_hor_min = EVDEV_HOR_MIN;
static int evdev_hor_max = EVDEV_HOR_MAX;
static int evdev_ver_min = EVDEV_VER_MIN;
static int evdev_ver_max = EVDEV_VER_MAX;

/**
 * reconfigure the device file for evdev
 * @param dev_name set the evdev device filename
 * @return true: the device file set complete
 *         false: the device file doesn't exist current system
 */
bool evdev_set_file(const char* dev_name)
{ 
     if(evdev_fd != -1) {
        close(evdev_fd);
     }
#if USE_BSD_EVDEV
     evdev_fd = open(dev_name, O_RDWR | O_NOCTTY);
#else
     evdev_fd = open(dev_name, O_RDWR | O_NOCTTY | O_NDELAY);
#endif

     if(evdev_fd == -1) {
        perror("unable to open evdev interface:");
        fflush(stdout);
        return false;
     }

#if USE_BSD_EVDEV
     fcntl(evdev_fd, F_SETFL, O_NONBLOCK);
#else
     fcntl(evdev_fd, F_SETFL, O_ASYNC | O_NONBLOCK);
#endif

     evdev_root_x = 0;
     evdev_root_y = 0;
     evdev_key_val = 0;
     evdev_button = LV_INDEV_STATE_REL;

     // Dynamic Calibration: Read axis limits from kernel
     struct input_absinfo abs_info;
     if (ioctl(evdev_fd, EVIOCGABS(ABS_X), &abs_info) >= 0) {
         evdev_hor_min = abs_info.minimum;
         evdev_hor_max = abs_info.maximum;
     } else {
         evdev_hor_min = 0;
         evdev_hor_max = 0; // Default to Mouse/Relative mode
     }

     if (ioctl(evdev_fd, EVIOCGABS(ABS_Y), &abs_info) >= 0) {
         evdev_ver_min = abs_info.minimum;
         evdev_ver_max = abs_info.maximum;
     } else {
         evdev_ver_min = 0;
         evdev_ver_max = 0; // Default to Mouse/Relative mode
     }

      printf("EVDEV: Calibrated X=%d..%d, Y=%d..%d\n", 
             evdev_hor_min, evdev_hor_max, evdev_ver_min, evdev_ver_max);

     puts("EVDEV: Open success"); fflush(stdout);
     return true;
}

/**
 * Get the current position and state of the evdev
 * @param data store the evdev data here
 */
void evdev_read(lv_indev_drv_t * drv, lv_indev_data_t * data)
{
    struct input_event in;

    while(read(evdev_fd, &in, sizeof(struct input_event)) > 0) {
        if(in.type == EV_REL || in.type == EV_ABS || in.type == EV_KEY) {
            if (in.type == EV_ABS) {
                // Debug raw inputs to verify range
                // if (in.code == ABS_X) printf("EVDEV: Raw ABS_X=%d\n", in.value);
                // else if (in.code == ABS_Y) printf("EVDEV: Raw ABS_Y=%d\n", in.value);
            }
        }
        if(in.type == EV_REL) {
            // Only use relative motion if we haven't calibrated Absolute X/Y
            // This prevents fighting between Mouse (REL) and Tablet (ABS)
            if (evdev_hor_max == 0 || evdev_hor_max == -1) {
               if(in.code == REL_X)
				#if EVDEV_SWAP_AXES
					evdev_root_y += in.value;
				#else
					evdev_root_x += in.value;
				#endif
               else if(in.code == REL_Y)
				#if EVDEV_SWAP_AXES
					evdev_root_x += in.value;
				#else
					evdev_root_y += in.value;
				#endif
            }
        } else if(in.type == EV_ABS) {
            // DEBUG RAW
            if (in.code == ABS_X) printf("EVDEV: Raw ABS_X=%d (Max=%d)\n", in.value, evdev_hor_max);
            else if (in.code == ABS_Y) printf("EVDEV: Raw ABS_Y=%d (Max=%d)\n", in.value, evdev_ver_max);

            if(in.code == ABS_X)
				#if EVDEV_SWAP_AXES
					evdev_root_y = in.value;
				#else
					evdev_root_x = in.value;
				#endif
            else if(in.code == ABS_Y)
				#if EVDEV_SWAP_AXES
					evdev_root_x = in.value;
				#else
					evdev_root_y = in.value;
				#endif
            else if(in.code == ABS_MT_POSITION_X)
                                #if EVDEV_SWAP_AXES
                                        evdev_root_y = in.value;
                                #else
                                        evdev_root_x = in.value;
                                #endif
            else if(in.code == ABS_MT_POSITION_Y)
                                #if EVDEV_SWAP_AXES
                                        evdev_root_x = in.value;
                                #else
                                        evdev_root_y = in.value;
                                #endif
            else if(in.code == ABS_MT_TRACKING_ID) {
                                if(in.value == -1)
                                    evdev_button = LV_INDEV_STATE_REL;
                                else if(in.value == 0)
                                    evdev_button = LV_INDEV_STATE_PR;
            }
        } else if(in.type == EV_KEY) {
            if(in.code == BTN_MOUSE || in.code == BTN_TOUCH) {
                if(in.value == 0)
                    evdev_button = LV_INDEV_STATE_REL;
                else if(in.value == 1)
                    evdev_button = LV_INDEV_STATE_PR;
            } else if(drv->type == LV_INDEV_TYPE_KEYPAD) {
#if USE_XKB
                data->key = xkb_process_key(in.code, in.value != 0);
#else
                switch(in.code) {
                    case KEY_BACKSPACE:
                        data->key = LV_KEY_BACKSPACE;
                        break;
                    case KEY_ENTER:
                        data->key = LV_KEY_ENTER;
                        break;
                    case KEY_PREVIOUS:
                        data->key = LV_KEY_PREV;
                        break;
                    case KEY_NEXT:
                        data->key = LV_KEY_NEXT;
                        break;
                    case KEY_UP:
                        data->key = LV_KEY_UP;
                        break;
                    case KEY_LEFT:
                        data->key = LV_KEY_LEFT;
                        break;
                    case KEY_RIGHT:
                        data->key = LV_KEY_RIGHT;
                        break;
                    case KEY_DOWN:
                        data->key = LV_KEY_DOWN;
                        break;
                    case KEY_TAB:
                        data->key = LV_KEY_NEXT;
                        break;
                    default:
                        data->key = 0;
                        break;
                }
#endif /* USE_XKB */
                if (data->key != 0) {
                    /* Only record button state when actual output is produced to prevent widgets from refreshing */
                    data->state = (in.value) ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
                    evdev_key_val = data->key;
                    evdev_button = data->state;
                    return;
                }
                // If key is 0 (e.g. modifier), return dummy REL event to prevent repeat
                data->key = 0;
                data->state = LV_INDEV_STATE_REL;
                return;
            }
        }
    }

    if(drv->type == LV_INDEV_TYPE_KEYPAD) {
        /* No data retrieved */
        data->key = evdev_key_val;
        data->state = evdev_button;
        if (data->key != 0 && data->state == LV_INDEV_STATE_PR) {
             // printf("Fallthrough REPEAT [InputDebug_Token_X123]. Key=%d State=%d\n", data->key, data->state);
        }
        return;
    }
    if(drv->type != LV_INDEV_TYPE_POINTER)
        return ;
    /*Store the collected data*/

    if (evdev_hor_max == 0) {
       /* Relative Mode: Use coordinates directly */
       data->point.x = evdev_root_x;
       data->point.y = evdev_root_y;
    } else {
       /* Absolute Mode: Scale from input range to screen resolution */
       data->point.x = map(evdev_root_x, evdev_hor_min, evdev_hor_max, 0, drv->disp->driver->hor_res - 1);
       data->point.y = map(evdev_root_y, evdev_ver_min, evdev_ver_max, 0, drv->disp->driver->ver_res - 1);
    }

    data->state = evdev_button;

    if(data->point.x < 0)
      data->point.x = 0;
    if(data->point.y < 0)
      data->point.y = 0;
    if(data->point.x >= drv->disp->driver->hor_res)
      data->point.x = drv->disp->driver->hor_res - 1;
    if(data->point.y >= drv->disp->driver->ver_res)
      data->point.y = drv->disp->driver->ver_res - 1;

    return ;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
int map(int x, int in_min, int in_max, int out_min, int out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

#endif
