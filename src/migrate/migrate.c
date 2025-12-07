#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../main.h"
#include "../database.h"

extern char **environ;

char last_migration_applied[MAX_PATH_LEN] = {0};

static bool
is_executable (const char migration_file[MAX_PATH_LEN])
{
  struct stat st;
  if (stat (migration_file, &st) != 0)
    {
      fprintf (stderr, "migrate/migrate.c: migration file does not exist or is not readable: %s\n", migration_file);
      return false;
    }

  if (st.st_mode & S_IXUSR)
    return true;

  return false;
}

static int
find_last_migration_applied ()
{
  int err = 0;
  sqlite3_stmt *stmt = NULL;
  char query[BUFSIZ] = "SELECT name FROM migrations ORDER BY name DESC LIMIT 1";

  err = db_exec ("CREATE TABLE IF NOT EXISTS migrations(name TEXT NOT NULL)");
  if (err)
    {
      fprintf (stderr, "migrate/migrate.c: find_last_migration_table(): can't create migrations table.\n");
      goto teardown;
    }

  sqlite3_prepare_v2 (db, query, -1, &stmt, NULL);

  while (1)
    {
      int s = sqlite3_step (stmt);
      if (s == SQLITE_ROW)
        {
          const char *name = (const char *) sqlite3_column_text (stmt, 0);
          snprintf (last_migration_applied, MAX_PATH_LEN, "%s", name);
        }
      else if (s == SQLITE_DONE)
        break;
      else
        {
          err = 1;
          fprintf (stderr, "migrate/migrate.c: find_last_migration_applied(): error while performing query: %s\n", sqlite3_errmsg (db));
          goto teardown;
        }
    }

  teardown:
  if (stmt) sqlite3_finalize (stmt);

  return err;
}

static int
filter_applied_migrations (const struct dirent *entry)
{
  if (strncmp (entry->d_name, ".", 1) == 0)
    return 0;

  return strncmp (entry->d_name, last_migration_applied, MAX_PATH_LEN) > 0;
}

static int
find_migration_files (const char migrations_dir[MAX_PATH_LEN], struct dirent ***entries, size_t *migration_files_len)
{
  int err = 0;
  int len = scandir (migrations_dir, entries, &filter_applied_migrations, alphasort);
  if (len == -1)
    {
      err = 1;
      fprintf (stderr, "migrate/migrate.c: find_migration_files(): can't scan migrations directory.\n");
      goto teardown;
    }

  *migration_files_len = (size_t) len;

  teardown:
  return err;
}

static int
apply_sql_migration (const char migration_file[MAX_PATH_LEN])
{
  int err = 0;
  FILE *file = NULL;
  char *sql = NULL;

  file = fopen (migration_file, "r");
  if (!file)
    {
      err = 1;
      fprintf (stderr, "migrate/migrate.c: apply_sql_migration(): can't open migration file: %s\n", migration_file);
      goto teardown;
    }

  fseek (file, 0, SEEK_END);
  long size = ftell (file);
  fseek (file, 0, SEEK_SET);

  sql = calloc (1, size + 1);
  if (!sql)
    {
      err = 1;
      fprintf (stderr, "migrate/migrate.c: apply_sql_migration(): out of memory.\n");
      goto teardown;
    }

  size_t read = fread (sql, 1, size, file);
  if (read != (size_t) size)
    {
      err = 1;
      fprintf (stderr, "migrate/migrate.c: apply_sql_migration(): could not read the whole migration file: %s\n", migration_file);
      goto teardown;
    }

  err = db_exec (sql);
  if (err)
    {
      fprintf (stderr, "migrate/migrate.c: apply_sql_migration(): could not execute migration: %s\n", migration_file);
      goto teardown;
    }

  teardown:
  if (file) fclose (file);
  if (sql) free (sql);
  return err;
}

static int
apply_executable_migration (const char migration_file[MAX_PATH_LEN], const char database_path[MAX_PATH_LEN])
{
  int err = 0;

  pid_t pid = fork ();
  if (pid < 0)
    {
      err = 1;
      fprintf (stderr, "migrate/migrate.c: apply_executable_migration(): can't fork to execute migration %s\n", migration_file);
      goto teardown;
    }

  if (pid == 0)
    {
      const char *args[] = { migration_file, database_path, NULL };
      execve (args[0], (char **) args, environ);
      exit (127);
    }

  int status = 0;
  waitpid (pid, &status, 0);

  if (WIFSIGNALED (status))
    {
      err = 1;
      fprintf (stderr, "migrate/migrate.c: apply_executable_migration(): migration executable was killed: %s\n", migration_file);
      goto teardown;
    }

  err = WEXITSTATUS (status);
  if (err)
    {
      fprintf (stderr, "migrate/migrate.c: apply_executable_migration(): migration executable returned non zero status (%d): %s\n", err, migration_file);
      goto teardown;
    }

  teardown:
  return err;
}

