#ifndef _DATABASE_H_
#define _DATABASE_H_

#include <sqlite3.h>

extern sqlite3 *db;
int db_exec (const char *query);
int open_db (const char db_path[static 1]);
void close_db ();
int backup_db (const char src[MAX_PATH_LEN], const char dest[MAX_PATH_LEN]);

#endif

