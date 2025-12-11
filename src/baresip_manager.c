#include "baresip_manager.h"
#include "history_manager.h"
#include <baresip.h>
#include <re.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Helper to check if string is empty
// str_isset is provided by re_fmt.h from re.h

// Static Module Exports
extern const struct mod_export exports_g711;
extern const struct mod_export exports_opus;
extern const struct mod_export exports_account;
extern const struct mod_export exports_stun;
extern const struct mod_export exports_turn;
extern const struct mod_export exports_audiounit;
extern const struct mod_export exports_coreaudio;

#define MAX_ACCOUNTS 10

// Account registration status tracking
typedef struct {
  char aor[256];
  reg_status_t status;
} account_status_t;

// Global state
static struct {
  enum call_state state;
  char peer_uri[256];
  bool muted;
  call_event_cb callback;
  reg_event_cb reg_callback;
  struct call *current_call;
  account_status_t accounts[MAX_ACCOUNTS];
  int account_count;
} g_call_state = {.state = CALL_STATE_IDLE,
                  .peer_uri = "",
                  .muted = false,
                  .callback = NULL,
                  .reg_callback = NULL,
                  .current_call = NULL,
                  .account_count = 0};

// Signal handler for call events
static void signal_handler(int sig) {
  printf("[BaresipManager] Signal: %d\n", sig);

  switch (sig) {
  case SIGINT:
  case SIGTERM:
    re_cancel();
    break;
  default:
    break;
  }
}

// Find account status by AOR
static account_status_t *find_account(const char *aor) {
  if (!aor)
    return NULL;

  for (int i = 0; i < g_call_state.account_count; i++) {
    if (strcmp(g_call_state.accounts[i].aor, aor) == 0) {
      return &g_call_state.accounts[i];
    }
  }
  return NULL;
}

// Add or update account status
static void update_account_status(const char *aor, reg_status_t status) {
  if (!aor)
    return;

  account_status_t *acc = find_account(aor);
  if (!acc && g_call_state.account_count < MAX_ACCOUNTS) {
    acc = &g_call_state.accounts[g_call_state.account_count++];
    strncpy(acc->aor, aor, sizeof(acc->aor) - 1);
  }

  if (acc) {
    acc->status = status;
    printf("[BaresipManager] Account %s status: %d\n", aor, status);

    if (g_call_state.reg_callback) {
      g_call_state.reg_callback(aor, status);
    }
  }
}

