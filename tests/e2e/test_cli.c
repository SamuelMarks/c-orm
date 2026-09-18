#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_cli.c
 * @brief Unit tests for c-orm CLI subcommands and option parsing.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_api.h"
#include "c_orm_db.h"
#include "c_orm_mysql.h"
#include "c_orm_postgres.h"
#include "greatest.h"
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "c_orm_migrations.h"

/**
 * @brief Mock migration directory loader for CLI tests.
 * @param dir_path Directory path.
 * @param out_migrations Pointer to receive array of migrations.
 * @param out_count Pointer to receive count.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t mock_load_dir(const char *dir_path,
                                   c_orm_migration_t **out_migrations,
                                   size_t *out_count) {
  if (strcmp(dir_path, ".") == 0 || strcmp(dir_path, "test_migrations_dir_cli") == 0) {
    *out_count = 1;
    *out_migrations = (c_orm_migration_t *)C_ORM_MALLOC(sizeof(c_orm_migration_t));
    memset(*out_migrations, 0, sizeof(c_orm_migration_t));
    C_ORM_STRCPY((*out_migrations)[0].version, sizeof((*out_migrations)[0].version), "1");
    C_ORM_STRCPY((*out_migrations)[0].name, sizeof((*out_migrations)[0].name), "test");
    C_ORM_STRCPY((*out_migrations)[0].hash, sizeof((*out_migrations)[0].hash), "hash");
    return C_ORM_OK;
  }
  if (strcmp(dir_path, "bad_dir") == 0) {
    *out_count = 0;
    *out_migrations = NULL;
    return C_ORM_OK;
  }
  *out_count = 0;
  *out_migrations = NULL;
  return C_ORM_ERROR_NOT_FOUND;
}

/**
 * @brief Mock migrate all callback.
 * @param db Database handle.
 * @param migrations Migration array.
 * @param count Migration count.
 * @param options Migration options.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t
mock_migrate_all(c_orm_db_t *db, const c_orm_migration_t *migrations,
                 size_t count, const c_orm_migration_options_t *options) {
  (void)db;
  (void)migrations;
  (void)count;
  (void)options;
  if (options && options->log_cb) {
    options->log_cb("Mock migrate all log");
  }
  return C_ORM_OK;
}

#define c_orm_migration_load_dir mock_load_dir
#define c_orm_migrate_all mock_migrate_all

static int mock_get_applied_fail = 0;

/**
 * @brief Mock get applied migrations callback.
 * @param db Database handle.
 * @param out_migrations Pointer to receive array of migrations.
 * @param out_count Pointer to receive count.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t mock_get_applied(c_orm_db_t *db,
                                      c_orm_migration_t **out_migrations,
                                      size_t *out_count) {
  (void)db;
  if (mock_get_applied_fail) {
    return C_ORM_ERROR_UNKNOWN;
  }
  *out_count = 1;
  *out_migrations = (c_orm_migration_t *)C_ORM_MALLOC(sizeof(c_orm_migration_t));
  memset(*out_migrations, 0, sizeof(c_orm_migration_t));
  C_ORM_STRCPY((*out_migrations)[0].version, sizeof((*out_migrations)[0].version), "1");
  C_ORM_STRCPY((*out_migrations)[0].name, sizeof((*out_migrations)[0].name), "test");
  C_ORM_STRCPY((*out_migrations)[0].hash, sizeof((*out_migrations)[0].hash), "hash");
  return C_ORM_OK;
}
#define c_orm_migration_get_applied mock_get_applied

int c_orm_cli_main(int argc, char **argv);
#define main c_orm_cli_main
#include "../../src/c_orm_cli.c"
/* clang-format on */
#undef main

/**
 * @brief Test CLI help flag invocation.
 * @return GREATEST test result.
 */
TEST test_cli_help(void) {
  c_orm_error_t rc;
  const char *argv[] = {"c-orm-cli", "--help"};
  int argc;

  argc = 2;
  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)argv);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  PASS();
}

/**
 * @brief Test CLI invocation with no arguments.
 * @return GREATEST test result.
 */
TEST test_cli_no_args(void) {
  c_orm_error_t rc;
  const char *argv[] = {"c-orm-cli"};
  int argc;

  argc = 1;
  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)argv);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  PASS();
}

