#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "logger.h"
#include <stdbool.h>
#include <stddef.h>

#define MAX_ACCOUNTS 10
#define CONFIG_DIR ".baresip-lvgl"

// Audio codec enumeration
typedef enum {
  CODEC_PCMU = 0,
  CODEC_PCMA,
  CODEC_OPUS,
  CODEC_G722,
  CODEC_GSM,
  CODEC_COUNT
} audio_codec_t;

// VoIP account structure
typedef struct {
  char display_name[64]; // Display Name
  char username[64];     // SIP Username
  char password[64];     // Auth Password
  char server[128];      // SIP Domain/Server
  int port;              // SIP Port (default 5060)
  bool enabled;          // Register enabled

  // Advanced / Extended settings
  char nickname[64];
  char auth_user[64];        // Authentication Username
  char outbound_proxy[128];  // Primary Proxy
  char outbound_proxy2[128]; // Secondary Proxy
  int reg_interval;          // Registration interval (default 900)

  // Media & Network
  char media_enc[32]; // e.g., srtp, zrtp, none
  // char media_nat[32]; // Deprecated, replaced by use_ice
  bool rtcp_mux;
  char transport[32]; // udp, tcp, tls
  bool use_ice;
  char stun_server[128];

  // SIP / Call Control
  bool prack;             // Reliable provisional responses
  char dtmf_mode[32];     // rtp, sdp, info
  char answer_mode[32];   // manual, auto
  char vm_uri[128];       // Voicemail URI
  char realm[64];         // SIP Realm
  char audio_codecs[256]; // Comma-separated list of audio codecs
  char video_codecs[256]; // Comma-separated list of video codecs
} voip_account_t;

// App configuration structure
typedef struct {
  // General
  bool start_automatically;
  char listen_address[64];
  int address_family; // 0=AF_UNSPEC, 1=AF_INET, 2=AF_INET6
  char dns_servers[128];
  char stun_server[128];

  // Security / TLS
  bool use_tls_client_cert;
  bool verify_server_cert;
  bool use_tls_ca_file;

  // SIP
  char user_agent[64];

  // Contacts
  int contacts_source; // 0=Baresip

  // Media
  int video_frame_size; // 0=1280x720 (Default)
  audio_codec_t preferred_codec;
  int log_level;

  // Account
  int default_account_index;
} app_config_t;

// Initialize config manager (creates config directory if needed)
void config_manager_init(void);

// Load/Save App Configuration
int config_load_app_settings(app_config_t *config);
int config_save_app_settings(const app_config_t *config);
const char *config_get_codec_name(audio_codec_t codec);

// Account management functions
int config_load_accounts(voip_account_t *accounts, int max_count);
int config_save_accounts(const voip_account_t *accounts, int count);

// Utility functions
void config_get_dir_path(char *path, size_t size);

#endif // CONFIG_MANAGER_H
