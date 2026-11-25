/**
 * @file test_migration.c
 * @brief Unit tests for database migration file parsing and runner stubs.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_migrations.h"
#include "functions/parse/fs.h"
#include "migration.h"
#include "migration_runner.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include "test_assert_zero_branch.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Tests parsing a valid migration file containing both UP and DOWN
 * sections.
 * @return GREATEST test result.
 */
TEST test_parse_migration_file_valid(void) {
  const char *filename = "test_valid_migration.sql";
  const char *content = "-- UP\n"
                        "CREATE TABLE test (id INT);\n"
                        "-- DOWN\n"
                        "DROP TABLE test;\n";
  struct MigrationStatements stmts;
  c_orm_error_t rc;

  rc = (c_orm_error_t)fs_write_to_file(filename, content);
  ASSERT_EQ(0, rc);

  rc = parse_migration_file(filename, &stmts);
  ASSERT_EQ(0, rc);
  ASSERT(stmts.up_statement != NULL);
  ASSERT(stmts.down_statement != NULL);
  ASSERT(strstr(stmts.up_statement, "CREATE TABLE test") != NULL);
  ASSERT(strstr(stmts.down_statement, "DROP TABLE test") != NULL);

  migration_statements_free(&stmts);
  remove(filename);
  PASS();
}

/**
 * @brief Tests parsing a migration file containing only an UP section.
 * @return GREATEST test result.
 */
TEST test_parse_migration_file_no_down(void) {
  const char *filename = "test_up_only.sql";
  const char *content = "-- UP\n"
                        "CREATE TABLE test2 (id INT);\n";
  struct MigrationStatements stmts;
  c_orm_error_t rc;

  rc = (c_orm_error_t)fs_write_to_file(filename, content);
  ASSERT_EQ(0, rc);

  rc = parse_migration_file(filename, &stmts);
  ASSERT_EQ(0, rc);
  ASSERT(stmts.up_statement != NULL);
  ASSERT_EQ(NULL, stmts.down_statement);
  ASSERT(strstr(stmts.up_statement, "CREATE TABLE test2") != NULL);

  migration_statements_free(&stmts);
  remove(filename);
  PASS();
}

/**
 * @brief Tests parsing a migration file without explicit UP or DOWN markers.
 * @return GREATEST test result.
 */
TEST test_parse_migration_file_no_markers(void) {
  const char *filename = "test_no_markers.sql";
  const char *content = "CREATE TABLE test3 (id INT);\n";
  struct MigrationStatements stmts;
  c_orm_error_t rc;

  rc = (c_orm_error_t)fs_write_to_file(filename, content);
  ASSERT_EQ(0, rc);

  rc = parse_migration_file(filename, &stmts);
  ASSERT_EQ(0, rc);
  ASSERT(stmts.up_statement != NULL);
  ASSERT_EQ(NULL, stmts.down_statement);
  ASSERT(strstr(stmts.up_statement, "CREATE TABLE test3") != NULL);

  migration_statements_free(&stmts);
  remove(filename);
  PASS();
}

/**
 * @brief Tests parsing a migration file containing only a DOWN section.
 * @return GREATEST test result.
 */
TEST test_parse_migration_file_no_up(void) {
  const char *filename = "test_down_only.sql";
  const char *content = "-- DOWN\n"
                        "DROP TABLE test3;\n";
  struct MigrationStatements stmts;
  c_orm_error_t rc;

  rc = (c_orm_error_t)fs_write_to_file(filename, content);
  ASSERT_EQ(0, rc);

  rc = parse_migration_file(filename, &stmts);
  ASSERT_EQ(0, rc);
  ASSERT(stmts.up_statement == NULL);
  ASSERT(stmts.down_statement != NULL);
  ASSERT(strstr(stmts.down_statement, "DROP TABLE test3") != NULL);

  migration_statements_free(&stmts);
  remove(filename);
  PASS();
}

/**
 * @brief Tests parsing a migration file with inverted DOWN then UP markers.
 * @return GREATEST test result.
 */
