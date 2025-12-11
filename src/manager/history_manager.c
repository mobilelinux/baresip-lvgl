#include "history_manager.h"
#include "database_manager.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_HISTORY 100

static call_log_entry_t g_history[MAX_HISTORY];
static int g_history_count = 0;

void history_manager_init(void) {
  db_init();
  history_load();
}

int history_get_count(void) { return g_history_count; }

const call_log_entry_t *history_get_at(int index) {
  if (index < 0 || index >= g_history_count) {
    return NULL;
  }
  return &g_history[index];
}

int history_add(const char *name, const char *number, call_type_t type) {
  sqlite3 *db = db_get_handle();
  if (!db)
    return -1;

  long timestamp = (long)time(NULL);

  char *sql =
      sqlite3_mprintf("INSERT INTO call_log (name, number, type, timestamp) "
                      "VALUES ('%q', '%q', %d, %ld);",
                      name ? name : "", number ? number : "", type, timestamp);

  char *errmsg = NULL;
  int rc = sqlite3_exec(db, sql, 0, 0, &errmsg);
  sqlite3_free(sql);

  if (rc != SQLITE_OK) {
    log_warn("HistoryManager", "Failed to add log: %s", errmsg);
    sqlite3_free(errmsg);
    return -1;
  }

  history_load();
  return 0;
}

void history_clear(void) {
  sqlite3 *db = db_get_handle();
  if (!db)
    return;

  char *errmsg = NULL;
  int rc = sqlite3_exec(db, "DELETE FROM call_log;", 0, 0, &errmsg);
  if (rc != SQLITE_OK) {
    log_warn("HistoryManager", "Failed to clear history: %s", errmsg);
    sqlite3_free(errmsg);
  }

  history_load();
}

int history_load(void) {
  sqlite3 *db = db_get_handle();
  if (!db)
    return 0;

  g_history_count = 0;

  const char *sql = "SELECT name, number, type, timestamp FROM call_log ORDER "
                    "BY timestamp DESC LIMIT 100;";
  sqlite3_stmt *stmt;

  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    log_error("HistoryManager", "Failed to prepare select: %s",
              sqlite3_errmsg(db));
    return 0;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW && g_history_count < MAX_HISTORY) {
    call_log_entry_t *e = &g_history[g_history_count];
    const char *name = (const char *)sqlite3_column_text(stmt, 0);
    const char *number = (const char *)sqlite3_column_text(stmt, 1);
    int type = sqlite3_column_int(stmt, 2);
    long timestamp = (long)sqlite3_column_int64(stmt, 3);

    strncpy(e->name, name ? name : "", sizeof(e->name) - 1);
    strncpy(e->number, number ? number : "", sizeof(e->number) - 1);
    e->type = (call_type_t)type;
    e->timestamp = timestamp;

    // Format time
    struct tm *tm_info = localtime(&e->timestamp);
    strftime(e->time, sizeof(e->time), "%Y-%m-%d %H:%M", tm_info);

    g_history_count++;
  }

  sqlite3_finalize(stmt);
  log_info("HistoryManager", "Loaded %d history entries", g_history_count);
  return g_history_count;
}

int history_save(void) {
  // No-op
  return 0;
}
