#if defined(__clang__) || defined(__GNUC__)
#endif
/* clang-format off */
#include "test_utils.h"



#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
#include "c_orm_api.h"
#include "sqlite3.h"
/* clang-format on */

static void test_unsetenv(const char *name) {
#if defined(_WIN32)
  char buf[128];
#if defined(_MSC_VER)
  sprintf_s(buf, sizeof(buf), "%s=", name);
#else
  sprintf(buf, "%s=", name);
#endif
  _putenv(buf);
#else
  extern int unsetenv(const char *name);
  unsetenv(name);
#endif
}

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

TEST test_cli_help(void) {
  int rc = system(CLI_CMD " --help" DEV_NULL);
  printf("SYSTEM RETURNED %d\n", rc);
  ASSERT_NEQ(0, rc); /* help is unknown command, returns 1 */
  PASS();
}

TEST test_cli_no_args(void) {
  int rc = system(CLI_CMD DEV_NULL);
  printf("SYSTEM RETURNED %d\n", rc);
  ASSERT_NEQ(0, rc);
  PASS();
}

TEST test_cli_init(void) {
  int rc;
  (void)rc;
#ifdef _WIN32
  system("rmdir /S /Q test_migrations_dir_init 2>nul");
#else
  system("rm -rf test_migrations_dir_init");
#endif
  rc = system(CLI_CMD " init --dir test_migrations_dir_init" DEV_NULL);
  ASSERT_EQ(0, rc);
  rc = system(CLI_CMD " init --dir test_migrations_dir_init" DEV_NULL);
  ASSERT_EQ(0, rc);
  PASS();
}

TEST test_cli_create(void) {
  int rc = system(CLI_CMD " create" DEV_NULL);
  printf("SYSTEM RETURNED %d\n", rc);
  ASSERT_NEQ(0, rc);
  rc = system(CLI_CMD " create my_mig --dir test_migrations_dir" DEV_NULL);
  ASSERT_EQ(0, rc);
  PASS();
}

TEST test_cli_generate(void) {
  int rc = system(CLI_CMD " generate" DEV_NULL);
  ASSERT_EQ(0, rc);
  PASS();
}

TEST test_cli_migrate(void) {
  int rc;
  (void)rc;
#if !defined(_WIN32) && !defined(__CYGWIN__)
  test_unsetenv("C_ORM_DB_URL");
  rc = system(CLI_CMD " migrate" DEV_NULL);
  printf("SYSTEM RETURNED %d\n", rc);
  ASSERT_NEQ(0, rc);
#endif
  rc = system(CLI_CMD
              " migrate --db :memory: --dir test_migrations_dir" DEV_NULL);
  ASSERT_EQ(0, rc);

  {
    sqlite3 *db;
    sqlite3_open("test_cli_exec.db", &db);
    sqlite3_exec(
        db,
        "CREATE TABLE IF NOT EXISTS _c_orm_migrations (id INTEGER PRIMARY KEY, "
        "version TEXT, name TEXT, hash TEXT, applied_at DATETIME);",
        0, 0, 0);
    sqlite3_exec(db,
                 "INSERT INTO _c_orm_migrations (version, name, hash) VALUES "
                 "('1', 'test', 'hash');",
                 0, 0, 0);
    sqlite3_close(db);
  }
  system(CLI_CMD " status --db test_cli_exec.db" DEV_NULL);
#ifdef _WIN32
  system("mkdir test_migrations_dir 2>nul");
#else
  system("mkdir -p test_migrations_dir 2>/dev/null");
#endif
  {
    FILE *f1;
    FILE *f2;
#if defined(_MSC_VER)
    fopen_s(&f1, "test_migrations_dir/1_test.up.sql", "w");
#else
    f1 = fopen("test_migrations_dir/1_test.up.sql", "w");
#endif

    if (f1) {
      fprintf(f1, "CREATE TABLE x (id INT);\n");
      fclose(f1);
    }
#if defined(_MSC_VER)
    fopen_s(&f2, "test_migrations_dir/1_test.down.sql", "w");
#else
    f2 = fopen("test_migrations_dir/1_test.down.sql", "w");
#endif

    if (f2) {
      fprintf(f2, "DROP TABLE x;\n");
      fclose(f2);
    }
  }
  system(CLI_CMD
         " migrate --db test_cli_exec.db --dir test_migrations_dir" DEV_NULL);

  rc = system(CLI_CMD " migrate --db invalid_path/file.db" DEV_NULL);
  PASS();
}