TEST test_parse_migration_file_inverted_markers(void) {
  const char *filename = "test_inverted_markers.sql";
  const char *content = "-- DOWN\n"
                        "DROP TABLE test3;\n"
                        "-- UP\n"
                        "CREATE TABLE test3 (id INT);\n";
  struct MigrationStatements stmts;
  c_orm_error_t rc;

  rc = (c_orm_error_t)fs_write_to_file(filename, content);
  ASSERT_EQ(0, rc);

  rc = parse_migration_file(filename, &stmts);
  ASSERT_EQ(0, rc);
  ASSERT(stmts.up_statement != NULL);
  ASSERT(stmts.down_statement != NULL);
  ASSERT(strstr(stmts.up_statement, "CREATE TABLE test3") != NULL);
  ASSERT(strstr(stmts.down_statement, "DROP TABLE test3") != NULL);

  migration_statements_free(&stmts);
  remove(filename);
  PASS();
}

/**
 * @brief Tests migration file parser error handling and NULL validation.
 * @return GREATEST test result.
 */
TEST test_parse_migration_file_errors(void) {
  struct MigrationStatements stmts;

  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, parse_migration_file(NULL, &stmts));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, parse_migration_file("test.sql", NULL));

  /* file not found */
  ASSERT_NEQ(C_ORM_OK, parse_migration_file("nonexistent.sql", &stmts));

  /* Empty file */
  {
    const char *filename = "empty.sql";
    c_orm_error_t wrc;
    wrc = (c_orm_error_t)fs_write_to_file(filename, "");
    ASSERT_EQ(C_ORM_OK, wrc);
    ASSERT_EQ(0, parse_migration_file(filename, &stmts));
    ASSERT_EQ(NULL, stmts.up_statement);
    ASSERT_EQ(NULL, stmts.down_statement);
    remove(filename);
  }

  migration_statements_free(NULL);
  migration_statements_init(NULL);

  PASS();
}

/** @brief Counter for controlling allocation failures. */
static int alloc_countdown = 0;

/**
 * @brief Mock malloc callback for simulated OOM testing.
 * @param size Requested allocation size.
 * @param out_ptr Output pointer receiving memory pointer or NULL.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on failure.
 */
static c_orm_error_t mock_malloc_migration(size_t size, void **out_ptr);

/** @brief Optional pre-built mock DB handle used by migration_runner.c. */
C_ORM_EXPORT extern c_orm_db_t *c_orm_mock_mig_runner_db;

/** @brief Mode selector for mock dump_schema vtable callbacks. */
static int g_mock_dump_mode = 0;

/** @brief Row emission counter for mock_dump_step. */
static int g_mock_dump_emitted = 0;

/**
 * @brief Mock disconnect callback for dump_schema branch coverage.
 * @param db Database handle.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_dump_disconnect(c_orm_db_t *db) {
  (void)db;
  return C_ORM_OK;
}

/**
 * @brief Mock prepare callback for dump_schema branch coverage.
 * @param db Database handle.
 * @param sql SQL statement string.
 * @param out_q Output query handle.
 * @return C_ORM_OK or C_ORM_ERROR_SQL.
 */
static c_orm_error_t mock_dump_prepare(c_orm_db_t *db, const char *sql,
                                       c_orm_query_t **out_q) {
  (void)db;
  (void)sql;
  if (g_mock_dump_mode == 1) {
    return C_ORM_ERROR_SQL;
  }
  g_mock_dump_emitted = 0;
  *out_q = (c_orm_query_t *)0x1;
  return C_ORM_OK;
}

/**
 * @brief Mock step callback for dump_schema branch coverage.
 * @param q Query handle.
 * @param out_has_row Output row indicator.
 * @return C_ORM_OK or C_ORM_ERROR_STEP.
 */
static c_orm_error_t mock_dump_step(c_orm_query_t *q, int *out_has_row) {
  (void)q;
  if (g_mock_dump_mode == 2) {
    return C_ORM_ERROR_STEP;
  }
  if (!g_mock_dump_emitted) {
    g_mock_dump_emitted = 1;
    *out_has_row = 1;
    return C_ORM_OK;
  }
  *out_has_row = 0;
  return C_ORM_OK;
}

