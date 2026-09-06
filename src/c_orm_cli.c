#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file c_orm_cli.c
 * @brief Command line interface for C-ORM migrations and tools.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_api.h"
#include "c_orm_migrations.h"
#include "c_orm_sqlite.h"
#include "c_orm_log.h"
#include "c_orm_codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
static void my_invalid_parameter_handler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, size_t pReserved) {
    (void)expression; (void)function; (void)file; (void)line; (void)pReserved;
}
#endif

#if defined(_WIN32) || defined(_WIN64)
extern __declspec(dllimport) unsigned int __stdcall SetErrorMode(unsigned int);
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif
#if defined(_MSC_VER) && defined(_DEBUG)
#endif
#else
#include <sys/stat.h>
#define MKDIR(path) mkdir(path, 0777)
#endif
/* clang-format on */

/**
 * @brief Prints the usage information.
 * @param prog The program name.
 */
static c_orm_error_t print_usage(const char *prog) {
  LOG_DEBUG("print_usage: entry");
  printf("Usage: %s <command> [options]\n\n", prog);
  printf("Commands:\n");
  printf("  init                      Setup the migrations directory\n");
  printf("  create <name>             Generate empty UP and DOWN migration "
         "scripts\n");
  printf("  generate <table_name>     Auto-generate migrations based on schema "
         "diff\n");
  printf("  sql2c <schema.sql> <out_dir> Generate C code (c-orm compatible) "
         "from SQL DDL\n");
  printf("  migrate                   Apply pending migrations\n");
  printf("  rollback [steps]          Rollback applied migrations\n");
  printf("  status                    Show pending and applied migrations\n\n");
  printf("Options:\n");
  printf("  --db <conn_str>           Database connection string\n");
  printf("  --dir <path>              Migrations directory (default: "
         "./migrations)\n");
  LOG_DEBUG("print_usage: exit");
  return C_ORM_OK;
}

/**
 * @brief Callback for migration logging.
 * @param msg The message to log.
 */
static c_orm_error_t log_cb(const char *msg) {
  LOG_DEBUG("log_cb: entry");
  printf("[Migration] %s\n", msg);
  LOG_DEBUG("log_cb: exit");
  return C_ORM_OK;
}

/**
 * @brief Main entry point for the CLI.
 * @param argc The argument count.
 * @param argv The argument values.
 * @return 0 on success, non-zero on failure.
 */
