#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "../main.h"
#include "../database.h"

#define ROTATE_TEMPLATE "\n\
ALTER TABLE %s RENAME TO %s_old;\n\
\n\
%s;\n\
\n\
INSERT INTO %s SELECT * FROM %s_old ORDER BY rowid;\n\
DROP TABLE %s_old;\n\
\n"

typedef struct {
  char name[MAX_NAME_LEN];
  char sql[MAX_OBJECT_LEN];
} database_object_t;

static int
add_to_string (char *content[static 1], const char adding[static 1], size_t total_max)
{
  int err = 0;

  size_t content_len = *content ? strnlen (*content, total_max) : 0;
  size_t adding_len = strnlen (adding, total_max);

  if (content_len + adding_len > total_max - 1)
    {
      err = 1;
      fprintf (stderr, "generate_migration.c: add_to_string(): max migration file length exceeded.\n");
      goto teardown;
    }

  *content = realloc (*content, content_len + adding_len + 1);
  if (!*content)
    {
      err = 1;
      fprintf (stderr, "generate/generate_migration.c: add_to_string(): can't allocate memory.\n");
      goto teardown;
    }

  size_t left = total_max - content_len;
  int written = snprintf (*content + content_len, left, "%s", adding);
  if (written < 0 || (size_t) written >= left)
    {
      err = 1;
      fprintf (stderr, "generate_migration.c: add_to_string(): max migration file length exceeded on adding.\n");
      goto teardown;
    }

  teardown:
  return err;
}

static int
ensure_migration_directory_exists (options_t *options)
{
  int err = 0;
  struct stat st = {0};

  if (stat (options->migrations, &st) != 0)
    {
      err = mkdir (options->migrations, 0755);
      if (err)
        {
          fprintf (stderr, "generate/generation_migration.c: ensure_migration_directory_exists(): can't create directory: %s\n", options->migrations);
          goto teardown;
        }
    }
  else
    {
      if (!S_ISDIR (st.st_mode))
        {
          err = 1;
          fprintf (stderr, "generate/generation_migration.c: ensure_migration_directory_exists(): migrations path is not a directory: %s\nPlease provide an other migrations directory path with --migrations.\n", options->migrations);
          goto teardown;
        }
    }

  teardown:
  return err;
}

static int
generate_filename (char filename[MAX_PATH_LEN], options_t *options)
{
  int err = 0;

  time_t timestamp = time (NULL);

  int written = snprintf (filename, MAX_PATH_LEN - 1, "%s/%ld-%s.sql", options->migrations, timestamp, options->migration_name);
  if (written > MAX_PATH_LEN - 1)
    {
      err = 1;
      fprintf (stderr, "generate/generate_migration.c: generate_filename(): filename too long, truncated: %s\n", filename);
      goto teardown;
    }

  teardown:
  return err;
}

static int
find_table_sql (char *sql[static 1], const char table_name[MAX_NAME_LEN])
{
  int err = 0;
  sqlite3_stmt *stmt = NULL;
  char query[BUFSIZ] = "SELECT sql FROM sqlite_master WHERE type='table' AND name = ?";

  sqlite3_prepare_v2 (db, query, -1, &stmt, NULL);
  sqlite3_bind_text (stmt, 1, table_name, -1, NULL);

  while (1)
    {
      int s = sqlite3_step (stmt);
      if (s == SQLITE_ROW)
        {
          const char *code = (const char *) sqlite3_column_text (stmt, 0);

          if (strnlen (code, MAX_OBJECT_LEN) > MAX_OBJECT_LEN - 1)
            {
              err = 1;
              fprintf (stderr, "generate/generate_migration.c: find_table_sql(): table code exceeds allowed size of %d bytes.\n", MAX_OBJECT_LEN);
              goto teardown;
            }

          *sql = calloc (1, strlen (code) + 1);
          if (!*sql)
            {
              err = 1;
              fprintf (stderr, "generate/generate_migration.c: find_table_sql(): out of memory.\n");
              goto teardown;
            }
          snprintf (*sql, MAX_OBJECT_LEN, "%s", code);
        }
      else if (s == SQLITE_DONE)
        break;
      else
        {
          err = 1;
          fprintf (stderr, "generate/generate_migration.c: find_table_sql(): error while performing query : %s\n", sqlite3_errmsg (db));
          goto teardown;
        }
    }

  if (!*sql)
    {
      err = 1;
      fprintf (stderr, "generate/generate_migration.c: find_table_sql(): no such table: %s\n", table_name);
      goto teardown;
    }

  teardown:
  if (stmt) sqlite3_finalize (stmt);

  return err;
}