/**
 * @brief Mock get_string callback for dump_schema branch coverage.
 * @param q Query handle.
 * @param idx Column index.
 * @param out_val Output string pointer.
 * @return C_ORM_OK or C_ORM_ERROR_SQL.
 */
static c_orm_error_t mock_dump_get_str(c_orm_query_t *q, int idx,
                                       const char **out_val) {
  (void)q;
  (void)idx;
  if (g_mock_dump_mode == 3) {
    return C_ORM_ERROR_SQL;
  }
  *out_val = NULL;
  return C_ORM_OK;
}

/**
 * @brief Mock finalize callback for dump_schema branch coverage.
 * @param q Query handle.
 * @return C_ORM_OK always.
 */
static c_orm_error_t mock_dump_finalize(c_orm_query_t *q) {
  (void)q;
  return (g_mock_dump_mode == 5) ? C_ORM_ERROR_SQL : C_ORM_OK;
}

/**
 * @brief Tests unified cross-driver migration runner functions and error/OOM
 * branches.
 * @return GREATEST test result.
 */
TEST test_migration_runner_stubs(void) {
  const char *rdir = "test_mig_runner_dir";
  const char *cdir = "test_mig_create_dir";
  const char *mig_file = "test_mig_runner_dir/001_init.sql";
  const char *empty_mig = "test_mig_runner_dir/002_empty.sql";
  const char *bad_mig = "test_mig_runner_dir/003_bad.sql";
  const char *seed_file = "test_mig_seed.sql";
  const char *empty_seed = "test_mig_empty_seed.sql";
  const char *bad_seed = "test_mig_bad_seed.sql";
  const char *dump_out = "test_mig_schema_dump.sql";
  const char *db_file = "test_mig_runner.db";
  char long_env[300];
  FILE *fp = NULL;
  c_orm_malloc_fn_t old_m = c_orm_malloc;
  c_orm_realloc_fn_t old_r = c_orm_realloc;
  c_orm_free_fn_t old_f = c_orm_free;

  {
    int rm1 = remove(db_file);
    int rm2 = remove(dump_out);
    ASSERT_NEQ(-999, rm1);
    ASSERT_NEQ(-999, rm2);
  }
  ASSERT_EQ(C_ORM_OK, C_ORM_UNSETENV("DATABASE_URL"));
  ASSERT_EQ(C_ORM_OK, C_ORM_UNSETENV("C_ORM_DB_URL"));

  /* Validation & unset env errors */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, create_migration_file(NULL, "init"));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, create_migration_file("", "init"));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, create_migration_file(rdir, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, create_migration_file(rdir, ""));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            create_migration_file("/dev/null/nonexistent_dir", "init"));

  ASSERT_EQ(C_ORM_ERROR_VALIDATION, apply_migration(NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, apply_migration(""));
  ASSERT_NEQ(C_ORM_OK, apply_migration("nonexistent_mig_file.sql"));

  ASSERT_EQ(C_ORM_ERROR_VALIDATION, rollback_migration(NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, rollback_migration(""));
  ASSERT_NEQ(C_ORM_OK, rollback_migration("nonexistent_mig_file.sql"));

  ASSERT_EQ(C_ORM_ERROR_VALIDATION, run_pending_migrations(NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, run_pending_migrations(""));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, run_pending_migrations(rdir));

  ASSERT_EQ(C_ORM_ERROR_VALIDATION, rollback_last_migration(NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, rollback_last_migration(""));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, rollback_last_migration(rdir));

  ASSERT_EQ(C_ORM_ERROR_VALIDATION, reset_database(NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, reset_database(""));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, reset_database(rdir));

  ASSERT_EQ(C_ORM_ERROR_VALIDATION, dump_schema(NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, dump_schema(""));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, dump_schema(dump_out));

  ASSERT_EQ(C_ORM_ERROR_VALIDATION, setup_test_database(NULL, rdir));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, setup_test_database("", rdir));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, setup_test_database(db_file, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, setup_test_database(db_file, ""));

  ASSERT_EQ(C_ORM_ERROR_VALIDATION, seed_database(NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, seed_database(""));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, seed_database("nonexistent_seed.sql"));

  /* Create boilerplate migration file unconditionally + OOM */
  ASSERT_EQ(C_ORM_OK, create_migration_file(cdir, "boilerplate"));
  ASSERT_EQ(C_ORM_OK, create_migration_file(rdir, "ensure_dir"));
  {
    c_orm_migration_t *cmigs = NULL;
    size_t cmcnt = 0;
    size_t ci;
    ASSERT_EQ(C_ORM_OK, c_orm_migration_load_dir(cdir, &cmigs, &cmcnt));
    for (ci = 0; ci < cmcnt; ci++) {
      char cpath[256];
      ASSERT_EQ(C_ORM_OK,
                C_ORM_SPRINTF(cpath, sizeof(cpath), "%s/%s_%s.sql", cdir,
                              cmigs[ci].version, cmigs[ci].name));
      remove(cpath);
    }
    ASSERT_EQ(C_ORM_OK, c_orm_migration_free_array(cmigs, cmcnt));

    cmigs = NULL;
    cmcnt = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_migration_load_dir(rdir, &cmigs, &cmcnt));
    for (ci = 0; ci < cmcnt; ci++) {
      char cpath[256];
      ASSERT_EQ(C_ORM_OK,
                C_ORM_SPRINTF(cpath, sizeof(cpath), "%s/%s_%s.sql", rdir,
                              cmigs[ci].version, cmigs[ci].name));
      remove(cpath);
    }
    ASSERT_EQ(C_ORM_OK, c_orm_migration_free_array(cmigs, cmcnt));
  }
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(mock_malloc_migration, old_r, old_f));
  alloc_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, create_migration_file(cdir, "oom_mig"));
  ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(old_m, old_r, old_f));

  /* Write concrete migration and seed fixtures */
  ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, mig_file, "wb"));
  fputs("-- UP\nCREATE TABLE IF NOT EXISTS runner_items (id INTEGER PRIMARY "
        "KEY, name TEXT);\n-- DOWN\nDROP TABLE IF EXISTS runner_items;\n",
        fp);
  fclose(fp);

  ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, empty_mig, "wb"));
  fputs("-- UP\n\n-- DOWN\n\n", fp);
  fclose(fp);

  ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, seed_file, "wb"));
  fputs("INSERT INTO runner_items (id, name) VALUES (1, 'seeded');\n", fp);
  fclose(fp);

  ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, empty_seed, "wb"));
  fputs(" \t\r\n", fp);
  fclose(fp);

  /* Env unset error after parsing migration / seed */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, apply_migration(mig_file));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, rollback_migration(mig_file));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, seed_database(seed_file));

  /* Env buffer overflow (>256 chars) */
  memset(long_env, 'x', sizeof(long_env) - 1);
  long_env[sizeof(long_env) - 1] = '\0';
  ASSERT_EQ(C_ORM_OK, C_ORM_SETENV("DATABASE_URL", long_env));
  ASSERT_NEQ(C_ORM_OK, run_pending_migrations(rdir));
  ASSERT_NEQ(C_ORM_OK, setup_test_database(db_file, rdir));
  ASSERT_EQ(C_ORM_OK, C_ORM_UNSETENV("DATABASE_URL"));

  ASSERT_EQ(C_ORM_OK, C_ORM_SETENV("C_ORM_DB_URL", long_env));
  ASSERT_NEQ(C_ORM_OK, run_pending_migrations(rdir));
  ASSERT_EQ(C_ORM_OK, C_ORM_UNSETENV("C_ORM_DB_URL"));

