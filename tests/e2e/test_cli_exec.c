/* clang-format off */
#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
/* clang-format on */

#ifdef _WIN32
#define CLI_CMD "..\\..\\bin\\c-orm-cli"
#else
#define CLI_CMD "../../bin/c-orm-cli"
#endif

TEST test_cli_help(void) {
  int rc = system(CLI_CMD " --help >/dev/null 2>&1");
  ASSERT_NEQ(0, rc); /* help is unknown command, returns 1 */
  PASS();
}

TEST test_cli_no_args(void) {
  int rc = system(CLI_CMD " >/dev/null 2>&1");
  ASSERT_NEQ(0, rc);
  PASS();
}

TEST test_cli_init(void) {
  int rc = system(CLI_CMD " init --dir test_migrations_dir >/dev/null 2>&1");
  ASSERT_EQ(0, rc);
  rc = system(CLI_CMD " init --dir test_migrations_dir >/dev/null 2>&1");
  ASSERT_EQ(0, rc);
  PASS();
}

TEST test_cli_create(void) {
  int rc = system(CLI_CMD " create >/dev/null 2>&1");
  ASSERT_NEQ(0, rc);
  rc = system(CLI_CMD
              " create my_mig --dir test_migrations_dir >/dev/null 2>&1");
  ASSERT_EQ(0, rc);
  PASS();
}

TEST test_cli_generate(void) {
  int rc = system(CLI_CMD " generate >/dev/null 2>&1");
  ASSERT_EQ(0, rc);
  PASS();
}

TEST test_cli_migrate(void) {
#ifdef _WIN32
  system("set C_ORM_DB_URL=");
#else
  unsetenv("C_ORM_DB_URL");
#endif
  int rc = system(CLI_CMD " migrate >/dev/null 2>&1");
  ASSERT_NEQ(0, rc);
  rc = system(
      CLI_CMD
      " migrate --db :memory: --dir test_migrations_dir >/dev/null 2>&1");
  ASSERT_EQ(0, rc);
  rc = system(CLI_CMD " migrate --db invalid_path/file.db >/dev/null 2>&1");
  PASS();
}

TEST test_cli_rollback(void) {
  int rc = system(CLI_CMD " rollback >/dev/null 2>&1");
  ASSERT_EQ(0, rc);
  PASS();
}

TEST test_cli_status(void) {
#ifdef _WIN32
  system("set C_ORM_DB_URL=");
#else
  unsetenv("C_ORM_DB_URL");
#endif
  int rc = system(CLI_CMD " status >/dev/null 2>&1");
  ASSERT_NEQ(0, rc);
  rc = system(CLI_CMD " status --db :memory: >/dev/null 2>&1");
  ASSERT_EQ(0, rc);
  PASS();
}

TEST test_cli_unknown(void) {
  int rc = system(CLI_CMD " unknown_command >/dev/null 2>&1");
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
