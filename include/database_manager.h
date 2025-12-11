#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <sqlite3.h>

// Initialize database (open connection, create tables)
int db_init(void);

// Close database connection
void db_close(void);

// Get SQLite handle
sqlite3 *db_get_handle(void);

#endif // DATABASE_MANAGER_H