// Call event handler
static void call_event_handler(enum bevent_ev ev, struct bevent *event,
                               void *arg) {
  (void)arg;

  // Log EVERY event for debugging
  printf("[BaresipManager] *** Event received: %d (%s) ***\n", ev,
         bevent_str(ev));

  // Handle registration events
  switch (ev) {
  case BEVENT_REGISTERING: {
    struct ua *ua = bevent_get_ua(event);
    if (ua) {
      const char *aor = account_aor(ua_account(ua));
      printf("[BaresipManager] >>> REGISTERING: %s\n", aor);
      update_account_status(aor, REG_STATUS_REGISTERING);
    } else {
      printf("[BaresipManager] >>> REGISTERING: ua is NULL!\n");
    }
    return;
  }
  case BEVENT_REGISTER_OK: {
    struct ua *ua = bevent_get_ua(event);
    if (ua) {
      const char *aor = account_aor(ua_account(ua));
      printf("[BaresipManager] >>> REGISTER_OK: %s ✓\n", aor);
      update_account_status(aor, REG_STATUS_REGISTERED);
    } else {
      printf("[BaresipManager] >>> REGISTER_OK: ua is NULL!\n");
    }
    return;
  }
  case BEVENT_REGISTER_FAIL: {
    struct ua *ua = bevent_get_ua(event);
    if (ua) {
      const char *aor = account_aor(ua_account(ua));
      const char *error_text = bevent_get_text(event);
      printf("[BaresipManager] >>> REGISTER_FAIL: %s (reason: %s) ✗\n", aor,
             error_text ? error_text : "unknown");
      update_account_status(aor, REG_STATUS_FAILED);
    } else {
      printf("[BaresipManager] >>> REGISTER_FAIL: ua is NULL!\n");
    }
    return;
  }
  default:
    break;
  }

  // Handle call events
  struct call *call = bevent_get_call(event);
  const char *peer = call ? call_peeruri(call) : "unknown";

  // Debug log for call events
  printf("[BaresipManager] Call Event: %d from %s (Call obj: %p)\n", ev, peer,
         (void *)call);

  switch (ev) {
  case BEVENT_CALL_INCOMING:
    printf("[BaresipManager] >>> INCOMING CALL from %s\n", peer);
    g_call_state.state = CALL_STATE_INCOMING;

    // logic to find call if NULL
    if (!call) {
      struct le *le;
      for (le = uag_list()->head; le; le = le->next) {
        struct ua *u = le->data;
        struct call *c = ua_call(u);
        if (c) {
          call = c;
          printf("[BaresipManager] Resolved NULL call in INCOMING event via "
                 "global scan: %p\n",
                 (void *)call);
          break;
        }
      }
    }

    if (call) {
      g_call_state.current_call = call;
      strncpy(g_call_state.peer_uri, peer, sizeof(g_call_state.peer_uri) - 1);
    } else {
      if (!g_call_state.current_call) {
        printf("[BaresipManager] WARNING: INCOMING event with no call object "
               "and none found via scan!\n");
      }
    }

    printf("[BaresipManager] Calling callback: %p with state INCOMING\n",
           (void *)g_call_state.callback);
    if (g_call_state.callback) {
      g_call_state.callback(CALL_STATE_INCOMING, peer);
    } else {
      printf("[BaresipManager] ERROR: Callback is NULL! UI will not update.\n");
    }
    break;

  case BEVENT_CALL_RINGING:
    printf("[BaresipManager] >>> CALL RINGING\n");
    g_call_state.state = CALL_STATE_OUTGOING;
    if (g_call_state.callback) {
      g_call_state.callback(CALL_STATE_OUTGOING, peer);
    }
    break;

  case BEVENT_CALL_ESTABLISHED:
    printf("[BaresipManager] >>> CALL ESTABLISHED\n");
    g_call_state.state = CALL_STATE_ESTABLISHED;
    g_call_state.current_call = call;
    if (g_call_state.callback) {
      g_call_state.callback(CALL_STATE_ESTABLISHED, peer);
    }
    break;

  case BEVENT_CALL_CLOSED: {
    printf("[BaresipManager] >>> CALL CLOSED\n");
    bool established = (g_call_state.state == CALL_STATE_ESTABLISHED);
    bool incoming = call ? !call_is_outgoing(call) : false;

    call_type_t type;
    if (incoming) {
      if (established) {
        type = CALL_TYPE_INCOMING;
      } else {
        type = CALL_TYPE_MISSED;
      }
    } else {
      type = CALL_TYPE_OUTGOING;
    }

    history_add(NULL, peer, type);

    g_call_state.state = CALL_STATE_TERMINATED;
    g_call_state.current_call = NULL;
    if (g_call_state.callback) {
      g_call_state.callback(CALL_STATE_TERMINATED, peer);
    }
    // Reset to idle after a moment
    g_call_state.state = CALL_STATE_IDLE;
    g_call_state.peer_uri[0] = '\0';
    break;
  }

  case BEVENT_SIPSESS_CONN:
    // If call is null, try to get it from UA
    if (!call) {
      struct ua *ua = bevent_get_ua(event);
      const struct sip_msg *msg = bevent_get_msg(event);

      printf("[DEBUG] SIPSESS_CONN: Event UA=%p, Msg=%p\n", (void *)ua,
             (void *)msg);

      if (!ua && msg) {
        ua = uag_find_msg(msg);
        printf("[DEBUG] SIPSESS_CONN: UA resolved via uag_find_msg: %p\n",
               (void *)ua);

        // Fuzzy Match Fallback
        if (!ua) {
          printf("[DEBUG] SIPSESS_CONN: uag_find_msg failed. Attempting fuzzy "
                 "match...\n");
          struct le *le;
          for (le = uag_list()->head; le; le = le->next) {
            struct ua *candidate_ua = le->data;
            struct account *acc = ua_account(candidate_ua);
            struct uri *acc_uri = account_luri(acc);

            if (acc_uri && acc_uri->user.l > 0 &&
                msg->uri.user.l >= acc_uri->user.l) {
              if (0 ==
                  memcmp(msg->uri.user.p, acc_uri->user.p, acc_uri->user.l)) {
                ua = candidate_ua;
                printf("[DEBUG] SIPSESS_CONN: Fuzzy match success! UA: %p\n",
                       (void *)ua);
                account_set_catchall(acc, true);
                break;
              }
            }
          }
        }
      }

      if (ua) {
        if (msg) {
          ua_accept(ua, msg);
        }
        call = ua_call(ua);
      }

      if (!call) {
        // Global search fallback
        struct le *le;
        for (le = uag_list()->head; le; le = le->next) {
          struct ua *u = le->data;
          struct call *c = ua_call(u);
          if (c) {
            call = c;
            break;
          }
        }
      }
    }

    if (g_call_state.state == CALL_STATE_IDLE ||
        g_call_state.state == CALL_STATE_INCOMING) {
      printf("[BaresipManager] >>> SIPSESS_CONN (IDLE/INCOMING)\n");
      g_call_state.state = CALL_STATE_INCOMING;
      g_call_state.current_call = call;
      if (call)
        strncpy(g_call_state.peer_uri, peer, sizeof(g_call_state.peer_uri) - 1);
      if (g_call_state.callback)
        g_call_state.callback(CALL_STATE_INCOMING, peer);
    }
    break;

  default:
    break;
  }
}

