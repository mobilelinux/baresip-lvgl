#include "config_manager.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

// Audio codec names
static const char *codec_names[] = {"G.711 PCMU", "G.711 PCMA", "Opus", "G.722", "GSM"};

// Helper: Ensure config directory exists
void config_manager_init(void) {
  char path[256];
  const char *home = getenv("HOME");
  if (home) {
    snprintf(path, sizeof(path), "%s/.baresip-lvgl", home);
  } else {
    snprintf(path, sizeof(path), "/root/.baresip-lvgl");
  }

  printf("ConfigManager: Init path='%s'\n", path);

  struct stat st = {0};
  if (stat(path, &st) == -1) {
    if (mkdir(path, 0755) == 0) {
      printf("ConfigManager: Created directory '%s'\n", path);
    } else {
      printf("ConfigManager: Failed to create directory '%s'\n", path);
    }
  }
}

// Helper: Get config directory path
void config_get_dir_path(char *path, size_t size) {
  const char *home = getenv("HOME");
  if (home) {
    snprintf(path, size, "%s/.baresip-lvgl", home);
  } else {
    strncpy(path, "/root/.baresip-lvgl", size);
  }
  printf("TEST_MATCH_CFG: Config Path: %s\n", path);
}

// Load app settings
int config_load_app_settings(app_config_t *config) {
  char path[256];
  FILE *fp;

  if (!config)
    return -1;

  // Set defaults
  memset(config, 0, sizeof(app_config_t));
  config->preferred_codec = CODEC_PCMU; // Default to G.711u for compatibility
  config->default_account_index = -1; // -1 means "Always Ask" or "None"
  config->start_automatically = 0;
  config->listen_address[0] = '\0';
  config->address_family = 0; // IPv4
  config->dns_servers[0] = '\0';
  config->stun_server[0] = '\0';
  config->use_tls_client_cert = 0;
  config->verify_server_cert = 0;
  config->use_tls_ca_file = 0;
  config->log_level = 3; // Info
  strcpy(config->user_agent, "Baresip-LVGL");
  config->contacts_source = 0; // None
  config->video_frame_size = 0; // 0=Disabled. Fixes 488 for audio-only calls.

  config_get_dir_path(path, sizeof(path));
  strcat(path, "/settings.conf");

  fp = fopen(path, "r");
  if (fp) {
    // Parse key-value pairs
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
      char *key = strtok(line, "=");
      char *val = strtok(NULL, "\n");
      if (key && val) {
        if (strcmp(key, "Codec") == 0) {
         config->preferred_codec = atoi(val); // Allow Codec Selection (Opus supported now)
         // log_info("ConfigManager", "Ignored saved codec preference '%s' to enforce PCMU", val);
        }
        else if (strcmp(key, "DefaultAccount") == 0)
          config->default_account_index = atoi(val);
        else if (strcmp(key, "StartAuto") == 0)
          config->start_automatically = atoi(val);
        else if (strcmp(key, "ListenAddr") == 0)
          strncpy(config->listen_address, val, sizeof(config->listen_address)-1);
        else if (strcmp(key, "AddrFam") == 0)
          config->address_family = atoi(val);
        else if (strcmp(key, "DNS") == 0)
          strncpy(config->dns_servers, val, sizeof(config->dns_servers)-1);
        else if (strcmp(key, "StunServer") == 0)
          strncpy(config->stun_server, val, sizeof(config->stun_server)-1);
        else if (strcmp(key, "UseTLSCert") == 0)
          config->use_tls_client_cert = atoi(val);
        else if (strcmp(key, "VerifyServerCert") == 0)
          config->verify_server_cert = atoi(val);
        else if (strcmp(key, "UseTLSCA") == 0)
          config->use_tls_ca_file = atoi(val);
        else if (strcmp(key, "UserAgent") == 0)
          strncpy(config->user_agent, val, sizeof(config->user_agent)-1);
        else if (strcmp(key, "ContactsSrc") == 0)
          config->contacts_source = atoi(val);
        else if (strcmp(key, "VideoSize") == 0)
          config->video_frame_size = atoi(val);
        else if (strcmp(key, "LogLevel") == 0)
          config->log_level = logger_parse_level(val);
      }
    }
    fclose(fp);
    log_info("ConfigManager",
             "Loaded settings: Codec=%s, DefAcc=%d, AutoStart=%d",
             config_get_codec_name(config->preferred_codec),
             config->default_account_index, config->start_automatically);
    return 0;
  }

  // Try legacy pipe format (Codec|DefAcc) for migration
  fp = fopen(path, "r"); // Re-open
  if (fp) {
    int c, d;
    if (fscanf(fp, "%d|%d", &c, &d) == 2) {
      config->preferred_codec = c;
      config->default_account_index = d;
    }
    fclose(fp);
    return 0;
  }

  log_info("ConfigManager", "Using defaults");
  return -1;
}

