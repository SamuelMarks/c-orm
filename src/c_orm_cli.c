/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_migrations.h"
#include "c_orm_sqlite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define MKDIR(path) mkdir(path, 0777)
#endif
/* clang-format on */

static void print_usage(const char *prog) {
  printf("Usage: %s <command> [options]\n\n", prog);
  printf("Commands:\n");
  printf("  init                      Setup the migrations directory\n");
  printf("  create <name>             Generate empty UP and DOWN migration "
         "scripts\n");
  printf("  generate <table_name>     Auto-generate migrations based on schema "
         "diff\n");
  printf("  migrate                   Apply pending migrations\n");
  printf("  rollback [steps]          Rollback applied migrations\n");
  printf("  status                    Show pending and applied migrations\n\n");
  printf("Options:\n");
  printf("  --db <conn_str>           Database connection string\n");
  printf("  --dir <path>              Migrations directory (default: "
         "./migrations)\n");
}

static void log_cb(const char *msg) { printf("[Migration] %s\n", msg); }

int main(int argc, char **argv) {
  int rc;

  const char *command = NULL;
  const char *db_str = getenv("C_ORM_DB_URL");
  const char *dir_path = "./migrations";
  const char *arg_name = NULL;
  int i;

  if (argc < 2) {
    print_usage(argv[0]);
    {
      rc = 1;
      {
        return rc;
      }
    }
  }

  command = argv[1];

  /* Parse args */
  for (i = 2; i < argc; i++) {
    if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
      db_str = argv[++i];
    } else if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc) {
      dir_path = argv[++i];
    } else if (!arg_name) {
      arg_name = argv[i];
    }
  }

  if (strcmp(command, "init") == 0) {
    if (MKDIR(dir_path) == 0) {
      printf("Created migrations directory at '%s'\n", dir_path);
    } else {
      printf("Directory '%s' already exists or could not be created\n",
             dir_path);
    }
  } else if (strcmp(command, "create") == 0) {
    char up_file[512];
    char down_file[512];
    FILE *fp;

    if (!arg_name) {
      printf("Error: 'create' requires a migration name.\n");
      {
        rc = 1;
        {
          return rc;
        }
      }
    }

#if defined(_MSC_VER)
    sprintf_s(up_file, sizeof(up_file), "%s/%s.up.sql", dir_path, arg_name);
    sprintf_s(down_file, sizeof(down_file), "%s/%s.down.sql", dir_path,
              arg_name);
#else
#if defined(_MSC_VER)
    sprintf_s(up_file, sizeof(up_file), "%s/%s.up.sql", dir_path, arg_name);
#else
    sprintf(up_file, "%s/%s.up.sql", dir_path, arg_name);
#endif
#if defined(_MSC_VER)
    sprintf_s(down_file, sizeof(down_file), "%s/%s.down.sql", dir_path,
              arg_name);
#else
    sprintf(down_file, "%s/%s.down.sql", dir_path, arg_name);
#endif
#endif

    fp = fopen(up_file, "w");
    if (fp) {
      fclose(fp);
      printf("Created %s\n", up_file);
    }
    fp = fopen(down_file, "w");
    if (fp) {
      fclose(fp);
      printf("Created %s\n", down_file);
    }

  } else if (strcmp(command, "generate") == 0) {
    printf("Schema diff auto-generation is not implemented dynamically via CLI "
           "yet.\n");
    printf("Please use cdd-c code generation or manual SQL creation.\n");
  } else if (strcmp(command, "migrate") == 0) {
    c_orm_db_t *db = NULL;
    c_orm_error_t err;
    c_orm_migration_options_t opts;
    /* we would load actual files from dir here */
    c_orm_migration_t *migs = NULL;
    size_t migs_count = 0;

    if (!db_str) {
      printf("Error: Database connection string required (--db or C_ORM_DB_URL "
             "env)\n");
      {
        rc = 1;
        {
          return rc;
        }
      }
    }

    err = c_orm_sqlite_connect(db_str, &db);
    if (err != C_ORM_OK) {
      printf("Error connecting to database.\n");
      {
        rc = 1;
        {
          return rc;
        }
      }
    }

    memset(&opts, 0, sizeof(opts));
    opts.log_cb = log_cb;

    err = c_orm_migration_load_dir(dir_path, &migs, &migs_count);
    if (err == C_ORM_OK && migs_count > 0) {
      c_orm_migrate_all(db, migs, migs_count, &opts);
      c_orm_migration_free_array(migs, migs_count);
    } else {
      printf("No pending migrations found in %s\n", dir_path);
    }

    db->vtable->disconnect(db);
  } else if (strcmp(command, "rollback") == 0) {
    printf("Rollback logic stubbed.\n");
  } else if (strcmp(command, "status") == 0) {
    c_orm_db_t *db = NULL;
    c_orm_error_t err;
    c_orm_migration_t *applied = NULL;
    size_t count = 0, j;

    if (!db_str) {
      printf("Error: Database connection string required (--db or C_ORM_DB_URL "
             "env)\n");
      {
        rc = 1;
        {
          return rc;
        }
      }
    }

    err = c_orm_sqlite_connect(db_str, &db);
    if (err != C_ORM_OK) {
      printf("Error connecting to database.\n");
      {
        rc = 1;
        {
          return rc;
        }
      }
    }

    err = c_orm_migration_get_applied(db, &applied, &count);
    if (err == C_ORM_OK) {
      printf("Applied Migrations (%d):\n", (int)count);
      for (j = 0; j < count; j++) {
        printf("  [%s] %s\n", applied[j].version, applied[j].name);
      }
      if (applied) {
        for (j = 0; j < count; j++) {
          /* Not fully allocated like full array but version/name in struct */
        }
        free(applied);
      }
    } else {
      printf("Failed to fetch migration status or no migrations applied.\n");
    }
    db->vtable->disconnect(db);
  } else {
    printf("Unknown command: %s\n", command);
    print_usage(argv[0]);
    {
      rc = 1;
      {
        return rc;
      }
    }
  }

  {
    rc = 0;
    {
      return rc;
    }
  }
}