// Create default configuration file
static void create_default_config(const char *config_path) {
  FILE *f = fopen(config_path, "w");
  if (f) {
    fprintf(f, "# Minimal Baresip Config\n"
               "poll_method\t\tpoll\n"
               "sip_listen\t\t0.0.0.0:5060\n"
               "sip_transports\t\tudp,tcp\n"
               "audio_path\t\t/usr/share/baresip\n"
               "# Modules\n"
               "module_path\t\t/usr/local/lib/baresip/modules\n"
               "# Audio\n"
#ifdef __APPLE__
               "audio_player\t\taudiounit,nil\n"
               "audio_source\t\taudiounit,nil\n"
               "audio_alert\t\taudiounit,nil\n"
#else
               "audio_player\t\tsdl,nil\n"
               "audio_source\t\tsdl,nil\n"
               "audio_alert\t\tsdl,nil\n"
#endif
               "# Video\n"
               "video_source\t\tsdl,nil\n"
               "video_display\t\tsdl,nil\n"
               "# Connect\n"
               "sip_verify_server\tyes\n"
               "answer_mode\t\tmanual\n"
               "# Modules\n"
               "module_load\t\tstdio.so\n"
               "module_load\t\tg711.so\n"
               "module_load\t\tuuid.so\n"
               "module_load\t\taccount.so\n"
               "# Network\n"
    // MacOS specific modules
#ifdef __APPLE__
               "module_load\t\taudiounit.so\n"
               "module_load\t\tavcapture.so\n"
               "module_load\t\tcoreaudio.so\n"
#endif
               "module_load\t\topus.so\n"
               "# Network Modules\n"
               "module_load\t\tudp.so\n"
               "module_load\t\ttcp.so\n"
               "module_load\t\tice.so\n"
               "module_load\t\tstun.so\n"
               "module_load\t\tturn.so\n"
               "module_load\t\toutbound.so\n"
               "# Keepalive\n"
               "sip_keepalive_interval\t15\n");
    fclose(f);
    printf("[BaresipManager] Wrote minimal config to %s\n", config_path);
  }
}