static int
find_triggers (database_object_t **triggers, const char table_name[MAX_NAME_LEN], size_t *len)
{
  int err = 0;
  sqlite3_stmt *stmt = NULL;
  char query[BUFSIZ] = "SELECT name, sql FROM sqlite_master WHERE type = 'trigger' AND (sql LIKE ? OR sql LIKE ? OR sql LIKE ?)";
  char pattern_1[MAX_NAME_LEN + 4] = {0};
  char pattern_2[MAX_NAME_LEN + 4] = {0};
  char pattern_3[MAX_NAME_LEN + 4] = {0};

  snprintf (pattern_1, MAX_NAME_LEN + 4, "%% %s %%", table_name);
  snprintf (pattern_2, MAX_NAME_LEN + 4, "%% %s(%%", table_name);
  snprintf (pattern_3, MAX_NAME_LEN + 4, "%% %s\n%%", table_name);

  sqlite3_prepare_v2 (db, query, -1, &stmt, NULL);
  sqlite3_bind_text (stmt, 1, pattern_1, -1, NULL);
  sqlite3_bind_text (stmt, 2, pattern_2, -1, NULL);
  sqlite3_bind_text (stmt, 3, pattern_3, -1, NULL);

  while (1)
    {
      int s = sqlite3_step (stmt);
      if (s == SQLITE_ROW)
        {
          (*len)++;
          *triggers = realloc (*triggers, sizeof (database_object_t) * *len);
          if (!*triggers)
            {
              err = 1;
              fprintf (stderr, "generate/generate_migration.c: find_triggers(): out of memory.\n");
              goto teardown;
            }

           const char *name = (const char *) sqlite3_column_text (stmt, 0);
           const char *sql = (const char *) sqlite3_column_text (stmt, 1);

           if (strnlen (name, MAX_NAME_LEN) > MAX_NAME_LEN - 1)
            {
              err = 1;
              fprintf (stderr, "generate/generate_migration.c: find_triggers(): trigger name exceeds maximum length authorized (%d bytes)\n", MAX_NAME_LEN);
              goto teardown;
            }

           if (strnlen (sql, MAX_OBJECT_LEN) > MAX_OBJECT_LEN - 1)
            {
              err = 1;
              fprintf (stderr, "generate/generate_migration.c: find_triggers(): trigger definition exceeds maximum length authorized (%d bytes)\n", MAX_OBJECT_LEN);
              goto teardown;
            }

          snprintf ((*triggers)[*len - 1].name, MAX_NAME_LEN, "%s", name);
          snprintf ((*triggers)[*len - 1].sql, MAX_OBJECT_LEN, "%s", sql);
        }
      else if (s == SQLITE_DONE)
        break;
      else
        {
          err = 1;
          fprintf (stderr, "generate/generate_migration.c: find_triggers(): error while performing query: %s\n", sqlite3_errmsg (db));
          goto teardown;
        }
    }

  teardown:
  if (stmt) sqlite3_finalize (stmt);

  return err;
}

