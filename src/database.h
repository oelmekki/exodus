#ifndef _DATABASE_H_
#define _DATABASE_H_

#include <sqlite3.h>
#include "main.h"

extern sqlite3 *db;
int db_exec (const char *query);
int open_db (const char db_path[MAX_PATH_LEN], const char init_path[MAX_PATH_LEN]);
void close_db ();
int reopen_db (const char db_file[MAX_PATH_LEN], const char init_path[MAX_PATH_LEN]);
int backup_db (const char src[MAX_PATH_LEN], const char dest[MAX_PATH_LEN]);

#endif

