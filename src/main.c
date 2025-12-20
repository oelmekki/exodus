#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "main.h"
#include "database.h"
#include "generate/generate_migration.h"
#include "migrate/migrate.h"

static void
usage (const char progname[static 1])
{
  printf ("\
%s [options] generate <migration name> [--recreate <table>]\n\
%s [options] migrate\n\
\n\
Exodus is a SQLite database migration tool.\n\
\n\
When using the `generate` subcommand, you specify a migration name (not a path),\n\
and it will create an SQL file in the migrations directory with that name, appending\n\
the `.sql` suffix and the current timestamp as prefix. The default migrations\n\
directory is `./migrations/`. You can change it with the `--migrations` option.\n\
\n\
If you specify a table name with the `--recreate` option, the migration file will\n\
be prefilled to:\n\
\n\
- drop triggers, indexes and views using that table\n\
- rename that table\n\
- create another table with the initial name\n\
- copy the data from the old table to the new one\n\
- drop the old table\n\
- recreate the triggers, indexes and views for the new table\n\
\n\
This will allow you to change in your table things that can only be changed by\n\
recreating it, like for example the `CHECK` constraints.\n\
\n\
When using the `migrate` subcommand, exodus will run the pending migrations on\n\
the database. The migrations directory is determined as for `generate`. The default\n\
database file is `./app.db`. You can change it with the `--database` option.\n\
\n\
`migrate` will create the `migrations` table in your database if it doesn't exist\n\
yet, and will execute every migration from the migrations directory that are not\n\
already referenced in this table, in alphabetical order. It will save the previous\n\
database as `<db name>.prev`, and if the migration fails, it will restore that\n\
previous database, and save the failed one as `<db name>.failed`. In case of success,\n\
it will dump the current structure in the structure file, which is `./structure.sql`\n\
by default, and can be changed with the `--structure` option.\n\
\n\
A migration file can either be a SQL file, or an executable. Executables will be\n\
executed once, provided they return a 0 status. Non zero status will be considered\n\
as a failure at applying the migration. The point of running those executables is\n\
to allow your migration to compute data changes, rather than hardcoding them. Your\n\
executable will be passed the database path as first parameter, but beside that,\n\
you're on your own. It's your responsibility to make that executable connect to\n\
the database and do whatever it wants with it.\n\
\n\
You can provide SQL code that will be called every time a connection is open\n\
(at the start of the program and after each migration has ran, ensuring it runs once\n\
per migration). This can be typically used to set up your PRAGMAs. The file used is\n\
the first one existing in this list:\n\
\n\
- something provided by the `--init` option\n\
- $XDG_CONFIG_HOME/exodus-init.sql\n\
- $HOME/.config/exodus-init.sql\n\
- /etc/exodus-init.sql\n\
\n\
Options can be:\n\
\n\
  -h, --help: display this help.\n\
  -d, --database <database file>: use this file as database.\n\
  -m, --migrations <migrations directory>: use this directory for migrations.\n\
  -s, --structure <structure file>: use this file for SQL structure.\n\
  -i, --init <SQL init file>: content of this file will be executed when opening each connection.\n\
", progname, progname);
}

static bool
file_exists (const char *path)
{
  struct stat st;
  if (stat (path, &st) == 0)
    return true;

  return false;
}

static void
find_init_file (char init[MAX_PATH_LEN])
{
  char *xdg_config = getenv ("XDG_CONFIG_HOME");

  if (xdg_config)
    {
      snprintf (init, MAX_PATH_LEN, "%s/exodus-init.sql", xdg_config);
      if (file_exists (init))
        return;
    }

  char *home = getenv ("HOME");
  if (home)
    {
      snprintf (init, MAX_PATH_LEN, "%s/.config/exodus-init.sql", home);
      if (file_exists (init))
        return;
    }

  snprintf (init, MAX_PATH_LEN, "/etc/exodus-init.sql");
  if (file_exists (init))
    return;

  init[0] = 0;
}

