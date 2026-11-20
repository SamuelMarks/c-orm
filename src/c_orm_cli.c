/**
 * @file c_orm_cli.c
 * @brief Command line interface for C-ORM migrations and tools.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_api.h"
#include "c_orm_migrations.h"
#include "c_orm_sqlite.h"
#include "c_orm_postgres.h"
#include "c_orm_mysql.h"
#include "c_orm_memory.h"
#include "c_orm_log.h"
#include "c_orm_codegen.h"
#include "c_orm_copybook.h"
#include "c_orm_string_builder.h"
#include <errno.h>
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
extern __declspec(dllimport) void *__stdcall GetModuleHandleA(const char *);
extern __declspec(dllimport) void *__stdcall GetProcAddress(void *, const char *);
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif
#else
#include <sys/stat.h>
#define MKDIR(path) mkdir(path, 0777)
#endif
/* clang-format on */

/**
 * @brief Prints the usage information.
 * @param prog The program name.
 * @return C_ORM_OK on success.
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
  printf("  cpy2c <input.cpy> <out_dir>  Generate C89 ORM models from COBOL "
         "Copybook\n");
  printf("  cpy2sql <input.cpy> <out.sql> Generate SQL DDL from COBOL "
         "Copybook\n");
  printf("  sql2cpy <schema.sql> <out.cpy> Generate COBOL Copybook / DCLGEN "
         "from SQL DDL\n");
  printf("  migrate                   Apply pending migrations\n");
  printf("  rollback [steps]          Rollback applied migrations\n");
  printf("  status                    Show pending and applied migrations\n\n");
  printf("Options:\n");
  printf("  --db <conn_str>           Database connection string\n");
  printf("  --dir <path>              Migrations directory (default: "
         "./migrations)\n");
  printf("  --format <auto|fixed|free> COBOL Copybook source format\n");
  printf("  --replacing <OLD=NEW>     COBOL Copybook REPLACING substitution\n");
  printf("  --dialect <db2|odbc|sqlite|postgres|mysql> Target SQL dialect\n");
  printf("  --dclgen                  Emit IBM Db2 EXEC SQL DECLARE + 49-level "
         "DCLGEN\n");
  LOG_DEBUG("print_usage: exit");
  return C_ORM_OK;
}

/**
 * @brief Parses a `--format` argument string into `c_orm_cobol_format_t`.
 * @param fmt_str Format option text (`"auto"`, `"fixed"`, or `"free"`).
 * @param out_fmt Output parsed format enum.
 * @return C_ORM_OK on success, or C_ORM_ERROR_VALIDATION if unrecognized.
 */
static c_orm_error_t cli_parse_cobol_format(const char *fmt_str,
                                            c_orm_cobol_format_t *out_fmt) {
  if (strcmp(fmt_str, "auto") == 0) {
    *out_fmt = C_ORM_COBOL_FORMAT_AUTO;
    return C_ORM_OK;
  }
  if (strcmp(fmt_str, "fixed") == 0) {
    *out_fmt = C_ORM_COBOL_FORMAT_FIXED;
    return C_ORM_OK;
  }
  if (strcmp(fmt_str, "free") == 0) {
    *out_fmt = C_ORM_COBOL_FORMAT_FREE;
    return C_ORM_OK;
  }
  return C_ORM_ERROR_VALIDATION;
}

/**
 * @brief Parses a `--dialect` argument string into `c_to_sql_dialect_t`.
 * @param dial_str Dialect option text (`"db2"`, `"odbc"`, `"sqlite"`,
 * `"postgres"`, `"postgresql"`, `"mysql"`).
 * @param out_dial Output parsed SQL dialect enum.
 * @return C_ORM_OK on success, or C_ORM_ERROR_VALIDATION if unrecognized.
 */
