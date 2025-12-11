#include "database_manager.h"
#include "config_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static sqlite3 *g_db = NULL;

int db_init(void) {
  if (g_db)
    return 0; // Already initialized

  char path[256];
  config_manager_init(); // Ensure config dir exists
  config_get_dir_path(path, sizeof(path));
  strcat(path, "/baresip.db");

  int rc = sqlite3_open(path, &g_db);
  if (rc) {
    printf("[DatabaseManager] Can't open database: %s\n", sqlite3_errmsg(g_db));
    return -1;
  }

  printf("[DatabaseManager] Opened database at %s\n", path);

  // Create Contacts table
  const char *sql_contacts = "CREATE TABLE IF NOT EXISTS contacts ("
                             "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                             "name TEXT NOT NULL, "
                             "number TEXT NOT NULL);";

  char *errmsg = NULL;
  rc = sqlite3_exec(g_db, sql_contacts, 0, 0, &errmsg);
  if (rc != SQLITE_OK) {
    printf("[DatabaseManager] SQL error (create contacts): %s\n", errmsg);
    sqlite3_free(errmsg);
    return -1;
  }

  // Try to add is_favorite column (ignore error if exists)
  // This is a simple migration strategy
  const char *sql_alter =
      "ALTER TABLE contacts ADD COLUMN is_favorite INTEGER DEFAULT 0;";
  sqlite3_exec(g_db, sql_alter, 0, 0, NULL);

  // Create Call Log table
  const char *sql_history = "CREATE TABLE IF NOT EXISTS call_log ("
                            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                            "name TEXT, "
                            "number TEXT, "
                            "type INTEGER, "
                            "timestamp INTEGER);";

  rc = sqlite3_exec(g_db, sql_history, 0, 0, &errmsg);
  if (rc != SQLITE_OK) {
    printf("[DatabaseManager] SQL error (create call_log): %s\n", errmsg);
    sqlite3_free(errmsg);
    return -1;
  }

  return 0;
}

void db_close(void) {
  if (g_db) {
    sqlite3_close(g_db);
    g_db = NULL;
    printf("[DatabaseManager] Closed database\n");
  }
}

sqlite3 *db_get_handle(void) { return g_db; }
