#ifndef LOGGER_H
#define LOGGER_H

#include <stdarg.h>

/**
 * Log levels
 */
typedef enum {
  LOG_LEVEL_TRACE = 0,
  LOG_LEVEL_DEBUG,
  LOG_LEVEL_INFO,
  LOG_LEVEL_WARN,
  LOG_LEVEL_ERROR,
  LOG_LEVEL_FATAL,
  LOG_LEVEL_COUNT
} log_level_t;

/**
 * Initialize the logger
 *
 * @param initial_level The initial logging level
 */
void logger_init(log_level_t initial_level);

/**
 * Set the current logging level
 *
 * @param level The new logging level. Messages below this level will be
 * ignored.
 */
void logger_set_level(log_level_t level);

/**
 * Get the current logging level
 *
 * @return log_level_t The current logging level
 */
log_level_t logger_get_level(void);

/**
 * Log a message
 *
 * @param level Message level
 * @param module Module name (optional, can be NULL)
 * @param fmt Format string
 * @param ... Arguments
 */
void logger_log(log_level_t level, const char *module, const char *fmt, ...);

// Convenience macros
// Convenience macros
#if defined(NDEBUG) // Release Build
#define log_trace(module, ...) ((void)0)
#define log_debug(module, ...) ((void)0)
#define log_info(module, ...) ((void)0)
#define log_warn(module, ...) ((void)0)
#define log_error(module, ...) logger_log(LOG_LEVEL_ERROR, module, __VA_ARGS__)
#define log_fatal(module, ...) logger_log(LOG_LEVEL_FATAL, module, __VA_ARGS__)
#else // Debug Build
#define log_trace(module, ...) logger_log(LOG_LEVEL_TRACE, module, __VA_ARGS__)
#define log_debug(module, ...) logger_log(LOG_LEVEL_DEBUG, module, __VA_ARGS__)
#define log_info(module, ...) logger_log(LOG_LEVEL_INFO, module, __VA_ARGS__)
#define log_warn(module, ...) logger_log(LOG_LEVEL_WARN, module, __VA_ARGS__)
#define log_error(module, ...) logger_log(LOG_LEVEL_ERROR, module, __VA_ARGS__)
#define log_fatal(module, ...) logger_log(LOG_LEVEL_FATAL, module, __VA_ARGS__)
#endif

// Helper to convert string to level (for config loading)
log_level_t logger_parse_level(const char *level_str);

// Helper to convert level to string
const char *logger_level_str(log_level_t level);

#endif // LOGGER_H