#if !defined(C_ORM_POSTGRESQL_LINKED) && !defined(C_ORM_POSTGRESQL_DYNAMIC)
  ASSERT_EQ(C_ORM_OK, C_ORM_SETENV("DATABASE_URL", "postgres://localhost/db"));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, run_pending_migrations(rdir));
  ASSERT_EQ(C_ORM_OK,
            C_ORM_SETENV("DATABASE_URL", "postgresql://localhost/db"));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, run_pending_migrations(rdir));
  ASSERT_EQ(C_ORM_OK, C_ORM_UNSETENV("DATABASE_URL"));
#endif

  /* Fallback to C_ORM_DB_URL + full lifecycle on SQLite */
  ASSERT_EQ(C_ORM_OK, C_ORM_SETENV("C_ORM_DB_URL", db_file));
  ASSERT_EQ(C_ORM_OK, apply_migration(mig_file));
  ASSERT_EQ(C_ORM_OK, apply_migration(empty_mig));
  ASSERT_EQ(C_ORM_OK, seed_database(seed_file));
  ASSERT_EQ(C_ORM_OK, seed_database(empty_seed));
  ASSERT_EQ(C_ORM_OK, dump_schema(dump_out));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, dump_schema("/dev/null/bad_dump.sql"));
  ASSERT_EQ(C_ORM_OK, rollback_migration(empty_mig));
  ASSERT_EQ(C_ORM_OK, rollback_migration(mig_file));

  /* setup_test_database with unset DATABASE_URL and with preset DATABASE_URL */
  ASSERT_EQ(C_ORM_OK, setup_test_database(db_file, rdir));
  ASSERT_EQ(C_ORM_OK, C_ORM_SETENV("DATABASE_URL", db_file));
  ASSERT_EQ(C_ORM_OK, setup_test_database("ignored.db", rdir));
  ASSERT_EQ(C_ORM_OK, rollback_last_migration(rdir));
  ASSERT_EQ(C_ORM_OK, run_pending_migrations(rdir));
  ASSERT_EQ(C_ORM_OK, reset_database(rdir));

  /* Non-SQLite memory:// dump_schema branch (prepare sqlite_master fails
   * cleanly) */
  ASSERT_EQ(C_ORM_OK, C_ORM_SETENV("DATABASE_URL", "memory://"));
  ASSERT_EQ(C_ORM_OK, dump_schema(dump_out));

  /* SQL error paths on apply_migration, rollback_migration, seed_database, dir
   * errors */
  ASSERT_EQ(C_ORM_OK, C_ORM_SETENV("DATABASE_URL", db_file));
  ASSERT_NEQ(C_ORM_OK, run_pending_migrations("nonexistent_migs_dir_xyz"));
  ASSERT_NEQ(C_ORM_OK, rollback_last_migration("nonexistent_migs_dir_xyz"));
  ASSERT_NEQ(C_ORM_OK, reset_database("nonexistent_migs_dir_xyz"));

  ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, bad_mig, "wb"));
  fputs("-- UP\nINVALID SQL UP;\n-- DOWN\nINVALID SQL DOWN;\n", fp);
  fclose(fp);
  ASSERT_NEQ(C_ORM_OK, apply_migration(bad_mig));
  ASSERT_NEQ(C_ORM_OK, rollback_migration(bad_mig));
  ASSERT_NEQ(C_ORM_OK, reset_database(rdir));
  ASSERT_EQ(0, remove(bad_mig));

  /* Rollback succeeds, then bad migration makes migrate_up fail in
   * reset_database */
  ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, bad_mig, "wb"));
  fputs("-- UP\nINVALID SQL UP;\n-- DOWN\nSELECT 1;\n", fp);
  fclose(fp);
  ASSERT_NEQ(C_ORM_OK, reset_database(rdir));
  ASSERT_EQ(0, remove(bad_mig));

  ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, bad_seed, "wb"));
  fputs("INVALID SEED SQL;\n", fp);
  fclose(fp);
  ASSERT_NEQ(C_ORM_OK, seed_database(bad_seed));

  /* OOM reading seed file */
  c_orm_set_allocators(mock_malloc_migration, old_r, old_f);
  alloc_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, seed_database(seed_file));
  c_orm_set_allocators(old_m, old_r, old_f);

  /* Exercise NULL up_statement / NULL down_statement + dump_schema mock
   * branches */
  {
    const char *null_mig = "test_mig_runner_dir/null_mig.sql";
    const char *zero_seed = "test_mig_zero_seed.sql";
    c_orm_driver_vtable_t mock_vt;
    c_orm_db_t mock_db;

    ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, zero_seed, "wb"));
    fclose(fp);
    ASSERT_EQ(C_ORM_OK, seed_database(zero_seed));
    ASSERT_EQ(0, remove(zero_seed));

    ASSERT_NEQ(C_ORM_OK,
               setup_test_database(db_file, "nonexistent_migs_dir_xyz"));

