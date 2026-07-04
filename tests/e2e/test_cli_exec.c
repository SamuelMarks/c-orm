/* clang-format off */
#include "greatest.h"
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
  system("rm -rf test_migrations_dir_init");
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
#if !defined(_WIN32) && !defined(__CYGWIN__)
  unsetenv("C_ORM_DB_URL");
  rc = system(CLI_CMD " migrate" DEV_NULL);
  printf("SYSTEM RETURNED %d\n", rc);
  ASSERT_NEQ(0, rc);
#endif
  rc = system(CLI_CMD
              " migrate --db :memory: --dir test_migrations_dir" DEV_NULL);
  ASSERT_EQ(0, rc);

  system("sqlite3 test_cli_exec.db \"CREATE TABLE IF NOT EXISTS "
         "_c_orm_migrations (id INTEGER PRIMARY KEY, version TEXT, name TEXT, "
         "hash TEXT, applied_at DATETIME);\"");
  system("sqlite3 test_cli_exec.db \"INSERT INTO _c_orm_migrations (version, "
         "name, hash) VALUES ('1', 'test', 'hash');\"");
  system(CLI_CMD " status --db test_cli_exec.db" DEV_NULL);
  system("echo 'CREATE TABLE x (id INT);' > test_migrations_dir/1_test.up.sql");
  system("echo 'DROP TABLE x;' > test_migrations_dir/1_test.down.sql");
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
#if !defined(_WIN32) && !defined(__CYGWIN__)
  unsetenv("C_ORM_DB_URL");
  rc = system(CLI_CMD " status" DEV_NULL);
  printf("SYSTEM RETURNED %d\n", rc);
  ASSERT_NEQ(0, rc);

  system("rm -f bad_schema.db && sqlite3 bad_schema.db \"CREATE TABLE "
         "_c_orm_migrations(id INTEGER);\"");
  rc = system(CLI_CMD " status --db bad_schema.db" DEV_NULL);
  printf("SYSTEM RETURNED %d\n", rc);
  ASSERT_NEQ(0, rc);

  rc = system(CLI_CMD " status --db /root/invalid.db" DEV_NULL);
  printf("SYSTEM RETURNED %d\n", rc);
  ASSERT_NEQ(0, rc);
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
  f = fopen("test_schema.sql", "w");
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
  ASSERT_EQ(0, rc);

  rc = system(CLI_CMD " sql2c invalid_missing.sql test_out" DEV_NULL);
  printf("SYSTEM RETURNED %d\n", rc);
  ASSERT_NEQ(0, rc);

#ifdef _WIN32
  system("mkdir test_schema_dir" DEV_NULL);
#else
  system("mkdir -p test_schema_dir");
#endif
  rc = system(CLI_CMD " sql2c test_schema_dir test_out" DEV_NULL);
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
