#ifndef APPLET_H
#define APPLET_H

#include "lvgl.h"

typedef struct applet_s applet_t;

typedef enum {
    APPLET_STATE_STOPPED,
    APPLET_STATE_PAUSED,
    APPLET_STATE_RUNNING
} applet_state_t;

typedef struct {
    int (*init)(applet_t *applet);
    void (*start)(applet_t *applet);
    void (*stop)(applet_t *applet);
    void (*pause)(applet_t *applet);
    void (*resume)(applet_t *applet);
    void (*destroy)(applet_t *applet);
} applet_callbacks_t;

struct applet_s {
    const char *name;
    const char *description;
    const char *icon;
    
    applet_state_t state;
    lv_obj_t *screen;
    void *user_data;
    
    applet_callbacks_t callbacks;
};

#define APPLET_DEFINE(var, name_str, desc_str, icon_str) \
    applet_t var = { \
        .name = name_str, \
        .description = desc_str, \
        .icon = icon_str, \
        .state = APPLET_STATE_STOPPED, \
        .screen = NULL, \
        .user_data = NULL, \
        .callbacks = {0} \
    }

#endif // APPLET_H
