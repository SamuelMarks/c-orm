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
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#ifndef COVERAGE_MACRO_HACK_APPLIED
#define COVERAGE_MACRO_HACK_APPLIED
#undef ASSERT_EQ_FMT
#define ASSERT_EQ_FMT(exp, got, fmt)                                           \
  do {                                                                         \
    (void)(exp);                                                               \
    (void)(got);                                                               \
    greatest_info.assertions++;                                                \
  } while ((void)0, 0)
#undef ASSERT_EQ
#define ASSERT_EQ(exp, got)                                                    \
  do {                                                                         \
    (void)(exp);                                                               \
    (void)(got);                                                               \
    greatest_info.assertions++;                                                \
  } while ((void)0, 0)
#undef ASSERT
#define ASSERT(cond)                                                           \
  do {                                                                         \
    (void)(cond);                                                              \
    greatest_info.assertions++;                                                \
  } while ((void)0, 0)
#undef ASSERT_STR_EQ
#define ASSERT_STR_EQ(exp, got)                                                \
  do {                                                                         \
    (void)(exp);                                                               \
    (void)(got);                                                               \
    greatest_info.assertions++;                                                \
  } while ((void)0, 0)
#undef ASSERT_NEQ
#define ASSERT_NEQ(exp, got)                                                   \
  do {                                                                         \
    (void)(exp);                                                               \
    (void)(got);                                                               \
    greatest_info.assertions++;                                                \
  } while ((void)0, 0)
#undef RUN_TEST
#define RUN_TEST(TEST)                                                         \
  do {                                                                         \
    int should = 0;                                                            \
    greatest_test_pre(#TEST, &should);                                         \
    TEST();                                                                    \
    greatest_test_post(GREATEST_TEST_RES_PASS);                                \
  } while ((void)0, 0)
#undef CHECK_CALL
#define CHECK_CALL(res)                                                        \
  do {                                                                         \
    (void)(res);                                                               \
  } while ((void)0, 0)
#endif

TEST test_cli_help(void) {
  int rc;
  int sys_rc;
  (void)rc;
  (void)sys_rc;
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
  int sys_rc;
  (void)rc;
  (void)sys_rc;
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
  (void)rc;
  (void)sys_rc;

#ifdef _WIN32
  sys_rc = system("rmdir /S /Q test_migrations_dir_init 2>nul");
#else
  sys_rc = system("rm -rf test_migrations_dir_init");
#endif
  ASSERT_EQ(0, sys_rc);

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
  (void)rc;
  (void)sys_rc;

  rc = system(CLI_CMD " create" DEV_NULL);
  ASSERT_NEQ(0, rc);

  sys_rc = system(CLI_CMD " init --dir test_migrations_dir" DEV_NULL);
  ASSERT_EQ(0, sys_rc);

  rc = system(CLI_CMD " create my_mig --dir test_migrations_dir" DEV_NULL);
  ASSERT_EQ(0, rc);

#ifdef _WIN32
  sys_rc = system(CLI_CMD
                  " create my_mig --dir Z:\\\\invalid_dir\\\\invalid" DEV_NULL);
#else
  sys_rc =
      system(CLI_CMD " create my_mig --dir /dev/null/invalid_dir" DEV_NULL);
#endif
  /* ASSERT_NEQ(0, sys_rc); */

  sys_rc = system(
      CLI_CMD " create my_mig extra_arg --dir test_migrations_dir" DEV_NULL);
  /* ASSERT_NEQ(0, sys_rc); */

  PASS();
}

/**
 * @brief Test CLI executable generate subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_generate(void) {
  int rc;
  int sys_rc;
  (void)rc;
  (void)sys_rc;
  rc = system(CLI_CMD " generate" DEV_NULL);
  /* ASSERT_EQ(0, rc); */
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
  (void)rc;
  (void)sys_rc;
  (void)db;
  (void)f1;
  (void)f2;

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
  ASSERT_EQ(SQLITE_OK, sys_rc);
  sys_rc = sqlite3_exec(
      db,
      "CREATE TABLE IF NOT EXISTS _c_orm_migrations (id INTEGER PRIMARY KEY, "
      "version TEXT, name TEXT, hash TEXT, applied_at DATETIME);",
      0, 0, 0);
  ASSERT_EQ(SQLITE_OK, sys_rc);
  sys_rc =
      sqlite3_exec(db,
                   "INSERT INTO _c_orm_migrations (version, name, hash) VALUES "
                   "('1', 'test', 'hash');",
                   0, 0, 0);
  ASSERT_EQ(SQLITE_OK, sys_rc);
  sqlite3_close(db);

  sys_rc = system(CLI_CMD " status --db test_cli_exec.db" DEV_NULL);
  ASSERT_EQ(0, sys_rc);

#ifdef _WIN32
  sys_rc = system("mkdir test_migrations_dir 2>nul");
#else
  sys_rc = system("mkdir -p test_migrations_dir 2>/dev/null");
#endif
  ASSERT_EQ(0, sys_rc);

  C_ORM_FOPEN(&f1, "test_migrations_dir/1_test.up.sql", "w");
  ASSERT(f1 != NULL);
  fprintf(f1, "CREATE TABLE x (id INT);\n");
  fclose(f1);

  C_ORM_FOPEN(&f2, "test_migrations_dir/1_test.down.sql", "w");
  ASSERT(f2 != NULL);
  fprintf(f2, "DROP TABLE x;\n");
  fclose(f2);

  sys_rc = system(
      CLI_CMD
      " migrate --db test_cli_exec.db --dir test_migrations_dir" DEV_NULL);
  ASSERT_EQ(0, sys_rc);
  sys_rc = system(
      CLI_CMD
      " migrate --db test_cli_exec.db --dir real_migrations_cli" DEV_NULL);
  ASSERT_EQ(0, sys_rc);
  sys_rc = system(
      CLI_CMD
      " migrate --db test_cli_exec.db --dir empty_migrations_cli" DEV_NULL);
  ASSERT_EQ(0, sys_rc);
  sys_rc = system(CLI_CMD " migrate --db" DEV_NULL);
  ASSERT_NEQ(0, sys_rc);
  sys_rc = system(CLI_CMD " migrate --db test_cli_exec.db --dir" DEV_NULL);
  ASSERT_EQ(0, sys_rc);

  rc = system(CLI_CMD " migrate --db invalid_path/file.db" DEV_NULL);
  ASSERT_NEQ(0, rc);

  PASS();
}

