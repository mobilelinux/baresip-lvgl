#include "baresip_manager.h"
#include "history_manager.h"
#include "logger.h"
#include "lv_drivers/sdl/sdl.h"
#include <SDL.h>
#include <baresip.h>
#include <re.h>
#include <rem_vid.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Helper to check if string is empty
// str_isset is provided by re_fmt.h from re.h

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

#define MAX_CALLS 4

typedef struct {
  struct call *call;
  char peer_uri[256];
  enum call_state state;
} active_call_t;

// Video Display Module Pointers
static struct vidisp *vid = NULL;  // Remote (sdl_vidisp)
static struct vidisp *vid2 = NULL; // Local (window)

static int baresip_manager_register_vidisp(void);

// Global state
static struct {
  enum call_state state;
  char peer_uri[256];
  bool muted;
  call_event_cb callback;
  reg_event_cb reg_callback;
  struct call *current_call;
  active_call_t active_calls[MAX_CALLS];
  account_status_t accounts[MAX_ACCOUNTS];
  int account_count;
} g_call_state = {.state = CALL_STATE_IDLE,
                  .peer_uri = "",
                  .muted = false,
                  .callback = NULL,
                  .reg_callback = NULL,
                  .current_call = NULL,
                  .account_count = 0};

static void add_or_update_call(struct call *call, enum call_state state,
                               const char *peer) {
  if (!call)
    return;
  // Find existing
  for (int i = 0; i < MAX_CALLS; i++) {
    if (g_call_state.active_calls[i].call == call) {
      g_call_state.active_calls[i].state = state;
      if (peer)
        strncpy(g_call_state.active_calls[i].peer_uri, peer, 255);
      return;
    }
  }
  // Add new
  for (int i = 0; i < MAX_CALLS; i++) {
    if (g_call_state.active_calls[i].call == NULL) {
      g_call_state.active_calls[i].call = call;
      g_call_state.active_calls[i].state = state;
      if (peer)
        strncpy(g_call_state.active_calls[i].peer_uri, peer, 255);
      else
        g_call_state.active_calls[i].peer_uri[0] = '\0';
      log_info("BaresipManager", "Added call %p to slot %d", call, i);
      return;
    }
  }
  log_warn("BaresipManager", "Max calls reached, could not track call %p",
           call);
}

static void remove_call(struct call *call) {
  if (!call)
    return;
  for (int i = 0; i < MAX_CALLS; i++) {
    if (g_call_state.active_calls[i].call == call) {
      log_info("BaresipManager", "Removed call %p from slot %d", call, i);
      g_call_state.active_calls[i].call = NULL;
      g_call_state.active_calls[i].state = CALL_STATE_IDLE;
      g_call_state.active_calls[i].peer_uri[0] = '\0';

      // If this was the current call, clear it and try to switch to another
      if (g_call_state.current_call == call) {
        g_call_state.current_call = NULL;
        g_call_state.state = CALL_STATE_IDLE;

        // Auto-switch to first available active call
        for (int j = 0; j < MAX_CALLS; j++) {
          if (g_call_state.active_calls[j].call) {
            g_call_state.current_call = g_call_state.active_calls[j].call;
            g_call_state.state = g_call_state.active_calls[j].state;
            log_info("BaresipManager", "Auto-switched to call %p",
                     g_call_state.current_call);
            break;
          }
        }
      }
      return;
    }
  }
}