#ifdef C_ORM_TEST_ALLOCATOR
    ASSERT_EQ(C_ORM_OK, C_ORM_SETENV("DATABASE_URL", db_file));
    c_orm_mock_safe_crt_countdown = 0;
    ASSERT_NEQ(C_ORM_OK, run_pending_migrations(rdir));
    c_orm_mock_safe_crt_countdown = 0;
    ASSERT_NEQ(C_ORM_OK, setup_test_database(db_file, rdir));
    c_orm_mock_safe_crt_countdown = -1;

    ASSERT_EQ(C_ORM_OK, C_ORM_UNSETENV("DATABASE_URL"));
    ASSERT_EQ(C_ORM_OK, C_ORM_SETENV("C_ORM_DB_URL", db_file));
    c_orm_mock_safe_crt_countdown = 0;
    ASSERT_NEQ(C_ORM_OK, run_pending_migrations(rdir));
    c_orm_mock_safe_crt_countdown = -1;
    ASSERT_EQ(C_ORM_OK, C_ORM_SETENV("DATABASE_URL", db_file));
#endif

    ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, null_mig, "wb"));
    fclose(fp);
    ASSERT_EQ(C_ORM_OK, apply_migration(null_mig));
    ASSERT_EQ(C_ORM_OK, rollback_migration(null_mig));
    ASSERT_EQ(0, remove(null_mig));

    memset(&mock_vt, 0, sizeof(mock_vt));
    mock_vt.disconnect = mock_dump_disconnect;
    mock_vt.prepare = mock_dump_prepare;
    mock_vt.step = mock_dump_step;
    mock_vt.get_string = mock_dump_get_str;
    mock_vt.finalize = mock_dump_finalize;
    memset(&mock_db, 0, sizeof(mock_db));
    mock_db.vtable = &mock_vt;

    c_orm_mock_mig_runner_db = &mock_db;
    mock_db.driver_name = NULL;
    ASSERT_EQ(C_ORM_OK, dump_schema(dump_out));

    mock_db.driver_name = "sqlite";
    g_mock_dump_mode = 1;
    ASSERT_EQ(C_ORM_ERROR_SQL, dump_schema(dump_out));
    g_mock_dump_mode = 2;
    ASSERT_EQ(C_ORM_ERROR_STEP, dump_schema(dump_out));
    g_mock_dump_mode = 3;
    ASSERT_EQ(C_ORM_ERROR_SQL, dump_schema(dump_out));
    g_mock_dump_mode = 4;
    ASSERT_EQ(C_ORM_OK, dump_schema(dump_out));
    g_mock_dump_mode = 5;
    ASSERT_EQ(C_ORM_ERROR_SQL, dump_schema(dump_out));
    g_mock_dump_mode = 0;
    c_orm_mock_mig_runner_db = NULL;
  }

  ASSERT_EQ(C_ORM_OK, C_ORM_UNSETENV("DATABASE_URL"));
  ASSERT_EQ(C_ORM_OK, C_ORM_UNSETENV("C_ORM_DB_URL"));
  remove(bad_seed);
  remove(seed_file);
  remove(empty_seed);
  remove(mig_file);
  remove(empty_mig);
  remove(dump_out);
  remove(db_file);
  PASS();
}

