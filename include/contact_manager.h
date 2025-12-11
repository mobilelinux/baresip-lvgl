#ifndef CONTACT_MANAGER_H
#define CONTACT_MANAGER_H

#include <stdbool.h>

#define CONFIG_DIR ".baresip-lvgl"

typedef struct {
  int id;
  char name[64];
  char number[64];
  bool is_favorite;
} contact_t;

// Initialize contact manager
void cm_init(void);

// Get number of contacts
int cm_get_count(void);

// Get contact at index
const contact_t *cm_get_at(int index);

// Add a new contact
int cm_add(const char *name, const char *number, bool is_favorite);

// Update a contact
int cm_update(int id, const char *name, const char *number, bool is_favorite);

// Remove a contact
int cm_remove(int id);

// Load contacts from file
int cm_load(void);

// Save contacts to file
int cm_save(void);

#endif // CONTACT_MANAGER_H
