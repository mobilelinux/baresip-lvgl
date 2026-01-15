#ifndef BARESIP_MANAGER_H
#define BARESIP_MANAGER_H

#include <stdbool.h>
#include <re.h>
#include <baresip.h>
#include "lvgl.h"
#include "logger.h"

// Registration Status
typedef enum {
    REG_STATUS_NONE,
    REG_STATUS_REGISTERING,
    REG_STATUS_REGISTERED,
    REG_STATUS_AUTH_FAILED, // Added
    REG_STATUS_FAILED
} reg_status_t;

// Call Type for History
// Call Type for History moved to history_manager.h


#include "config_manager.h"

// Call Info Struct
typedef struct {
    char id[64];
    char peer_uri[128];
    enum call_state state;
    bool is_current;
    bool is_held;
    void *call_ptr;
} call_info_t;

// Callbacks
typedef void (*call_event_cb)(enum call_state state, const char *peer_uri, void *call_ptr);
typedef void (*reg_event_cb)(const char *aor, reg_status_t status);

// Functions
int baresip_manager_init(void);
void baresip_manager_loop(void (*ui_cb)(void), int interval_ms);
int baresip_manager_connect(const char *uri, const char *account_aor, bool video);
int baresip_manager_call(const char *uri);
int baresip_manager_call_with_account(const char *uri, const char *account_aor);
int baresip_manager_videocall(const char *uri);
int baresip_manager_videocall_with_account(const char *uri, const char *account_aor);

int baresip_manager_answer_call(bool video);
int baresip_manager_reject_call(void *call_ptr);
int baresip_manager_hangup(void);
int baresip_manager_hold_call(void *call_id);
int baresip_manager_resume_call(void *call_id);
int baresip_manager_switch_to(const char *call_id);
int baresip_manager_send_dtmf(char key);
int baresip_manager_get_active_calls(call_info_t *calls, int max_count);

void baresip_manager_mute(bool mute);
bool baresip_manager_is_muted(void);

// SIP Messaging
int baresip_manager_send_message(const char *uri, const char *text);

enum call_state baresip_manager_get_state(void);
const char *baresip_manager_get_peer(void);

int baresip_manager_account_register(const char *aor);
int baresip_manager_account_register_simple(const char *user, const char *domain);
int baresip_manager_add_account(const voip_account_t *acc);
reg_status_t baresip_manager_get_account_status(const char *aor);

void baresip_manager_set_callback(call_event_cb cb);
void baresip_manager_set_reg_callback(reg_event_cb cb);
void baresip_manager_set_log_level(int level);

// Video
void baresip_manager_set_video_objects(lv_obj_t *remote, lv_obj_t *local);
void baresip_manager_set_video_rect(int x, int y, int w, int h);
void baresip_manager_set_local_video_rect(int x, int y, int w, int h);

// Account Management
void baresip_manager_set_current_account(int account_index);
void baresip_manager_process_video(void);

// Helper to get peer name with priority: Contact > Display Name > URI
void baresip_manager_get_peer_display_name(struct call *call, const char *peer_uri, char *out_buf, size_t size);

// Helper to get peer name for current active call
void baresip_manager_get_current_call_display_name(char *out_buf, size_t size);

#endif // BARESIP_MANAGER_H
