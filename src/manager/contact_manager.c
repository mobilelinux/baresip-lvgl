#include "contact_manager.h"
#include "database_manager.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <sqlite3.h>

#define MAX_CONTACTS 100
static contact_t g_contacts[MAX_CONTACTS];
static int g_count = 0;

int cm_load(void) {
    g_count = db_get_contacts((db_contact_t*)g_contacts, MAX_CONTACTS);
    return g_count;
}

int cm_init(void) {
    int ret = db_init();
    cm_load();
    return ret;
}

int cm_get_count(void) {
    return g_count;
}

const contact_t *cm_get_at(int index) {
    if (index < 0 || index >= g_count) return NULL;
    return &g_contacts[index];
}

int cm_get_all(contact_t *contacts, int max_count) {
    // db_contact_t structure matches contact_t
    return db_get_contacts((db_contact_t*)contacts, max_count);
}

int cm_get_favorites(contact_t *contacts, int max_count) {
    return db_get_favorite_contacts((db_contact_t*)contacts, max_count);
}

int cm_add(const char *name, const char *number, bool is_favorite) {
    sqlite3 *db = db_get_handle();
    if (!db) return -1;

    const char *sql = "INSERT INTO contacts (name, number, is_favorite) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_error("ContactManager", "Add prepare failed: %s", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, name ? name : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, number ? number : "", -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, is_favorite ? 1 : 0);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        log_error("ContactManager", "Add step failed: %s", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }
    
    sqlite3_finalize(stmt);
    cm_load();
    return 0;
}

int cm_update(int id, const char *name, const char *number, bool is_favorite) {
    sqlite3 *db = db_get_handle();
    if (!db) return -1;

    const char *sql = "UPDATE contacts SET name=?, number=?, is_favorite=? WHERE id=?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_error("ContactManager", "Update prepare failed: %s", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, name ? name : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, number ? number : "", -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, is_favorite ? 1 : 0);
    sqlite3_bind_int(stmt, 4, id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        log_error("ContactManager", "Update step failed: %s", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }

    sqlite3_finalize(stmt);
    cm_load();
    return 0;
}

int cm_delete(int id) {
    sqlite3 *db = db_get_handle();
    if (!db) return -1;

    const char *sql = "DELETE FROM contacts WHERE id=?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_error("ContactManager", "Delete prepare failed: %s", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        log_error("ContactManager", "Delete step failed: %s", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }

    sqlite3_finalize(stmt);
    cm_load();
    return 0;
}

int cm_set_favorite(int id, bool favorite) {
    sqlite3 *db = db_get_handle();
    if (!db) return -1;

    const char *sql = "UPDATE contacts SET is_favorite=? WHERE id=?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_int(stmt, 1, favorite ? 1 : 0);
    sqlite3_bind_int(stmt, 2, id);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    cm_load();
    return 0;
}