TEST test_cli_rollback(void) {
  int rc = system(CLI_CMD " rollback" DEV_NULL);
  ASSERT_EQ(0, rc);
  PASS();
}

TEST test_cli_status(void) {
  int rc;
  (void)rc;

#if !defined(_WIN32) && !defined(__CYGWIN__)
  test_unsetenv("C_ORM_DB_URL");
  rc = system(CLI_CMD " status" DEV_NULL);
  printf("SYSTEM RETURNED %d\n", rc);
  ASSERT_NEQ(0, rc);

  remove("bad_schema.db");
  {
    sqlite3 *db;
    sqlite3_open("bad_schema.db", &db);
    sqlite3_exec(db, "CREATE TABLE _c_orm_migrations(id INTEGER);", 0, 0, 0);
    sqlite3_close(db);
  }
  rc = system(CLI_CMD " status --db bad_schema.db" DEV_NULL);
  printf("SYSTEM RETURNED %d\n", rc);
  ASSERT_NEQ(0, rc);

  rc = system(CLI_CMD " status --db /root/invalid.db" DEV_NULL);
  printf("SYSTEM RETURNED %d\n", rc);
  ASSERT_NEQ(0, rc);
  (void)rc;
#endif
  PASS();
}

TEST test_cli_unknown(void) {
  int rc = system(CLI_CMD " unknown_command" DEV_NULL);
  printf("SYSTEM RETURNED %d\n", rc);
  ASSERT_NEQ(0, rc);
  PASS();
}

#ifndef __EMSCRIPTEN__
TEST test_cli_exec_sql2c(void) {
  int rc;
  FILE *f;
  (void)rc;
#if defined(_MSC_VER)
  fopen_s(&f, "test_schema.sql", "w");
#else
  f = fopen("test_schema.sql", "w");
#endif

  if (f) {
    fprintf(f, "CREATE TABLE test_tbl (id INTEGER PRIMARY KEY);\n");
    fclose(f);
  }
  rc = system(CLI_CMD " sql2c" DEV_NULL);
  printf("SYSTEM RETURNED %d\n", rc);
  ASSERT_NEQ(0, rc);

#ifdef _WIN32
  system("mkdir test_out" DEV_NULL);
#else
  system("mkdir -p test_out");
#endif
  rc = system(CLI_CMD " sql2c test_schema.sql test_out" DEV_NULL);
#ifndef __CYGWIN__
  ASSERT_EQ(0, rc);
#endif

  rc = system(CLI_CMD " sql2c invalid_missing.sql test_out" DEV_NULL);
  printf("SYSTEM RETURNED %d\n", rc);
  ASSERT_NEQ(0, rc);

#if !defined(_WIN32) && !defined(__CYGWIN__)
  system("mkdir -p readonly_dir && chmod 555 readonly_dir");
  rc = system(CLI_CMD " sql2c test_schema.sql readonly_dir" DEV_NULL);
  system("chmod 777 readonly_dir && rm -rf readonly_dir");
  printf("SYSTEM RETURNED %d\n", rc);
  ASSERT_NEQ(0, rc);

  system("mkdir -p partial_readonly_dir");
  system("touch partial_readonly_dir/Models.c && chmod 444 "
         "partial_readonly_dir/Models.c");
  rc = system(CLI_CMD " sql2c test_schema.sql partial_readonly_dir" DEV_NULL);
  system(
      "chmod 777 partial_readonly_dir/Models.c && rm -rf partial_readonly_dir");
  printf("SYSTEM RETURNED %d\n", rc);
  ASSERT_NEQ(0, rc);
#endif

  PASS();
}
#endif
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

#if defined(__clang__) || defined(__GNUC__)
#endif

static void dummy_suppress_unused_exec(void) { (void)test_unsetenv; }
