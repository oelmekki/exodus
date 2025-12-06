#include <sqlite3.h>
#include <stdio.h>

#include "main.h"

sqlite3 *db = NULL;

/*
 * Executes a simple query.
 *
 * This query should have no bind parameter and you don't get result rows.
 */
static int
db_exec (const char *query)
{
  int err = 0;
  int rc = 0;
  char *sql_err = NULL;

  rc = sqlite3_exec (db, query, NULL, NULL, &sql_err);
  if (rc != SQLITE_OK)
    {
      err = 1;
      fprintf(stderr, "database.c: db_exec() : SQL error: %s\n", sql_err);
      goto teardown;
    }

  teardown:
  if (sql_err) sqlite3_free (sql_err);
  return err;
}

/*
 * Open file from application database, where we put data in.
 *
 * TODO the pragmas here should come from the defaults set in config file.
 */
int
open_db (const char db_file[static 1])
{
  int err = 0;

  err = sqlite3_open (db_file, &db);
  if (err)
    {
      fprintf (stderr, "database.c: open_db() : can't open database %s\n", db_file);
      return err;
    }
  sqlite3_busy_timeout (db, 5000);

  db_exec ("PRAGMA journal_mode = WAL");
  db_exec ("PRAGMA auto_vacuum = INCREMENTAL");
  db_exec ("PRAGMA synchronous = NORMAL");
  db_exec ("PRAGMA journal_size_limit = 27103364");
  db_exec ("PRAGMA page_size = 8192");
  db_exec ("PRAGMA cache_size = 2000");
  db_exec ("PRAGMA foreign_keys = ON");
  db_exec ("PRAGMA mmap_size = 2147483648");
  db_exec ("PRAGMA temp_store = MEMORY");
  db_exec ("PRAGMA busy_timeout = 5000");

  return err;
}

void
close_db ()
{
  if (db) sqlite3_close (db);
}