/**
 * @brief Mock malloc callback for simulated OOM testing.
 * @param size Requested allocation size.
 * @param out_ptr Output pointer receiving memory pointer or NULL.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on failure.
 */
static c_orm_error_t mock_malloc_migration(size_t size, void **out_ptr) {
  if (alloc_countdown == 0) {
    *out_ptr = NULL;
    return C_ORM_ERROR_MEMORY;
  }
  alloc_countdown--;
  return c_orm_default_malloc(size, out_ptr);
}

/** @brief Countdown for mock_ok_null_malloc_mig. */
static int mig_ok_null_countdown = -1;

/**
 * @brief Mock malloc returning C_ORM_OK with NULL *out_ptr when countdown hits
 * 0.
 * @param size Requested allocation size.
 * @param out_ptr Output pointer receiving allocated address or NULL.
 * @return C_ORM_OK or C_ORM_ERROR_MEMORY.
 */
static c_orm_error_t mock_ok_null_malloc_mig(size_t size, void **out_ptr) {
  if (mig_ok_null_countdown == 0) {
    mig_ok_null_countdown--;
    *out_ptr = NULL;
    return C_ORM_OK;
  }
  mig_ok_null_countdown--;
  return c_orm_default_malloc(size, out_ptr);
}

/** @brief Bitmask controlling simulated free failures in mock_fail_free_mig. */
static unsigned int mig_free_fail_mask = 0;
/** @brief Invocation counter for mock_fail_free_mig. */
static unsigned int mig_free_call_idx = 0;