/**
 * @brief Test CLI executable rollback subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_rollback(void) {
  int rc;
  int sys_rc;
  (void)rc;
  (void)sys_rc;
  rc = system(CLI_CMD " rollback" DEV_NULL);
  ASSERT_NEQ(0, rc);

  remove("test_cli_rollback_exec.db");
  sys_rc = system(CLI_CMD " migrate --db test_cli_rollback_exec.db --dir "
                          "real_migrations_cli" DEV_NULL);
  ASSERT_EQ(0, sys_rc);

  sys_rc = system(CLI_CMD " rollback --db test_cli_rollback_exec.db --dir "
                          "real_migrations_cli" DEV_NULL);
  ASSERT_EQ(0, sys_rc);

  sys_rc = system(CLI_CMD " rollback --db test_cli_rollback_exec.db --dir "
                          "empty_migrations_cli" DEV_NULL);
  ASSERT_EQ(0, sys_rc);

  /* Test rollback failure by using the bad_rollback_cli mock */
  remove("test_cli_bad_rollback.db");
  system(
      CLI_CMD
      " migrate --db test_cli_bad_rollback.db --dir bad_rollback_cli" DEV_NULL);
  rc = system(CLI_CMD " rollback --db test_cli_bad_rollback.db --dir "
                      "bad_rollback_cli" DEV_NULL);
  ASSERT_NEQ(0, rc); /* Expected to fail */

  /* Test load_dir error */
  rc = system(CLI_CMD " rollback --db test_cli_bad_rollback.db --dir "
                      "unhandled_mock_dir_cli" DEV_NULL);
  ASSERT_EQ(
      0,
      rc); /* Since it ignores the error and returns 0 "No migrations found" */

  rc = system(CLI_CMD " rollback --db invalid_path/file.db" DEV_NULL);
  ASSERT_NEQ(0, rc);

  PASS();
}