static int
find_views (database_object_t **views, const char table_name[MAX_NAME_LEN], size_t *len)
{
  int err = 0;
  sqlite3_stmt *stmt = NULL;
  char query[BUFSIZ] = "SELECT name, sql FROM sqlite_master WHERE type = 'view' AND (sql LIKE ? OR sql LIKE ? OR sql LIKE ?)";
  char pattern_1[MAX_NAME_LEN + 4] = {0};
  char pattern_2[MAX_NAME_LEN + 4] = {0};
  char pattern_3[MAX_NAME_LEN + 4] = {0};

  snprintf (pattern_1, MAX_NAME_LEN + 4, "%% %s %%", table_name);
  snprintf (pattern_2, MAX_NAME_LEN + 4, "%% %s(%%", table_name);
  snprintf (pattern_3, MAX_NAME_LEN + 4, "%% %s\n%%", table_name);

  sqlite3_prepare_v2 (db, query, -1, &stmt, NULL);
  sqlite3_bind_text (stmt, 1, pattern_1, -1, NULL);
  sqlite3_bind_text (stmt, 2, pattern_2, -1, NULL);
  sqlite3_bind_text (stmt, 3, pattern_3, -1, NULL);

  while (1)
    {
      int s = sqlite3_step (stmt);
      if (s == SQLITE_ROW)
        {
          (*len)++;
          *views = realloc (*views, sizeof (database_object_t) * *len);
          if (!*views)
            {
              err = 1;
              fprintf (stderr, "generate/generate_migration.c: find_views(): out of memory.\n");
              goto teardown;
            }

           const char *name = (const char *) sqlite3_column_text (stmt, 0);
           const char *sql = (const char *) sqlite3_column_text (stmt, 1);

           if (strnlen (name, MAX_NAME_LEN) > MAX_NAME_LEN - 1)
            {
              err = 1;
              fprintf (stderr, "generate/generate_migration.c: find_views(): view name exceeds maximum length authorized (%d bytes)\n", MAX_NAME_LEN);
              goto teardown;
            }

           if (strnlen (sql, MAX_OBJECT_LEN) > MAX_OBJECT_LEN - 1)
            {
              err = 1;
              fprintf (stderr, "generate/generate_migration.c: find_views(): view definition exceeds maximum length authorized (%d bytes)\n", MAX_OBJECT_LEN);
              goto teardown;
            }

          snprintf ((*views)[*len - 1].name, MAX_NAME_LEN, "%s", name);
          snprintf ((*views)[*len - 1].sql, MAX_OBJECT_LEN, "%s", sql);
        }
      else if (s == SQLITE_DONE)
        break;
      else
        {
          err = 1;
          fprintf (stderr, "generate/generate_migration.c: find_views(): error while performing query: %s\n", sqlite3_errmsg (db));
          goto teardown;
        }
    }

  teardown:
  if (stmt) sqlite3_finalize (stmt);

  return err;
}

static int
find_indexes (database_object_t **indexes, const char table_name[MAX_NAME_LEN], size_t *len)
{
  int err = 0;
  sqlite3_stmt *stmt = NULL;
  char query[BUFSIZ] = "SELECT name, sql FROM sqlite_master WHERE type = 'index' AND (sql LIKE ? OR sql LIKE ? OR sql LIKE ?)";
  char pattern_1[MAX_NAME_LEN + 4] = {0};
  char pattern_2[MAX_NAME_LEN + 4] = {0};
  char pattern_3[MAX_NAME_LEN + 4] = {0};

  snprintf (pattern_1, MAX_NAME_LEN + 4, "%% %s %%", table_name);
  snprintf (pattern_2, MAX_NAME_LEN + 4, "%% %s(%%", table_name);
  snprintf (pattern_3, MAX_NAME_LEN + 4, "%% %s\n%%", table_name);

  sqlite3_prepare_v2 (db, query, -1, &stmt, NULL);
  sqlite3_bind_text (stmt, 1, pattern_1, -1, NULL);
  sqlite3_bind_text (stmt, 2, pattern_2, -1, NULL);
  sqlite3_bind_text (stmt, 3, pattern_3, -1, NULL);

  while (1)
    {
      int s = sqlite3_step (stmt);
      if (s == SQLITE_ROW)
        {
          (*len)++;
          *indexes = realloc (*indexes, sizeof (database_object_t) * *len);
          if (!*indexes)
            {
              err = 1;
              fprintf (stderr, "generate/generate_migration.c: find_indexes(): out of memory.\n");
              goto teardown;
            }

           const char *name = (const char *) sqlite3_column_text (stmt, 0);
           const char *sql = (const char *) sqlite3_column_text (stmt, 1);

           if (strnlen (name, MAX_NAME_LEN) > MAX_NAME_LEN - 1)
            {
              err = 1;
              fprintf (stderr, "generate/generate_migration.c: find_indexes(): index name exceeds maximum length authorized (%d bytes)\n", MAX_NAME_LEN);
              goto teardown;
            }

           if (strnlen (sql, MAX_OBJECT_LEN) > MAX_OBJECT_LEN - 1)
            {
              err = 1;
              fprintf (stderr, "generate/generate_migration.c: find_indexes(): index definition exceeds maximum length authorized (%d bytes)\n", MAX_OBJECT_LEN);
              goto teardown;
            }

          snprintf ((*indexes)[*len - 1].name, MAX_NAME_LEN, "%s", name);
          snprintf ((*indexes)[*len - 1].sql, MAX_OBJECT_LEN, "%s", sql);
        }
      else if (s == SQLITE_DONE)
        break;
      else
        {
          err = 1;
          fprintf (stderr, "generate/generate_migration.c: find_indexes(): error while performing query: %s\n", sqlite3_errmsg (db));
          goto teardown;
        }
    }

  teardown:
  if (stmt) sqlite3_finalize (stmt);

  return err;
}

