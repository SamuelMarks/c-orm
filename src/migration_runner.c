/**
 * @file migration_runner.c
 * @brief Unified cross-driver SQL migration runner built on `c_orm_db_t`.
 *
 * Supports PostgreSQL (`C_ORM_POSTGRESQL_LINKED` / `C_ORM_POSTGRESQL_DYNAMIC`),
 * SQLite, MySQL, Db2, ODBC, and in-memory databases via `DATABASE_URL` (or
 * `C_ORM_DB_URL`), and generates boilerplate migration files unconditionally
 * without requiring `libpq` at compile time.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "migration_runner.h"
#include "c_orm_api.h"
#include "c_orm_migrations.h"
#include "migration.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32) || defined(_WIN64)
#include <direct.h>
#define MIG_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MIG_MKDIR(p) mkdir(p, 0777)
#endif
/* clang-format on */

/** @brief Optional connection URL override set during `setup_test_database`. */
static const char *g_override_db_url = NULL;

/** @brief Optional pre-built mock DB handle used by unit tests. */
C_ORM_EXPORT c_orm_db_t *c_orm_mock_mig_runner_db = NULL;

/**
 * @brief Resolves `g_override_db_url`, `DATABASE_URL`, or `C_ORM_DB_URL` and
 * opens a unified `c_orm_db_t` connection.
 * @param out_db Output pointer receiving connected database handle.
 * @return C_ORM_OK on success, `C_ORM_ERROR_VALIDATION` when unset, or
 * connection error code.
 */
