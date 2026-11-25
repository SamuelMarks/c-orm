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
#include "c_orm_sql.h"
#include "c_orm_string_builder.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32) || defined(_WIN64)
extern __declspec(dllimport) unsigned int __stdcall SetErrorMode(unsigned int);
extern __declspec(dllimport) void *__stdcall GetModuleHandleA(const char *);
extern __declspec(dllimport) void *__stdcall GetProcAddress(void *,
                                                            const char *);
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif
#else
#include <sys/stat.h>
#define MKDIR(path) mkdir(path, 0777)
#endif
/* clang-format on */

/**
 * @brief Prints the usage information and returns unknown command status.
 * @param prog The program name.
 * @return C_ORM_ERROR_UNKNOWN.
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
  return C_ORM_ERROR_UNKNOWN;
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
 * @return C_ORM_OK.
 */
static c_orm_error_t cli_replace_pairs_free(char **rep_bufs, size_t num_reps) {
  size_t ri;
  for (ri = 0; ri < num_reps; ++ri) {
    c_orm_system_free(rep_bufs[ri]);
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
    c_orm_system_free(dup);
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
 * @brief Checks if `table_name` (and optionally `col_name`) already exists in
 * any `CREATE TABLE` DDL across `migs`.
 * @param migs Loaded migration array (may be NULL).
 * @param migs_count Number of loaded migrations.
 * @param table_name Target table name.
 * @param col_name Column name to search, or NULL to test table existence.
 * @param out_found Output flag set to 1 if found, 0 otherwise.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t cli_check_col_in_migrations(const c_orm_migration_t *migs,
                                                 size_t migs_count,
                                                 const char *table_name,
                                                 const char *col_name,
                                                 int *out_found) {
  size_t m;
  c_orm_error_t rc;
  *out_found = 0;
  for (m = 0; m < migs_count; ++m) {
    struct sql_table_t *tables = NULL;
    size_t n_tables = 0;
    size_t t;
    if (!migs[m].up_sql) {
      continue;
    }
    rc = parse_sql_ddl(migs[m].up_sql, &tables, &n_tables);
    if (rc != C_ORM_OK) {
      return rc;
    }
    for (t = 0; t < n_tables; ++t) {
      if (strcmp(tables[t].name, table_name) == 0) {
        size_t c;
        if (!col_name) {
          size_t k;
          for (k = 0; k < n_tables; ++k) {
            sql_table_free(&tables[k]);
          }
          c_orm_system_free(tables);
          *out_found = 1;
          return C_ORM_OK;
        }
        for (c = 0; c < tables[t].n_columns; ++c) {
          if (strcmp(tables[t].columns[c].name, col_name) == 0) {
            size_t k;
            for (k = 0; k < n_tables; ++k) {
              sql_table_free(&tables[k]);
            }
            c_orm_system_free(tables);
            *out_found = 1;
            return C_ORM_OK;
          }
        }
      }
    }
    for (t = 0; t < n_tables; ++t) {
      sql_table_free(&tables[t]);
    }
    c_orm_system_free(tables);
  }
  return C_ORM_OK;
}

/**
 * @brief Disconnects `db` during cleanup.
 * @param db Database connection handle.
 * @return C_ORM_OK on success, or driver disconnect error code.
 */
static c_orm_error_t cli_conn_cleanup_free(c_orm_db_t *db) {
  return db->vtable->disconnect(db);
}

/**
 * @brief Releases an inspected table schema during cleanup.
 * @param schema Table schema metadata pointer.
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t cli_schema_cleanup_free(cdd_c_meta_t *schema) {
  return c_orm_migration_free_table_schema(schema);
}

/**
 * @brief Releases a loaded migration array during cleanup.
 * @param migs Migration array pointer.
 * @param count Number of elements in `migs`.
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t cli_migs_cleanup_free(c_orm_migration_t *migs,
                                           size_t count) {
  return c_orm_migration_free_array(migs, count);
}

/**
 * @brief Introspects `table_name` on `db_str`, diffs against existing SQL
 * migrations in `dir_path`, and writes `.up.sql` and `.down.sql` files.
 * @param db_str Database connection URL.
 * @param dir_path Target migrations directory.
 * @param table_name Table name to introspect.
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t cli_generate_migration(const char *db_str,
                                            const char *dir_path,
                                            const char *table_name) {
  c_orm_db_t *db = NULL;
  cdd_c_meta_t *schema = NULL;
  c_orm_migration_t *migs = NULL;
  size_t migs_count = 0;
  char up_file[512];
  char down_file[512];
  char ts_buf[32];
  unsigned long now_val;
  char rev[24];
  size_t rlen = 0;
  size_t tlen = 0;
  FILE *fp_up = NULL;
  FILE *fp_down = NULL;
  size_t i;
  int has_existing_table = 0;
  c_orm_error_t rc;

  if (!table_name || !table_name[0]) {
    printf("Error: 'generate' requires a table name.\n");
    return C_ORM_ERROR_VALIDATION;
  }
  if (!db_str) {
    printf("Error: Database connection string required (--db or C_ORM_DB_URL "
           "env)\n");
    return C_ORM_ERROR_UNKNOWN;
  }

  rc = c_orm_connect_url(db_str, &db);
  if (rc != C_ORM_OK) {
    printf("Error connecting to database.\n");
    return rc;
  }

  rc = c_orm_migration_fetch_table_schema(db, table_name, &schema);
  if (rc != C_ORM_OK) {
    cli_conn_cleanup_free(db);
    return rc;
  }
  cli_conn_cleanup_free(db);

  if (schema->num_props == 0) {
    cli_schema_cleanup_free(schema);
    return C_ORM_ERROR_NOT_FOUND;
  }

  rc = c_orm_migration_load_dir(dir_path, &migs, &migs_count);
  if (rc != C_ORM_OK) {
    if (rc == C_ORM_ERROR_MEMORY) {
      cli_schema_cleanup_free(schema);
      return rc;
    }
    migs = NULL;
    migs_count = 0;
  }

  if (migs_count > 0) {
    rc = cli_check_col_in_migrations(migs, migs_count, table_name, NULL,
                                     &has_existing_table);
    if (rc != C_ORM_OK) {
      goto gen_cleanup;
    }
  }

  MKDIR(dir_path);

  now_val = (unsigned long)time(NULL);
  do {
    rev[rlen++] = (char)('0' + (now_val % 10UL));
    now_val /= 10UL;
  } while (now_val > 0UL);
  while (rlen > 0U) {
    ts_buf[tlen++] = rev[--rlen];
  }
  ts_buf[tlen] = '\0';

  rc = C_ORM_SPRINTF(up_file, sizeof(up_file), "%s/%s_auto_%s.up.sql", dir_path,
                     ts_buf, table_name);
  if (rc != C_ORM_OK) {
    goto gen_cleanup;
  }
  rc = C_ORM_SPRINTF(down_file, sizeof(down_file), "%s/%s_auto_%s.down.sql",
                     dir_path, ts_buf, table_name);
  if (rc != C_ORM_OK) {
    goto gen_cleanup;
  }

  rc = C_ORM_FOPEN(&fp_up, up_file, "wb");
  if (rc != C_ORM_OK) {
    goto gen_cleanup;
  }
  rc = C_ORM_FOPEN(&fp_down, down_file, "wb");
  if (rc != C_ORM_OK) {
    fclose(fp_up);
    goto gen_cleanup;
  }

  if (has_existing_table) {
    for (i = 0; i < schema->num_props; ++i) {
      int has_col = 0;
      rc = cli_check_col_in_migrations(migs, migs_count, table_name,
                                       schema->props[i].name, &has_col);
      if (rc != C_ORM_OK) {
        fclose(fp_up);
        fclose(fp_down);
        goto gen_cleanup;
      }
      if (!has_col) {
        fprintf(fp_up, "ALTER TABLE %s ADD COLUMN %s %s;\n", table_name,
                schema->props[i].name, schema->props[i].type);
        fprintf(fp_down, "ALTER TABLE %s DROP COLUMN %s;\n", table_name,
                schema->props[i].name);
      }
    }
  } else {
    fprintf(fp_up, "CREATE TABLE %s (\n", table_name);
    for (i = 0; i < schema->num_props; ++i) {
      fprintf(fp_up, "  %s %s%s\n", schema->props[i].name,
              schema->props[i].type, i + 1 < schema->num_props ? "," : "");
    }
    fprintf(fp_up, ");\n");
    fprintf(fp_down, "DROP TABLE %s;\n", table_name);
  }

  fclose(fp_up);
  fclose(fp_down);
  printf("Generated %s and %s\n", up_file, down_file);
  rc = C_ORM_OK;

gen_cleanup:;
  cli_migs_cleanup_free(migs, migs_count);
  cli_schema_cleanup_free(schema);
  return rc;
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

#if defined(_MSC_VER) && defined(_DEBUG)
  if (!GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version")) {
    _CrtSetReportMode(_CRT_ASSERT, 0);
  }
#endif
  LOG_DEBUG("c_orm_cli_run: entry");

  db_str_buf[0] = '\0';
  rc = C_ORM_GETENV(db_str_buf, sizeof(db_str_buf), "C_ORM_DB_URL");
  if (rc != C_ORM_OK) {
    goto cleanup;
  }
  if (db_str_buf[0] != '\0') {
    db_str = db_str_buf;
  }

  if (argc < 2) {
    LOG_DEBUG("c_orm_cli_run: missing command");
    return print_usage(argv[0]);
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
    rc = cli_generate_migration(db_str, dir_path, arg_name);
    if (rc != C_ORM_OK) {
      goto cleanup;
    }
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
          cli_replace_pairs_free(rep_bufs, num_reps);
          goto cleanup;
        }
      } else if (strcmp(argv[opt_i], "--replacing") == 0 && opt_i + 1 < argc) {
        rc =
            cli_add_replace_pair(argv[++opt_i], rep_pairs, rep_bufs, &num_reps);
        if (rc != C_ORM_OK) {
          cli_replace_pairs_free(rep_bufs, num_reps);
          goto cleanup;
        }
      } else {
        cli_replace_pairs_free(rep_bufs, num_reps);
        rc = C_ORM_ERROR_VALIDATION;
        goto cleanup;
      }
    }

    rc = c_orm_copybook_parse_file(
        argv[2], cpy_fmt, num_reps > 0 ? rep_pairs : NULL, num_reps, &rec);
    if (rc != C_ORM_OK) {
      cli_replace_pairs_free(rep_bufs, num_reps);
      printf("Failed to parse COBOL Copybook (rc=%d).\n", (int)rc);
      goto cleanup;
    }
    cli_replace_pairs_free(rep_bufs, num_reps);
    MKDIR(argv[3]);
    rc = c_orm_copybook_generate_c_files(rec, argv[3]);
    if (rc != C_ORM_OK) {
      c_orm_copybook_record_free(rec);
      printf("Failed to generate C89 files from Copybook (rc=%d).\n", (int)rc);
      goto cleanup;
    }
    c_orm_copybook_record_free(rec);
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
          cli_replace_pairs_free(rep_bufs, num_reps);
          goto cleanup;
        }
      } else if (strcmp(argv[opt_i], "--format") == 0 && opt_i + 1 < argc) {
        rc = cli_parse_cobol_format(argv[++opt_i], &cpy_fmt);
        if (rc != C_ORM_OK) {
          cli_replace_pairs_free(rep_bufs, num_reps);
          goto cleanup;
        }
      } else if (strcmp(argv[opt_i], "--replacing") == 0 && opt_i + 1 < argc) {
        rc =
            cli_add_replace_pair(argv[++opt_i], rep_pairs, rep_bufs, &num_reps);
        if (rc != C_ORM_OK) {
          cli_replace_pairs_free(rep_bufs, num_reps);
          goto cleanup;
        }
      } else {
        cli_replace_pairs_free(rep_bufs, num_reps);
        rc = C_ORM_ERROR_VALIDATION;
        goto cleanup;
      }
    }

    rc = c_orm_copybook_parse_file(
        argv[2], cpy_fmt, num_reps > 0 ? rep_pairs : NULL, num_reps, &rec);
    if (rc != C_ORM_OK) {
      cli_replace_pairs_free(rep_bufs, num_reps);
      printf("Failed to parse COBOL Copybook (rc=%d).\n", (int)rc);
      goto cleanup;
    }
    cli_replace_pairs_free(rep_bufs, num_reps);
    rc = c_orm_copybook_emit_sql_ddl(rec, sql_dial, &sql_out);
    if (rc != C_ORM_OK) {
      c_orm_copybook_record_free(rec);
      goto cleanup;
    }
    c_orm_copybook_record_free(rec);
    rc = C_ORM_FOPEN(&fp, argv[3], "wb");
    if (rc != C_ORM_OK) {
      c_orm_system_free(sql_out);
      rc = C_ORM_ERROR_IO;
      goto cleanup;
    }
    fputs(sql_out, fp);
    fclose(fp);
    c_orm_system_free(sql_out);
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
        if (rc != C_ORM_OK) {
          goto cleanup;
        }
        if (cpy_fmt == C_ORM_COBOL_FORMAT_AUTO) {
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
          c_orm_string_builder_free(sb);
          fclose(fp);
          goto cleanup;
        }
      }
      fclose(fp);
      fp = NULL;
      rc = c_orm_string_builder_get(sb, &sql_text);
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        goto cleanup;
      }
      rc = c_orm_sql_to_copybook_emit(sql_text, cpy_fmt, emit_dclgen, &cpy_out);
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        printf("Failed to generate COBOL Copybook from SQL (rc=%d).\n",
               (int)rc);
        goto cleanup;
      }
      c_orm_string_builder_free(sb);
    }
    rc = C_ORM_FOPEN(&fp, argv[3], "wb");
    if (rc != C_ORM_OK) {
      c_orm_system_free(cpy_out);
      rc = C_ORM_ERROR_IO;
      goto cleanup;
    }
    fputs(cpy_out, fp);
    fclose(fp);
    c_orm_system_free(cpy_out);
    printf("SQL to COBOL Copybook generation successful.\n");
  } else if (strcmp(command, "migrate") == 0) {
    c_orm_db_t *db = NULL;
    c_orm_migration_options_t opts;
    c_orm_migration_t *migs = NULL;
    size_t migs_count = 0;

    if (!db_str) {
      printf("Error: Database connection string required (--db or C_ORM_DB_URL "
             "env)\n");
      rc = C_ORM_ERROR_UNKNOWN;
      goto cleanup;
    }

    rc = c_orm_connect_url(db_str, &db);
    if (rc != C_ORM_OK) {
      printf("Error connecting to database.\n");
      goto cleanup;
    }

    memset(&opts, 0, sizeof(opts));
    opts.log_cb = log_cb;

    rc = c_orm_migration_load_dir(dir_path, &migs, &migs_count);
    if (rc != C_ORM_OK) {
      if (rc == C_ORM_ERROR_MEMORY) {
        cli_conn_cleanup_free(db);
        goto cleanup;
      }
      migs = NULL;
      migs_count = 0;
    }
    if (migs_count > 0) {
      rc = c_orm_migrate_all(db, migs, migs_count, &opts);
      if (rc != C_ORM_OK) {
        cli_migs_cleanup_free(migs, migs_count);
        cli_conn_cleanup_free(db);
        goto cleanup;
      }
      cli_migs_cleanup_free(migs, migs_count);
    } else {
      printf("No pending migrations found in %s\n", dir_path);
    }
    return db->vtable->disconnect(db);
  } else if (strcmp(command, "rollback") == 0) {
    c_orm_db_t *db = NULL;
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

    rc = c_orm_connect_url(db_str, &db);
    if (rc != C_ORM_OK) {
      printf("Error connecting to database.\n");
      goto cleanup;
    }

    memset(&opts, 0, sizeof(opts));
    opts.log_cb = log_cb;

    rc = c_orm_migration_load_dir(dir_path, &migs, &migs_count);
    if (rc != C_ORM_OK) {
      if (rc == C_ORM_ERROR_MEMORY) {
        cli_conn_cleanup_free(db);
        goto cleanup;
      }
      migs = NULL;
      migs_count = 0;
    }
    if (migs_count > 0) {
      rc = c_orm_migrate_rollback(db, migs, migs_count, steps, &opts);
      if (rc != C_ORM_OK) {
        printf("Rollback failed.\n");
        cli_migs_cleanup_free(migs, migs_count);
        cli_conn_cleanup_free(db);
        goto cleanup;
      }
      printf("Rollback completed.\n");
      cli_migs_cleanup_free(migs, migs_count);
    } else {
      printf("No migrations found in %s to rollback.\n", dir_path);
    }
    return db->vtable->disconnect(db);
  } else if (strcmp(command, "status") == 0) {
    c_orm_db_t *db = NULL;
    c_orm_migration_t *applied = NULL;
    size_t count = 0;
    size_t j;

    if (!db_str) {
      printf("Error: Database connection string required (--db or C_ORM_DB_URL "
             "env)\n");
      rc = C_ORM_ERROR_UNKNOWN;
      goto cleanup;
    }

    rc = c_orm_connect_url(db_str, &db);
    if (rc != C_ORM_OK) {
      printf("Error connecting to database.\n");
      goto cleanup;
    }

    rc = c_orm_migration_init_table(db);
    if (rc != C_ORM_OK) {
      cli_conn_cleanup_free(db);
      goto cleanup;
    }

    rc = c_orm_migration_get_applied(db, &applied, &count);
    if (rc != C_ORM_OK) {
      printf("Failed to fetch migration status or no migrations applied.\n");
      cli_conn_cleanup_free(db);
      goto cleanup;
    }
    printf("Applied Migrations (%d):\n", (int)count);
    for (j = 0; j < count; j++) {
      printf("  [%s] %s\n", applied[j].version, applied[j].name);
    }
    cli_migs_cleanup_free(applied, count);
    return db->vtable->disconnect(db);
  } else {
    printf("Unknown command: %s\n", command);
    return print_usage(argv[0]);
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
int main(int argc, char **argv) {
#if defined(_MSC_VER) && defined(_DEBUG)
  _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
  return (int)c_orm_cli_run(argc, argv);
}
#endif
