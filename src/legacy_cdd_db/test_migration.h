/**
 * @file test_migration.h
 * @brief Unit tests for database migration file parsing and runner stubs.
 */

#ifndef C_CDD_TEST_MIGRATION_H
#define C_CDD_TEST_MIGRATION_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "functions/parse/fs.h"
#include "migration.h"
#include "migration_runner.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_assert_zero_branch.h"
/* clang-format on */

/**
 * @brief Executes test_parse_migration_file_valid.
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

/**
 * @brief Tests migration runner validation and unset-environment error paths.
 * @return GREATEST test result.
 */
TEST test_migration_runner_stubs(void) {
  ASSERT_EQ(C_ORM_OK, C_ORM_UNSETENV("DATABASE_URL"));
  ASSERT_EQ(C_ORM_OK, C_ORM_UNSETENV("C_ORM_DB_URL"));

  ASSERT_EQ(C_ORM_ERROR_VALIDATION, apply_migration(NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, rollback_migration(NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, run_pending_migrations("dummy"));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, rollback_last_migration("dummy"));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, create_migration_file(NULL, "dummy"));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, reset_database("dummy"));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, dump_schema("dummy"));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, setup_test_database(NULL, "dummy"));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, seed_database(NULL));
  PASS();
}

/** @brief Counter for controlling allocation failures. */
static int alloc_countdown = 0;

/**
 * @brief Mock malloc callback for simulated OOM testing.
 * @param size Requested allocation size.
 * @param out_ptr Output pointer receiving allocated memory or NULL.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on failure.
 */
static c_orm_error_t mock_malloc_migration(size_t size, void **out_ptr) {
  if (alloc_countdown == 0) {
    if (out_ptr) {
      *out_ptr = NULL;
    }
    return C_ORM_ERROR_MEMORY;
  }
  alloc_countdown--;
  return c_orm_default_malloc(size, out_ptr);
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
  c_orm_malloc_fn_t orig_malloc_migration;
  c_orm_error_t rc;

  orig_malloc_migration = c_orm_malloc;

  rc = (c_orm_error_t)fs_write_to_file(filename, content);
  ASSERT_EQ(C_ORM_OK, rc);

  /* Force OOM on file_data (0), up_stmt (1), and down_stmt (2) */
  alloc_countdown = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(mock_malloc_migration, c_orm_realloc,
                                           c_orm_free));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, parse_migration_file(filename, &stmts));

  alloc_countdown = 1;
  ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(mock_malloc_migration, c_orm_realloc,
                                           c_orm_free));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, parse_migration_file(filename, &stmts));

  alloc_countdown = 2;
  ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(mock_malloc_migration, c_orm_realloc,
                                           c_orm_free));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, parse_migration_file(filename, &stmts));

  ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(orig_malloc_migration, c_orm_realloc,
                                           c_orm_free));
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

  migration_statements_free(&stmts);
  remove(filename);

  /* also test single empty marker at end of file */
  content = "-- UP";
  rc = (c_orm_error_t)fs_write_to_file(filename, content);
  ASSERT_EQ(0, rc);
  rc = parse_migration_file(filename, &stmts);
  ASSERT_EQ(0, rc);
  migration_statements_free(&stmts);

  content = "-- DOWN";
  rc = (c_orm_error_t)fs_write_to_file(filename, content);
  ASSERT_EQ(0, rc);
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

#endif /* C_CDD_TEST_MIGRATION_H */
