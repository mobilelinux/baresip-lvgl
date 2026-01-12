#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <re.h>
#include <baresip.h>

// Use baresip's log level enum
typedef enum log_level log_level_t;

// Map LOG_LEVEL_* to LEVEL_*
#define LOG_LEVEL_TRACE LEVEL_DEBUG // Baresip might not have TRACE
#define LOG_LEVEL_DEBUG LEVEL_DEBUG
#define LOG_LEVEL_INFO  LEVEL_INFO
#define LOG_LEVEL_WARN  LEVEL_WARN
#define LOG_LEVEL_ERROR LEVEL_ERROR
#define LOG_LEVEL_FATAL LEVEL_ERROR // Map FATAL to ERROR

// Simple logger stubs
static inline void logger_set_level(log_level_t level) {
    (void)level; // No-op for now
}

static inline void logger_init(log_level_t level) {
    (void)level;
}

static inline log_level_t logger_parse_level(const char *str) {
    if (!str) return LEVEL_INFO;
    if (strcasecmp(str, "TRACE") == 0) return LEVEL_DEBUG;
    if (strcasecmp(str, "DEBUG") == 0) return LEVEL_DEBUG;
    if (strcasecmp(str, "INFO") == 0) return LEVEL_INFO;
    if (strcasecmp(str, "WARN") == 0) return LEVEL_WARN;
    if (strcasecmp(str, "ERROR") == 0) return LEVEL_ERROR;
    if (strcasecmp(str, "FATAL") == 0) return LEVEL_ERROR;
    return LEVEL_INFO;
}

static inline const char *logger_level_str(log_level_t level) {
    switch (level) {
        case LEVEL_DEBUG: return "DEBUG";
        case LEVEL_INFO:  return "INFO";
        case LEVEL_WARN:  return "WARN";
        case LEVEL_ERROR: return "ERROR";
        default:          return "INFO";
    }
}

// Simple stdout logger
#define log_info(tag, fmt, ...)  printf("[INFO]  [%-15s] " fmt "\n", tag, ##__VA_ARGS__)
#define log_warn(tag, fmt, ...)  printf("[WARN]  [%-15s] " fmt "\n", tag, ##__VA_ARGS__)
#define log_error(tag, fmt, ...) printf("[ERROR] [%-15s] " fmt "\n", tag, ##__VA_ARGS__)
#define log_debug(tag, fmt, ...) printf("[DEBUG] [%-15s] " fmt "\n", tag, ##__VA_ARGS__)

#endif // LOGGER_H