int main(int argc, char **argv) {
  c_orm_error_t rc;
  const char *command = NULL;
  char db_str_buf[1024];
  const char *db_str = NULL;

  const char *dir_path = "./migrations";
  const char *arg_name = NULL;
  int i;

#if defined(_MSC_VER) && defined(_DEBUG)
  _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#if defined(_WIN32) || defined(_WIN64)
  { SetErrorMode(0x0001 | 0x0002 | 0x8000); }
#endif

#if defined(_MSC_VER)
  _set_invalid_parameter_handler(my_invalid_parameter_handler);
  _CrtSetReportMode(_CRT_ASSERT, 0);
#endif
  LOG_DEBUG("main: entry");

  C_ORM_GETENV(db_str_buf, sizeof(db_str_buf), "C_ORM_DB_URL");
  if (db_str_buf[0] != '\0') {
    db_str = db_str_buf;
  }

  if (argc < 2) {
    print_usage(argv[0]);
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("main: missing command");
    LOG_DEBUG("main: exit");
    printf("RETURNING RC %d\n", rc);
    goto cleanup;
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
      rc = C_ORM_ERROR_UNKNOWN;
      LOG_DEBUG("main: create requires migration name");
      LOG_DEBUG("main: exit");
      printf("RETURNING RC %d\n", rc);
      goto cleanup;
    }

    C_ORM_SPRINTF(up_file, sizeof(up_file), "%s/%s.up.sql", dir_path, arg_name);
    C_ORM_SPRINTF(down_file, sizeof(down_file), "%s/%s.down.sql", dir_path,
                  arg_name);

    C_ORM_FOPEN(&fp, up_file, "w");
    if (fp) {
      fclose(fp);
      printf("Created %s\n", up_file);
    } else {
      LOG_DEBUG("main: OOM or IO error opening up_file");
    }

    C_ORM_FOPEN(&fp, down_file, "w");
    if (fp) {
      fclose(fp);
      printf("Created %s\n", down_file);
    } else {
      LOG_DEBUG("main: OOM or IO error opening down_file");
    }

  } else if (strcmp(command, "generate") == 0) {
    printf("Schema diff auto-generation is not implemented dynamically via CLI "
           "yet.\n");
    printf("Please use cdd-c code generation or manual SQL creation.\n");
  } else if (strcmp(command, "sql2c") == 0) {
    if (argc < 4) {
      printf("Error: 'sql2c' requires <schema.sql> and <out_dir>.\n");
      rc = C_ORM_ERROR_UNKNOWN;
      LOG_DEBUG("main: sql2c requires arguments");
      LOG_DEBUG("main: exit");
      printf("RETURNING RC %d\n", rc);
      goto cleanup;
    }
    printf("SQL2C CALLED WITH %s AND %s\n", argv[2], argv[3]);
    rc = c_orm_codegen_generate(argv[2], argv[3]);
    printf("CODEGEN RC IS %d\n", rc);
    if (rc != 0) {
      printf("Failed to generate code from schema.\n");
      printf("RETURNING RC %d\n", rc);
      goto cleanup;
    } else {
      printf("Code generation successful.\n");
    }
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
      rc = C_ORM_ERROR_UNKNOWN;
      LOG_DEBUG("main: DB string required for migrate");
      LOG_DEBUG("main: exit");
      printf("RETURNING RC %d\n", rc);
      goto cleanup;
    }

    err = c_orm_sqlite_connect(db_str, &db);
    printf("SQLITE CONNECT RETURNED %d FOR %s\n", err, db_str);
    if (err != C_ORM_OK) {
      printf("Error connecting to database.\n");
      rc = C_ORM_ERROR_UNKNOWN;
      LOG_DEBUG("main: failed connecting to db in migrate");
      LOG_DEBUG("main: exit");
      printf("RETURNING RC %d\n", rc);
      goto cleanup;
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

    if (db->vtable && db->vtable->disconnect)
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
      rc = C_ORM_ERROR_UNKNOWN;
      LOG_DEBUG("main: DB string required for status");
      LOG_DEBUG("main: exit");
      printf("RETURNING RC %d\n", rc);
      goto cleanup;
    }

    err = c_orm_sqlite_connect(db_str, &db);
    printf("SQLITE CONNECT RETURNED %d FOR %s\n", err, db_str);
    if (err != C_ORM_OK) {
      printf("Error connecting to database.\n");
      rc = C_ORM_ERROR_UNKNOWN;
      LOG_DEBUG("main: failed connecting to db in status");
      LOG_DEBUG("main: exit");
      printf("RETURNING RC %d\n", rc);
      goto cleanup;
    }

    c_orm_migration_init_table(db);

    err = c_orm_migration_get_applied(db, &applied, &count);
    if (err == C_ORM_OK) {
      printf("Applied Migrations (%d):\n", (int)count);
      for (j = 0; j < count; j++) {
        printf("  [%s] %s\n", applied[j].version, applied[j].name);
      }
      if (applied) {
        c_orm_migration_free_array(applied, count);
      }
    } else {
      printf("Failed to fetch migration status or no migrations applied.\n");
      if (db->vtable && db->vtable->disconnect)
        db->vtable->disconnect(db);
      rc = C_ORM_ERROR_UNKNOWN;
      LOG_DEBUG("main: failed to fetch migration status");
      LOG_DEBUG("main: exit");
      printf("RETURNING RC %d\n", rc);
      goto cleanup;
    }
    if (db->vtable && db->vtable->disconnect)
      db->vtable->disconnect(db);
  } else {
    printf("Unknown command: %s\n", command);
    print_usage(argv[0]);
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("main: unknown command");
    LOG_DEBUG("main: exit");
    printf("RETURNING RC %d\n", rc);
    goto cleanup;
  }

  rc = C_ORM_OK;
  LOG_DEBUG("main: exit");
  printf("RETURNING RC %d\n", rc);
cleanup:
  return rc;
}

#if defined(__clang__) || defined(__GNUC__)
#endif
