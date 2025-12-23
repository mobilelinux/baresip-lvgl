/**
 * @file gb28181.c GB28181 Module for Baresip
 */
#include <baresip.h>
#include <re.h>
#include <string.h>

static struct tmr tmr_heartbeat;
static uint32_t sn_counter = 0;

static void heartbeat_handler(void *arg) {
  struct le *le;
  (void)arg;

  // Reschedule timer (e.g., every 60 seconds)
  tmr_start(&tmr_heartbeat, 60000, heartbeat_handler, NULL);

  // Iterate through all User Agents
  for (le = uag_list()->head; le; le = le->next) {
    struct ua *ua = le->data;
    struct call *call = NULL;
    (void)call; // Unused for now

    // Only send heartbeat if registered
    if (!ua_isregistered(ua)) {
      continue;
    }

    struct pl device_id_pl = pl_null;
    const char *auth_user = account_auth_user(ua_account(ua));

    if (auth_user) {
      pl_set_str(&device_id_pl, auth_user);
    } else {
      const struct uri *luri = account_luri(ua_account(ua));
      if (luri) {
        device_id_pl = luri->user;
      }
    }

    if (!device_id_pl.p)
      continue;

    char xml_body[512];
    sn_counter++;

    // Construct GB28181 Keepalive XML
    snprintf(xml_body, sizeof(xml_body),
             "<?xml version=\"1.0\"?>\n"
             "<Notify>\n"
             "<CmdType>Keepalive</CmdType>\n"
             "<SN>%u</SN>\n"
             "<DeviceID>%.*s</DeviceID>\n"
             "<Status>OK</Status>\n"
             "<Info>\n"
             "</Info>\n"
             "</Notify>",
             sn_counter, (int)device_id_pl.l, device_id_pl.p);

    // Send SIP MESSAGE
    const char *dest_uri = account_outbound(ua_account(ua), 0);
    if (!dest_uri) {
      dest_uri = account_aor(ua_account(ua));
    }

    // Send message (stateless)
    message_send(ua, dest_uri, xml_body, NULL, NULL);
  }
}

static int gb28181_init(void) {
  info("gb28181: init\n");
  // Start Heartbeat Timer (initial delay 5s)
  tmr_init(&tmr_heartbeat);
  tmr_start(&tmr_heartbeat, 5000, heartbeat_handler, NULL);
  return 0;
}

static int gb28181_close(void) {
  info("gb28181: close\n");
  tmr_cancel(&tmr_heartbeat);
  return 0;
}

const struct mod_export exports_gb28181 = {
    "gb28181",
    "application",
    gb28181_init,
    gb28181_close,
};
