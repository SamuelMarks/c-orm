#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_cli_exec.c
 * @brief End-to-end integration tests executing the c-orm CLI binary via
 * system.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_api.h"
#include "greatest.h"
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
/* clang-format on */

#ifndef C_ORM_CLI_EXECUTABLE
#ifdef _WIN32
#define C_ORM_CLI_EXECUTABLE "..\\..\\bin\\c-orm-cli"
#else
#define C_ORM_CLI_EXECUTABLE "../../bin/c-orm-cli"
#endif
#endif

#ifdef _WIN32
#define CLI_CMD "\"" C_ORM_CLI_EXECUTABLE "\""
#define DEV_NULL " >nul 2>&1"
#else
#define CLI_CMD "\"" C_ORM_CLI_EXECUTABLE "\""
#define DEV_NULL " >/dev/null 2>&1"
#endif

/**
 * @brief Test CLI executable with --help argument.
 * @return GREATEST test result.
 */
TEST test_cli_help(void) {
  int rc;
  rc = system(CLI_CMD " --help" DEV_NULL);
  ASSERT_NEQ(0, rc);
  PASS();
}

/**
 * @brief Test CLI executable with no arguments.
 * @return GREATEST test result.
 */
TEST test_cli_no_args(void) {
  int rc;
  rc = system(CLI_CMD DEV_NULL);
  ASSERT_NEQ(0, rc);
  PASS();
}

/**
 * @brief Test CLI executable init subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_init(void) {
  int rc;
  int sys_rc;

#ifdef _WIN32
  sys_rc = system("rmdir /S /Q test_migrations_dir_init 2>nul");
#else
  sys_rc = system("rm -rf test_migrations_dir_init");
#endif
  (void)sys_rc;

  rc = system(CLI_CMD " init --dir test_migrations_dir_init" DEV_NULL);
  ASSERT_EQ(0, rc);
  rc = system(CLI_CMD " init --dir test_migrations_dir_init" DEV_NULL);
  ASSERT_EQ(0, rc);
  PASS();
}

/**
 * @brief Test CLI executable create subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_create(void) {
  int rc;
  int sys_rc;

  rc = system(CLI_CMD " create" DEV_NULL);
  ASSERT_NEQ(0, rc);

  sys_rc = system(CLI_CMD " init --dir test_migrations_dir" DEV_NULL);
  (void)sys_rc;

  rc = system(CLI_CMD " create my_mig --dir test_migrations_dir" DEV_NULL);
  ASSERT_EQ(0, rc);

#ifdef _WIN32
  sys_rc = system(CLI_CMD
                  " create my_mig --dir Z:\\\\invalid_dir\\\\invalid" DEV_NULL);
#else
  sys_rc =
      system(CLI_CMD " create my_mig --dir /dev/null/invalid_dir" DEV_NULL);
#endif
  (void)sys_rc;

  sys_rc = system(
      CLI_CMD " create my_mig extra_arg --dir test_migrations_dir" DEV_NULL);
  (void)sys_rc;

  PASS();
}

/**
 * @brief Test CLI executable generate subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_generate(void) {
  int rc;
  rc = system(CLI_CMD " generate" DEV_NULL);
  ASSERT_EQ(0, rc);
  PASS();
}

/**
 * @brief Test CLI executable migrate subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_migrate(void) {
  int rc;
  int sys_rc;
  sqlite3 *db;
  FILE *f1;
  FILE *f2;

  db = NULL;
  f1 = NULL;
  f2 = NULL;

#if !defined(_WIN32) && !defined(__CYGWIN__)
  C_ORM_UNSETENV("C_ORM_DB_URL");
  rc = system(CLI_CMD " migrate" DEV_NULL);
  ASSERT_NEQ(0, rc);
#endif

  rc = system(CLI_CMD
              " migrate --db :memory: --dir test_migrations_dir" DEV_NULL);
  ASSERT_EQ(0, rc);

  sys_rc = sqlite3_open("test_cli_exec.db", &db);
  (void)sys_rc;
  sys_rc = sqlite3_exec(
      db,
      "CREATE TABLE IF NOT EXISTS _c_orm_migrations (id INTEGER PRIMARY KEY, "
      "version TEXT, name TEXT, hash TEXT, applied_at DATETIME);",
      0, 0, 0);
  (void)sys_rc;
  sys_rc =
      sqlite3_exec(db,
                   "INSERT INTO _c_orm_migrations (version, name, hash) VALUES "
                   "('1', 'test', 'hash');",
                   0, 0, 0);
  (void)sys_rc;
  sqlite3_close(db);

  sys_rc = system(CLI_CMD " status --db test_cli_exec.db" DEV_NULL);
  (void)sys_rc;

#ifdef _WIN32
  sys_rc = system("mkdir test_migrations_dir 2>nul");
#else
  sys_rc = system("mkdir -p test_migrations_dir 2>/dev/null");
#endif
  (void)sys_rc;

  C_ORM_FOPEN(&f1, "test_migrations_dir/1_test.up.sql", "w");
  if (f1 != NULL) {
    fprintf(f1, "%s\n", "CREATE TABLE x (id INT);");
    fclose(f1);
  }
  C_ORM_FOPEN(&f2, "test_migrations_dir/1_test.down.sql", "w");
  if (f2 != NULL) {
    fprintf(f2, "%s\n", "DROP TABLE x;");
    fclose(f2);
  }

  sys_rc = system(
      CLI_CMD
      " migrate --db test_cli_exec.db --dir test_migrations_dir" DEV_NULL);
  (void)sys_rc;
  sys_rc = system(
      CLI_CMD
      " migrate --db test_cli_exec.db --dir real_migrations_cli" DEV_NULL);
  (void)sys_rc;
  sys_rc = system(
      CLI_CMD
      " migrate --db test_cli_exec.db --dir empty_migrations_cli" DEV_NULL);
  (void)sys_rc;
  sys_rc = system(CLI_CMD " migrate --db" DEV_NULL);
  (void)sys_rc;
  sys_rc = system(CLI_CMD " migrate --db test_cli_exec.db --dir" DEV_NULL);
  (void)sys_rc;

  rc = system(CLI_CMD " migrate --db invalid_path/file.db" DEV_NULL);
  (void)rc;

  PASS();
}

/**
 * @brief Test CLI executable rollback subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_rollback(void) {
  int rc;
  rc = system(CLI_CMD " rollback" DEV_NULL);
  ASSERT_EQ(0, rc);
  PASS();
}

/**
 * @brief Test CLI executable status subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_status(void) {
  int rc;
  int sys_rc;
  sqlite3 *db;

  db = NULL;
  (void)rc;
  (void)sys_rc;
  (void)db;
#if !defined(_WIN32) && !defined(__CYGWIN__)
  C_ORM_UNSETENV("C_ORM_DB_URL");
  rc = system(CLI_CMD " status" DEV_NULL);
  ASSERT_NEQ(0, rc);

  sys_rc = system("C_ORM_DB_URL=test_cli_exec.db " CLI_CMD " status" DEV_NULL);
  (void)sys_rc;

  remove("bad_schema.db");
  sys_rc = sqlite3_open("bad_schema.db", &db);
  (void)sys_rc;
  sys_rc =
      sqlite3_exec(db, "CREATE TABLE _c_orm_migrations(id INTEGER);", 0, 0, 0);
  (void)sys_rc;
  sqlite3_close(db);

  rc = system(CLI_CMD " status --db bad_schema.db" DEV_NULL);
  ASSERT_NEQ(0, rc);

  rc = system(CLI_CMD " status --db /dev/null/invalid.db" DEV_NULL);
  ASSERT_NEQ(0, rc);
#endif
  PASS();
}

/**
 * @brief Test CLI executable with unknown subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_unknown(void) {
  int rc;
  rc = system(CLI_CMD " unknown_command" DEV_NULL);
  ASSERT_NEQ(0, rc);
  PASS();
}

#ifndef __EMSCRIPTEN__
/**
 * @brief Test CLI executable sql2c code generator.
 * @return GREATEST test result.
 */
