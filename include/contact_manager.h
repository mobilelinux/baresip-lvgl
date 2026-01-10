#ifndef CONTACT_MANAGER_H
#define CONTACT_MANAGER_H

#include <stdbool.h>

typedef struct {
    int id;
    char name[64];
    char number[128];
    bool is_favorite;
} contact_t;

int cm_init(void);
int cm_get_all(contact_t *contacts, int max_count);
int cm_get_favorites(contact_t *contacts, int max_count);
int cm_add(const char *name, const char *number, bool is_favorite);
int cm_update(int id, const char *name, const char *number, bool is_favorite);
int cm_delete(int id);
#define cm_remove cm_delete
int cm_set_favorite(int id, bool favorite);

// Stateful API
int cm_load(void);
int cm_get_count(void);
const contact_t *cm_get_at(int index);

#endif // CONTACT_MANAGER_H
