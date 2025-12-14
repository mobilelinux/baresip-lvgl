#ifndef BARESIP_MANAGER_H
#define BARESIP_MANAGER_H

#include "config_manager.h"
#include <baresip.h>
#include <inttypes.h>
#include <re.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

typedef enum {
  REG_STATUS_NONE = 0,
  REG_STATUS_REGISTERING,
  REG_STATUS_REGISTERED,
  REG_STATUS_FAILED
} reg_status_t;

typedef void (*call_event_cb)(enum call_state state, const char *peer_uri,
                              void *call_id);
typedef void (*reg_event_cb)(const char *aor, reg_status_t status);

int baresip_manager_init(void);
void baresip_manager_set_callback(call_event_cb cb);
void baresip_manager_set_reg_callback(reg_event_cb cb);
reg_status_t baresip_manager_get_account_status(const char *aor);
int baresip_manager_call(const char *uri);
int baresip_manager_answer(void);
int baresip_manager_hangup(void);
int baresip_manager_send_dtmf(char key);
enum call_state baresip_manager_get_state(void);
const char *baresip_manager_get_peer(void);
void baresip_manager_mute(bool mute);
bool baresip_manager_is_muted(void);
int baresip_manager_add_account(const voip_account_t *account);
void baresip_manager_destroy(void);

/**
 * Run the Baresip main loop (re_main)
 * @param ui_loop_cb Callback to run periodically (e.g. for LVGL)
 * @param interval_ms Interval in milliseconds for the callback
 */
void baresip_manager_loop(void (*ui_loop_cb)(void), int interval_ms);

typedef struct {
  void *id; // opaque pointer to struct call
  char peer_uri[256];
  enum call_state state;
  bool is_held;
  bool is_current;
} call_info_t;

int baresip_manager_get_active_calls(call_info_t *calls, int max_count);
int baresip_manager_switch_to(void *call_id);
int baresip_manager_hold_call(void *call_id);
int baresip_manager_resume_call(void *call_id);

#endif // BARESIP_MANAGER_H