TEST test_cli_exec_sql2c(void) {
  int rc;
  int sys_rc;
  FILE *f;

  f = NULL;
  C_ORM_FOPEN(&f, "test_schema.sql", "w");
  if (f != NULL) {
    fprintf(f, "%s\n", "CREATE TABLE test_tbl (id INTEGER PRIMARY KEY);");
    fclose(f);
  }
  rc = system(CLI_CMD " sql2c" DEV_NULL);
  ASSERT_NEQ(0, rc);

#ifdef _WIN32
  sys_rc = system("mkdir test_out" DEV_NULL);
#else
  sys_rc = system("mkdir -p test_out");
#endif
  (void)sys_rc;

  rc = system(CLI_CMD " sql2c test_schema.sql test_out" DEV_NULL);
#ifndef __CYGWIN__
  ASSERT_EQ(0, rc);
#endif

  rc = system(CLI_CMD " sql2c invalid_missing.sql test_out" DEV_NULL);
  ASSERT_NEQ(0, rc);

#if !defined(_WIN32) && !defined(__CYGWIN__)
  sys_rc = system("mkdir -p readonly_dir && chmod 555 readonly_dir");
  (void)sys_rc;
  rc = system(CLI_CMD " sql2c test_schema.sql readonly_dir" DEV_NULL);
  sys_rc = system("chmod 777 readonly_dir && rm -rf readonly_dir");
  (void)sys_rc;
  ASSERT_NEQ(0, rc);

  sys_rc = system("mkdir -p partial_readonly_dir");
  (void)sys_rc;
  sys_rc = system("touch partial_readonly_dir/Models.c && chmod 444 "
                  "partial_readonly_dir/Models.c");
  (void)sys_rc;
  rc = system(CLI_CMD " sql2c test_schema.sql partial_readonly_dir" DEV_NULL);
  sys_rc = system(
      "chmod 777 partial_readonly_dir/Models.c && rm -rf partial_readonly_dir");
  (void)sys_rc;
  ASSERT_NEQ(0, rc);
#endif

  PASS();
}
#endif

/**
 * @brief CLI exec integration test suite runner.
 */
SUITE(cli_exec_suite) {
  RUN_TEST(test_cli_help);
  RUN_TEST(test_cli_no_args);
  RUN_TEST(test_cli_init);
  RUN_TEST(test_cli_create);
  RUN_TEST(test_cli_generate);
  RUN_TEST(test_cli_migrate);
  RUN_TEST(test_cli_rollback);
  RUN_TEST(test_cli_status);
  RUN_TEST(test_cli_unknown);
#ifndef __EMSCRIPTEN__
  RUN_TEST(test_cli_exec_sql2c);
#endif
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#if defined(__clang__) || defined(__GNUC__)
#endif
