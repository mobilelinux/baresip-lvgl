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

// Helper to execute SQL via API
static int execute_sql(const char *sql) {
    sqlite3 *db = db_get_handle();
    if (!db) return -1;
    char *errmsg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        log_error("HistoryManager", "SQL Error: %s in %s", errmsg, sql);
        sqlite3_free(errmsg);
        return -1;
    }
    return 0;
}

static bool g_history_initialized = false;

void history_manager_init(void) {
  if (g_history_initialized) return;
  
  printf("HistoryManager: Initializing (API Mode)...\n");
  // db_init(); // Called by BaresipManager

  // Load history
  history_load();
  g_history_initialized = true;


}

int history_get_count(void) { return g_history_count; }

const call_log_entry_t *history_get_at(int index) {
  if (index < 0 || index >= g_history_count) {
    return NULL;
  }
  return &g_history[index];
}

int history_add(const char *name, const char *number, call_type_t type,
                const char *account_aor) {
  sqlite3 *db = db_get_handle();
  if (!db) {
      log_error("HistoryManager", "DB Handle is NULL!");
      return -1;
  }

  long timestamp = (long)time(NULL);
  
  const char *sql = "INSERT INTO call_log (name, number, type, timestamp, account_aor) VALUES (?, ?, ?, ?, ?);";
  sqlite3_stmt *stmt = NULL;
  
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
      log_error("HistoryManager", "Prepare failed: %d, Msg: %s", rc, sqlite3_errmsg(db));
      return -1;
  }
  if (!stmt) {
       log_error("HistoryManager", "Statement is NULL after prepare!");
       return -1;
  }

  sqlite3_bind_text(stmt, 1, name ? name : "", -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, number ? number : "", -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 3, type);
  sqlite3_bind_int64(stmt, 4, timestamp);
  sqlite3_bind_text(stmt, 5, account_aor ? account_aor : "", -1, SQLITE_STATIC);

  if (sqlite3_step(stmt) != SQLITE_DONE) {
      log_error("HistoryManager", "Add step failed: %s", sqlite3_errmsg(db));
  }
  
  sqlite3_finalize(stmt);
  
  printf("HistoryManager: Added log for %s\n", number);
  history_load();
  return 0;
}

void history_clear(void) {
  execute_sql("DELETE FROM call_log;");
  history_load();
}

void history_remove(int index) {
  if (index < 0 || index >= g_history_count)
    return;

  call_log_entry_t *e = &g_history[index];
  sqlite3 *db = db_get_handle();
  if (!db) return;

  const char *sql = "DELETE FROM call_log WHERE timestamp=? AND number=?;";
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return;

  sqlite3_bind_int64(stmt, 1, e->timestamp);
  sqlite3_bind_text(stmt, 2, e->number, -1, SQLITE_STATIC);

  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  history_load();
}

int history_load(void) {
    g_history_count = 0;
    sqlite3 *db = db_get_handle();
    if (!db) return 0;

    const char *sql = "SELECT name, number, type, timestamp, account_aor FROM call_log ORDER BY timestamp DESC LIMIT 100;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_error("HistoryManager", "Load failed: %s", sqlite3_errmsg(db));
        return 0;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW && g_history_count < MAX_HISTORY) {
        call_log_entry_t *e = &g_history[g_history_count];
        memset(e, 0, sizeof(call_log_entry_t));

        // Name
        const char *txt = (const char *)sqlite3_column_text(stmt, 0);
        if (txt) strncpy(e->name, txt, sizeof(e->name)-1);

        // Number
        txt = (const char *)sqlite3_column_text(stmt, 1);
        if (txt) strncpy(e->number, txt, sizeof(e->number)-1);

        // Type
        e->type = (call_type_t)sqlite3_column_int(stmt, 2);

        // Timestamp
        e->timestamp = (long)sqlite3_column_int64(stmt, 3);

        // AOR
        txt = (const char *)sqlite3_column_text(stmt, 4);
        if (txt) strncpy(e->account_aor, txt, sizeof(e->account_aor)-1);

        // Format Time
        struct tm *tm_info = localtime(&e->timestamp);
        if (tm_info) {
            strftime(e->time, sizeof(e->time), "%Y-%m-%d %H:%M", tm_info);
        } else {
            strcpy(e->time, "Invalid");
        }

        g_history_count++;
    }

    sqlite3_finalize(stmt);
    printf("HistoryManager: Loaded %d history entries via API\n", g_history_count);
    return g_history_count;
}

int history_save(void) {
  // No-op
  return 0;
}

void history_delete_mask(const bool *selection, int count) {
    if (!selection || count <= 0) return;
    
    sqlite3 *db = db_get_handle();
    if (!db) return;
    
    const char *sql = "DELETE FROM call_log WHERE timestamp=? AND number=?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return;
    
    int deleted = 0;
    // Note: g_history_count is static global in this file
    extern const call_log_entry_t *history_get_at(int index); // Or access g_history direct since we are inside
    // accessing g_history direct:
    // static call_log_entry_t g_history[MAX_HISTORY];
    
    for (int i=0; i<count && i < MAX_HISTORY; i++) {
        if (selection[i]) {
            // We read from internal array which reflects current DB state (before deletions)
            // Since we delete by ID/Timestamp, order doesn't matter provided g_history hasn't changed.
            call_log_entry_t *e = &g_history[i]; 
            sqlite3_bind_int64(stmt, 1, e->timestamp);
            sqlite3_bind_text(stmt, 2, e->number, -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
            deleted++;
        }
    }
    sqlite3_finalize(stmt);
    
    if (deleted > 0) {
        history_load();
    }
}