static int
parse_options (int argc, char **argv, options_t options[static 1])
{
  int err = 0;

  if (argc > 1)
    {
      for (int i = 1; i < argc; i++)
        {
          if (strncmp (argv[i], "--help", 10) == 0 || strncmp (argv[i], "-h", 10) == 0)
            {
              usage (argv[0]);
              exit (0);
            }

          if (strncmp (argv[i], "--database", 20) == 0 || strncmp (argv[i], "-d", 10) == 0)
            {
              if (argc < i + 2)
                {
                  fprintf (stderr, "You need to provide a value for %s.\n\n", argv[i]);
                  usage (argv[0]);
                  err = 1;
                  goto teardown;
                }

              snprintf (options->database, MAX_PATH_LEN - 1, "%s", argv[++i]);
              continue;
            }

          if (strncmp (argv[i], "--migrations", 20) == 0 || strncmp (argv[i], "-m", 10) == 0)
            {
              if (argc < i + 2)
                {
                  fprintf (stderr, "You need to provide a value for %s.\n\n", argv[i]);
                  usage (argv[0]);
                  err = 1;
                  goto teardown;
                }

              snprintf (options->migrations, MAX_PATH_LEN - 1, "%s", argv[++i]);
              continue;
            }

          if (strncmp (argv[i], "--structure", 20) == 0 || strncmp (argv[i], "-s", 10) == 0)
            {
              if (argc < i + 2)
                {
                  fprintf (stderr, "You need to provide a value for %s.\n\n", argv[i]);
                  usage (argv[0]);
                  err = 1;
                  goto teardown;
                }

              snprintf (options->structure, MAX_PATH_LEN - 1, "%s", argv[++i]);
              continue;
            }

          if (strncmp (argv[i], "--init", 10) == 0 || strncmp (argv[i], "-i", 10) == 0)
            {
              if (argc < i + 2)
                {
                  fprintf (stderr, "You need to provide a value for %s.\n\n", argv[i]);
                  usage (argv[0]);
                  err = 1;
                  goto teardown;
                }

              snprintf (options->init, MAX_PATH_LEN - 1, "%s", argv[++i]);
              continue;
            }

          if (strncmp (argv[i], "--recreate", 20) == 0)
            {
              if (argc < i + 2)
                {
                  fprintf (stderr, "You need to provide a value for --recreate.\n\n");
                  usage (argv[0]);
                  err = 1;
                  goto teardown;
                }

              snprintf (options->recreate, MAX_NAME_LEN - 1, "%s", argv[++i]);
              continue;
            }

          if (strncmp (argv[i], "generate", 10) == 0)
            {
              options->command = COMMAND_GENERATE;
              continue;
            }

          if (strncmp (argv[i], "migrate", 10) == 0)
            {
              options->command = COMMAND_MIGRATE;
              continue;
            }

          if (options->command == COMMAND_GENERATE && options->migration_name[0] == 0)
            {
              snprintf (options->migration_name, MAX_NAME_LEN - 1, "%s", argv[i]);
              continue;
            }

          err = 1;
          fprintf (stderr, "Unknown parameter: %s\n\n", argv[i]);
          usage (argv[0]);
          goto teardown;
        }
    }

  if (options->database[0] == 0)
    snprintf (options->database, MAX_PATH_LEN - 1, "./app.db");

  if (options->migrations[0] == 0)
    snprintf (options->migrations, MAX_PATH_LEN - 1, "./migrations");

  if (options->structure[0] == 0)
    snprintf (options->structure, MAX_PATH_LEN - 1, "./structure.sql");

  if (options->init[0] == 0)
    find_init_file (options->init);

  teardown:
  return err;
}

int
main (int argc, char **argv)
{
  int err = 0;

  options_t options = {0};
  err = parse_options (argc, argv, &options);
  if (err)
    goto teardown;

  switch (options.command)
    {
      case COMMAND_GENERATE:
        err = generate_migration (&options);
        if (err)
          {
            fprintf (stderr, "main.c: main(): could not generate migration.\n");
            goto teardown;
          }
        break;

      case COMMAND_MIGRATE:
        err = migrate (&options);
        if (err)
          {
            fprintf (stderr, "main.c: main(): could not migrate.\n");
            goto teardown;
          }
        break;

      default:
        fprintf (stderr, "unknown command.\n\n");
        usage (argv[0]);
        err = 1;
        goto teardown;
    }

  teardown:
  close_db ();
  return err;
}