// Save app settings
int config_save_app_settings(const app_config_t *config) {
  char path[256];
  FILE *fp;

  if (!config)
    return -1;

  config_manager_init(); // Ensure directory exists

  config_get_dir_path(path, sizeof(path));
  strcat(path, "/settings.conf");

  fp = fopen(path, "w");
  if (!fp) {
    log_warn("ConfigManager", "Failed to save settings");
    return -1;
  }

  fprintf(fp, "Codec=%d\n", config->preferred_codec);
  fprintf(fp, "DefaultAccount=%d\n", config->default_account_index);
  fprintf(fp, "StartAuto=%d\n", config->start_automatically);
  fprintf(fp, "ListenAddr=%s\n", config->listen_address);
  fprintf(fp, "AddrFam=%d\n", config->address_family);
  fprintf(fp, "DNS=%s\n", config->dns_servers);
  fprintf(fp, "StunServer=%s\n", config->stun_server);
  fprintf(fp, "UseTLSCert=%d\n", config->use_tls_client_cert);
  fprintf(fp, "VerifyServerCert=%d\n", config->verify_server_cert);
  fprintf(fp, "UseTLSCA=%d\n", config->use_tls_ca_file);
  fprintf(fp, "UserAgent=%s\n", config->user_agent);
  fprintf(fp, "ContactsSrc=%d\n", config->contacts_source);
  fprintf(fp, "VideoSize=%d\n", config->video_frame_size);
  fprintf(fp, "LogLevel=%s\n", logger_level_str(config->log_level));

  fclose(fp);

  log_info("ConfigManager", "Saved settings");
  return 0;
}

// Get codec name
const char *config_get_codec_name(audio_codec_t codec) {
  if (codec >= 0 && codec < CODEC_COUNT) {
    return codec_names[codec];
  }
  return "Unknown";
}