// Check and fix audio driver in existing config
static void check_and_fix_audio_config(const char *config_path) {
#ifdef __APPLE__
  FILE *f = fopen(config_path, "r");
  if (!f)
    return;

  char content[4096];
  size_t n = fread(content, 1, sizeof(content) - 1, f);
  fclose(f);
  content[n] = '\0';

  if (strstr(content, "audio_player\t\tsdl") ||
      strstr(content, "audio_source\t\tsdl")) {
    printf("[BaresipManager] Detected incompatible 'sdl' audio driver in "
           "config. Patching to 'audiounit'...\n");

    // Simple string replacement approach for this specific issue
    // We will just rewrite the default config if we detect the broken one,
    // or we can try to replace.
    // safer to just append the override to the end, Baresip might use the last
    // one? No, Baresip usually uses the first or last, unpredictable. Better to
    // rewrite.

    // Let's use a simple approach: if we see 'sdl', we assume it's the broken
    // default config and overwrite it.
    create_default_config(config_path);
  }
#endif
}

// Check and append keepalive setting if missing
static void check_and_append_keepalive(const char *config_path) {
  check_and_fix_audio_config(config_path);

  FILE *f_check_ka = fopen(config_path, "r");
  bool has_keepalive = false;
  if (f_check_ka) {
    char line[256];
    while (fgets(line, sizeof(line), f_check_ka)) {
      if (strstr(line, "sip_keepalive_interval")) {
        has_keepalive = true;
        break;
      }
    }
    fclose(f_check_ka);
  }

  if (!has_keepalive) {
    FILE *f_append = fopen(config_path, "a");
    if (f_append) {
      fprintf(f_append,
              "\n# Keepalive (Auto-added)\nsip_keepalive_interval\t15\n");
      fclose(f_append);
      printf("[BaresipManager] Added sip_keepalive_interval (15s) to "
             "existing config\n");
    }
  }
}