static int
append_name_in_migrations_table (const char migration_file[MAX_NAME_LEN])
{
  int err = 0;
  sqlite3_stmt *stmt = NULL;
  char query[BUFSIZ] = "INSERT INTO migrations(name) VALUES (?)";

  sqlite3_prepare_v2 (db, query, -1, &stmt, NULL);
  sqlite3_bind_text (stmt, 1, migration_file, -1, NULL);

  while (1)
    {
      int s = sqlite3_step (stmt);
      if (s == SQLITE_ROW)
        continue;
      else if (s == SQLITE_DONE)
        break;
      else
        {
          err = 1;
          fprintf (stderr, "migrate/migrate.c: append_name_in_migrations_table(): error while performing query: %s\n", sqlite3_errmsg (db));
          goto teardown;
        }
    }

  teardown:
  if (stmt) sqlite3_finalize (stmt);

  return err;
}

int
migrate (options_t *options)
{
  int err = 0;
  bool should_restore_db = false;

  struct dirent **migration_files = NULL;
  size_t migration_files_len = 0;
  char backup_file[MAX_PATH_LEN] = {0};
  char fail_file[MAX_PATH_LEN] = {0};

  int written = snprintf (backup_file, MAX_PATH_LEN, "%s.prev", options->database);
  if (written > MAX_PATH_LEN)
    {
      err = 1;
      fprintf (stderr, "migrate/migrate.c: migrate(): truncated backup database file path:%s\n", backup_file);
      goto teardown;
    }

  written = snprintf (fail_file, MAX_PATH_LEN, "%s.fail", options->database);
  if (written > MAX_PATH_LEN)
    {
      err = 1;
      fprintf (stderr, "migrate/migrate.c: migrate(): truncated fail database file path:%s\n", fail_file);
      goto teardown;
    }

  err = open_db (options->database);
  if (err)
    {
      fprintf (stderr, "migrate/migrate.c: migrate(): can't open database.\n");
      goto teardown;
    }

  err = find_last_migration_applied ();
  if (err)
    {
      fprintf (stderr, "migrate/migrate.c: migrate(): can't find last migration applied.\n");
      goto teardown;
    }

  err = find_migration_files (options->migrations, &migration_files, &migration_files_len);
  if (err)
    {
      fprintf (stderr, "migrate/migrate.c: migrate(): can't find migration files.\n");
      goto teardown;
    }

  err = backup_db (options->database, backup_file);
  if (err)
    {
      fprintf (stderr, "migrate/migrate.c: migrate(): can't backup database.\n");
      goto teardown;
    }

  for (size_t i = 0; i < migration_files_len; i++)
    {
      const char *migration_file = migration_files[i]->d_name;
      char migration_path[MAX_PATH_LEN] = {0};
      int written = snprintf (migration_path, MAX_PATH_LEN, "%s/%s", options->migrations, migration_file);
      if (written > MAX_PATH_LEN)
        {
          should_restore_db = true;
          fprintf (stderr, "migrate/migrate.c: migrate(): truncated migration path: %s\n", migration_path);
          goto teardown;
        }

      printf ("Applying migration %s…\n", migration_path);

      if (strncmp (migration_file + strnlen (migration_file, MAX_PATH_LEN) - 4, ".sql", MAX_PATH_LEN) == 0)
        {
          err = apply_sql_migration (migration_path);
          if (err)
            {
              should_restore_db = true;
              fprintf (stderr, "migrate/migrate.c: migrate(): can't apply migration: %s\n", migration_path);
              goto teardown;
            }
        }
      else
        {
          if (!is_executable (migration_path))
            {
              err = 1;
              should_restore_db = true;
              fprintf (stderr, "migrate/migrate.c: migrate(): migration is not an executable and does not have .sql extension: %s\n", migration_path);
              goto teardown;
            }

          err = apply_executable_migration (migration_path, options->database);
          if (err)
            {
              should_restore_db = true;
              fprintf (stderr, "migrate/migrate.c: migrate(): can't apply migration: %s\n", migration_path);
              goto teardown;
            }
        }

      err = append_name_in_migrations_table (migration_file);
      if (err)
        {
          should_restore_db = true;
          fprintf (stderr, "migrate/migrate.c: migrate(): can't remember migration was executed: %s\n", migration_file);
          goto teardown;
        }
    }

  teardown:
  if (migration_files)
    {
      for (size_t i = 0; i < migration_files_len; i++)
        free (migration_files[i]);
      free (migration_files);
    }

  if (should_restore_db)
    {
      int err = backup_db (options->database, fail_file);
      if (err)
        fprintf (stderr, "migrate/migrate.c: migrate(): can't save current state to fail database dump.\n");

      err = backup_db (backup_file, options->database);
      if (err)
        fprintf (stderr, "migrate/migrate.c: migrate(): can't restore database. Sorry, we tried. 😢\n");
    }

  return err;
}
