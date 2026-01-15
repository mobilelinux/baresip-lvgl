#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <sqlite3.h>
#include <stddef.h>

int db_init(void);
void db_close(void);
sqlite3 *db_get_handle(void);

// Lookup contact name by number/URI
// Returns 0 on success (found), -1 on not found/error
int db_contact_find(const char *number, char *name_out, size_t size);

#include <stdbool.h>

typedef struct {
    int id;
    char name[64];
    char number[128];
    bool is_favorite;
} db_contact_t;

// Get all contacts using C API (limit to max_count)
int db_get_contacts(db_contact_t *contacts, int max_count);
int db_get_favorite_contacts(db_contact_t *contacts, int max_count);

// Chat
typedef struct {
    int id;
    char peer_uri[128];
    int direction; // 0=In, 1=Out
    char content[1024];
    long timestamp;
    int status;
} chat_message_t;

int db_chat_add(const char *peer_uri, int direction, const char *content);
int db_chat_get_threads(chat_message_t *threads, int max_count);
int db_chat_get_history(const char *peer_uri, chat_message_t *messages, int max_count);
int db_chat_delete_thread(const char *peer_uri);
int db_chat_bump_thread(const char *peer_uri);

// Notifications
int db_get_unread_comp_count(int *missed_calls, int *unread_msgs);
int db_mark_missed_calls_read(void);
int db_mark_chat_read(const char *peer_uri);

#endif