static c_orm_error_t cli_parse_sql_dialect(const char *dial_str,
                                           c_to_sql_dialect_t *out_dial) {
  if (strcmp(dial_str, "db2") == 0) {
    *out_dial = C_TO_SQL_DIALECT_DB2;
    return C_ORM_OK;
  }
  if (strcmp(dial_str, "odbc") == 0) {
    *out_dial = C_TO_SQL_DIALECT_ODBC;
    return C_ORM_OK;
  }
  if (strcmp(dial_str, "sqlite") == 0) {
    *out_dial = C_TO_SQL_DIALECT_SQLITE;
    return C_ORM_OK;
  }
  if (strcmp(dial_str, "postgres") == 0 ||
      strcmp(dial_str, "postgresql") == 0) {
    *out_dial = C_TO_SQL_DIALECT_POSTGRESQL;
    return C_ORM_OK;
  }
  if (strcmp(dial_str, "mysql") == 0 || strcmp(dial_str, "mariadb") == 0) {
    *out_dial = C_TO_SQL_DIALECT_MYSQL;
    return C_ORM_OK;
  }
  return C_ORM_ERROR_VALIDATION;
}

/**
 * @brief Frees duplicated `--replacing` buffer strings.
 * @param rep_bufs Array of allocated strings.
 * @param num_reps Number of active entries.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t cli_free_replace_pairs(char **rep_bufs, size_t num_reps) {
  size_t ri;
  for (ri = 0; ri < num_reps; ++ri) {
    (void)c_orm_system_free(rep_bufs[ri]);
  }
  return C_ORM_OK;
}

/**
 * @brief Parses an `OLD=NEW` `--replacing` option into `rep_pairs`.
 * @param spec Raw `OLD=NEW` argument string.
 * @param rep_pairs Destination replacement pair array (capacity 16).
 * @param rep_bufs Backing heap buffer array (capacity 16).
 * @param io_num_reps In/out count of active replacement pairs.
 * @return C_ORM_OK on success, C_ORM_ERROR_OVERFLOW, C_ORM_ERROR_MEMORY, or
 * C_ORM_ERROR_VALIDATION.
 */
static c_orm_error_t cli_add_replace_pair(const char *spec,
                                          c_orm_cobol_replace_pair_t *rep_pairs,
                                          char **rep_bufs,
                                          size_t *io_num_reps) {
  char *eq_pos = NULL;
  char *dup = NULL;
  c_orm_error_t rc;

  if (*io_num_reps >= 16U) {
    return C_ORM_ERROR_OVERFLOW;
  }
  rc = c_orm_strdup(spec, &dup);
  if (rc != C_ORM_OK) {
    return rc;
  }
  eq_pos = strchr(dup, '=');
  if (!eq_pos || eq_pos == dup) {
    (void)c_orm_system_free(dup);
    return C_ORM_ERROR_VALIDATION;
  }
  *eq_pos = '\0';
  rep_bufs[*io_num_reps] = dup;
  rep_pairs[*io_num_reps].from_text = dup;
  rep_pairs[*io_num_reps].to_text = eq_pos + 1;
  (*io_num_reps)++;
  return C_ORM_OK;
}

/**
 * @brief Callback for migration logging.
 * @param msg The message to log.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t log_cb(const char *msg) {
  LOG_DEBUG("log_cb: entry");
  printf("[Migration] %s\n", msg);
  LOG_DEBUG("log_cb: exit");
  return C_ORM_OK;
}

/**
 * @brief Connects to a database driver dispatched by connection URL scheme.
 * @param db_str Connection URL (postgres://, mysql://, memory://, sqlite://, or
 * file path).
 * @param out_db Output pointer receiving connected database handle.
 * @return C_ORM_OK on success, or driver connection error code.
 */
static c_orm_error_t cli_connect_url(const char *db_str, c_orm_db_t **out_db) {
  return c_orm_connect_url(db_str, out_db);
}

/**
 * @brief Executes the C-ORM CLI command dispatcher returning c_orm_error_t.
 * @param argc The argument count.
 * @param argv The argument values.
 * @return C_ORM_OK on success, or an error enum on failure.
 */
c_orm_error_t c_orm_cli_run(int argc, char **argv);

