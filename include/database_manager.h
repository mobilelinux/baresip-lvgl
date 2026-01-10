#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <sqlite3.h>

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    int id;
    char name[128];
    char number[128];
    bool is_favorite;
} db_contact_t;

typedef struct {
    char peer_uri[256];
    char content[1024];
    long timestamp;
    int direction; // 0=Incoming, 1=Outgoing
} chat_message_t;

// Initialize database (open connection, create tables)
int db_init(void);

// Close database connection
void db_close(void);

// Get SQLite handle
sqlite3 *db_get_handle(void);

int db_contact_find(const char *number, char *name_out, size_t size);
int db_get_contacts(db_contact_t *contacts, int max_count);
int db_get_favorite_contacts(db_contact_t *contacts, int max_count);

int db_chat_add(const char *peer_uri, int direction, const char *content);
int db_chat_get_threads(chat_message_t *threads, int max_count);
int db_chat_get_history(const char *peer_uri, chat_message_t *messages, int max_count);
int db_chat_delete_thread(const char *peer_uri);
int db_chat_bump_thread(const char *peer_uri);

#endif // DATABASE_MANAGER_H