// Initialize Baresip
int baresip_manager_init(void) {
  int err;

  printf("[BaresipManager] Initializing...\n");

  // Initialize libre
  history_manager_init();
  err = libre_init();
  if (err) {
    printf("[BaresipManager] Failed to initialize libre: %d\n", err);
    return err;
  }

  // Set up signal handlers
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  // Create baresip configuration
  // Ensure .baresip directory exists
  char home_dir[256];
  const char *home = getenv("HOME");
  if (home) {
    snprintf(home_dir, sizeof(home_dir), "%s/.baresip", home);
    mkdir(home_dir, 0755);

    // Create accounts file if not exists
    char accounts_path[512];
    snprintf(accounts_path, sizeof(accounts_path), "%s/accounts", home_dir);
    FILE *f = fopen(accounts_path, "a");
    if (f)
      fclose(f);

    // Config path logic
    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%s/config", home_dir);

    FILE *f_check = fopen(config_path, "r");
    long file_size = 0;
    if (f_check) {
      fseek(f_check, 0, SEEK_END);
      file_size = ftell(f_check);
      fclose(f_check);
    }

    if (file_size == 0) {
      create_default_config(config_path);
    } else {
      check_and_append_keepalive(config_path);
    }

    printf("[BaresipManager] Config dir: %s\n", home_dir);
  }

  // Configure baresip from config file
  int cfg_err = conf_configure();
  if (cfg_err) {
    printf("[BaresipManager] conf_configure failed: %d (Using defaults)\n",
           cfg_err);
  }

  struct config *cfg = conf_config();
  if (!cfg) {
    printf("[BaresipManager] Failed to get config\n");
    libre_close();
    return EINVAL;
  }

#ifdef __APPLE__
  // FORCE audio driver to audiounit on macOS, ignoring config file errors
  printf("[BaresipManager] Forcing audio driver to 'audiounit' for macOS...\n");
  snprintf(cfg->audio.play_mod, sizeof(cfg->audio.play_mod), "audiounit");
  snprintf(cfg->audio.src_mod, sizeof(cfg->audio.src_mod), "audiounit");
  snprintf(cfg->audio.alert_mod, sizeof(cfg->audio.alert_mod), "audiounit");

  // Audio Quality Tuning (Fix for "unsmoothy" audio)
  cfg->audio.adaptive = true;
  cfg->audio.buffer.min = 100; // Minimum buffer 100ms
  cfg->audio.buffer.max = 300; // Maximum buffer 300ms (allows for jitter)
  printf("[BaresipManager] Audio tuning applied: adaptive=yes, "
         "buffer=100-300ms\n");
#endif

  // Initialize Baresip core
  err = baresip_init(cfg);
  if (err) {
    printf("[BaresipManager] Failed to initialize baresip: %d\n", err);
    libre_close();
    return err;
  }

  // Initialize User Agents
  err = ua_init("baresip-lvgl", true, true, true);
  if (err) {
    printf("[BaresipManager] Failed to initialize UA: %d\n", err);
    baresip_close();
    libre_close();
    return err;
  }

  // Manually enable transports (missing modules workaround)
  struct sa laddr_any;
  sa_init(&laddr_any, AF_INET);

  printf("[BaresipManager] Manually adding UDP transport...\n");
  err = sip_transp_add(uag_sip(), SIP_TRANSP_UDP, &laddr_any);
  if (err)
    printf("[BaresipManager] Failed to add UDP transport: %d\n", err);

  printf("[BaresipManager] Manually adding TCP transport...\n");
  err = sip_transp_add(uag_sip(), SIP_TRANSP_TCP, &laddr_any);
  if (err)
    printf("[BaresipManager] Failed to add TCP transport: %d\n", err);

  // Static Module Registration (Direct init calls)
  printf("[BaresipManager] Registering static modules...\n");
  if (exports_g711.init)
    exports_g711.init();
  if (exports_opus.init)
    exports_opus.init();
  if (exports_account.init)
    exports_account.init();
  if (exports_stun.init)
    exports_stun.init();
  if (exports_turn.init)
    exports_turn.init();

#ifdef __APPLE__
  if (exports_audiounit.init)
    exports_audiounit.init();
  if (exports_coreaudio.init)
    exports_coreaudio.init();
#endif

  // Manual UUID generation if outbound needed it
  if (cfg) {
    if (!str_isset(cfg->sip.uuid)) {
      printf("[BaresipManager] generating missing UUID...\n");
      snprintf(cfg->sip.uuid, sizeof(cfg->sip.uuid),
               "%08x-%04x-%04x-%04x-%08x%04x", rand(), rand() & 0xFFFF,
               rand() & 0xFFFF, rand() & 0xFFFF, rand(), rand() & 0xFFFF);
    }
    printf("[BaresipManager] Using UUID: %s\n", cfg->sip.uuid);
  }

  printf("[BaresipManager] Modules registered.\n");

  // Debug: Print network interfaces
  printf("[BaresipManager] --- Network Interface Debug ---\n");
  net_debug(NULL, NULL);
  printf("[BaresipManager] ------------------------------\n");

  // Register event handler
  bevent_register(call_event_handler, NULL);

  printf("[BaresipManager] Initialization complete\n");
  return 0;
}

void baresip_manager_set_callback(call_event_cb cb) {
  g_call_state.callback = cb;
}

void baresip_manager_set_reg_callback(reg_event_cb cb) {
  g_call_state.reg_callback = cb;
}

reg_status_t baresip_manager_get_account_status(const char *aor) {
  if (!aor)
    return REG_STATUS_NONE;
  account_status_t *acc = find_account(aor);
  return acc ? acc->status : REG_STATUS_NONE;
}

