#include "database_manager.h"
#include "config_manager.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static pthread_mutex_t g_db_mutex = PTHREAD_MUTEX_INITIALIZER;

static sqlite3 *g_db = NULL;

int db_init(void) {
  if (g_db)
    return 0; // Already initialized

  // Ensure mutex is active (static init is usually enough but good practice)
  // pthread_mutex_init(&g_db_mutex, NULL); 

  char path[256];
  config_manager_init(); // Ensure config dir exists
  config_get_dir_path(path, sizeof(path));
  strcat(path, "/baresip.db");

  int rc = sqlite3_open(path, &g_db);
  if (rc) {
    printf("DatabaseManager: ERROR: Can't open database: %s\n", sqlite3_errmsg(g_db));
    // log_warn("DatabaseManager", "Can't open database: %s", sqlite3_errmsg(g_db));
    return -1;
  }
  printf("DatabaseManager: SUCCESS: Opened database at %s\n", path);
  
  // Create Contacts table
  char *errmsg = NULL;
  rc = sqlite3_exec(g_db, "CREATE TABLE IF NOT EXISTS contacts (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, number TEXT NOT NULL);", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK) {
      log_error("DatabaseManager", "Failed to create contacts table: %s", errmsg);
      sqlite3_free(errmsg);
  }

  // Add is_favorite
  sqlite3_exec(g_db, "ALTER TABLE contacts ADD COLUMN is_favorite INTEGER DEFAULT 0;", NULL, NULL, NULL);

  // Create Call Log
  rc = sqlite3_exec(g_db, "CREATE TABLE IF NOT EXISTS call_log (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, number TEXT, type INTEGER, timestamp INTEGER, account_aor TEXT, viewed INTEGER DEFAULT 0);", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK) {
      log_error("DatabaseManager", "Failed to create call_log table: %s", errmsg);
      sqlite3_free(errmsg);
  }
  
  // Migration: Add viewed column if missing
  sqlite3_exec(g_db, "ALTER TABLE call_log ADD COLUMN viewed INTEGER DEFAULT 0;", NULL, NULL, NULL);

  // Migration: Fix number
  sqlite3_exec(g_db, "UPDATE contacts SET number = 'sip:808086@fanvil.com' WHERE number = 'sip:8080866@fanvil.com';", NULL, NULL, NULL);

  // Create Chat Messages Table
  rc = sqlite3_exec(g_db, "CREATE TABLE IF NOT EXISTS chat_messages ("
          "id INTEGER PRIMARY KEY AUTOINCREMENT, "
          "peer_uri TEXT NOT NULL, "
          "direction INTEGER, " /* 0=Incoming, 1=Outgoing */
          "content TEXT, "
          "timestamp INTEGER, "
          "status INTEGER DEFAULT 0);", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK) {
        log_error("DatabaseManager", "Failed to create chat_messages table: %s", errmsg);
        sqlite3_free(errmsg);
  }

  return 0;
}

void db_close(void) {
  pthread_mutex_lock(&g_db_mutex);
  if (g_db) {
    sqlite3_close(g_db);
    g_db = NULL;
    log_info("DatabaseManager", "Closed database");
  }
  pthread_mutex_unlock(&g_db_mutex);
}

sqlite3 *db_get_handle(void) { return g_db; }

int db_contact_find(const char *number, char *name_out, size_t size) {
  pthread_mutex_lock(&g_db_mutex);
  if (!g_db || !number || !name_out || size == 0) {
      pthread_mutex_unlock(&g_db_mutex);
      return -1;
  }
  const char *sql = "SELECT name FROM contacts WHERE number = ?;";
  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
      pthread_mutex_unlock(&g_db_mutex);
      return -1;
  }
  
  sqlite3_bind_text(stmt, 1, number, -1, SQLITE_STATIC);
  int result = -1;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
      const char *name = (const char *)sqlite3_column_text(stmt, 0);
      if (name) {
          strncpy(name_out, name, size - 1);
          name_out[size - 1] = '\0';
          result = 0;
      }
  }
  sqlite3_finalize(stmt);
  pthread_mutex_unlock(&g_db_mutex);
  return result;
}