/**
 * @brief Mock free callback returning C_ORM_ERROR_MEMORY on masked calls.
 * @param ptr Pointer to free.
 * @return C_ORM_OK or C_ORM_ERROR_MEMORY.
 */
static c_orm_error_t mock_fail_free_mig(void *ptr) {
  unsigned int bit = 1U << mig_free_call_idx++;
  c_orm_error_t rc = c_orm_default_free(ptr);
  if ((mig_free_fail_mask & bit) != 0U) {
    return C_ORM_ERROR_MEMORY;
  }
  return rc;
}

/**
 * @brief Tests migration file parser handling of allocation failures.
 * @return GREATEST test result.
 */
TEST test_parse_migration_file_oom(void) {
  const char *filename = "test_oom_migration.sql";
  const char *content = "-- UP\n"
                        "CREATE TABLE test (id INT);\n"
                        "-- DOWN\n"
                        "DROP TABLE test;\n";
  struct MigrationStatements stmts;
  c_orm_malloc_fn_t old_malloc;
  c_orm_realloc_fn_t old_realloc;
  c_orm_free_fn_t old_free;
  c_orm_error_t rc;

  old_malloc = c_orm_malloc;
  old_realloc = c_orm_realloc;
  old_free = c_orm_free;

  rc = (c_orm_error_t)fs_write_to_file(filename, content);
  ASSERT_EQ(C_ORM_OK, rc);

  /* Force OOM on file_data (0), up_stmt (1), and down_stmt (2) */
  alloc_countdown = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(mock_malloc_migration, old_realloc, old_free));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, parse_migration_file(filename, &stmts));

  alloc_countdown = 1;
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(mock_malloc_migration, old_realloc, old_free));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, parse_migration_file(filename, &stmts));

  alloc_countdown = 2;
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(mock_malloc_migration, old_realloc, old_free));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, parse_migration_file(filename, &stmts));

  /* Force OOM on down_stmt (2) when freeing up_statement (call idx 1) fails */
  alloc_countdown = 2;
  mig_free_call_idx = 0;
  mig_free_fail_mask = 0x02U;
  ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(mock_malloc_migration, old_realloc,
                                           mock_fail_free_mig));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, parse_migration_file(filename, &stmts));
  mig_free_fail_mask = 0;

  /* Force C_ORM_OK + NULL *out_ptr on file_data (0), up_stmt (1), down_stmt (2)
   */
  mig_ok_null_countdown = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(mock_ok_null_malloc_mig, old_realloc,
                                           old_free));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, parse_migration_file(filename, &stmts));

  mig_ok_null_countdown = 1;
  ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(mock_ok_null_malloc_mig, old_realloc,
                                           old_free));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, parse_migration_file(filename, &stmts));

  mig_ok_null_countdown = 2;
  ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(mock_ok_null_malloc_mig, old_realloc,
                                           old_free));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, parse_migration_file(filename, &stmts));

  /* Exercise all free failure branches in migration_statements_free */
  ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(old_malloc, old_realloc, old_free));
  ASSERT_EQ(C_ORM_OK, parse_migration_file(filename, &stmts));
  mig_free_call_idx = 0;
  mig_free_fail_mask = 0x01U;
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(old_malloc, old_realloc, mock_fail_free_mig));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, migration_statements_free(&stmts));

  ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(old_malloc, old_realloc, old_free));
  ASSERT_EQ(C_ORM_OK, parse_migration_file(filename, &stmts));
  mig_free_call_idx = 0;
  mig_free_fail_mask = 0x02U;
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(old_malloc, old_realloc, mock_fail_free_mig));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, migration_statements_free(&stmts));

  ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(old_malloc, old_realloc, old_free));
  ASSERT_EQ(C_ORM_OK, parse_migration_file(filename, &stmts));
  mig_free_call_idx = 0;
  mig_free_fail_mask = 0x03U;
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(old_malloc, old_realloc, mock_fail_free_mig));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, migration_statements_free(&stmts));

  mig_free_fail_mask = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(old_malloc, old_realloc, old_free));
  remove(filename);
  PASS();
}