int baresip_manager_call(const char *uri) {
  struct ua *ua;
  int err;
  char full_uri[256];

  if (!uri)
    return -1;

  printf("[BaresipManager] Making call to: %s\n", uri);

  // Get first user agent (default)
  ua = uag_find_aor(NULL);
  if (!ua) {
    printf("[BaresipManager] No user agent available\n");
    return -1;
  }

  // Normalize URI
  if (strstr(uri, "sip:") == uri) {
    // Already has sip: prefix
    strncpy(full_uri, uri, sizeof(full_uri) - 1);
  } else if (strchr(uri, '@')) {
    // Has domain but missing sip:
    snprintf(full_uri, sizeof(full_uri), "sip:%s", uri);
  } else {
    // Just a number? Append domain from account
    struct account *acc = ua_account(ua);
    const struct uri *luri = account_luri(acc);
    if (luri && luri->host.p) {
      char domain[128];
      pl_strcpy(&luri->host, domain, sizeof(domain));
      snprintf(full_uri, sizeof(full_uri), "sip:%s@%s", uri, domain);
    } else {
      // Fallback if no domain found (unlikely if registered)
      snprintf(full_uri, sizeof(full_uri), "sip:%s", uri);
    }
  }
  full_uri[sizeof(full_uri) - 1] = '\0';

  // Make call
  printf("[BaresipManager] Calling normalized uri: '%s' using ua: %p\n",
         full_uri, (void *)ua);

  struct call *call = NULL;
  err = ua_connect(ua, &call, NULL, full_uri, VIDMODE_OFF);
  if (err) {
    printf("[BaresipManager] Failed to make call: %d\n", err);
    return err;
  }
  printf("[BaresipManager] Call initiated successfully (err=0)\n");

  if (call) {
    g_call_state.current_call = call;
    printf("[BaresipManager] Stored outgoing call object: %p\n", (void *)call);
  } else {
    printf("[BaresipManager] WARNING: ua_connect succeeded but returned NULL "
           "call object\n");
  }

  strncpy(g_call_state.peer_uri, full_uri, sizeof(g_call_state.peer_uri) - 1);
  g_call_state.state = CALL_STATE_OUTGOING;

  return 0;
}

int baresip_manager_answer(void) {
  printf("[BaresipManager] baresip_manager_answer called\n");

  if (!g_call_state.current_call) {
    printf("[BaresipManager] current_call is NULL, trying to find active "
           "call...\n");
    struct le *le;
    for (le = uag_list()->head; le; le = le->next) {
      struct ua *ua = le->data;
      struct call *c = ua_call(ua);
      if (c) {
        printf("[BaresipManager] Found active call in UA: %p\n", (void *)c);
        g_call_state.current_call = c;
        if (call_state(c) == CALL_STATE_INCOMING) {
          printf("[BaresipManager] Call state is INCOMING, proceeding\n");
        } else {
          printf("[BaresipManager] Call state is NOT INCOMING: %d\n",
                 call_state(c));
        }
        break;
      }
    }
  }

  if (!g_call_state.current_call) {
    printf("[BaresipManager] No incoming call to answer (Scan failed)\n");
    return -1;
  }

  printf("[BaresipManager] Answering call %p\n",
         (void *)g_call_state.current_call);

  int err = call_answer(g_call_state.current_call, 200, VIDMODE_OFF);
  if (err) {
    printf("[BaresipManager] Failed to answer call: %d\n", err);
    return err;
  }

  printf("[BaresipManager] call_answer returned success (0)\n");

  // Manually force state update just in case event is slow
  g_call_state.state = CALL_STATE_ESTABLISHED;
  if (g_call_state.callback) {
    g_call_state.callback(CALL_STATE_ESTABLISHED, g_call_state.peer_uri);
  }

  return 0;
}

int baresip_manager_hangup(void) {
  if (!g_call_state.current_call) {
    printf("[BaresipManager] No active call to hangup\n");
    return -1;
  }

  printf("[BaresipManager] Hanging up call\n");

  call_hangup(g_call_state.current_call, 0, NULL);
  g_call_state.current_call = NULL;
  g_call_state.state = CALL_STATE_IDLE;

  return 0;
}

enum call_state baresip_manager_get_state(void) { return g_call_state.state; }

const char *baresip_manager_get_peer(void) {
  return g_call_state.peer_uri[0] ? g_call_state.peer_uri : NULL;
}

void baresip_manager_mute(bool mute) {
  g_call_state.muted = mute;

  if (g_call_state.current_call) {
    struct audio *audio = call_audio(g_call_state.current_call);
    if (audio) {
      audio_mute(audio, mute);
    }
  }

  printf("[BaresipManager] Microphone %s\n", mute ? "muted" : "unmuted");
}

// Dummy timer for loop control
static struct tmr g_loop_tmr;

static void loop_timeout_cb(void *arg) {
  (void)arg;
  re_cancel();
}