// Signal handler for call events
static void signal_handler(int sig) {
  log_info("BaresipManager", "Signal: %d", sig);

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
    log_info("BaresipManager", "Account %s status: %d", aor, status);

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
  log_debug("BaresipManager", "*** Event received: %d (%s) ***", ev,
            bevent_str(ev));

  // Handle registration events
  switch (ev) {
  case BEVENT_REGISTERING: {
    struct ua *ua = bevent_get_ua(event);
    if (ua) {
      const char *aor = account_aor(ua_account(ua));
      log_info("BaresipManager", ">>> REGISTERING: %s", aor);
      update_account_status(aor, REG_STATUS_REGISTERING);
    } else {
      log_warn("BaresipManager", ">>> REGISTERING: ua is NULL!");
    }
    return;
  }
  case BEVENT_REGISTER_OK: {
    struct ua *ua = bevent_get_ua(event);
    if (ua) {
      const char *aor = account_aor(ua_account(ua));
      log_info("BaresipManager", ">>> REGISTER_OK: %s ✓", aor);
      update_account_status(aor, REG_STATUS_REGISTERED);
    } else {
      log_warn("BaresipManager", ">>> REGISTER_OK: ua is NULL!");
    }
    return;
  }
  case BEVENT_REGISTER_FAIL: {
    struct ua *ua = bevent_get_ua(event);
    if (ua) {
      const char *aor = account_aor(ua_account(ua));
      const char *error_text = bevent_get_text(event);
      log_warn("BaresipManager", ">>> REGISTER_FAIL: %s (reason: %s) ✗", aor,
               error_text ? error_text : "unknown");
      update_account_status(aor, REG_STATUS_FAILED);
    } else {
      log_warn("BaresipManager", ">>> REGISTER_FAIL: ua is NULL!");
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
  log_info("BaresipManager", "Call Event: %d from %s (Call obj: %p)", ev, peer,
           (void *)call);

  switch (ev) {
  case BEVENT_CALL_INCOMING:
    log_info("BaresipManager", ">>> INCOMING CALL from %s", peer);
    g_call_state.state = CALL_STATE_INCOMING;

    // logic to find call if NULL
    if (!call) {
      struct le *le;
      for (le = uag_list()->head; le; le = le->next) {
        struct ua *u = le->data;
        struct call *c = ua_call(u);
        if (c) {
          call = c;
          log_debug("BaresipManager",
                    "Resolved NULL call in INCOMING event via global scan: %p",
                    (void *)call);
          break;
        }
      }
    }

    if (call) {
      g_call_state.current_call = call;
      strncpy(g_call_state.peer_uri, peer, sizeof(g_call_state.peer_uri) - 1);
      add_or_update_call(call, CALL_STATE_INCOMING, peer);
    } else {
      if (!g_call_state.current_call) {
        log_warn("BaresipManager", "WARNING: INCOMING event with no call "
                                   "object and none found via scan!");
      }
    }

    log_debug("BaresipManager", "Calling callback: %p with state INCOMING",
              (void *)g_call_state.callback);
    if (g_call_state.callback) {
      g_call_state.callback(CALL_STATE_INCOMING, peer, (void *)call);
    } else {
      log_error("BaresipManager",
                "ERROR: Callback is NULL! UI will not update.");
    }
    break;

  case BEVENT_CALL_RINGING:
    log_info("BaresipManager", ">>> CALL RINGING");
    g_call_state.state = CALL_STATE_RINGING; // was OUTGOING, better RINGING
    if (call)
      add_or_update_call(call, CALL_STATE_RINGING, peer);
    if (g_call_state.callback) {
      g_call_state.callback(CALL_STATE_RINGING, peer, (void *)call);
    }
    break;

  case BEVENT_CALL_PROGRESS:
    log_info("BaresipManager", ">>> CALL PROGRESS (Early Media/183)");
    g_call_state.state = CALL_STATE_EARLY;
    if (call)
      add_or_update_call(call, CALL_STATE_EARLY, peer);
    if (g_call_state.callback) {
      g_call_state.callback(CALL_STATE_EARLY, peer, (void *)call);
    }
    break;

  case BEVENT_CALL_ESTABLISHED:
    log_info("BaresipManager", ">>> CALL ESTABLISHED");
    g_call_state.state = CALL_STATE_ESTABLISHED;
    g_call_state.current_call = call;
    if (call)
      add_or_update_call(call, CALL_STATE_ESTABLISHED, peer);
    if (g_call_state.callback) {
      g_call_state.callback(CALL_STATE_ESTABLISHED, peer, (void *)call);
    }
    break;

  case BEVENT_CALL_CLOSED: {
    log_info("BaresipManager", ">>> CALL CLOSED");
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

    const char *acc_aor = "";
    /*
    if (call) {
      struct ua *ua = call_ua(call);
      if (ua)
        acc_aor = ua_aor(ua);
    }
    */
    history_add(NULL, peer, type, acc_aor);

    // Remove from active list
    if (call)
      remove_call(call);

    // If the closed call was the current one, try to switch to another
    if (g_call_state.current_call == call) {
      g_call_state.current_call = NULL;
      // Search for another active call
      int others = 0;
      for (int i = 0; i < MAX_CALLS; i++) {
        if (g_call_state.active_calls[i].call) {
          g_call_state.current_call = g_call_state.active_calls[i].call;
          g_call_state.state = g_call_state.active_calls[i].state;
          strncpy(g_call_state.peer_uri, g_call_state.active_calls[i].peer_uri,
                  255);
          others++;
          break;
        }
      }
      if (others == 0) {
        g_call_state.state = CALL_STATE_TERMINATED;
        g_call_state.peer_uri[0] = '\0';
      }
    } else if (g_call_state.current_call == NULL) {
      g_call_state.state = CALL_STATE_TERMINATED;
    }

    if (g_call_state.state == CALL_STATE_TERMINATED && g_call_state.callback) {
      g_call_state.callback(CALL_STATE_TERMINATED, peer, (void *)call);
    } else if (g_call_state.callback) {
      // Notify applet to refresh list because a background call ended
      // Reuse CALL_STATE_ESTABLISHED to trigger refresh?
      // Or just let the applet poll? The applet uses `on_call_state_change`.
      // We should signal that the stack state changed.
      // Ideally we need a separate event, but re-sending ESTABLISHED with
      // current peer might work.
      if (g_call_state.current_call)
        g_call_state.callback(g_call_state.state, g_call_state.peer_uri,
                              (void *)g_call_state.current_call);
    }

    // Reset to idle if really no calls
    bool any_calls = false;
    for (int i = 0; i < MAX_CALLS; i++)
      if (g_call_state.active_calls[i].call)
        any_calls = true;
    if (!any_calls) {
      g_call_state.state = CALL_STATE_IDLE;
      g_call_state.peer_uri[0] = '\0';
    }
    break;
  }

  case BEVENT_SIPSESS_CONN:
    // If call is null, try to get it from UA
    if (!call) {
      struct ua *ua = bevent_get_ua(event);
      const struct sip_msg *msg = bevent_get_msg(event);

      log_debug("BaresipManager", "SIPSESS_CONN: Event UA=%p, Msg=%p",
                (void *)ua, (void *)msg);

      if (!ua && msg) {
        ua = uag_find_msg(msg);
        log_debug("BaresipManager",
                  "SIPSESS_CONN: UA resolved via uag_find_msg: %p", (void *)ua);

        // Fuzzy Match Fallback
        if (!ua) {
          log_debug("BaresipManager", "SIPSESS_CONN: uag_find_msg failed. "
                                      "Attempting fuzzy match...");
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
                log_debug("BaresipManager",
                          "SIPSESS_CONN: Fuzzy match success! UA: %p",
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
      log_debug("BaresipManager", ">>> SIPSESS_CONN (IDLE/INCOMING)");
      g_call_state.state = CALL_STATE_INCOMING;
      g_call_state.current_call = call;
      if (call)
        strncpy(g_call_state.peer_uri, peer, sizeof(g_call_state.peer_uri) - 1);
      if (g_call_state.callback)
        g_call_state.callback(CALL_STATE_INCOMING, peer, (void *)call);
    }
    break;

  default:
    break;
  }
}

// ============================================================================
// SDL Video Display Module Implementation
// ============================================================================

struct vidisp_st {
  struct le le;
  SDL_Texture *texture;
  struct vidframe *frame;
  struct vidsz size;
  mtx_t *lock;
  bool new_frame;
  int orientation;
  bool is_local;
};

static struct list vidisp_list;
static mtx_t *vidisp_list_lock = NULL;

static void sdl_vidisp_destructor(void *arg) {
  struct vidisp_st *st = arg;

  if (vidisp_list_lock) {
    mtx_lock(vidisp_list_lock);
    list_unlink(&st->le);
    mtx_unlock(vidisp_list_lock);
  }

  if (st->frame)
    mem_deref(st->frame);
  if (st->lock)
    mem_deref(st->lock);
  // Texture is destroyed by SDL/Renderer effectively
}

static int sdl_vidisp_alloc(struct vidisp_st **stp, const struct vidisp *vd,
                            struct vidisp_prm *prm, const char *dev,
                            vidisp_resize_h *resizeh, void *arg) {
  struct vidisp_st *st;
  int err;

  // Determine if this is the local self-view
  // We check for specific device names OR if the display module instance
  // matches our registered local video display alias (vid2)
  // ALSO: If dev is NULL, it is likely the 'selfview' module allocating (remote
  // streams imply a peer name)
  bool is_local =
      (!dev) ||
      (dev && (strcmp(dev, "local") == 0 || strcmp(dev, "selfview") == 0 ||
               strcmp(dev, "window") == 0)) ||
      (vd == vid2);

  (void)dev;
  (void)resizeh;
  (void)arg;

  st = mem_zalloc(sizeof(*st), sdl_vidisp_destructor);
  if (!st) {
    // vidisp_instance_count--; // Removed
    return ENOMEM;
  }

  err = mutex_alloc(&st->lock);
  if (err) {
    // vidisp_instance_count--; // Removed
    mem_deref(st);
    return err;
  }

  mtx_lock(vidisp_list_lock);
  list_append(&vidisp_list, &st->le, st);
  mtx_unlock(vidisp_list_lock);

  st->is_local = is_local;
  *stp = st;
  return 0;
}

static int sdl_vidisp_update(struct vidisp_st *st, bool fullscreen, int orient,
                             const struct vidrect *window) {
  (void)fullscreen;
  (void)window;
  if (!st)
    return EINVAL;
  st->orientation = orient;
  return 0;
}

static int sdl_vidisp_disp(struct vidisp_st *st, const char *title,
                           const struct vidframe *frame, uint64_t timestamp) {
  (void)title;
  (void)timestamp;
  if (!st || !frame)
    return EINVAL;

  mtx_lock(st->lock);

  // Check if size changed
  if (!st->frame || !vidsz_cmp(&st->size, &frame->size) ||
      st->frame->fmt != frame->fmt) {
    if (st->frame)
      mem_deref(st->frame);
    st->frame = NULL;
    st->size = frame->size;
    int err = vidframe_alloc(&st->frame, frame->fmt, &frame->size);
    if (err) {
      mtx_unlock(st->lock);
      return err;
    }
  }

  // Copy frame data
  vidframe_copy(st->frame, frame);
  st->new_frame = true;

  mtx_unlock(st->lock);

  log_debug("BaresipManager", "sdl_vidisp_disp: Frame received (fmt=%d, %dx%d)",
            frame->fmt, frame->size.w, frame->size.h);

  // Trigger SDL refresh
  SDL_Event event;
  event.type = SDL_WINDOWEVENT;
  event.window.event = SDL_WINDOWEVENT_EXPOSED;
  // Push event is thread safe in SDL2
  SDL_PushEvent(&event);

  return 0;
}

static void sdl_vidisp_hide(struct vidisp_st *st) { (void)st; }

static SDL_Rect g_video_rect = {0, 0, 0, 0};
static SDL_Rect g_local_video_rect = {0, 0, 0, 0};

// Video Display Module Pointers - Moved to Global Scope

static Uint32 vidfmt_to_sdl(enum vidfmt fmt) {
  switch (fmt) {
  case VID_FMT_YUV420P:
    return SDL_PIXELFORMAT_IYUV;
  case VID_FMT_YUYV422:
    return SDL_PIXELFORMAT_YUY2;
  case VID_FMT_UYVY422:
    return SDL_PIXELFORMAT_UYVY;
  case VID_FMT_RGB32:
    return SDL_PIXELFORMAT_ARGB8888;
  default:
    return SDL_PIXELFORMAT_UNKNOWN;
  }
}

void baresip_manager_set_video_rect(int x, int y, int w, int h) {
  g_video_rect.x = x;
  g_video_rect.y = y;
  g_video_rect.w = w;
  g_video_rect.h = h;
}

void baresip_manager_set_local_video_rect(int x, int y, int w, int h) {
  g_local_video_rect.x = x;
  g_local_video_rect.y = y;
  g_local_video_rect.w = w;
  g_local_video_rect.h = h;
}

// Render Callback (Runs in Main Thread)
static void sdl_vid_render(void *renderer_ptr) {
  static int entry_log = 0;
  if (entry_log++ % 60 == 0)
    fprintf(stderr, "sdl_vid_render: Entry\n");
  SDL_Renderer *renderer = (SDL_Renderer *)renderer_ptr;

  if (!vidisp_list_lock)
    return;

  mtx_lock(vidisp_list_lock);

  // Pass 1: Remote Video (Background)
  struct le *le;
  for (le = vidisp_list.head; le; le = le->next) {
    struct vidisp_st *st = le->data;
    // 1. Create/Recreate Texture if needed
    if (st->frame && st->size.w > 0 && st->size.h > 0) {
      bool create = !st->texture;
      Uint32 format = vidfmt_to_sdl(st->frame->fmt);
      if (format == SDL_PIXELFORMAT_UNKNOWN) {
        log_warn("BaresipManager", "Unknown/Unsupported video format: %d",
                 st->frame->fmt);
        format = SDL_PIXELFORMAT_IYUV; // Try fallback
      }

      if (st->texture) {
        int w, h;
        Uint32 existing_fmt;
        SDL_QueryTexture(st->texture, &existing_fmt, NULL, &w, &h);
        if (w != st->size.w || h != st->size.h || existing_fmt != format) {
          SDL_DestroyTexture(st->texture);
          st->texture = NULL;
          create = true;
        }
      }

      if (create) {
        st->texture =
            SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_STREAMING,
                              st->size.w, st->size.h);
        if (!st->texture) {
          log_warn("BaresipManager", "Failed to create SDL texture: %s",
                   SDL_GetError());
        } else {
          log_info("BaresipManager", "Created SDL Texture %dx%d (fmt=%d)",
                   st->size.w, st->size.h, st->frame->fmt);
        }
      }
    }

    // 2. Update Texture if new frame
    if (st->texture && st->new_frame && st->frame) {
      if (st->frame->fmt == VID_FMT_YUV420P) {
        SDL_UpdateYUVTexture(st->texture, NULL, st->frame->data[0],
                             st->frame->linesize[0], st->frame->data[1],
                             st->frame->linesize[1], st->frame->data[2],
                             st->frame->linesize[2]);
      } else {
        // Generic update for packed formats (RGB, YUYV, etc.)
        SDL_UpdateTexture(st->texture, NULL, st->frame->data[0],
                          st->frame->linesize[0]);
      }
      st->new_frame = false;
    }

    // 3. Render Remote Video (Skip if local - rendered in Pass 2)
    if (!st->is_local) {
      if (!st->texture) {
        static int log_no_tex_rem = 0;
        if (log_no_tex_rem++ % 60 == 0)
          log_warn("BaresipManager", "Remote video has NO TEXTURE (fmt=%d)",
                   st->frame ? st->frame->fmt : -1);
      } else if (g_video_rect.w <= 0 || g_video_rect.h <= 0) {
        static int log_rect_inv = 0;
        if (log_rect_inv++ % 60 == 0)
          log_warn("BaresipManager", "Remote video RECT INVALID: %d,%d %dx%d",
                   g_video_rect.x, g_video_rect.y, g_video_rect.w,
                   g_video_rect.h);
      } else {
        // Ready to render

        SDL_Rect target_rect = g_video_rect; // Default to full fill

        // Aspect Fit Logic
        if (st->size.w > 0 && st->size.h > 0) {
          float src_ratio = (float)st->size.w / (float)st->size.h;
          float dst_ratio = (float)g_video_rect.w / (float)g_video_rect.h;

          if (src_ratio > dst_ratio) {
            // Source is wider than dest: Fit Width
            target_rect.w = g_video_rect.w;
            target_rect.h = (int)((float)g_video_rect.w / src_ratio);
            target_rect.x = g_video_rect.x;
            target_rect.y =
                g_video_rect.y + (g_video_rect.h - target_rect.h) / 2;
          } else {
            // Source is taller than dest: Fit Height
            target_rect.h = g_video_rect.h;
            target_rect.w = (int)((float)g_video_rect.h * src_ratio);
            target_rect.y = g_video_rect.y;
            target_rect.x =
                g_video_rect.x + (g_video_rect.w - target_rect.w) / 2;
          }
        }

        // Logging
        static int log_div = 0;
        if (log_div++ % 60 == 0) {
          fprintf(stderr,
                  "RENDER_REMOTE_EXEC: Target=%d,%d %dx%d (Src %dx%d)\n",
                  target_rect.x, target_rect.y, target_rect.w, target_rect.h,
                  st->size.w, st->size.h);
        }

        // Render (No Clip needed for Aspect Fit usually, but keeps it clean)
        // Draw Black Background first?
        // SDL_RenderFillRect with black? The layer is already
        // black/transparent.

        int ret = SDL_RenderCopy(renderer, st->texture, NULL, &target_rect);
        if (ret != 0) {
          log_warn("BaresipManager", "SDL_RenderCopy failed: %s",
                   SDL_GetError());
        }
      }
    }

    mtx_unlock(st->lock);
  }

  // Pass 2: Local Video (Overlay)
  int local_count = 0;
  for (le = vidisp_list.head; le; le = le->next) {
    struct vidisp_st *st = le->data;
    if (!st->is_local)
      continue;

    local_count++;
    mtx_lock(st->lock);
    if (st->texture) {
      SDL_Rect *dest = NULL;
      if (g_local_video_rect.w > 0 && g_local_video_rect.h > 0) {
        dest = &g_local_video_rect;
      }

      // Throttle logs
      static int log_div_loc = 0;
      if (log_div_loc++ % 60 == 0) {
        fprintf(stderr,
                "sdl_vid_render: Local Stream found. Rect: %d,%d %dx%d. "
                "Texture: %p\n",
                g_local_video_rect.x, g_local_video_rect.y,
                g_local_video_rect.w, g_local_video_rect.h,
                (void *)st->texture);
      }

      if (dest) {
        SDL_RenderCopy(renderer, st->texture, NULL, dest);
      } else {
        if (log_div_loc % 60 == 0)
          fprintf(stderr,
                  "sdl_vid_render: Local video SKIPPED (Invalid Rect)\n");
      }
    } else {
      static int log_no_tex = 0;
      if (log_no_tex++ % 60 == 0)
        fprintf(stderr, "sdl_vid_render: Local video has NO TEXTURE\n");
    }
    mtx_unlock(st->lock);
  }

  static int log_lc = 0;
  if (log_lc++ % 60 == 0 && local_count == 0) {
    fprintf(stderr, "sdl_vid_render: NO LOCAL VIDEO STREAMS FOUND in list!\n");
  }

  mtx_unlock(vidisp_list_lock);
}

// Helper to access module functions
struct vidisp *baresip_manager_vidisp(void) {
  static struct vidisp vd = {
      .name = "sdl_vidisp",
      .alloch = sdl_vidisp_alloc,
      .updateh = sdl_vidisp_update,
      .disph = sdl_vidisp_disp,
      .hideh = sdl_vidisp_hide,
  };
  return &vd;
}

// Create default configuration file
static void create_default_config(const char *config_path) {
  FILE *f = fopen(config_path, "w");
  if (f) {
    fprintf(f, "# Minimal Baresip Config\n"
               "poll_method\t\tpoll\n"
               "sip_listen\t\t0.0.0.0:0\n"
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
#ifdef __APPLE__
               "video_source\t\tfakevideo\n"
#else
               "video_source\t\tdummy,nil\n" // Use dummy or v4l2 if available
#endif
               "video_display\t\tsdl_vidisp,nil\n"
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
               "module_load\t\tselfview.so\n"
#endif
               "module_load\t\topus.so\n"
               "# Network Modules\n"
               "module_load\t\tudp.so\n"
               "module_load\t\ttcp.so\n"
               "module_load\t\tice.so\n"
               "module_load\t\tstun.so\n"
               "module_load\t\tturn.so\n"
               "module_load\t\toutbound.so\n"
               "# Selfview\n"
               "video_selfview\t\twindow\n"
               "# Keepalive\n"
               "sip_keepalive_interval\t15\n");
    fclose(f);
    log_info("BaresipManager", "Wrote minimal config to %s", config_path);
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
    log_warn("BaresipManager", "Detected incompatible 'sdl' audio driver in "
                               "config. Patching to 'audiounit'...");

    // Simple string replacement approach for this specific issue
    // We will just rewrite the default config if we detect the broken one,
    // or we can try to replace.
    // safer to just append the override to the end, Baresip might use the
    // last one? No, Baresip usually uses the first or last, unpredictable.
    // Better to rewrite.

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
      fprintf(f_append, "\n# Keepalive\nsip_keepalive_interval\t15\n");
      fclose(f_append);
      log_info("BaresipManager", "Appended missing keepalive config");
    }
  }

  // Check for video_selfview
  bool has_selfview = false;
  f_check_ka = fopen(config_path, "r");
  if (f_check_ka) {
    char line[256];
    while (fgets(line, sizeof(line), f_check_ka)) {
      if (strstr(line, "video_selfview")) {
        has_selfview = true;
        break;
      }
    }
    fclose(f_check_ka);
  }

  if (!has_selfview) {
    FILE *f_append = fopen(config_path, "a");
    if (f_append) {
      fprintf(f_append, "\n# Selfview\nvideo_selfview\t\twindow\n");
      fclose(f_append);
      log_info("BaresipManager",
               "Appended missing video_selfview config (window)");
    }
  }

  // Enforce avcapture on macOS (simple check)
#ifdef __APPLE__
  // Logic to replace video_source fakevideo with avcapture if found?
  // For now, let's rely on create_default checks or just warn.
  // The baresip_init code forces cfg->video.src_mod anyway if we use my
  // previous edit. Wait, I removed the programmatic force? No, I updated it. I
  // will rely on the programmatic update I made in step 140 (which was
  // successful in content, just conf_set failed). So config file check is
  // backup.
#endif
}

// Test harness for video verification
static struct tmr test_video_tmr;
static void test_video_call_cb(void *arg) {
  (void)arg;
  log_info("BaresipManager", ">>> AUTO-STARTING VIDEO CALL TEST to 808086 <<<");
  baresip_manager_videocall("808082");
}

// Initialize Baresip
int baresip_manager_init(void) {
  static bool g_initialized = false;
  if (g_initialized)
    return 0;
  g_initialized = true;

  int err;

  log_info("BaresipManager", "Initializing...");

  // Initialize libre
  history_manager_init();
  err = libre_init();
  if (err) {
    log_error("BaresipManager", "Failed to initialize libre: %d", err);
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

    log_info("BaresipManager", "Config dir: %s", home_dir);
  }

  // Configure baresip from config file
  int cfg_err = conf_configure();
  if (cfg_err) {
    log_warn("BaresipManager", "conf_configure failed: %d (Using defaults)",
             cfg_err);
  }

  struct config *cfg = conf_config();
  if (!cfg) {
    log_error("BaresipManager", "Failed to get config");
    libre_close();
    return EINVAL;
  }

#ifdef __APPLE__
  // FORCE audio driver to audiounit on macOS, ignoring config file errors
  log_info("BaresipManager",
           "Forcing audio driver to 'audiounit' for macOS...");
  snprintf(cfg->audio.play_mod, sizeof(cfg->audio.play_mod), "audiounit");
  snprintf(cfg->audio.src_mod, sizeof(cfg->audio.src_mod), "audiounit");
  snprintf(cfg->audio.alert_mod, sizeof(cfg->audio.alert_mod), "audiounit");

  // Audio Device Tuning (Fix for "unsmoothy" audio)
  cfg->audio.adaptive = true;
  cfg->audio.buffer.min = 100; // Minimum device buffer 100ms
  cfg->audio.buffer.max = 300; // Maximum device buffer 300ms

  // Network Jitter Buffer Tuning (Fix for jbuf drops)
  cfg->avt.audio.jbtype = JBUF_ADAPTIVE;
  cfg->avt.audio.jbuf_del.min = 100; // Min network delay 100ms
  cfg->avt.audio.jbuf_del.max = 500; // Max network delay 500ms

  log_info(
      "BaresipManager",
      "Audio tuning applied: Device=[100-300ms], Network(AVT)=[100-500ms]");
#endif

  // Video
  // Default to avcapture on Apple, v4l2 on Linux if not set
  if (strlen(cfg->video.src_mod) == 0) {
#ifdef __APPLE__
    re_snprintf(cfg->video.src_mod, sizeof(cfg->video.src_mod), "avcapture");
#else
    re_snprintf(cfg->video.src_mod, sizeof(cfg->video.src_mod), "v4l2");
#endif
  }

  // Ensure we use our custom display module
  re_snprintf(cfg->video.disp_mod, sizeof(cfg->video.disp_mod), "sdl_vidisp");
  cfg->video.enc_fmt = VID_FMT_YUV420P;

  // Initialize Baresip core (Must be FIRST to init lists)
  err = baresip_init(cfg);
  if (err) {
    log_error("BaresipManager", "Failed to initialize baresip: %d", err);
    libre_close();
    return err;
  }

  // --- Register Static Modules ---
  struct mod *m = NULL;

  // Audio Codecs
  extern const struct mod_export exports_opus;
  extern const struct mod_export exports_g711;
  mod_add(&m, &exports_opus);
  mod_add(&m, &exports_g711);

  // Audio Driver
#ifdef __APPLE__
  extern const struct mod_export exports_audiounit;
  mod_add(&m, &exports_audiounit);
#endif

  // Video Codecs & Formats
  extern const struct mod_export exports_avcodec;
  extern const struct mod_export exports_avformat;
  extern const struct mod_export exports_swscale;

  mod_add(&m, &exports_avcodec);
  mod_add(&m, &exports_avformat);
  mod_add(&m, &exports_swscale);

  // Video Display
  extern const struct mod_export exports_sdl_vidisp;
  // extern const struct mod_export exports_window; // CONFLICT: we handle
  // 'window' alias in sdl_vidisp_init

  err = mod_add(&m, &exports_sdl_vidisp);
  if (err)
    log_warn("BaresipManager", "mod_add(sdl_vidisp) failed: %d", err);
  else
    log_info("BaresipManager", "mod_add(sdl_vidisp) success");

  // Video Sources
  extern const struct mod_export exports_fakevideo;
  mod_add(&m, &exports_fakevideo);

#ifdef __APPLE__
  extern const struct mod_export exports_avcapture;
  mod_add(&m, &exports_avcapture);
#endif

  // Register selfview module to enable local video looping
  extern const struct mod_export exports_selfview;
  mod_add(&m, &exports_selfview);

  // mod_add(&m, &exports_window); // DISABLE STANDARD WINDOW MODULE

  sdl_set_background_draw_cb(sdl_vid_render); // Hook renderer

  // NAT Traversal
  extern const struct mod_export exports_stun;
  extern const struct mod_export exports_turn;
  extern const struct mod_export exports_ice;

  mod_add(&m, &exports_stun);
  mod_add(&m, &exports_turn);
  mod_add(&m, &exports_ice);

  log_info("BaresipManager",
           "Modules registered. Forced video display to 'sdl_vidisp'");

  // Initialize User Agents
  err = ua_init("baresip-lvgl", true, true, true);
  if (err) {
    log_error("BaresipManager", "Failed to initialize UA: %d", err);
    baresip_close();
    libre_close();
    return err;
  }

  // Manually enable transports
  struct sa laddr_any;
  sa_init(&laddr_any, AF_INET);

  log_info("BaresipManager", "Manually adding UDP transport...");
  err = sip_transp_add(uag_sip(), SIP_TRANSP_UDP, &laddr_any);
  if (err)
    log_warn("BaresipManager", "Failed to add UDP transport: %d", err);

  // Generate UUID if missing
  if (cfg && !str_isset(cfg->sip.uuid)) {
    log_info("BaresipManager", "Generating missing UUID...");
    snprintf(cfg->sip.uuid, sizeof(cfg->sip.uuid),
             "%08x-%04x-%04x-%04x-%08x%04x", rand(), rand() & 0xFFFF,
             rand() & 0xFFFF, rand() & 0xFFFF, rand(), rand() & 0xFFFF);
  }

  // Debug: Print network interfaces
  log_debug("BaresipManager", "--- Network Interface Debug ---");
  net_debug(NULL, NULL);
  log_debug("BaresipManager", "------------------------------");

  // Register event handler
  bevent_register(call_event_handler, NULL);

  if (!vidisp_list_lock) {
    mutex_alloc(&vidisp_list_lock);
  }

  log_info("BaresipManager", "Initialization complete");
  return 0;
}

static int sdl_vidisp_init(void) {
  int err = 0;
  if (!vid) {
    err =
        vidisp_register(&vid, baresip_vidispl(), "sdl_vidisp", sdl_vidisp_alloc,
                        sdl_vidisp_update, sdl_vidisp_disp, sdl_vidisp_hide);
    if (err) {
      log_warn("BaresipManager", "Failed to register sdl_vidisp: %d", err);
      return err;
    } else {
      log_info("BaresipManager", "Registered sdl_vidisp module");
    }
  }

  // Also register 'window' alias for selfview
  if (!vid2) {
    err = vidisp_register(&vid2, baresip_vidispl(), "window", sdl_vidisp_alloc,
                          sdl_vidisp_update, sdl_vidisp_disp, sdl_vidisp_hide);
    if (err) {
      log_warn("BaresipManager", "Failed to register window alias: %d", err);
    } else {
      log_info("BaresipManager",
               "Registered window alias for local video (inside sdl_vidisp)");
    }
  }
  return err;
}

static int sdl_vidisp_close(void) {
  vid = mem_deref(vid);
  return 0;
}

const struct mod_export exports_sdl_vidisp = {
    "sdl_vidisp",
    "vidisp",
    sdl_vidisp_init,
    sdl_vidisp_close,
};

static int window_init(void) {
  if (vid2)
    return 0;
  int err =
      vidisp_register(&vid2, baresip_vidispl(), "window", sdl_vidisp_alloc,
                      sdl_vidisp_update, sdl_vidisp_disp, sdl_vidisp_hide);
  if (err) {
    fprintf(stderr, "BaresipManager: Failed to register window alias: %d\n",
            err);
    log_warn("BaresipManager", "Failed to register window alias: %d", err);
  } else {
    fprintf(stderr,
            "BaresipManager: Registered window alias for local video\n");
    log_info("BaresipManager", "Registered window alias for local video");
  }
  return err;
}

static int window_close(void) {
  vid2 = mem_deref(vid2);
  return 0;
}

const struct mod_export exports_window = {
    "window",
    "vidisp",
    window_init,
    window_close,
};

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

// Internal helper for making calls
static int baresip_manager_connect(const char *uri, const char *account_aor,
                                   bool video) {
  struct ua *ua = NULL;
  int err;
  char full_uri[256];

  if (!uri)
    return -1;

  log_info("BaresipManager", "Making %s call to: %s (Account: %s)",
           video ? "VIDEO" : "AUDIO", uri,
           account_aor ? account_aor : "Default");

  //  // DEBUG UA LIST
  struct list *ual = uag_list();
  log_info("BaresipManager",
           "Connect: Request to %s using Account %s (UA Count: %d)", uri,
           account_aor ? account_aor : "Default", list_count(ual));

  struct le *le_debug = list_head(ual);
  while (le_debug) {
    struct ua *u = le_debug->data;
    log_info("BaresipManager", "  - UA Available: %p", (void *)u);
    le_debug = le_debug->next;
  }

  // Select User Agent
  if (account_aor) {
    ua = uag_find_aor(account_aor);
    if (!ua) {
      log_warn("BaresipManager", "Account %s not found, attempting defaults...",
               account_aor);
    }
  }

  if (!ua) {
    // Try first in list (Fallback since uag_current is not public/available)
    struct le *le = list_head(uag_list());
    if (le)
      ua = le->data;
  }

  if (!ua) {
    log_warn("BaresipManager", "No user agent available - Cannot connect");
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
  if (g_call_state.current_call && !call_is_onhold(g_call_state.current_call)) {
    log_info("BaresipManager",
             "Holding active call %p before new outgoing call",
             g_call_state.current_call);
    call_hold(g_call_state.current_call, true);
  }

  log_info("BaresipManager", "Calling normalized uri: '%s' using ua: %p",
           full_uri, (void *)ua);

  struct call *call = NULL;
  // Use VIDMODE_ON if video is requested
  err = ua_connect(ua, &call, NULL, full_uri, video ? VIDMODE_ON : VIDMODE_OFF);
  if (err) {
    log_error("BaresipManager", "Failed to make call: %d", err);
    return err;
  }
  log_info("BaresipManager", "Call initiated successfully (err=0)");

  if (call) {
    g_call_state.current_call = call;
    add_or_update_call(call, CALL_STATE_OUTGOING, full_uri);
    log_debug("BaresipManager", "Stored outgoing call object: %p",
              (void *)call);
  } else {
    log_warn("BaresipManager",
             "WARNING: ua_connect succeeded but returned NULL "
             "call object");
  }

  strncpy(g_call_state.peer_uri, full_uri, sizeof(g_call_state.peer_uri) - 1);
  g_call_state.state = CALL_STATE_OUTGOING;

  return 0;
}

int baresip_manager_call_with_account(const char *uri,
                                      const char *account_aor) {
  return baresip_manager_connect(uri, account_aor, false);
}

int baresip_manager_call(const char *uri) {
  return baresip_manager_connect(uri, NULL, false);
}

int baresip_manager_videocall_with_account(const char *uri,
                                           const char *account_aor) {
  return baresip_manager_connect(uri, account_aor, true);
}

int baresip_manager_videocall(const char *uri) {
  return baresip_manager_connect(uri, NULL, true);
}

int baresip_manager_answer_call(bool video) {
  if (!g_call_state.current_call) {
    log_warn("BaresipManager", "current_call is NULL, trying to find active "
                               "call...");
    struct le *le;
    for (le = uag_list()->head; le; le = le->next) {
      struct ua *ua = le->data;
      struct call *c = ua_call(ua);
      if (c) {
        log_debug("BaresipManager", "Found active call in UA: %p", (void *)c);
        g_call_state.current_call = c;
        if (call_state(c) == CALL_STATE_INCOMING) {
          log_debug("BaresipManager", "Call state is INCOMING, proceeding");
        } else {
          log_debug("BaresipManager", "Call state is NOT INCOMING: %d",
                    call_state(c));
        }
        break;
      }
    }
  }

  if (!g_call_state.current_call) {
    log_warn("BaresipManager", "No incoming call to answer (Scan failed)");
    return -1;
  }

  log_info("BaresipManager", "Answering call %p (Video=%d)",
           (void *)g_call_state.current_call, video);

  // Hold other calls first
  for (int i = 0; i < MAX_CALLS; i++) {
    struct call *c = g_call_state.active_calls[i].call;
    if (c && c != g_call_state.current_call && !call_is_onhold(c)) {
      log_info("BaresipManager", "Holding existing call %p before answering",
               c);
      call_hold(c, true);
    }
  }

  int err = call_answer(g_call_state.current_call, 200,
                        video ? VIDMODE_ON : VIDMODE_OFF);
  if (err) {
    log_error("BaresipManager", "Failed to answer call: %d", err);
    return err;
  }

  log_info("BaresipManager", "call_answer returned success (0)");

  // Manually force state update just in case event is slow
  g_call_state.state = CALL_STATE_ESTABLISHED;
  if (g_call_state.callback) {
    g_call_state.callback(CALL_STATE_ESTABLISHED, g_call_state.peer_uri,
                          (void *)g_call_state.current_call);
  }

  return 0;
}

int baresip_manager_reject_call(void *call_ptr) {
  if (!call_ptr)
    return -1;
  log_info("BaresipManager", "Rejecting specific call %p", call_ptr);
  call_hangup((struct call *)call_ptr, 486, "Busy Here");
  return 0;
}

int baresip_manager_hangup(void) {
  // Failsafe: If current_call is NULL, try to find one
  if (!g_call_state.current_call) {
    for (int i = 0; i < MAX_CALLS; i++) {
      if (g_call_state.active_calls[i].call) {
        g_call_state.current_call = g_call_state.active_calls[i].call;
        g_call_state.state = g_call_state.active_calls[i].state;
        log_info("BaresipManager", "Hangup: Auto-selected call %p",
                 g_call_state.current_call);
        break;
      }
    }
  }

  if (!g_call_state.current_call) {
    log_warn("BaresipManager", "No active call to hangup");
    return -1;
  }

  log_info("BaresipManager", "Hanging up call");

  call_hangup(g_call_state.current_call, 0, NULL);

  // Do NOT clear current_call here.
  // We let remove_call() or zombie_cleanup handle the clearing and
  // auto-switching. This ensures the pointer remains valid for the cleanup
  // logic to detect "Current was removed".

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

  log_info("BaresipManager", "Microphone %s", mute ? "muted" : "unmuted");
}

// Dummy timer for loop control
static struct tmr g_loop_tmr;

static void loop_timeout_cb(void *arg) {
  (void)arg;
  re_cancel();
}

void baresip_manager_loop(void (*ui_cb)(void), int interval_ms) {
  int err;

  // --- INITIALIZATION START ---
  // Initialize manager (accounts, ua, etc.)
  // This function is now GUARDED and handles all init (libre, config, baresip)
  err = baresip_manager_init();
  if (err) {
    log_error("BaresipManager", "baresip_manager_init failed: %d", err);
    goto cleanup;
  }
  // --- INITIALIZATION END ---

  log_info("BaresipManager", "Starting Main Loop...");
  tmr_init(&g_loop_tmr);

  while (true) {
    // Schedule cancellation to return control to UI
    tmr_start(&g_loop_tmr, interval_ms, loop_timeout_cb, NULL);

    // Run re_main (blocks until loop_timeout_cb fires or other signal)
    // process events
    int err = re_main(NULL);
    if (err && err != EINTR) {
      if (err != 0) {
        // Throttle error logging to avoid spam (e.g. error 22)
        static int err_count = 0;
        if (err_count++ % 100 == 0) {
          log_warn("BaresipManager", "re_main returned: %d (throttling)", err);
        }
        usleep(100000); // 100ms delay
      }
    }

    if (ui_cb) {
      ui_cb();
    }
  }

cleanup:
  log_info("BaresipManager", "Shutdown");
  baresip_close();
  libre_close();
}

bool baresip_manager_is_muted(void) { return g_call_state.muted; }

int baresip_manager_add_account(const voip_account_t *acc) {
  char aor[1024];
  int err;

  if (!acc)
    return EINVAL;

  /* Format: "Display Name"
   * <sip:user:password@domain;transport=udp>;regint=3600
   */
  /* Simplified for now: <sip:user:password@domain>;transport=udp */

  char transport_param[32] = ";transport=udp";

  // Handle Custom Port
  char server_with_port[150];
  if (acc->port > 0 && acc->port != 5060) {
    snprintf(server_with_port, sizeof(server_with_port), "%s:%d", acc->server,
             acc->port);
  } else {
    snprintf(server_with_port, sizeof(server_with_port), "%s", acc->server);
  }

  if (strlen(acc->outbound_proxy) > 0) {
    // With Outbound Proxy
    // Format:
    // <sip:user@domain;transport=udp>;auth_pass=pass;auth_user=user;outbound=sip:proxy;regint=600

    char proxy_buf[256];
    if (strstr(acc->outbound_proxy, "sip:") == acc->outbound_proxy) {
      snprintf(proxy_buf, sizeof(proxy_buf), "%s", acc->outbound_proxy);
    } else {
      snprintf(proxy_buf, sizeof(proxy_buf), "sip:%s", acc->outbound_proxy);
    }

    const char *a_user =
        (strlen(acc->auth_user) > 0) ? acc->auth_user : acc->username;

    snprintf(aor, sizeof(aor),
             "<sip:%s@%s%s>;auth_pass=%s;auth_user=%s;outbound=%s;regint=600",
             acc->username, server_with_port, transport_param, acc->password,
             a_user, proxy_buf);
  } else {
    // Direct Registration
    snprintf(aor, sizeof(aor), "<sip:%s:%s@%s%s>;regint=3600", acc->username,
             acc->password, server_with_port, transport_param);
  }

  // Add display name if present
  if (strlen(acc->display_name) > 0) {
    char temp[1024];
    snprintf(temp, sizeof(temp), "\"%s\" %s", acc->display_name, aor);
    strcpy(aor, temp);
  }

  log_info("BaresipManager", "Adding account: %s", aor);

  struct ua *ua = NULL;
  err = ua_alloc(&ua, aor);
  if (err) {
    log_error("BaresipManager", "Failed to allocate UA: %d", err);
    return err;
  }

  // Auto-register
  if (ua) {
    if (acc->enabled) {
      err = ua_register(ua);
      if (err) {
        log_warn("BaresipManager", "Failed to register UA: %d", err);
      }
    }

    // Update our internal tracking list
    const char *final_aor = account_aor(ua_account(ua));
    update_account_status(final_aor, REG_STATUS_NONE);
  }

  return 0;
}

int baresip_manager_get_active_calls(call_info_t *calls, int max_count) {
  int count = 0;
  for (int i = 0; i < MAX_CALLS && count < max_count; i++) {
    struct call *c = g_call_state.active_calls[i].call;
    if (c) {
      // Self-healing: Check actual Baresip state
      enum call_state real_state = call_state(c);

      if (real_state == CALL_STATE_TERMINATED ||
          g_call_state.active_calls[i].state == CALL_STATE_TERMINATED) {
        log_warn("BaresipManager",
                 "Found zombie call %p (TERMINATED) in active list. Cleaning "
                 "up...",
                 (void *)c);
        // Force cleanup
        g_call_state.active_calls[i].call = NULL;
        g_call_state.active_calls[i].state = CALL_STATE_IDLE;
        g_call_state.active_calls[i].peer_uri[0] = '\0';

        // Also check if it was current
        if (g_call_state.current_call == c) {
          g_call_state.current_call = NULL;
          g_call_state.state = CALL_STATE_IDLE;

          // Auto-switch to next active call
          for (int j = 0; j < MAX_CALLS; j++) {
            if (g_call_state.active_calls[j].call) {
              g_call_state.current_call = g_call_state.active_calls[j].call;
              g_call_state.state = g_call_state.active_calls[j].state;
              log_info("BaresipManager", "Auto-switched (zombie) to call %p",
                       g_call_state.current_call);
              break;
            }
          }
        }
        continue;
      }

      // Update our cache with real state
      g_call_state.active_calls[i].state = real_state;

      calls[count].id = c;
      strncpy(calls[count].peer_uri, g_call_state.active_calls[i].peer_uri,
              sizeof(calls[count].peer_uri) - 1);
      calls[count].state = g_call_state.active_calls[i].state;
      calls[count].is_held = call_is_onhold(c);
      calls[count].is_current = (c == g_call_state.current_call);
      count++;
    }
  }
  return count;
}

int baresip_manager_send_dtmf(char key) {
  struct call *call = g_call_state.current_call;
  if (!call)
    return -1;
  return call_send_digit(call, key);
}

int baresip_manager_hold_call(void *call_id) {
  struct call *call = (struct call *)call_id;
  if (!call)
    call = g_call_state.current_call;

  if (!call) {
    log_warn("BaresipManager", "No call to hold");
    return -1;
  }
  log_info("BaresipManager", "Holding call %p", call);
  return call_hold(call, true);
}

int baresip_manager_resume_call(void *call_id) {
  struct call *call = (struct call *)call_id;
  if (!call)
    call = g_call_state.current_call;

  if (!call) {
    log_warn("BaresipManager", "No call to resume");
    return -1;
  }

  // Hold other calls first
  for (int i = 0; i < MAX_CALLS; i++) {
    struct call *c = g_call_state.active_calls[i].call;
    if (c && c != call && !call_is_onhold(c)) {
      log_info("BaresipManager", "Holding existing call %p before resuming %p",
               (void *)c, (void *)call);
      call_hold(c, true);
    }
  }

  log_info("BaresipManager", "Resuming call %p", (void *)call);
  return call_hold(call, false);
}

int baresip_manager_switch_to(void *call_id) {
  struct call *target = (struct call *)call_id;
  if (!target)
    return -1;

  // HOLD/RESUME LOGIC REMOVED per valid usage requirement.
  // User explicitly wants context switch without automatic audio hold/resume.
  // Hold/Resume actions must be triggered by UI buttons or new call setup.

  g_call_state.current_call = target;

  // find state
  for (int i = 0; i < MAX_CALLS; i++) {
    if (g_call_state.active_calls[i].call == target) {
      g_call_state.state = g_call_state.active_calls[i].state;
      strncpy(g_call_state.peer_uri, g_call_state.active_calls[i].peer_uri,
              255);
      break;
    }
  }

  return 0;
}

/* Format: "Display Name" <sip:user:password@domain;transport=udp>;regint=3600
 */
/* Simplified for now: <sip:user:password@domain>;transport=udp */

// End of baresip_manager.c

void baresip_manager_destroy(void) {
  ua_stop_all(false);
  ua_close();
  baresip_close();
  libre_close();
}