static c_orm_error_t mig_runner_connect_env(c_orm_db_t **out_db) {
  char db_url[256];
  const char *target = g_override_db_url;
  c_orm_error_t rc;

  if (c_orm_mock_mig_runner_db) {
    *out_db = c_orm_mock_mig_runner_db;
    return C_ORM_OK;
  }

  if (!target) {
    db_url[0] = '\0';
    rc = C_ORM_GETENV(db_url, sizeof(db_url), "DATABASE_URL");
    if (rc != C_ORM_OK) {
      return rc;
    }
    if (db_url[0] == '\0') {
      rc = C_ORM_GETENV(db_url, sizeof(db_url), "C_ORM_DB_URL");
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
    if (db_url[0] == '\0') {
      return C_ORM_ERROR_VALIDATION;
    }
    target = db_url;
  }

#if !defined(C_ORM_POSTGRESQL_LINKED) && !defined(C_ORM_POSTGRESQL_DYNAMIC)
  if (strncmp(target, "postgres://", 11) == 0 ||
      strncmp(target, "postgresql://", 13) == 0) {
    return C_ORM_ERROR_NOT_IMPLEMENTED;
  }
#endif

  return c_orm_connect_url(target, out_db);
}

/**
 * @brief Reads an entire SQL file into a `c-orm` heap buffer.
 * @param filepath Path to the SQL file.
 * @param out_sql Output pointer receiving NUL-terminated file contents.
 * @return C_ORM_OK on success, or IO / memory error code.
 */
static c_orm_error_t mig_runner_read_file(const char *filepath,
                                          char **out_sql) {
  FILE *fp = NULL;
  long fsize;
  size_t ulen;
  char *buf = NULL;
  size_t nread;
  c_orm_error_t rc;

  rc = C_ORM_FOPEN(&fp, filepath, "rb");
  if (rc != C_ORM_OK) {
    return rc;
  }
  fseek(fp, 0, SEEK_END);
  fsize = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  ulen = fsize > 0 ? (size_t)fsize : 0;

  rc = c_orm_system_malloc(ulen + 1, (void **)&buf);
  if (rc != C_ORM_OK) {
    fclose(fp);
    return rc;
  }

  nread = ulen > 0 ? fread(buf, 1, ulen, fp) : 0;
  fclose(fp);
  buf[nread] = '\0';
  *out_sql = buf;
  return C_ORM_OK;
}

/**
 * @brief Disconnects `db` during error cleanup.
 * @param db Database connection handle.
 * @return C_ORM_OK on success, or driver disconnect error code.
 */
static c_orm_error_t mig_runner_conn_cleanup_free(c_orm_db_t *db) {
  return db->vtable->disconnect(db);
}

/**
 * @brief Finalizes `q` during error cleanup.
 * @param db Database connection handle.
 * @param q Prepared statement handle.
 * @return C_ORM_OK on success, or driver finalize error code.
 */
static c_orm_error_t mig_runner_stmt_cleanup_free(c_orm_db_t *db,
                                                  c_orm_query_t *q) {
  return db->vtable->finalize(q);
}

/**
 * @brief Applies a migration file by executing its UP SQL statement.
 * @param filepath Path to the migration `.sql` file.
 * @return C_ORM_OK on success, or validation/IO/SQL error code.
 */
C_ORM_EXPORT c_orm_error_t apply_migration(const char *filepath) {
  struct MigrationStatements stmts;
  c_orm_db_t *db = NULL;
  const char *trimmed = NULL;
  c_orm_error_t rc;

  if (!filepath || !filepath[0]) {
    return C_ORM_ERROR_VALIDATION;
  }

  memset(&stmts, 0, sizeof(stmts));
  rc = parse_migration_file(filepath, &stmts);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = mig_runner_connect_env(&db);
  if (rc != C_ORM_OK) {
    migration_statements_free(&stmts);
    return rc;
  }

  trimmed = stmts.up_statement;
  while (trimmed && (unsigned char)*trimmed > 0U &&
         (unsigned char)*trimmed <= 32U) {
    trimmed++;
  }

  if (trimmed && *trimmed != '\0') {
    rc = c_orm_execute_raw(db, trimmed);
    if (rc != C_ORM_OK) {
      migration_statements_free(&stmts);
      mig_runner_conn_cleanup_free(db);
      return rc;
    }
  }

  migration_statements_free(&stmts);
  return db->vtable->disconnect(db);
}

/**
 * @brief Rolls back a migration file by executing its DOWN SQL statement.
 * @param filepath Path to the migration `.sql` file.
 * @return C_ORM_OK on success, or validation/IO/SQL error code.
 */
C_ORM_EXPORT c_orm_error_t rollback_migration(const char *filepath) {
  struct MigrationStatements stmts;
  c_orm_db_t *db = NULL;
  const char *trimmed = NULL;
  c_orm_error_t rc;

  if (!filepath || !filepath[0]) {
    return C_ORM_ERROR_VALIDATION;
  }

  memset(&stmts, 0, sizeof(stmts));
  rc = parse_migration_file(filepath, &stmts);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = mig_runner_connect_env(&db);
  if (rc != C_ORM_OK) {
    migration_statements_free(&stmts);
    return rc;
  }

  trimmed = stmts.down_statement;
  while (trimmed && (unsigned char)*trimmed > 0U &&
         (unsigned char)*trimmed <= 32U) {
    trimmed++;
  }

  if (trimmed && *trimmed != '\0') {
    rc = c_orm_execute_raw(db, trimmed);
    if (rc != C_ORM_OK) {
      migration_statements_free(&stmts);
      mig_runner_conn_cleanup_free(db);
      return rc;
    }
  }

  migration_statements_free(&stmts);
  return db->vtable->disconnect(db);
}

/**
 * @brief Runs all pending UP migrations found in `migrations_dir`.
 * @param migrations_dir Directory containing `.sql` migration files.
 * @return C_ORM_OK on success, or validation/SQL error code.
 */
C_ORM_EXPORT c_orm_error_t run_pending_migrations(const char *migrations_dir) {
  c_orm_db_t *db = NULL;
  c_orm_error_t rc;

  if (!migrations_dir || !migrations_dir[0]) {
    return C_ORM_ERROR_VALIDATION;
  }

  rc = mig_runner_connect_env(&db);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = c_orm_migrate_up(db, migrations_dir, NULL);
  if (rc != C_ORM_OK) {
    mig_runner_conn_cleanup_free(db);
    return rc;
  }
  return db->vtable->disconnect(db);
}

/**
 * @brief Rolls back the most recently applied migration in `migrations_dir`.
 * @param migrations_dir Directory containing `.sql` migration files.
 * @return C_ORM_OK on success, or validation/SQL error code.
 */
C_ORM_EXPORT c_orm_error_t rollback_last_migration(const char *migrations_dir) {
  c_orm_db_t *db = NULL;
  c_orm_error_t rc;

  if (!migrations_dir || !migrations_dir[0]) {
    return C_ORM_ERROR_VALIDATION;
  }

  rc = mig_runner_connect_env(&db);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = c_orm_migrate_down(db, migrations_dir, 1, NULL);
  if (rc != C_ORM_OK) {
    mig_runner_conn_cleanup_free(db);
    return rc;
  }
  return db->vtable->disconnect(db);
}

/**
 * @brief Generates a new timestamped boilerplate `.sql` migration file.
 * @param migrations_dir Target directory for the migration file.
 * @param name Descriptive migration name suffix.
 * @return C_ORM_OK on success, or validation/IO/memory error code.
 */
C_ORM_EXPORT c_orm_error_t create_migration_file(const char *migrations_dir,
                                                 const char *name) {
  unsigned long now_val;
  char rev[24];
  char ts_buf[24];
  size_t rlen = 0;
  size_t tlen = 0;
  char *out_path = NULL;
  size_t dlen;
  size_t nlen;
  FILE *fp = NULL;
  c_orm_error_t rc;

  if (!migrations_dir || !migrations_dir[0] || !name || !name[0]) {
    return C_ORM_ERROR_VALIDATION;
  }

  MIG_MKDIR(migrations_dir);

  now_val = (unsigned long)time(NULL);
  do {
    rev[rlen++] = (char)('0' + (now_val % 10UL));
    now_val /= 10UL;
  } while (now_val > 0UL);
  while (rlen > 0U) {
    ts_buf[tlen++] = rev[--rlen];
  }
  ts_buf[tlen] = '\0';

  dlen = strlen(migrations_dir);
  nlen = strlen(name);

  rc = c_orm_system_malloc(dlen + 1 + tlen + 1 + nlen + 5, (void **)&out_path);
  if (rc != C_ORM_OK) {
    return rc;
  }

  memcpy(out_path, migrations_dir, dlen);
  out_path[dlen] = '/';
  memcpy(out_path + dlen + 1, ts_buf, tlen);
  out_path[dlen + 1 + tlen] = '_';
  memcpy(out_path + dlen + 1 + tlen + 1, name, nlen);
  memcpy(out_path + dlen + 1 + tlen + 1 + nlen, ".sql", 5);

  rc = C_ORM_FOPEN(&fp, out_path, "wb");
  c_orm_system_free(out_path);
  if (rc != C_ORM_OK) {
    return rc;
  }

  fputs("-- UP", fp);
  fputc(10, fp);
  fputc(10, fp);
  fputs("-- DOWN", fp);
  fputc(10, fp);
  fclose(fp);
  return C_ORM_OK;
}

/**
 * @brief Rolls back all applied migrations and re-applies `migrations_dir`.
 * @param migrations_dir Directory containing `.sql` migration files.
 * @return C_ORM_OK on success, or validation/SQL error code.
 */
C_ORM_EXPORT c_orm_error_t reset_database(const char *migrations_dir) {
  c_orm_db_t *db = NULL;
  c_orm_error_t rc;

  if (!migrations_dir || !migrations_dir[0]) {
    return C_ORM_ERROR_VALIDATION;
  }

  rc = mig_runner_connect_env(&db);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = c_orm_migrate_down(db, migrations_dir, 0, NULL);
  if (rc != C_ORM_OK) {
    mig_runner_conn_cleanup_free(db);
    return rc;
  }

  rc = c_orm_migrate_up(db, migrations_dir, NULL);
  if (rc != C_ORM_OK) {
    mig_runner_conn_cleanup_free(db);
    return rc;
  }

  return db->vtable->disconnect(db);
}

/**
 * @brief Exports the current database DDL schema to `out_filepath`.
 * @param out_filepath Output SQL file path.
 * @return C_ORM_OK on success, or validation/IO/SQL error code.
 */
C_ORM_EXPORT c_orm_error_t dump_schema(const char *out_filepath) {
  c_orm_db_t *db = NULL;
  c_orm_query_t *q = NULL;
  FILE *fp = NULL;
  int has_row = 0;
  c_orm_error_t rc;
  c_orm_error_t fin_rc;

  if (!out_filepath || !out_filepath[0]) {
    return C_ORM_ERROR_VALIDATION;
  }

  rc = mig_runner_connect_env(&db);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = C_ORM_FOPEN(&fp, out_filepath, "wb");
  if (rc != C_ORM_OK) {
    mig_runner_conn_cleanup_free(db);
    return rc;
  }

  fputs("-- c-orm schema dump", fp);
  fputc(10, fp);

  if (db->driver_name && strcmp(db->driver_name, "sqlite") == 0) {
    rc = db->vtable->prepare(
        db,
        "SELECT sql FROM sqlite_master WHERE sql IS NOT NULL AND name NOT LIKE "
        "'sqlite_%' ORDER BY name ASC",
        &q);
    if (rc != C_ORM_OK) {
      fclose(fp);
      mig_runner_conn_cleanup_free(db);
      return rc;
    }
    for (;;) {
      const char *ddl = NULL;
      rc = db->vtable->step(q, &has_row);
      if (rc != C_ORM_OK) {
        goto dump_err;
      }
      if (!has_row) {
        break;
      }
      rc = db->vtable->get_string(q, 0, &ddl);
      if (rc != C_ORM_OK) {
        goto dump_err;
      }
      if (ddl) {
        fputs(ddl, fp);
        fputc(';', fp);
        fputc(10, fp);
      }
    }
    fin_rc = db->vtable->finalize(q);
    if (fin_rc != C_ORM_OK) {
      fclose(fp);
      mig_runner_conn_cleanup_free(db);
      return fin_rc;
    }
  }

  fclose(fp);
  return db->vtable->disconnect(db);

dump_err:;
  fclose(fp);
  mig_runner_stmt_cleanup_free(db, q);
  mig_runner_conn_cleanup_free(db);
  return rc;
}

/**
 * @brief Connects to `DATABASE_URL` (or `db_name`) and runs pending migrations.
 * @param db_name Fallback database URL/path when `DATABASE_URL` is unset.
 * @param migrations_dir Directory containing `.sql` migration files.
 * @return C_ORM_OK on success, or validation/SQL error code.
 */
C_ORM_EXPORT c_orm_error_t setup_test_database(const char *db_name,
                                               const char *migrations_dir) {
  char env_url[256];
  c_orm_error_t rc;

  if (!db_name || !db_name[0] || !migrations_dir || !migrations_dir[0]) {
    return C_ORM_ERROR_VALIDATION;
  }

  env_url[0] = '\0';
  rc = C_ORM_GETENV(env_url, sizeof(env_url), "DATABASE_URL");
  if (rc != C_ORM_OK) {
    return rc;
  }

  g_override_db_url = (env_url[0] != '\0') ? env_url : db_name;
  rc = run_pending_migrations(migrations_dir);
  if (rc != C_ORM_OK) {
    g_override_db_url = NULL;
    return rc;
  }
  g_override_db_url = NULL;
  return C_ORM_OK;
}

/**
 * @brief Executes all SQL seed statements contained in `seed_filepath`.
 * @param seed_filepath Path to `.sql` seed file.
 * @return C_ORM_OK on success, or validation/IO/SQL error code.
 */
C_ORM_EXPORT c_orm_error_t seed_database(const char *seed_filepath) {
  char *sql = NULL;
  const char *trimmed = NULL;
  c_orm_db_t *db = NULL;
  c_orm_error_t rc;

  if (!seed_filepath || !seed_filepath[0]) {
    return C_ORM_ERROR_VALIDATION;
  }

  rc = mig_runner_read_file(seed_filepath, &sql);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = mig_runner_connect_env(&db);
  if (rc != C_ORM_OK) {
    c_orm_system_free(sql);
    return rc;
  }

  trimmed = sql;
  while ((unsigned char)*trimmed > 0U && (unsigned char)*trimmed <= 32U) {
    trimmed++;
  }

  if (*trimmed != '\0') {
    rc = c_orm_execute_raw(db, trimmed);
    if (rc != C_ORM_OK) {
      c_orm_system_free(sql);
      mig_runner_conn_cleanup_free(db);
      return rc;
    }
  }

  c_orm_system_free(sql);
  return db->vtable->disconnect(db);
}