int db_get_contacts(db_contact_t *contacts, int max_count) {
    pthread_mutex_lock(&g_db_mutex);
    if (!g_db || !contacts || max_count <= 0) {
        pthread_mutex_unlock(&g_db_mutex);
        return 0;
    }
    
    const char *sql = "SELECT id, name, number, is_favorite FROM contacts LIMIT ?;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_error("DatabaseManager", "Failed to prepare get_contacts: %s", sqlite3_errmsg(g_db));
        return 0;
    }
    
    sqlite3_bind_int(stmt, 1, max_count);
    
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_count) {
        db_contact_t *c = &contacts[count];
        c->id = sqlite3_column_int(stmt, 0);
        
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        if (name) strncpy(c->name, name, sizeof(c->name)-1);
        else c->name[0] = 0;
        
        const char *num = (const char *)sqlite3_column_text(stmt, 2);
        if (num) strncpy(c->number, num, sizeof(c->number)-1);
        else c->number[0] = 0;
        
        c->is_favorite = sqlite3_column_int(stmt, 3) != 0;

        count++;
    }
    
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    return count;
}

int db_get_favorite_contacts(db_contact_t *contacts, int max_count) {
    pthread_mutex_lock(&g_db_mutex);
    if (!g_db || !contacts || max_count <= 0) {
        pthread_mutex_unlock(&g_db_mutex);
        return 0;
    }
    
    // Filter by is_favorite = 1
    const char *sql = "SELECT id, name, number, is_favorite FROM contacts WHERE is_favorite=1 LIMIT ?;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_error("DatabaseManager", "Failed to prepare get_favorites: %s", sqlite3_errmsg(g_db));
        pthread_mutex_unlock(&g_db_mutex);
        return 0;
    }
    
    sqlite3_bind_int(stmt, 1, max_count);
    
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_count) {
        db_contact_t *c = &contacts[count];
        c->id = sqlite3_column_int(stmt, 0);
        
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        if (name) strncpy(c->name, name, sizeof(c->name)-1);
        else c->name[0] = 0;
        
        const char *num = (const char *)sqlite3_column_text(stmt, 2);
        if (num) strncpy(c->number, num, sizeof(c->number)-1);
        else c->number[0] = 0;
        
        c->is_favorite = sqlite3_column_int(stmt, 3) != 0;

        count++;
    }
    
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    return count;
}

#include <time.h>
int db_chat_add(const char *peer_uri, int direction, const char *content) {
    if(!peer_uri || !content) {
        printf("DatabaseManager: db_chat_add: NULL input\n");
        return -1;
    }
    printf("DEBUG_STEP: db_chat_add: Locking mutex...\n"); fflush(stdout);
    pthread_mutex_lock(&g_db_mutex);
    printf("DEBUG_STEP: db_chat_add: Mutex locked. Checking DB...\n"); fflush(stdout);
    if (!g_db) {
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }
    
    // Clean URI logic (consistent with Call Log)
    // Increased buffer size to 256 to avoid truncation
    char clean_uri[256];
    strncpy(clean_uri, peer_uri, sizeof(clean_uri)-1);
    clean_uri[sizeof(clean_uri)-1] = '\0';
    char *p = strchr(clean_uri, ';'); if (p) *p = '\0';
    char *s = clean_uri; 
    if(strncmp(s, "sip:", 4)==0) s+=4;
    else if(strncmp(s, "sips:", 5)==0) s+=5;
    
    printf("DEBUG_STEP: db_chat_add: Saving '%s' -> '%s' (len=%d)\n", peer_uri, s, (int)strlen(content));

    const char *sql = "INSERT INTO chat_messages (peer_uri, direction, content, timestamp, status) VALUES (?, ?, ?, ?, 0);";
    sqlite3_stmt *stmt;
    printf("DEBUG_STEP: db_chat_add: Preparing statement...\n"); fflush(stdout);
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_error("DatabaseManager", "Failed to prepare chat insert: %s", sqlite3_errmsg(g_db));
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }
    
    printf("DEBUG_STEP: db_chat_add: Binding values (s=%p, content=%p)...\n", s, content); fflush(stdout);
    sqlite3_bind_text(stmt, 1, s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, direction);
    sqlite3_bind_text(stmt, 3, content, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, (sqlite3_int64)time(NULL));
    
    printf("DEBUG_STEP: db_chat_add: Stepping...\n"); fflush(stdout);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
         log_error("DatabaseManager", "Failed to step chat insert: %s", sqlite3_errmsg(g_db));
    }
    printf("DEBUG_STEP: db_chat_add: Finalizing...\n"); fflush(stdout);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    printf("DEBUG_STEP: db_chat_add: DONE.\n"); fflush(stdout);
    return 0;
}