/**
 * @brief Test CLI init subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_init(void) {
  c_orm_error_t rc;
  const char *argv_dir_no_arg[] = {"c-orm-cli", "init", "--dir"};
  const char *argv[] = {"c-orm-cli", "init", "--dir",
                        "test_migrations_dir_cli"};
  int argc;
  int sys_rc;

  argc = 4;
#ifdef _WIN32
  sys_rc = system("rmdir /s /q test_migrations_dir_cli >nul 2>&1");
#else
  sys_rc = system("rm -rf test_migrations_dir_cli");
  sys_rc = system("rm -rf ./test_migrations_dir_cli");
#endif
  (void)sys_rc;

  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)argv);
  ASSERT_EQ(C_ORM_OK, rc);

  /* call again to hit the already exists branch */
  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)argv);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = (c_orm_error_t)c_orm_cli_main(3, (char **)argv_dir_no_arg);
  ASSERT_EQ(C_ORM_OK, rc);

  PASS();
}

/**
 * @brief Test CLI create subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_create(void) {
  c_orm_error_t rc;
  const char *argv[] = {"c-orm-cli", "create"};
  const char *argv_init[] = {"c-orm-cli", "init", "--dir",
                             "test_migrations_dir_cli"};
  const char *argv2[] = {"c-orm-cli", "create", "my_mig", "--dir",
                         "test_migrations_dir_cli"};
  const char *argv3[] = {"c-orm-cli", "create", "my_mig", "--dir",
                         "nonexistent_dir_12345/sub"};
  const char *argv_multi[] = {"c-orm-cli", "create", "name1", "name2"};
  int argc;

  argc = 2;
  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)argv);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  /* ensure test_migrations_dir_cli exists */
  rc = (c_orm_error_t)c_orm_cli_main(4, (char **)argv_init);
  ASSERT_EQ(C_ORM_OK, rc);

  argc = 5;
  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)argv2);
  ASSERT_EQ(C_ORM_OK, rc);

  /* simulate missing dir to hit fopen failure */
  argc = 5;
  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)argv3);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = (c_orm_error_t)c_orm_cli_main(4, (char **)argv_multi);
  ASSERT_EQ(C_ORM_OK, rc);

  PASS();
}

/**
 * @brief Test CLI generate subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_generate(void) {
  c_orm_error_t rc;
  const char *argv[] = {"c-orm-cli", "generate"};
  int argc;

  argc = 2;
  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)argv);
  ASSERT_EQ(C_ORM_OK, rc);
  PASS();
}

/**
 * @brief Test CLI migrate subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_migrate(void) {
  c_orm_error_t rc;
  const char *argv[] = {"c-orm-cli", "migrate"};
  const char *argv2[] = {"c-orm-cli",   "migrate", "--db",
                         "test_cli.db", "--dir",   "test_migrations_dir_cli"};
#ifdef _WIN32
  const char *argv3[] = {"c-orm-cli", "migrate", "--db",
                         "Z:\\\\invalid_dir\\\\invalid.db"};
#else
  const char *argv3[] = {"c-orm-cli", "migrate", "--db",
                         "/dev/null/invalid.db"};
#endif
  const char *argv4[] = {"c-orm-cli",   "migrate", "--db",
                         "test_cli.db", "--dir",   "empty_dir"};
  const char *argv_db_no_arg[] = {"c-orm-cli", "migrate", "--db"};
  const char *argv5[] = {"c-orm-cli", "status", "--db", "test_cli.db"};
  const char *argv6[] = {"c-orm-cli",   "migrate", "--db",
                         "test_cli.db", "--dir",   "bad_dir"};
  int argc;

  argc = 2;

  /* unset env so db is missing */
  C_ORM_UNSETENV("C_ORM_DB_URL");

  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)argv);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  C_ORM_SETENV("C_ORM_DB_URL", "test_cli.db");
  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)argv);
  ASSERT_EQ(C_ORM_OK, rc);
  C_ORM_UNSETENV("C_ORM_DB_URL");

  argc = 6;
  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)argv2);
  ASSERT_EQ(C_ORM_OK, rc);

  /* connection error */
  rc = (c_orm_error_t)c_orm_cli_main(4, (char **)argv3);
  ASSERT(rc != C_ORM_OK);

  /* empty dir */
  rc = (c_orm_error_t)c_orm_cli_main(6, (char **)argv4);
  ASSERT_EQ(C_ORM_OK, rc);

  /* no db argument */
  rc = (c_orm_error_t)c_orm_cli_main(3, (char **)argv_db_no_arg);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  /* status failure */
  mock_get_applied_fail = 1;
  rc = (c_orm_error_t)c_orm_cli_main(4, (char **)argv5);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  mock_get_applied_fail = 0;

  /* bad dir load failure */
  rc = (c_orm_error_t)c_orm_cli_main(6, (char **)argv6);
  ASSERT_EQ(C_ORM_OK, rc);

  PASS();
}

