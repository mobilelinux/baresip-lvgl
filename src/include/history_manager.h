#ifndef HISTORY_MANAGER_H
#define HISTORY_MANAGER_H

#include <stdbool.h>

typedef enum {
    CALL_TYPE_INCOMING = 0,
    CALL_TYPE_OUTGOING,
    CALL_TYPE_MISSED,
    CALL_TYPE_REJECTED
} call_type_t;

typedef struct {
    char name[64];
    char number[64];
    call_type_t type;
    long timestamp;
    char time[32]; // Formatted time
    char account_aor[128];
} call_log_entry_t;

void history_manager_init(void);
int history_get_count(void);
const call_log_entry_t *history_get_at(int index);
int history_add(const char *name, const char *number, call_type_t type, const char *account_aor);
void history_clear(void);
void history_remove(int index);
int history_load(void);
int history_save(void);
void history_delete_mask(const bool *selection, int count);

#endif // HISTORY_MANAGER_H