void baresip_manager_loop(void (*ui_cb)(void), int interval_ms) {
  tmr_init(&g_loop_tmr);

  while (true) {
    // Schedule cancellation to return control to UI
    tmr_start(&g_loop_tmr, interval_ms, loop_timeout_cb, NULL);

    // Run re_main (blocks until loop_timeout_cb fires or other signal)
    // process events
    int err = re_main(NULL);
    if (err && err != EINTR) {
      // EINTR is expected from re_cancel
      if (err != 0)
        printf("[BaresipManager] re_main returned: %d\n", err);
    }

    if (ui_cb) {
      ui_cb();
    }
  }
}

bool baresip_manager_is_muted(void) { return g_call_state.muted; }

int baresip_manager_add_account(const voip_account_t *acc) {
  char aor[1024];
  int err;

  if (!acc)
    return EINVAL;

  /* Format: "Display Name" <sip:user:password@domain;transport=udp>;regint=3600
   */
  /* Simplified for now: <sip:user:password@domain>;transport=udp */

  char extra_params[512] = {0};

  // Process Audio Codecs
  if (str_isset(acc->audio_codecs)) {
    char buf[256] = {0};
    char *dup = strdup(acc->audio_codecs);
    char *tok = strtok(dup, ",");
    while (tok) {
      char *slash = strchr(tok, '/');
      if (slash)
        *slash = '\0'; // Truncate at /

      if (strlen(buf) > 0)
        strcat(buf, ",");
      strcat(buf, tok);
      tok = strtok(NULL, ",");
    }
    free(dup);

    if (strlen(buf) > 0) {
      char tmp[300];
      snprintf(tmp, sizeof(tmp), ";audio_codecs=%s", buf);
      strcat(extra_params, tmp);
    }
  }

  // Process Video Codecs
  if (str_isset(acc->video_codecs)) {
    char buf[256] = {0};
    char *dup = strdup(acc->video_codecs);
    char *tok = strtok(dup, ",");
    while (tok) {
      char *slash = strchr(tok, '/');
      if (slash)
        *slash = '\0'; // Truncate at /

      if (strlen(buf) > 0)
        strcat(buf, ",");
      strcat(buf, tok);
      tok = strtok(NULL, ",");
    }
    free(dup);

    if (strlen(buf) > 0) {
      char tmp[300];
      snprintf(tmp, sizeof(tmp), ";video_codecs=%s", buf);
      strcat(extra_params, tmp);
    }
  }

  if (str_isset(acc->outbound_proxy)) {
    // Format:
    // <sip:user@domain;transport=udp>;auth_pass=pass;auth_user=user;outbound=sip:proxy;regint=600
    char proxy_buf[256];
    if (strstr(acc->outbound_proxy, "sip:") == acc->outbound_proxy) {
      snprintf(proxy_buf, sizeof(proxy_buf), "%s", acc->outbound_proxy);
    } else {
      snprintf(proxy_buf, sizeof(proxy_buf), "sip:%s", acc->outbound_proxy);
    }

    snprintf(aor, sizeof(aor),
             "<sip:%s@%s;transport=udp>;auth_pass=%s;auth_user=%s;outbound=%s;"
             "regint=600%s",
             acc->username, acc->server, acc->password, acc->username,
             proxy_buf, extra_params);
  } else {
    snprintf(aor, sizeof(aor),
             "<sip:%s@%s;transport=udp>;auth_pass=%s;auth_user=%s;regint=600%s",
             acc->username, acc->server, acc->password, acc->username,
             extra_params);
  }

  printf("[BaresipManager] Adding account: %s\n", aor);

  struct ua *ua = NULL;
  err = ua_alloc(&ua, aor);
  if (err) {
    printf("[BaresipManager] Failed to add account: %d\n", err);
    return err;
  }

  if (ua) {
    printf("[BaresipManager] ua_alloc succeeded, UA created: %p\n", (void *)ua);
    printf("[BaresipManager] Calling ua_register to start registration...\n");
    err = ua_register(ua);
    if (err) {
      printf("[BaresipManager] ua_register failed: %d\n", err);
    } else {
      printf("[BaresipManager] ua_register succeeded, registration started\n");
    }
  }

  return 0;
}

void baresip_manager_destroy(void) {
  ua_stop_all(false);
  ua_close();
  baresip_close();
  libre_close();
}