static int
write_drop_objects (char **content, database_object_t *triggers, size_t triggers_len, database_object_t *views, size_t views_len, database_object_t *indexes, size_t indexes_len)
{
  int err = 0;

  for (size_t i = 0; i < triggers_len; i ++)
    {
      char drop_statement[MAX_OBJECT_LEN] = {0};
      int written = snprintf (drop_statement, MAX_OBJECT_LEN, "DROP TRIGGER IF EXISTS %s;\n", triggers[i].name);
      if (written > MAX_OBJECT_LEN)
        {
          err = 1;
          fprintf (stderr, "generate/generate_migration.c: write_drop_objects(): truncated trigger's drop statement.\n");
          goto teardown;
        }
      err = add_to_string (content, drop_statement, MAX_FILE_LEN);
      if (err)
        {
          fprintf (stderr, "generate/generate_migration.c: write_drop_objects(): can't add trigger's drop statement.\n");
          goto teardown;
        }
    }

  for (size_t i = 0; i < views_len; i ++)
    {
      char drop_statement[MAX_OBJECT_LEN] = {0};
      int written = snprintf (drop_statement, MAX_OBJECT_LEN, "DROP VIEW IF EXISTS %s;\n", views[i].name);
      if (written > MAX_OBJECT_LEN)
        {
          err = 1;
          fprintf (stderr, "generate/generate_migration.c: write_drop_objects(): truncated view's drop statement.\n");
          goto teardown;
        }
      err = add_to_string (content, drop_statement, MAX_FILE_LEN);
      if (err)
        {
          fprintf (stderr, "generate/generate_migration.c: write_drop_objects(): can't add view's drop statement.\n");
          goto teardown;
        }
    }

  for (size_t i = 0; i < indexes_len; i ++)
    {
      char drop_statement[MAX_OBJECT_LEN] = {0};
      int written = snprintf (drop_statement, MAX_OBJECT_LEN, "DROP INDEX IF EXISTS %s;\n", indexes[i].name);
      if (written > MAX_OBJECT_LEN)
        {
          err = 1;
          fprintf (stderr, "generate/generate_migration.c: write_drop_objects(): truncated index' drop statement.\n");
          goto teardown;
        }
      err = add_to_string (content, drop_statement, MAX_FILE_LEN);
      if (err)
        {
          fprintf (stderr, "generate/generate_migration.c: write_drop_objects(): can't add index' drop statement.\n");
          goto teardown;
        }
    }

  teardown:
  return err;
}