// Load accounts
int config_load_accounts(voip_account_t *accounts, int max_count) {
  char path[256];
  FILE *fp;
  int count = 0;

  if (!accounts || max_count <= 0)
    return 0;

  config_get_dir_path(path, sizeof(path));
  strcat(path, "/accounts.conf");

  fp = fopen(path, "r");
  if (!fp) {
    log_info("ConfigManager", "No accounts file found");
    return 0;
  }

  while (count < max_count) {
    voip_account_t *acc = &accounts[count];
    memset(acc, 0, sizeof(voip_account_t));

    // Set defaults
    acc->port = 5060;
    acc->reg_interval = 900;
    // acc->media_nat no longer exists. defaults to use_ice
    // strcpy(acc->media_nat, "");
    strcpy(acc->dtmf_mode, "rtp");
    strcpy(acc->answer_mode, "manual");
    strcpy(acc->audio_codecs,
           "opus/48000/2,PCMU/8000/1,PCMA/8000/1,G722/16000/1");
    strcpy(acc->video_codecs, "H264");
    strcpy(acc->transport, "udp");
    acc->use_ice = false;
    acc->stun_server[0] = '\0';

    char line[1024];
    if (fgets(line, sizeof(line), fp)) {
      // Remove newline
      line[strcspn(line, "\n")] = 0;
      
      // --- FIX START: Skip comments and empty lines ---
      if (strlen(line) < 3 || line[0] == '#') { 
        continue;
      }
      // --- FIX END ---

      char *token;
      char *rest = line;
      int field = 0;

      while ((token = strsep(&rest, "|"))) {
        switch (field) {
        case 0:
            if (token) strncpy(acc->display_name, token, sizeof(acc->display_name) - 1);
          break;
        case 1:
            if (token) strncpy(acc->username, token, sizeof(acc->username) - 1);
          break;
        case 2:
            if (token) strncpy(acc->password, token, sizeof(acc->password) - 1);
          break;
        case 3:
            if (token) strncpy(acc->server, token, sizeof(acc->server) - 1);
          break;
        case 4:
            if (token) acc->port = atoi(token);
          break;
        case 5:
            if (token) acc->enabled = atoi(token);
          break;
        case 6:
            if (token) strncpy(acc->realm, token, sizeof(acc->realm) - 1);
          break;
        case 7:
            if (token) strncpy(acc->outbound_proxy, token, sizeof(acc->outbound_proxy) - 1);
          break;
        case 8:
            if (token) strncpy(acc->outbound_proxy2, token,
                  sizeof(acc->outbound_proxy2) - 1);
          break;
        case 9:
            if (token) strncpy(acc->auth_user, token, sizeof(acc->auth_user) - 1);
          break;
        case 10:
            if (token) strncpy(acc->nickname, token, sizeof(acc->nickname) - 1);
          break;
        case 11:
            if (token) acc->reg_interval = atoi(token);
          break;
        case 12:
            if (token) strncpy(acc->media_enc, token, sizeof(acc->media_enc) - 1);
          break;
        case 13:
            // if (token) strncpy(acc->media_nat, token, sizeof(acc->media_nat) - 1);
            // Ignore legacy media_nat
          break;
        case 14:
            if (token) acc->rtcp_mux = atoi(token);
          break;
        case 15:
            if (token) acc->prack = atoi(token);
          break;
        case 16:
            if (token) strncpy(acc->dtmf_mode, token, sizeof(acc->dtmf_mode) - 1);
          break;
        case 17:
            if (token) strncpy(acc->answer_mode, token, sizeof(acc->answer_mode) - 1);
          break;
        case 18:
            if (token) strncpy(acc->vm_uri, token, sizeof(acc->vm_uri) - 1);
          break;
        case 19:
            if (token) strncpy(acc->audio_codecs, token, sizeof(acc->audio_codecs) - 1);
          break;
        case 20:
            if (token) strncpy(acc->video_codecs, token, sizeof(acc->video_codecs) - 1);
          break;
        case 21:
            if (token) strncpy(acc->transport, token, sizeof(acc->transport) - 1);
          break;
        case 22:
            if (token) acc->use_ice = atoi(token);
          break;
        case 23:
            if (token) strncpy(acc->stun_server, token, sizeof(acc->stun_server) - 1);
          break;
        }
        field++;
      }

      // Cleanup defaults if atoi on empty string returned 0
      if (acc->port == 0)
        acc->port = 5060;
      if (acc->reg_interval == 0)
        acc->reg_interval = 900;
        
      // FIX: Ensure mandatory fields are present
      if (strlen(acc->username) == 0 || strlen(acc->server) == 0) {
          printf("TEST_MATCH_MGR: Ignored invalid account line (missing user/server)\n");
          continue; 
      }
      printf("TEST_MATCH_CFG: Loaded Line: %s\n", line); // DEBUG
      printf("TEST_MATCH_CFG: Parsed -> User: %s, Server: %s, Auth: %s\n", acc->username, acc->server, acc->auth_user);

      count++;

      // Auto-correct typo in legacy config (fanvi.com -> fanvil.com)
      if (strcmp(acc->server, "fanvi.com") == 0) {
        log_warn("ConfigManager", "Auto-correcting server: %s -> fanvil.com",
                 acc->server);
        strcpy(acc->server, "fanvil.com");
        // Clear potentially unreachable private proxy
        if (acc->outbound_proxy[0] != '\0') {
          log_warn("ConfigManager", "Clearing outbound proxy: %s",
                   acc->outbound_proxy);
          acc->outbound_proxy[0] = '\0';
        }
      }
    } else {
      // fgets returned NULL, either EOF or error
      break;
    }
  }

  fclose(fp);
  log_info("ConfigManager", "Loaded %d account(s)", count);
  return count;
}

// Save accounts
int config_save_accounts(const voip_account_t *accounts, int count) {
  char path[256];
  FILE *fp;

  if (!accounts || count < 0)
    return -1;

  config_manager_init(); // Ensure directory exists

  config_get_dir_path(path, sizeof(path));
  strcat(path, "/accounts.conf");

  fp = fopen(path, "w");
  if (!fp) {
    log_warn("ConfigManager", "Failed to save accounts");
    return -1;
  }

  for (int i = 0; i < count; i++) {
    const voip_account_t *acc = &accounts[i];
    fprintf(
        fp, "%s|%s|%s|%s|%d|%d|%s|%s|%s|%s|%s|%d|%s|%s|%d|%d|%s|%s|%s|%s|%s|%s|%d|%s\n",
        acc->display_name, acc->username, acc->password, acc->server, acc->port,
        acc->enabled, acc->realm, acc->outbound_proxy, acc->outbound_proxy2,
        acc->auth_user, acc->nickname, acc->reg_interval, acc->media_enc,
        "", acc->rtcp_mux, acc->prack, acc->dtmf_mode,
        acc->answer_mode, acc->vm_uri, acc->audio_codecs, acc->video_codecs,
        acc->transport, acc->use_ice, acc->stun_server);
  }

  fclose(fp);
  log_info("ConfigManager", "Saved %d account(s)", count);
  return 0;
}