int db_chat_get_threads(chat_message_t *threads, int max_count) {
    pthread_mutex_lock(&g_db_mutex);
    if (!g_db || !threads || max_count <= 0) {
        pthread_mutex_unlock(&g_db_mutex);
        return 0;
    }
    
    printf("DEBUG_STEP: db_chat_get_threads: Getting max %d threads...\n", max_count);

    // Get last message for each unique peer
    const char *sql = "SELECT peer_uri, content, timestamp, direction FROM chat_messages "
                      "WHERE id IN (SELECT MAX(id) FROM chat_messages GROUP BY peer_uri) "
                      "ORDER BY timestamp DESC LIMIT ?;";
                     
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        printf("DEBUG_STEP: db_chat_get_threads: Prepare failed: %s\n", sqlite3_errmsg(g_db));
        pthread_mutex_unlock(&g_db_mutex);
        return 0;
    }
    
    sqlite3_bind_int(stmt, 1, max_count);
    
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_count) {
        chat_message_t *m = &threads[count];
        
        const char *uri = (const char *)sqlite3_column_text(stmt, 0);
        if (uri) strncpy(m->peer_uri, uri, sizeof(m->peer_uri)-1);
        
        const char *txt = (const char *)sqlite3_column_text(stmt, 1);
        if (txt) strncpy(m->content, txt, sizeof(m->content)-1);
        
        m->timestamp = (long)sqlite3_column_int64(stmt, 2);
        m->direction = sqlite3_column_int(stmt, 3);
        
        printf("DEBUG_STEP: db_chat_get_threads: Found thread [%d]: %s (%s)\n", count, m->peer_uri, m->content);
        
        count++;
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    printf("DEBUG_STEP: db_chat_get_threads: Returning %d threads.\n", count);
    return count;
}

int db_chat_get_history(const char *peer_uri, chat_message_t *messages, int max_count) {
    pthread_mutex_lock(&g_db_mutex);
    if (!g_db || !peer_uri || !messages || max_count <= 0) {
        pthread_mutex_unlock(&g_db_mutex);
        return 0;
    }
    
    const char *sql = "SELECT direction, content, timestamp FROM chat_messages "
                      "WHERE peer_uri = ? ORDER BY id ASC LIMIT ?;";
    
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&g_db_mutex);
        return 0;
    }
    
    sqlite3_bind_text(stmt, 1, peer_uri, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, max_count);
    
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_count) {
        chat_message_t *m = &messages[count];
        
        strncpy(m->peer_uri, peer_uri, sizeof(m->peer_uri)-1);
        m->direction = sqlite3_column_int(stmt, 0);
        
        const char *txt = (const char *)sqlite3_column_text(stmt, 1);
        if (txt) strncpy(m->content, txt, sizeof(m->content)-1);
        
        m->timestamp = (long)sqlite3_column_int64(stmt, 2);
        
        count++;
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    return count;
}

