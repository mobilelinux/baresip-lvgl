#ifndef HISTORY_MANAGER_H
#define HISTORY_MANAGER_H

#include <stdbool.h>

typedef enum {
  CALL_TYPE_INCOMING,
  CALL_TYPE_OUTGOING,
  CALL_TYPE_MISSED
} call_type_t;

typedef struct {
  char name[64];
  char number[64];
  call_type_t type;
  char time[64];         // Simplified time string for now
  long timestamp;        // Unix timestamp
  char account_aor[128]; // Account AOR used for the call
} call_log_entry_t;

// Initialize history manager
void history_manager_init(void);

// Get number of history entries
int history_get_count(void);

// Get history entry at index
const call_log_entry_t *history_get_at(int index);

// Add a new history entry
int history_add(const char *name, const char *number, call_type_t type,
                const char *account_aor);

// Clear all history
void history_clear(void);

// Load history from file
int history_load(void);

// Save history to file
int history_save(void);

#endif // HISTORY_MANAGER_H