static int
write_table_rotation (char **content, const char table_sql[static 1], const char table_name[static 1])
{
  int err = 0;

  char rotation_statement[MAX_OBJECT_LEN] = {0};
  int written = snprintf (rotation_statement, MAX_OBJECT_LEN, ROTATE_TEMPLATE, table_name, table_name, table_sql, table_name, table_name, table_name);
  if (written > MAX_OBJECT_LEN)
    {
      err = 1;
      fprintf (stderr, "generate/generate_migration.c: write_table_rotation(): rotation SQL too long.\n");
      goto teardown;
    }

  err = add_to_string (content, rotation_statement, MAX_FILE_LEN);
  if (err)
    {
      fprintf (stderr, "generate/generate_migration.c: write_table_rotation(): can't add rotation to SQL.\n");
      goto teardown;
    }

  teardown:
  return err;
}

static int
write_recreate_objects (char **content, database_object_t *triggers, size_t triggers_len, database_object_t *views, size_t views_len, database_object_t *indexes, size_t indexes_len)
{
  int err = 0;

  for (size_t i = 0; i < triggers_len; i ++)
    {
      char create_statement[MAX_OBJECT_LEN] = {0};
      int written = snprintf (create_statement, MAX_OBJECT_LEN, "%s;\n\n", triggers[i].sql);
      if (written > MAX_OBJECT_LEN)
        {
          err = 1;
          fprintf (stderr, "generate/generate_migration.c: write_recreate_objects(): truncated trigger's drop statement.\n");
          goto teardown;
        }
      err = add_to_string (content, create_statement, MAX_FILE_LEN);
      if (err)
        {
          fprintf (stderr, "generate/generate_migration.c: write_recreate_objects(): can't add trigger's create statement.\n");
          goto teardown;
        }
    }

  for (size_t i = 0; i < views_len; i ++)
    {
      char create_statement[MAX_OBJECT_LEN] = {0};
      int written = snprintf (create_statement, MAX_OBJECT_LEN, "%s;\n\n", views[i].sql);
      if (written > MAX_OBJECT_LEN)
        {
          err = 1;
          fprintf (stderr, "generate/generate_migration.c: write_recreate_objects(): truncated view's drop statement.\n");
          goto teardown;
        }
      err = add_to_string (content, create_statement, MAX_FILE_LEN);
      if (err)
        {
          fprintf (stderr, "generate/generate_migration.c: write_recreate_objects(): can't add view's create statement.\n");
          goto teardown;
        }
    }

  for (size_t i = 0; i < indexes_len; i ++)
    {
      char create_statement[MAX_OBJECT_LEN] = {0};
      int written = snprintf (create_statement, MAX_OBJECT_LEN, "%s;\n\n", indexes[i].sql);
      if (written > MAX_OBJECT_LEN)
        {
          err = 1;
          fprintf (stderr, "generate/generate_migration.c: write_recreate_objects(): truncated index' drop statement.\n");
          goto teardown;
        }
      err = add_to_string (content, create_statement, MAX_FILE_LEN);
      if (err)
        {
          fprintf (stderr, "generate/generate_migration.c: write_recreate_objects(): can't add index' create statement.\n");
          goto teardown;
        }
    }

  teardown:
  return err;
}