/**
 * @brief Test CLI rollback subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_rollback(void) {
  c_orm_error_t rc;
  const char *argv[] = {"c-orm-cli", "rollback"};
  int argc;

  argc = 2;
  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)argv);
  ASSERT_EQ(C_ORM_OK, rc);
  PASS();
}

/**
 * @brief Test CLI status subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_status(void) {
  c_orm_error_t rc;
  sqlite3 *sdb;
  const char *argv[] = {"c-orm-cli", "status"};
  const char *argv2[] = {"c-orm-cli", "status", "--db", "test_cli.db"};
#ifdef _WIN32
  const char *argv3[] = {"c-orm-cli", "status", "--db",
                         "Z:\\\\invalid_dir\\\\invalid.db"};
#else
  const char *argv3[] = {"c-orm-cli", "status", "--db", "/dev/null/invalid.db"};
#endif
  int argc;
  int s_rc;

  argc = 2;
  sdb = NULL;
  C_ORM_UNSETENV("C_ORM_DB_URL");

  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)argv);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  s_rc = sqlite3_open("test_cli.db", &sdb);
  (void)s_rc;
  s_rc =
      sqlite3_exec(sdb,
                   "CREATE TABLE IF NOT EXISTS _c_orm_migrations (id INTEGER "
                   "PRIMARY KEY, version TEXT, name TEXT, applied_at DATETIME)",
                   0, 0, 0);
  (void)s_rc;
  sqlite3_close(sdb);

  rc = (c_orm_error_t)c_orm_cli_main(4, (char **)argv2);
  ASSERT_EQ(C_ORM_OK, rc);

  mock_get_applied_fail = 1;
  rc = (c_orm_error_t)c_orm_cli_main(4, (char **)argv2);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  mock_get_applied_fail = 0;

  rc = (c_orm_error_t)c_orm_cli_main(4, (char **)argv3);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  PASS();
}

/**
 * @brief Test CLI logger callback.
 * @return GREATEST test result.
 */
TEST test_cli_log(void) {
  c_orm_error_t rc;
  rc = log_cb("test log");
  ASSERT_EQ(C_ORM_OK, rc);
  PASS();
}

/**
 * @brief Test CLI with unknown subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_unknown(void) {
  c_orm_error_t rc;
  const char *argv[] = {"c-orm-cli", "unknown"};
  int argc;

  argc = 2;
  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)argv);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  PASS();
}

#ifndef __EMSCRIPTEN__
/**
 * @brief Test CLI sql2c code generation subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_sql2c(void) {
  c_orm_error_t rc;
  FILE *f;
  const char *argv1[] = {"c-orm-cli", "sql2c"};
  const char *argv2[] = {"c-orm-cli", "sql2c", "test_schema.sql", "test_out"};
  const char *argv3[] = {"c-orm-cli", "sql2c", "invalid_missing.sql",
                         "test_out"};
  int sys_rc;

  f = NULL;
#ifdef _WIN32
  sys_rc = system("mkdir test_out >nul 2>&1");
#else
  sys_rc = system("mkdir -p test_out");
#endif
  (void)sys_rc;

  C_ORM_FOPEN(&f, "test_schema.sql", "w");
  if (f != NULL) {
    fprintf(f, "%s\n", "CREATE TABLE test_tbl (id INTEGER PRIMARY KEY);");
    fclose(f);
  }

  rc = (c_orm_error_t)c_orm_cli_main(2, (char **)argv1);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  rc = (c_orm_error_t)c_orm_cli_main(4, (char **)argv2);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = (c_orm_error_t)c_orm_cli_main(4, (char **)argv3);
  ASSERT(rc != C_ORM_OK);
  PASS();
}
#endif

/**
 * @brief CLI test suite runner.
 */
SUITE(cli_suite) {
  RUN_TEST(test_cli_help);
  RUN_TEST(test_cli_no_args);
  RUN_TEST(test_cli_init);
  RUN_TEST(test_cli_create);
  RUN_TEST(test_cli_generate);
  RUN_TEST(test_cli_migrate);
  RUN_TEST(test_cli_rollback);
  RUN_TEST(test_cli_status);
  RUN_TEST(test_cli_log);
  RUN_TEST(test_cli_unknown);
#ifndef __EMSCRIPTEN__
  RUN_TEST(test_cli_sql2c);
#endif
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#if defined(__clang__) || defined(__GNUC__)
#endif