int db_chat_delete_thread(const char *peer_uri) {
    pthread_mutex_lock(&g_db_mutex);
    if (!g_db || !peer_uri) {
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }
    
    // Clean URI logic if needed? Assuming peer_uri passed is clean from DB logic
    
    const char *sql = "DELETE FROM chat_messages WHERE peer_uri = ?;";
    
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
         log_error("DatabaseManager", "Failed to prepare chat delete: %s", sqlite3_errmsg(g_db));
         pthread_mutex_unlock(&g_db_mutex);
         return -1;
    }
    
    sqlite3_bind_text(stmt, 1, peer_uri, -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
         log_error("DatabaseManager", "Failed to execute chat delete: %s", sqlite3_errmsg(g_db));
         sqlite3_finalize(stmt);
         pthread_mutex_unlock(&g_db_mutex); // Added as per user's code edit
         return -1;
    }
    
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex); // Added as per instruction 3
    return 0;
}

int db_chat_bump_thread(const char *peer_uri) {
    if (!peer_uri) return -1;
    
    pthread_mutex_lock(&g_db_mutex);
    if (!g_db) {
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }
    
    // Update timestamp of the latest message for this peer to NOW
    const char *sql = "UPDATE chat_messages SET timestamp = ? WHERE id = (SELECT MAX(id) FROM chat_messages WHERE peer_uri = ?);";
    
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_error("DatabaseManager", "Failed to prepare chat bump: %s", sqlite3_errmsg(g_db));
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }
    
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)time(NULL));
    sqlite3_bind_text(stmt, 2, peer_uri, -1, SQLITE_STATIC);
    
    if (sqlite3_step(stmt) != SQLITE_DONE) {
         log_error("DatabaseManager", "Failed to execute chat bump: %s", sqlite3_errmsg(g_db));
    }
    
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    return 0;
}

int db_chat_get_unread_count(void) {
    pthread_mutex_lock(&g_db_mutex);
    if (!g_db) {
        pthread_mutex_unlock(&g_db_mutex);
        return 0;
    }
    
    // Count status=0 messages that are Incoming (direction=0)
    const char *sql = "SELECT COUNT(*) FROM chat_messages WHERE status = 0 AND direction = 0;";
    
    sqlite3_stmt *stmt;
    int count = 0;
    
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    
    pthread_mutex_unlock(&g_db_mutex);
    return count;
}

int db_chat_mark_read(const char *peer_uri) {
    if (!peer_uri) return -1;
    pthread_mutex_lock(&g_db_mutex);
    if (!g_db) {
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }
    
    const char *sql = "UPDATE chat_messages SET status = 1 WHERE peer_uri = ? AND direction = 0;";
    
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, peer_uri, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    pthread_mutex_unlock(&g_db_mutex);
    return 0;
}

int db_call_get_missed_count(void) {
    pthread_mutex_lock(&g_db_mutex);
    if (!g_db) {
        pthread_mutex_unlock(&g_db_mutex);
        return 0;
    }
    
    // Count type=2 (MISSED) and viewed=0
    const char *sql = "SELECT COUNT(*) FROM call_log WHERE type = 2 AND viewed = 0;";
    
    sqlite3_stmt *stmt;
    int count = 0;
    
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    
    pthread_mutex_unlock(&g_db_mutex);
    return count;
}

int db_call_mark_all_viewed(void) {
    pthread_mutex_lock(&g_db_mutex);
    if (!g_db) {
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }
    
    const char *sql = "UPDATE call_log SET viewed = 1 WHERE viewed = 0;";
    char *errmsg = NULL;
    int rc = sqlite3_exec(g_db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        log_error("DatabaseManager", "Failed to mark calls viewed: %s", errmsg);
        sqlite3_free(errmsg);
    }
    
    pthread_mutex_unlock(&g_db_mutex);
    return 0;
}
