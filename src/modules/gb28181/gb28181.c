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

    const char *user = account_auth_user(ua_account(ua));
    if (!user) {
      // Fallback to AOR user part if auth_user is not set
      // (Simpler: just use AOR for now if NULL, essentially "sip:user@domain")
      user = account_aor(ua_account(ua));
    }

    char xml_body[512];
    sn_counter++;

    // Construct GB28181 Keepalive XML
    snprintf(xml_body, sizeof(xml_body),
             "<?xml version=\"1.0\"?>\n"
             "<Notify>\n"
             "<CmdType>Keepalive</CmdType>\n"
             "<SN>%u</SN>\n"
             "<DeviceID>%s</DeviceID>\n"
             "<Status>OK</Status>\n"
             "<Info>\n"
             "</Info>\n"
             "</Notify>",
             sn_counter, user);

    // Send SIP MESSAGE
    // We need destination URI. Usually the Registrar.
    const char *reg_uri = account_outbound(ua_account(ua), 0);
    if (!reg_uri)
      reg_uri = account_aor(ua_account(ua));

    // baresip `message` module uses `message_send`.
    // We can manually send using `ua_send_request`.

    struct pl body;
    pl_set_str(&body, xml_body);

    // We use MESSAGE method
    // Peer: We assume the registrar is the target.
    // If we can't easily get registrar URI, this might fail.
    // Assuming AOR domain for now.
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
