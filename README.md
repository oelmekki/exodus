# Exodus - movement of your tables

Exodus allows you to create and apply migrations for SQLite. Its main features
are:

- applying only migrations not yet applied to the database
- creating a backup database and restoring it in case there's an error during migration
- allowing migrations to be both SQL files or (any language) executables
- generating boilerplate to recreate a table (and all the related views,
  triggers and indexes) when the change you want to make can't be done with
  `ALTER TABLE`.

## Dependencies

Exodus' only dependency is SQLite.

Debian/Ubuntu users will have to install SQLite headers to build Exodus:

```
sudo apt install libsqlite3-dev
```

## Build and installation

```
make
sudo make install
```

It will be installed in `$PREFIX/bin`, which is by default `/usr/local/bin`. You
can change the PREFIX variable like this:

```
make
sudo make install PREFIX=/usr
```

## Usage

```
exodus [options] generate <migration name> [--recreate <table>]
exodus [options] migrate

Exodus is a SQLite database migration tool.

When using the `generate` subcommand, you specify a migration name (not a path),
and it will create an SQL file in the migrations directory with that name, appending
the `.sql` suffix and the current timestamp as prefix. The default migrations
directory is `./migrations/`. You can change it with the `--migrations` option.

If you specify a table name with the `--recreate` option, the migration file will
be prefilled to:

- drop triggers, indexes and views using that table
- rename that table
- create another table with the initial name
- copy the data from the old table to the new one
- drop the old table
- recreate the triggers, indexes and views for the new table

This will allow you to change in your table things that can only be changed by
recreating it, like for example the `CHECK` constraints.

When using the `migrate` subcommand, exodus will run the pending migrations on
the database. The migrations directory is determined as for `generate`. The default
database file is `./app.db`. You can change it with the `--database` option.

`migrate` will create the `migrations` table in your database if it doesn't exist
yet, and will execute every migration from the migrations directory that are not
already referenced in this table, in alphabetical order. It will save the previous
database as `<db name>.prev`, and if the migration fails, it will restore that
previous database, and save the failed one as `<db name>.failed`. In case of success,
it will dump the current structure in the structure file, which is `./structure.sql`
by default, and can be changed with the `--structure` option.

A migration file can either be a SQL file, or an executable. Executables will be
executed once, provided they return a 0 status. Non zero status will be considered
as a failure at applying the migration. The point of running those executables is
to allow your migration to compute data changes, rather than hardcoding them. Your
executable will be passed the database path as first parameter, but beside that,
you're on your own. It's your responsibility to make that executable connect to
the database and do whatever it wants with it.

You can provide SQL code that will be called every time a connection is open
(at the start of the program and after each migration has ran, ensuring it runs once
per migration). This can be typically used to set up your PRAGMAs. The file used is
the first one existing in this list:

- something provided by the `--init` option
- $XDG_CONFIG_HOME/exodus-init.sql
- $HOME/.config/exodus-init.sql
- /etc/exodus-init.sql

Options can be:

  -h, --help: display this help.
  -d, --database <database file>: use this file as database.
  -m, --migrations <migrations directory>: use this directory for migrations.
  -s, --structure <structure file>: use this file for SQL structure.
  -i, --init <SQL init file>: content of this file will be executed when opening each connection.
```

## Made to last

If you see this project has not been updated in years, it is not a bug, it's a
feature. Exodus is built on a standard stack with a single rock stable
dependency, it's meant to last without requiring maintenance.
