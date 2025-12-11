#include "contact_manager.h"
#include "database_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CONTACTS 100

static contact_t g_contacts[MAX_CONTACTS];
static int g_contact_count = 0;

void cm_init(void) {
  db_init();
  cm_load();
}

int cm_get_count(void) { return g_contact_count; }

const contact_t *cm_get_at(int index) {
  if (index < 0 || index >= g_contact_count) {
    return NULL;
  }
  return &g_contacts[index];
}

int cm_add(const char *name, const char *number, bool is_favorite) {
  if (g_contact_count >= MAX_CONTACTS) {
    return -1;
  }
  if (!name || !number) {
    return -1;
  }

  sqlite3 *db = db_get_handle();
  if (!db)
    return -1;

  char *sql = sqlite3_mprintf("INSERT INTO contacts (name, number, "
                              "is_favorite) VALUES ('%q', '%q', %d);",
                              name, number, is_favorite ? 1 : 0);
  char *errmsg = NULL;
  int rc = sqlite3_exec(db, sql, 0, 0, &errmsg);
  sqlite3_free(sql);

  if (rc != SQLITE_OK) {
    printf("[ContactManager] Failed to add contact: %s\n", errmsg);
    sqlite3_free(errmsg);
    return -1;
  }

  // Update internal cache
  cm_load();
  return 0;
}

int cm_update(int id, const char *name, const char *number, bool is_favorite) {
  sqlite3 *db = db_get_handle();
  if (!db)
    return -1;

  char *sql = sqlite3_mprintf(
      "UPDATE contacts SET name='%q', number='%q', is_favorite=%d WHERE id=%d;",
      name, number, is_favorite ? 1 : 0, id);
  char *errmsg = NULL;
  int rc = sqlite3_exec(db, sql, 0, 0, &errmsg);
  sqlite3_free(sql);

  if (rc != SQLITE_OK) {
    printf("[ContactManager] Failed to update contact: %s\n", errmsg);
    sqlite3_free(errmsg);
    return -1;
  }

  // Update internal cache
  cm_load();
  return 0;
}

int cm_remove(int id) {
  sqlite3 *db = db_get_handle();
  if (!db)
    return -1;

  char *sql = sqlite3_mprintf("DELETE FROM contacts WHERE id=%d;", id);
  char *errmsg = NULL;
  int rc = sqlite3_exec(db, sql, 0, 0, &errmsg);
  sqlite3_free(sql);

  if (rc != SQLITE_OK) {
    printf("[ContactManager] Failed to remove contact: %s\n", errmsg);
    sqlite3_free(errmsg);
    return -1;
  }

  cm_load();
  return 0;
}

int cm_load(void) {
  sqlite3 *db = db_get_handle();
  if (!db)
    return 0;

  g_contact_count = 0;

  const char *sql =
      "SELECT id, name, number, is_favorite FROM contacts ORDER BY name;";
  sqlite3_stmt *stmt;

  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    printf("[ContactManager] Failed to prepare select: %s\n",
           sqlite3_errmsg(db));
    return 0;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW && g_contact_count < MAX_CONTACTS) {
    int id = sqlite3_column_int(stmt, 0);
    const char *name = (const char *)sqlite3_column_text(stmt, 1);
    const char *number = (const char *)sqlite3_column_text(stmt, 2);
    int is_favorite = sqlite3_column_int(stmt, 3);

    g_contacts[g_contact_count].id = id;
    g_contacts[g_contact_count].is_favorite = (is_favorite != 0);
    strncpy(g_contacts[g_contact_count].name, name ? name : "",
            sizeof(g_contacts[0].name) - 1);
    strncpy(g_contacts[g_contact_count].number, number ? number : "",
            sizeof(g_contacts[0].number) - 1);
    g_contact_count++;
  }

  sqlite3_finalize(stmt);
  printf("[ContactManager] Loaded %d contacts\n", g_contact_count);
  return g_contact_count;
}

int cm_save(void) {
  // No-op, saving happens on add/remove
  return 0;
}
