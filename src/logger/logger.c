#include "logger.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static log_level_t current_level = LOG_LEVEL_INFO;

static const char *level_strings[] = {"TRACE", "DEBUG", "INFO",
                                      "WARN",  "ERROR", "FATAL"};

static const char *level_colors[] = {
    "\x1b[90m", // TRACE - Gray
    "\x1b[36m", // DEBUG - Cyan
    "\x1b[32m", // INFO  - Green
    "\x1b[33m", // WARN  - Yellow
    "\x1b[31m", // ERROR - Red
    "\x1b[35m"  // FATAL - Magenta
};

static const char *reset_color = "\x1b[0m";

void logger_init(log_level_t initial_level) { current_level = initial_level; }

void logger_set_level(log_level_t level) {
  if (level < LOG_LEVEL_COUNT) {
    current_level = level;
  }
}

log_level_t logger_get_level(void) { return current_level; }

void logger_log(log_level_t level, const char *module, const char *fmt, ...) {
  if (level < current_level) {
    return;
  }

  time_t now;
  time(&now);
  struct tm *local = localtime(&now);
  char time_str[20];
  strftime(time_str, sizeof(time_str), "%H:%M:%S", local);

  // Color start
  const char *color = (level < LOG_LEVEL_COUNT) ? level_colors[level] : "";
  const char *lvl_str =
      (level < LOG_LEVEL_COUNT) ? level_strings[level] : "UNKNOWN";

  // Print Header: [Time] [Level] [Module]
  printf("%s[%s] [%-5s] ", color, time_str, lvl_str);
  if (module) {
    printf("[%s] ", module);
  }

  // Print Message
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);

  // Color reset and newline
  printf("%s\n", reset_color);
}

log_level_t logger_parse_level(const char *level_str) {
  if (!level_str)
    return LOG_LEVEL_INFO;

  for (int i = 0; i < LOG_LEVEL_COUNT; i++) {
    if (strcasecmp(level_str, level_strings[i]) == 0) {
      return (log_level_t)i;
    }
  }
  return LOG_LEVEL_INFO;
}

const char *logger_level_str(log_level_t level) {
  if (level < LOG_LEVEL_COUNT) {
    return level_strings[level];
  }
  return "UNKNOWN";
}