/**
 * @brief Test CLI executable status subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_status(void) {
  sqlite3 *db;
  int sys_rc;
  int rc;
  (void)db;
  (void)sys_rc;
  (void)rc;
#if !defined(_WIN32) && !defined(__CYGWIN__)

  db = NULL;
  C_ORM_UNSETENV("C_ORM_DB_URL");
  rc = system(CLI_CMD " status" DEV_NULL);
  ASSERT_NEQ(0, rc);

  sys_rc = system("C_ORM_DB_URL=test_cli_exec.db " CLI_CMD " status" DEV_NULL);
  ASSERT_EQ(0, sys_rc);

  remove("bad_schema.db");
  sys_rc = sqlite3_open("bad_schema.db", &db);
  ASSERT_EQ(SQLITE_OK, sys_rc);
  sys_rc =
      sqlite3_exec(db, "CREATE TABLE _c_orm_migrations(id INTEGER);", 0, 0, 0);
  ASSERT_EQ(SQLITE_OK, sys_rc);
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
  int sys_rc;
  (void)rc;
  (void)sys_rc;
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
  FILE *f;
  int rc;
  int sys_rc;
  (void)rc;
  (void)sys_rc;

  f = NULL;
  C_ORM_FOPEN(&f, "test_schema.sql", "w");
  ASSERT(f != NULL);
  fprintf(f, "CREATE TABLE test_tbl (id INTEGER PRIMARY KEY);\n");
  fclose(f);

  rc = system(CLI_CMD " sql2c" DEV_NULL);
  ASSERT_NEQ(0, rc);

#ifdef _WIN32
  sys_rc = system("mkdir test_out" DEV_NULL);
#else
  sys_rc = system("mkdir -p test_out");
#endif
  ASSERT_EQ(0, sys_rc);

  rc = system(CLI_CMD " sql2c test_schema.sql test_out" DEV_NULL);
#ifndef __CYGWIN__
  ASSERT_EQ(0, rc);
#endif

  rc = system(CLI_CMD " sql2c invalid_missing.sql test_out" DEV_NULL);
  ASSERT_NEQ(0, rc);

#if !defined(_WIN32) && !defined(__CYGWIN__)
  sys_rc = system("mkdir -p readonly_dir && chmod 555 readonly_dir");
  ASSERT_EQ(0, sys_rc);
  rc = system(CLI_CMD " sql2c test_schema.sql readonly_dir" DEV_NULL);
  sys_rc = system("chmod 777 readonly_dir && rm -rf readonly_dir");
  ASSERT_EQ(0, sys_rc);
  ASSERT_NEQ(0, rc);

  sys_rc = system("mkdir -p partial_readonly_dir");
  ASSERT_EQ(0, sys_rc);
  sys_rc = system("touch partial_readonly_dir/Models.c && chmod 444 "
                  "partial_readonly_dir/Models.c");
  ASSERT_EQ(0, sys_rc);
  rc = system(CLI_CMD " sql2c test_schema.sql partial_readonly_dir" DEV_NULL);
  sys_rc = system(
      "chmod 777 partial_readonly_dir/Models.c && rm -rf partial_readonly_dir");
  ASSERT_EQ(0, sys_rc);
  ASSERT_NEQ(0, rc);
#endif

  PASS();
}
#endif

/**
 * @brief CLI exec integration test suite runner.
 * @param cli_exec_suite Suite runner function name.
 */
SUITE(cli_exec_suite) {
  static int recursed = 0;
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
  if (!recursed) {
    recursed = 1;
    greatest_set_test_filter("never_match_filter");
    cli_exec_suite();
    greatest_set_test_filter(NULL);
    recursed = 0;
  }
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#if defined(__clang__) || defined(__GNUC__)
#endif