static int
recreate_table_migration (char **content, const char table_name[MAX_NAME_LEN])
{
  int err = 0;
  char *table_sql = NULL;
  database_object_t *triggers = NULL;
  database_object_t *views = NULL;
  database_object_t *indexes = NULL;

  // We need legacy_alter_table to prevent renaming foreign keys when renaming the table
  const char *pragmas = "PRAGMA foreign_keys = OFF;\nPRAGMA legacy_alter_table = ON;\n";

  err = add_to_string (content, pragmas, MAX_FILE_LEN);
  if (err)
    {
      fprintf (stderr, "generate/generate_migration.c: recreate_table_migration(): can't add pragmas.\n");
      goto teardown;
    }

  err = find_table_sql (&table_sql, table_name);
  if (err)
    {
      fprintf (stderr, "generate/generate_migration.c: recreate_table_migration(): can't retrieve table's SQL code.\n");
      goto teardown;
    }

  size_t triggers_len = 0;
  err = find_triggers (&triggers, table_name, &triggers_len);
  if (err)
    {
      fprintf (stderr, "generate/generate_migration.c: recreate_table_migration(): can't retrieve triggers.\n");
      goto teardown;
    }

  size_t views_len = 0;
  err = find_views (&views, table_name, &views_len);
  if (err)
    {
      fprintf (stderr, "generate/generate_migration.c: recreate_table_migration(): can't retrieve views.\n");
      goto teardown;
    }

  size_t indexes_len = 0;
  err = find_indexes (&indexes, table_name, &indexes_len);
  if (err)
    {
      fprintf (stderr, "generate/generate_migration.c: recreate_table_migration(): can't retrieve indexes.\n");
      goto teardown;
    }

  err = write_drop_objects (content, triggers, triggers_len, views, views_len, indexes, indexes_len);
  if (err)
    {
      fprintf (stderr, "generate/generate_migration.c: recreate_table_migration(): can't write drop statements.\n");
      goto teardown;
    }

  err = write_table_rotation (content, table_sql, table_name);
  if (err)
    {
      fprintf (stderr, "generate/generate_migration.c: recreate_table_migration(): can't write table rotation statements.\n");
      goto teardown;
    }

  err = write_recreate_objects (content, triggers, triggers_len, views, views_len, indexes, indexes_len);
  if (err)
    {
      fprintf (stderr, "generate/generate_migration.c: recreate_table_migration(): can't write dropped objects recreation statements.\n");
      goto teardown;
    }

  teardown:
  if (table_sql) free (table_sql);
  if (triggers) free (triggers);
  if (views) free (views);
  if (indexes) free (indexes);
  return err;
}

static int
raw_migration (char **content)
{
  int err = 0;

  const char *raw_content = "-- Your SQL\n";

  *content = calloc (1, strlen (raw_content) + 1);
  if (!*content)
    {
      err = 1;
      fprintf (stderr, "generate/generate_migration.c: raw_migration(): can't allocate memory.\n");
      goto teardown;
    }

  snprintf (*content, strlen (raw_content) + 1, "%s", raw_content);

  teardown:
  return err;
}

static int
save_migration (char *content, const char filename[MAX_PATH_LEN])
{
  int err = 0;
  FILE *file = NULL;

  file = fopen (filename, "w");
  if (!file)
    {
      err = 1;
      fprintf (stderr, "generate/generate_migration.c: save_migration(): can't open file for writing: %s\n", filename);
      goto teardown;
    }

  fprintf (file, "%s", content);
  printf ("Migration created in %s\n", filename);

  teardown:
  if (file) fclose (file);
  return err;
}

int
generate_migration (options_t *options)
{
  int err = 0;
  char filename[MAX_PATH_LEN] = {0};
  char *content = NULL;

  err = ensure_migration_directory_exists (options);
  if (err)
    {
      fprintf (stderr, "generate/generate_migration.c: generate_migration(): can't ensure directory exists.\n");
      goto teardown;
    }

  err = generate_filename (filename, options);
  if (err)
    {
      fprintf (stderr, "generate/generate_migration.c: generate_migration(): can't generate filename.\n");
      goto teardown;
    }

  if (options->recreate[0] != 0)
    {
      err = open_db (options->database, options->init);
      if (err)
        {
          fprintf (stderr, "generate/generate_migration.c: generate_migration(): can't open database.\n");
          goto teardown;
        }

      err = recreate_table_migration (&content, options->recreate);
      if (err)
        {
          fprintf (stderr, "generate/generate_migration.c: generate_migration(): can't generate table recreation migration.\n");
          goto teardown;
        }
    }
  else
    {
      err = raw_migration (&content);
      if (err)
        {
          fprintf (stderr, "generate/generate_migration.c: generate_migration(): can't generate raw migration.\n");
          goto teardown;
        }
    }

  err = save_migration (content, filename);
  if (err)
    {
      fprintf (stderr, "generate/generate_migration.c: generate_migration(): could not write migration to filesystem.\n");
      goto teardown;
    }

  teardown:
  if (content) free (content);
  return err;
}