/** @brief Global flag to simulate migration statement init failure. */
C_ORM_EXPORT extern int c_orm_mock_migration_statements_init_fail;

/**
 * @brief Tests migration file parsing with empty blocks and failure injections.
 * @return GREATEST test result.
 */
TEST test_parse_migration_file_empty_blocks(void) {
  const char *filename = "test_empty_blocks.sql";
  const char *content = "-- UP\n-- DOWN\n";
  struct MigrationStatements stmts;
  c_orm_error_t rc;

  rc = (c_orm_error_t)fs_write_to_file(filename, content);
  ASSERT_EQ(0, rc);

  rc = parse_migration_file(filename, &stmts);
  ASSERT_EQ(0, rc);
  ASSERT(stmts.up_statement != NULL);
  ASSERT(stmts.down_statement != NULL);

  ASSERT_EQ(C_ORM_OK, migration_statements_free(&stmts));
  remove(filename);

  /* also test single empty marker at end of file */
  content = "-- UP";
  rc = (c_orm_error_t)fs_write_to_file(filename, content);
  ASSERT_EQ(0, rc);
  rc = parse_migration_file(filename, &stmts);
  ASSERT_EQ(0, rc);
  ASSERT_EQ(C_ORM_OK, migration_statements_free(&stmts));

  content = "-- DOWN";
  rc = (c_orm_error_t)fs_write_to_file(filename, content);
  ASSERT_EQ(0, rc);
  rc = parse_migration_file(filename, &stmts);
  ASSERT_EQ(0, rc);
  ASSERT_EQ(C_ORM_OK, migration_statements_free(&stmts));
  remove(filename);

  /* test init fail */
  c_orm_mock_migration_statements_init_fail = 1;
  rc = parse_migration_file("dummy.sql", &stmts);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  c_orm_mock_migration_statements_init_fail = 0;

  PASS();
}

/**
 * @brief Migration test suite runner.
 * @param migration_suite Suite runner function name.
 */
SUITE(migration_suite) {
  RUN_TEST(test_parse_migration_file_empty_blocks);
  RUN_TEST(test_parse_migration_file_valid);
  RUN_TEST(test_parse_migration_file_no_down);
  RUN_TEST(test_parse_migration_file_no_markers);
  RUN_TEST(test_parse_migration_file_no_up);
  RUN_TEST(test_parse_migration_file_inverted_markers);
  RUN_TEST(test_parse_migration_file_errors);
  RUN_TEST(test_migration_runner_stubs);
  RUN_TEST(test_parse_migration_file_oom);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
