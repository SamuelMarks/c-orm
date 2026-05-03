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
#define CLI_CMD C_ORM_CLI_EXECUTABLE
#define DEV_NULL " >/dev/null 2>&1"
#endif

TEST test_cli_help(void) {
  int rc = system(CLI_CMD " --help" DEV_NULL);
  ASSERT_NEQ(0, rc); /* help is unknown command, returns 1 */
  PASS();
}

TEST test_cli_no_args(void) {
  int rc = system(CLI_CMD DEV_NULL);
  ASSERT_NEQ(0, rc);
  PASS();
}

TEST test_cli_init(void) {
  int rc = system(CLI_CMD " init --dir test_migrations_dir" DEV_NULL);
  ASSERT_EQ(0, rc);
  rc = system(CLI_CMD " init --dir test_migrations_dir" DEV_NULL);
  ASSERT_EQ(0, rc);
  PASS();
}

TEST test_cli_create(void) {
  int rc = system(CLI_CMD " create" DEV_NULL);
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
#ifdef _WIN32
  _putenv("C_ORM_DB_URL=");
#else
  unsetenv("C_ORM_DB_URL");
#endif
  rc = system(CLI_CMD " migrate" DEV_NULL);
  ASSERT_NEQ(0, rc);
  rc = system(CLI_CMD
              " migrate --db :memory: --dir test_migrations_dir" DEV_NULL);
  ASSERT_EQ(0, rc);
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
#ifdef _WIN32
  _putenv("C_ORM_DB_URL=");
#else
  unsetenv("C_ORM_DB_URL");
#endif
  rc = system(CLI_CMD " status" DEV_NULL);
  ASSERT_NEQ(0, rc);
  rc = system(CLI_CMD " status --db :memory:" DEV_NULL);
  ASSERT_EQ(0, rc);
  PASS();
}

TEST test_cli_unknown(void) {
  int rc = system(CLI_CMD " unknown_command" DEV_NULL);
  ASSERT_NEQ(0, rc);
  PASS();
}

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
}
