#ifndef APPLET_H
#define APPLET_H

#include "lvgl.h"

/**
 * Applet lifecycle states
 */
typedef enum {
    APPLET_STATE_STOPPED,   // Not running
    APPLET_STATE_RUNNING,   // Active and visible
    APPLET_STATE_PAUSED     // Running but not visible
} applet_state_t;

/**
 * Forward declaration
 */
typedef struct applet_t applet_t;

/**
 * Applet lifecycle callbacks
 */
typedef struct {
    /**
     * Initialize the applet (allocate resources, create UI)
     * @param applet The applet instance
     * @return 0 on success, negative on error
     */
    int (*init)(applet_t *applet);
    
    /**
     * Start the applet (called when becoming visible)
     * @param applet The applet instance
     */
    void (*start)(applet_t *applet);
    
    /**
     * Pause the applet (called when hidden but kept in memory)
     * @param applet The applet instance
     */
    void (*pause)(applet_t *applet);
    
    /**
     * Resume the applet (called when returning to foreground)
     * @param applet The applet instance
     */
    void (*resume)(applet_t *applet);
    
    /**
     * Stop the applet (called before destroy)
     * @param applet The applet instance
     */
    void (*stop)(applet_t *applet);
    
    /**
     * Destroy the applet (cleanup resources)
     * @param applet The applet instance
     */
    void (*destroy)(applet_t *applet);
} applet_callbacks_t;

/**
 * Applet structure
 */
struct applet_t {
    const char *name;               // Applet name
    const char *description;        // Short description
    const char *icon;               // Icon symbol (LV_SYMBOL_*)
    
    applet_callbacks_t callbacks;   // Lifecycle callbacks
    applet_state_t state;           // Current state
    
    lv_obj_t *screen;              // LVGL screen object (for UI isolation)
    void *user_data;               // Applet-specific data
};

/**
 * Helper macros for defining applets
 */
#define APPLET_DEFINE(var_name, app_name, app_desc, app_icon) \
    applet_t var_name = { \
        .name = app_name, \
        .description = app_desc, \
        .icon = app_icon, \
        .state = APPLET_STATE_STOPPED, \
        .screen = NULL, \
        .user_data = NULL \
    }

#endif // APPLET_H