c_orm_error_t c_orm_cli_run(int argc, char **argv) {
  c_orm_error_t rc = C_ORM_OK;
  const char *command = NULL;
  char db_str_buf[1024];
  const char *db_str = NULL;
  const char *dir_path = "./migrations";
  const char *arg_name = NULL;
  int i;

#if defined(_MSC_VER) && defined(_DEBUG)
  if (!GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version")) {
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  }
#endif
#if defined(_WIN32) || defined(_WIN64)
  { SetErrorMode(0x0001 | 0x0002 | 0x8000); }
#endif

#if defined(_MSC_VER)
  _set_invalid_parameter_handler(my_invalid_parameter_handler);
#if defined(_DEBUG)
  if (!GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version")) {
    _CrtSetReportMode(_CRT_ASSERT, 0);
  }
#endif
#endif
  LOG_DEBUG("c_orm_cli_run: entry");

  C_ORM_GETENV(db_str_buf, sizeof(db_str_buf), "C_ORM_DB_URL");
  if (db_str_buf[0] != '\0') {
    db_str = db_str_buf;
  }

  if (argc < 2) {
    print_usage(argv[0]);
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_cli_run: missing command");
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
    errno = 0;
    if (MKDIR(dir_path) == 0) {
      printf("Created migrations directory at '%s'\n", dir_path);
    } else if (errno == EEXIST) {
      printf("Directory '%s' already exists\n", dir_path);
    } else {
      printf("Could not create directory '%s'\n", dir_path);
      rc = C_ORM_ERROR_IO;
      goto cleanup;
    }
  } else if (strcmp(command, "create") == 0) {
    char up_file[512];
    char down_file[512];
    FILE *fp = NULL;

    if (!arg_name) {
      printf("Error: 'create' requires a migration name.\n");
      rc = C_ORM_ERROR_UNKNOWN;
      goto cleanup;
    }

    rc = C_ORM_SPRINTF(up_file, sizeof(up_file), "%s/%s.up.sql", dir_path,
                       arg_name);
    if (rc != C_ORM_OK) {
      goto cleanup;
    }
    rc = C_ORM_SPRINTF(down_file, sizeof(down_file), "%s/%s.down.sql", dir_path,
                       arg_name);
    if (rc != C_ORM_OK) {
      goto cleanup;
    }

    rc = C_ORM_FOPEN(&fp, up_file, "w");
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_cli_run: IO error opening up_file");
      rc = C_ORM_ERROR_IO;
      goto cleanup;
    }
    fclose(fp);
    fp = NULL;
    printf("Created %s\n", up_file);

    rc = C_ORM_FOPEN(&fp, down_file, "w");
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_cli_run: IO error opening down_file");
      rc = C_ORM_ERROR_IO;
      goto cleanup;
    }
    fclose(fp);
    printf("Created %s\n", down_file);

  } else if (strcmp(command, "generate") == 0) {
    printf("Error: Schema diff auto-generation is not implemented.\n");
    rc = C_ORM_ERROR_NOT_IMPLEMENTED;
    goto cleanup;
  } else if (strcmp(command, "sql2c") == 0) {
    if (argc < 4) {
      printf("Error: 'sql2c' requires <schema.sql> and <out_dir>.\n");
      rc = C_ORM_ERROR_UNKNOWN;
      goto cleanup;
    }
    rc = c_orm_codegen_generate(argv[2], argv[3]);
    if (rc != C_ORM_OK) {
      printf("Failed to generate code from schema.\n");
      goto cleanup;
    }
    printf("Code generation successful.\n");
  } else if (strcmp(command, "cpy2c") == 0) {
    c_orm_cobol_format_t cpy_fmt = C_ORM_COBOL_FORMAT_AUTO;
    c_orm_cobol_replace_pair_t rep_pairs[16];
    char *rep_bufs[16];
    size_t num_reps = 0;
    int opt_i;
    c_orm_cobol_record_t *rec = NULL;

    if (argc < 4 || argv[2][0] == '-' || argv[3][0] == '-') {
      printf("Error: 'cpy2c' requires <input.cpy> and <out_dir>.\n");
      rc = C_ORM_ERROR_UNKNOWN;
      goto cleanup;
    }
    for (opt_i = 4; opt_i < argc; ++opt_i) {
      if (strcmp(argv[opt_i], "--format") == 0 && opt_i + 1 < argc) {
        rc = cli_parse_cobol_format(argv[++opt_i], &cpy_fmt);
        if (rc != C_ORM_OK) {
          cli_free_replace_pairs(rep_bufs, num_reps);
          goto cleanup;
        }
      } else if (strcmp(argv[opt_i], "--replacing") == 0 && opt_i + 1 < argc) {
        rc =
            cli_add_replace_pair(argv[++opt_i], rep_pairs, rep_bufs, &num_reps);
        if (rc != C_ORM_OK) {
          cli_free_replace_pairs(rep_bufs, num_reps);
          goto cleanup;
        }
      } else {
        cli_free_replace_pairs(rep_bufs, num_reps);
        rc = C_ORM_ERROR_VALIDATION;
        goto cleanup;
      }
    }

    rc = c_orm_copybook_parse_file(
        argv[2], cpy_fmt, num_reps > 0 ? rep_pairs : NULL, num_reps, &rec);
    cli_free_replace_pairs(rep_bufs, num_reps);
    if (rc != C_ORM_OK) {
      printf("Failed to parse COBOL Copybook (rc=%d).\n", (int)rc);
      goto cleanup;
    }
    (void)MKDIR(argv[3]);
    rc = c_orm_copybook_generate_c_files(rec, argv[3]);
    (void)c_orm_copybook_record_free(rec);
    if (rc != C_ORM_OK) {
      printf("Failed to generate C89 files from Copybook (rc=%d).\n", (int)rc);
      goto cleanup;
    }
    printf("COBOL Copybook C89 code generation successful.\n");
  } else if (strcmp(command, "cpy2sql") == 0) {
    c_orm_cobol_format_t cpy_fmt = C_ORM_COBOL_FORMAT_AUTO;
    c_to_sql_dialect_t sql_dial = C_TO_SQL_DIALECT_DB2;
    c_orm_cobol_replace_pair_t rep_pairs[16];
    char *rep_bufs[16];
    size_t num_reps = 0;
    int opt_i;
    c_orm_cobol_record_t *rec = NULL;
    char *sql_out = NULL;
    FILE *fp = NULL;

    if (argc < 4 || argv[2][0] == '-' || argv[3][0] == '-') {
      printf("Error: 'cpy2sql' requires <input.cpy> and <output.sql>.\n");
      rc = C_ORM_ERROR_UNKNOWN;
      goto cleanup;
    }
    for (opt_i = 4; opt_i < argc; ++opt_i) {
      if (strcmp(argv[opt_i], "--dialect") == 0 && opt_i + 1 < argc) {
        rc = cli_parse_sql_dialect(argv[++opt_i], &sql_dial);
        if (rc != C_ORM_OK) {
          cli_free_replace_pairs(rep_bufs, num_reps);
          goto cleanup;
        }
      } else if (strcmp(argv[opt_i], "--format") == 0 && opt_i + 1 < argc) {
        rc = cli_parse_cobol_format(argv[++opt_i], &cpy_fmt);
        if (rc != C_ORM_OK) {
          cli_free_replace_pairs(rep_bufs, num_reps);
          goto cleanup;
        }
      } else if (strcmp(argv[opt_i], "--replacing") == 0 && opt_i + 1 < argc) {
        rc =
            cli_add_replace_pair(argv[++opt_i], rep_pairs, rep_bufs, &num_reps);
        if (rc != C_ORM_OK) {
          cli_free_replace_pairs(rep_bufs, num_reps);
          goto cleanup;
        }
      } else {
        cli_free_replace_pairs(rep_bufs, num_reps);
        rc = C_ORM_ERROR_VALIDATION;
        goto cleanup;
      }
    }

    rc = c_orm_copybook_parse_file(
        argv[2], cpy_fmt, num_reps > 0 ? rep_pairs : NULL, num_reps, &rec);
    cli_free_replace_pairs(rep_bufs, num_reps);
    if (rc != C_ORM_OK) {
      printf("Failed to parse COBOL Copybook (rc=%d).\n", (int)rc);
      goto cleanup;
    }
    rc = c_orm_copybook_emit_sql_ddl(rec, sql_dial, &sql_out);
    (void)c_orm_copybook_record_free(rec);
    if (rc != C_ORM_OK) {
      goto cleanup;
    }
    rc = C_ORM_FOPEN(&fp, argv[3], "wb");
    if (rc != C_ORM_OK) {
      (void)c_orm_system_free(sql_out);
      rc = C_ORM_ERROR_IO;
      goto cleanup;
    }
    fputs(sql_out, fp);
    fclose(fp);
    (void)c_orm_system_free(sql_out);
    printf("COBOL Copybook SQL DDL generation successful.\n");
  } else if (strcmp(command, "sql2cpy") == 0) {
    c_orm_cobol_format_t cpy_fmt = C_ORM_COBOL_FORMAT_FIXED;
    int emit_dclgen = 0;
    int opt_i;
    FILE *fp = NULL;
    char *cpy_out = NULL;

    if (argc < 4 || argv[2][0] == '-' || argv[3][0] == '-') {
      printf("Error: 'sql2cpy' requires <schema.sql> and <output.cpy>.\n");
      rc = C_ORM_ERROR_UNKNOWN;
      goto cleanup;
    }
    for (opt_i = 4; opt_i < argc; ++opt_i) {
      if (strcmp(argv[opt_i], "--dclgen") == 0) {
        emit_dclgen = 1;
      } else if (strcmp(argv[opt_i], "--format") == 0 && opt_i + 1 < argc) {
        rc = cli_parse_cobol_format(argv[++opt_i], &cpy_fmt);
        if (rc != C_ORM_OK || cpy_fmt == C_ORM_COBOL_FORMAT_AUTO) {
          rc = C_ORM_ERROR_VALIDATION;
          goto cleanup;
        }
      } else {
        rc = C_ORM_ERROR_VALIDATION;
        goto cleanup;
      }
    }

    rc = C_ORM_FOPEN(&fp, argv[2], "rb");
    if (rc != C_ORM_OK) {
      rc = C_ORM_ERROR_IO;
      goto cleanup;
    }
    {
      char chunk[1024];
      size_t got;
      c_orm_string_builder_t *sb = NULL;
      const char *sql_text = NULL;
      rc = c_orm_string_builder_init(&sb);
      if (rc != C_ORM_OK) {
        fclose(fp);
        goto cleanup;
      }
      while ((got = fread(chunk, 1, sizeof(chunk) - 1, fp)) > 0) {
        chunk[got] = '\0';
        rc = c_orm_string_builder_append(sb, chunk);
        if (rc != C_ORM_OK) {
          (void)c_orm_string_builder_free(sb);
          fclose(fp);
          goto cleanup;
        }
      }
      fclose(fp);
      fp = NULL;
      c_orm_string_builder_get(sb, &sql_text);
      rc = c_orm_sql_to_copybook_emit(sql_text, cpy_fmt, emit_dclgen, &cpy_out);
      (void)c_orm_string_builder_free(sb);
    }
    if (rc != C_ORM_OK) {
      printf("Failed to generate COBOL Copybook from SQL (rc=%d).\n", (int)rc);
      goto cleanup;
    }
    rc = C_ORM_FOPEN(&fp, argv[3], "wb");
    if (rc != C_ORM_OK) {
      (void)c_orm_system_free(cpy_out);
      rc = C_ORM_ERROR_IO;
      goto cleanup;
    }
    fputs(cpy_out, fp);
    fclose(fp);
    (void)c_orm_system_free(cpy_out);
    printf("SQL to COBOL Copybook generation successful.\n");
  } else if (strcmp(command, "migrate") == 0) {
    c_orm_db_t *db = NULL;
    c_orm_error_t err;
    c_orm_migration_options_t opts;
    c_orm_migration_t *migs = NULL;
    size_t migs_count = 0;

    if (!db_str) {
      printf("Error: Database connection string required (--db or C_ORM_DB_URL "
             "env)\n");
      rc = C_ORM_ERROR_UNKNOWN;
      goto cleanup;
    }

    err = cli_connect_url(db_str, &db);
    if (err != C_ORM_OK) {
      printf("Error connecting to database.\n");
      rc = C_ORM_ERROR_UNKNOWN;
      goto cleanup;
    }

    memset(&opts, 0, sizeof(opts));
    opts.log_cb = log_cb;

    err = c_orm_migration_load_dir(dir_path, &migs, &migs_count);
    if (err == C_ORM_OK && migs_count > 0) {
      rc = c_orm_migrate_all(db, migs, migs_count, &opts);
      c_orm_migration_free_array(migs, migs_count);
    } else {
      printf("No pending migrations found in %s\n", dir_path);
    }

    db->vtable->disconnect(db);
    if (rc != C_ORM_OK) {
      goto cleanup;
    }
  } else if (strcmp(command, "rollback") == 0) {
    c_orm_db_t *db = NULL;
    c_orm_error_t err;
    c_orm_migration_options_t opts;
    c_orm_migration_t *migs = NULL;
    size_t migs_count = 0;
    size_t steps = 1;

    if (!db_str) {
      printf("Error: Database connection string required (--db or C_ORM_DB_URL "
             "env)\n");
      rc = C_ORM_ERROR_UNKNOWN;
      goto cleanup;
    }

    if (arg_name && arg_name[0] != '-') {
      long parsed = strtol(arg_name, NULL, 10);
      if (parsed > 0) {
        steps = (size_t)parsed;
      }
    }

    err = cli_connect_url(db_str, &db);
    if (err != C_ORM_OK) {
      printf("Error connecting to database.\n");
      rc = err;
      goto cleanup;
    }

    memset(&opts, 0, sizeof(opts));
    opts.log_cb = log_cb;

    err = c_orm_migration_load_dir(dir_path, &migs, &migs_count);
    if (err == C_ORM_OK && migs_count > 0) {
      err = c_orm_migrate_rollback(db, migs, migs_count, steps, &opts);
      if (err != C_ORM_OK) {
        printf("Rollback failed.\n");
        rc = err;
      } else {
        printf("Rollback completed.\n");
      }
      c_orm_migration_free_array(migs, migs_count);
    } else {
      printf("No migrations found in %s to rollback.\n", dir_path);
    }

    db->vtable->disconnect(db);
    if (rc != C_ORM_OK) {
      goto cleanup;
    }
  } else if (strcmp(command, "status") == 0) {
    c_orm_db_t *db = NULL;
    c_orm_error_t err;
    c_orm_migration_t *applied = NULL;
    size_t count = 0;
    size_t j;

    if (!db_str) {
      printf("Error: Database connection string required (--db or C_ORM_DB_URL "
             "env)\n");
      rc = C_ORM_ERROR_UNKNOWN;
      goto cleanup;
    }

    err = cli_connect_url(db_str, &db);
    if (err != C_ORM_OK) {
      printf("Error connecting to database.\n");
      rc = C_ORM_ERROR_UNKNOWN;
      goto cleanup;
    }

    err = c_orm_migration_init_table(db);
    if (err != C_ORM_OK) {
      db->vtable->disconnect(db);
      rc = err;
      goto cleanup;
    }

    err = c_orm_migration_get_applied(db, &applied, &count);
    if (err == C_ORM_OK) {
      printf("Applied Migrations (%d):\n", (int)count);
      for (j = 0; j < count; j++) {
        printf("  [%s] %s\n", applied[j].version, applied[j].name);
      }
      c_orm_migration_free_array(applied, count);
    } else {
      printf("Failed to fetch migration status or no migrations applied.\n");
      db->vtable->disconnect(db);
      rc = C_ORM_ERROR_UNKNOWN;
      goto cleanup;
    }
    db->vtable->disconnect(db);
  } else {
    printf("Unknown command: %s\n", command);
    print_usage(argv[0]);
    rc = C_ORM_ERROR_UNKNOWN;
    goto cleanup;
  }

  rc = C_ORM_OK;
cleanup:
  LOG_DEBUG("c_orm_cli_run: exit");
  return rc;
}

#ifndef C_ORM_CLI_NO_MAIN
/**
 * @brief Main entry point for the CLI executable.
 * @param argc The argument count.
 * @param argv The argument values.
 * @return 0 on success, non-zero error code on failure.
 */
int main(int argc, char **argv) { return (int)c_orm_cli_run(argc, argv); }
#endif
