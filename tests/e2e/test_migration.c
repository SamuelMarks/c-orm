/* clang-format off */
#include "c_orm_safe_crt.h"
#include "migration.h"
#include "migration_runner.h"
#include "functions/parse/fs.h"
#include <greatest.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
/* clang-format on */

TEST test_parse_migration_file_valid(void) {
  const char *filename = "test_valid_migration.sql";
  const char *content = "-- UP\n"
                        "CREATE TABLE test (id INT);\n"
                        "-- DOWN\n"
                        "DROP TABLE test;\n";
  struct MigrationStatements stmts;
  int rc;

  rc = fs_write_to_file(filename, content);
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

TEST test_parse_migration_file_no_down(void) {
  const char *filename = "test_up_only.sql";
  const char *content = "-- UP\n"
                        "CREATE TABLE test2 (id INT);\n";
  struct MigrationStatements stmts;
  int rc;

  rc = fs_write_to_file(filename, content);
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

TEST test_parse_migration_file_no_markers(void) {
  const char *filename = "test_no_markers.sql";
  const char *content = "CREATE TABLE test3 (id INT);\n";
  struct MigrationStatements stmts;
  int rc;

  rc = fs_write_to_file(filename, content);
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

TEST test_parse_migration_file_no_up(void) {
  const char *filename = "test_down_only.sql";
  const char *content = "-- DOWN\n"
                        "DROP TABLE test3;\n";
  struct MigrationStatements stmts;
  int rc;

  rc = fs_write_to_file(filename, content);
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

TEST test_parse_migration_file_inverted_markers(void) {
  const char *filename = "test_inverted_markers.sql";
  const char *content = "-- DOWN\n"
                        "DROP TABLE test3;\n"
                        "-- UP\n"
                        "CREATE TABLE test3 (id INT);\n";
  struct MigrationStatements stmts;
  int rc;

  rc = fs_write_to_file(filename, content);
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

TEST test_parse_migration_file_errors(void) {
  struct MigrationStatements stmts;

  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, parse_migration_file(NULL, &stmts));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, parse_migration_file("test.sql", NULL));

  /* file not found */
  ASSERT_NEQ(C_ORM_OK, parse_migration_file("nonexistent.sql", &stmts));

  /* Empty file */
  {
    const char *filename = "empty.sql";
    fs_write_to_file(filename, "");
    ASSERT_EQ(0, parse_migration_file(filename, &stmts));
    ASSERT_EQ(NULL, stmts.up_statement);
    ASSERT_EQ(NULL, stmts.down_statement);
    remove(filename);
  }

  migration_statements_free(NULL);
  migration_statements_init(NULL);

  PASS();
}

TEST test_migration_runner_stubs(void) {
#if defined(__EMSCRIPTEN__) ||                                                 \
    (!defined(USE_LIBPQ_LINKED) && !defined(USE_LIBPQ_DYNAMIC))
  ASSERT_EQ(ENOSYS, apply_migration("dummy"));
  ASSERT_EQ(ENOSYS, rollback_migration("dummy"));
  ASSERT_EQ(ENOSYS, run_pending_migrations("dummy"));
  ASSERT_EQ(ENOSYS, rollback_last_migration("dummy"));
  ASSERT_EQ(ENOSYS, create_migration_file("dummy", "dummy"));
  ASSERT_EQ(ENOSYS, reset_database("dummy"));
  ASSERT_EQ(ENOSYS, dump_schema("dummy"));
  ASSERT_EQ(ENOSYS, setup_test_database("dummy", "dummy"));
  ASSERT_EQ(ENOSYS, seed_database("dummy"));
#endif
  PASS();
}

static int alloc_countdown = 0;
static void *mock_malloc_migration(size_t size) {
  if (alloc_countdown == 0)
    return NULL;
  alloc_countdown--;
  return malloc(size);
}

TEST test_parse_migration_file_oom(void) {
  const char *filename = "test_oom_migration.sql";
  const char *content = "-- UP\n"
                        "CREATE TABLE test (id INT);\n"
                        "-- DOWN\n"
                        "DROP TABLE test;\n";
  struct MigrationStatements stmts;
  void *(*old_malloc)(size_t) = c_orm_malloc;

  fs_write_to_file(filename, content);

  /* Force OOM on first alloc (up) */
  alloc_countdown = 0;
  c_orm_set_allocators(mock_malloc_migration, c_orm_realloc, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, parse_migration_file(filename, &stmts));

  /* Force OOM on second alloc (down) */
  alloc_countdown = 1;
  c_orm_set_allocators(mock_malloc_migration, c_orm_realloc, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, parse_migration_file(filename, &stmts));

  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
  remove(filename);
  PASS();
}
C_ORM_EXPORT extern int c_orm_mock_migration_statements_init_fail;

TEST test_parse_migration_file_empty_blocks(void) {
  const char *filename = "test_empty_blocks.sql";
  const char *content = "-- UP\n-- DOWN\n";
  struct MigrationStatements stmts;
  int rc;

  rc = fs_write_to_file(filename, content);
  ASSERT_EQ(0, rc);

  rc = parse_migration_file(filename, &stmts);
  ASSERT_EQ(0, rc);
  ASSERT(stmts.up_statement != NULL);
  ASSERT(stmts.down_statement != NULL);

  migration_statements_free(&stmts);
  remove(filename);

  /* also test single empty marker at end of file */
  content = "-- UP";
  fs_write_to_file(filename, content);
  rc = parse_migration_file(filename, &stmts);
  ASSERT_EQ(0, rc);
  migration_statements_free(&stmts);

  content = "-- DOWN";
  fs_write_to_file(filename, content);
  rc = parse_migration_file(filename, &stmts);
  ASSERT_EQ(0, rc);
  migration_statements_free(&stmts);
  remove(filename);

  /* test init fail */
  c_orm_mock_migration_statements_init_fail = 1;
  rc = parse_migration_file("dummy.sql", &stmts);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  c_orm_mock_migration_statements_init_fail = 0;

  PASS();
}

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
