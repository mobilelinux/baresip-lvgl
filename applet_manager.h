#ifndef APPLET_MANAGER_H
#define APPLET_MANAGER_H

#include "applet.h"

/**
 * Maximum number of applets that can be registered
 */
#define MAX_APPLETS 16

/**
 * Maximum navigation stack depth
 */
#define MAX_NAV_STACK 8

/**
 * Applet manager structure
 */
typedef struct {
  applet_t *applets[MAX_APPLETS]; // Registered applets
  int applet_count;               // Number of registered applets

  applet_t *nav_stack[MAX_NAV_STACK]; // Navigation stack
  int nav_depth;                      // Current navigation depth

  applet_t *current_applet; // Currently active applet
} applet_manager_t;

/**
 * Initialize the applet manager
 * @return 0 on success, negative on error
 */
int applet_manager_init(void);

/**
 * Register an applet with the manager
 * @param applet The applet to register
 * @return 0 on success, negative on error
 */
int applet_manager_register(applet_t *applet);

/**
 * Launch an applet by name
 * @param name The name of the applet to launch
 * @return 0 on success, negative on error
 */
int applet_manager_launch(const char *name);
applet_t *applet_manager_get_applet(const char *name);

/**
 * Launch an applet by pointer
 * @param applet The applet to launch
 * @return 0 on success, negative on error
 */
int applet_manager_launch_applet(applet_t *applet);

/**
 * Go back to the previous applet in the navigation stack
 * @return 0 on success, negative on error
 */
int applet_manager_back(void);

/**
 * Close the current applet and return to previous
 * @return 0 on success, negative on error
 */
int applet_manager_close_current(void);

/**
 * Get the current active applet
 * @return Pointer to current applet or NULL
 */
applet_t *applet_manager_get_current(void);

/**
 * Get all registered applets
 * @param count Output parameter for applet count
 * @return Array of applet pointers
 */
applet_t **applet_manager_get_all(int *count);

/**
 * Cleanup and destroy the applet manager
 */
void applet_manager_destroy(void);

/**
 * Show a toast message
 * @param msg The message to display
 */
void applet_manager_show_toast(const char *msg);

#endif // APPLET_MANAGER_H
