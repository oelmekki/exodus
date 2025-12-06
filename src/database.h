#ifndef _DATABASE_H_
#define _DATABASE_H_

#include <sqlite3.h>

extern sqlite3 *db;
int open_db (const char db_path[static 1]);
void close_db ();

#endif

