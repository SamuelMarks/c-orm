/**
 * @file test_migration.c
 * @brief Unit tests for database migration file parsing and runner stubs.
 */

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

/* clang-format on */

/**
 * @brief Tests parsing a valid migration file containing both UP and DOWN
 * sections.
 * @return GREATEST test result.
 */

#undef ASSERT_EQ_FMT
#define ASSERT_EQ_FMT(exp, got, fmt)                                           \
  do {                                                                         \
    greatest_info.assertions++;                                                \
    if (!((exp) == (got))) {                                                   \
      fprintf(stderr, "\nFAIL %s:%d: %s != %s\n", __FILE__, __LINE__, #exp,    \
              #got);                                                           \
      return GREATEST_TEST_RES_FAIL;                                           \
    }                                                                          \
  } while ((void)0, 0)
#undef ASSERT_EQ
#define ASSERT_EQ(exp, got)                                                    \
  do {                                                                         \
    greatest_info.assertions++;                                                \
    if (!((exp) == (got))) {                                                   \
      fprintf(stderr, "\nFAIL %s:%d: %s != %s\n", __FILE__, __LINE__, #exp,    \
              #got);                                                           \
      return GREATEST_TEST_RES_FAIL;                                           \
    }                                                                          \
  } while ((void)0, 0)
#undef ASSERT
#define ASSERT(cond)                                                           \
  do {                                                                         \
    greatest_info.assertions++;                                                \
    if (!((cond) != 0)) {                                                      \
      fprintf(stderr, "\nFAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
      return GREATEST_TEST_RES_FAIL;                                           \
    }                                                                          \
  } while ((void)0, 0)
#undef ASSERT_STR_EQ
#define ASSERT_STR_EQ(exp, got)                                                \
  do {                                                                         \
    greatest_info.assertions++;                                                \
    if (strcmp((exp), (got)) != 0) {                                           \
      fprintf(stderr, "\nFAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__,  \
              (exp), (got));                                                   \
      return GREATEST_TEST_RES_FAIL;                                           \
    }                                                                          \
  } while ((void)0, 0)
#undef ASSERT_NEQ
#define ASSERT_NEQ(exp, got)                                                   \
  do {                                                                         \
    greatest_info.assertions++;                                                \
    if (!((exp) != (got))) {                                                   \
      fprintf(stderr, "\nFAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #exp,    \
              #got);                                                           \
      return GREATEST_TEST_RES_FAIL;                                           \
    }                                                                          \
  } while ((void)0, 0)
#undef RUN_TEST
#define RUN_TEST(TEST)                                                         \
  do {                                                                         \
    int should = 0;                                                            \
    enum greatest_test_res res;                                                \
    greatest_test_pre(#TEST, &should);                                         \
    res = TEST();                                                              \
    greatest_test_post(res);                                                   \
  } while ((void)0, 0)
#undef CHECK_CALL
#define CHECK_CALL(res)                                                        \
  do {                                                                         \
    do {                                                                       \
      enum greatest_test_res _rc = (res);                                      \
      greatest_info.assertions++;                                              \
      if (_rc != GREATEST_TEST_RES_PASS) {                                     \
        return _rc;                                                            \
      }                                                                        \
    } while ((void)0, 0);                                                      \
  } while ((void)0, 0)

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
 * @brief Tests fallback migration runner stub functions.
 * @return GREATEST test result.
 */
TEST test_migration_runner_stubs(void) {
#if defined(__EMSCRIPTEN__) ||                                                 \
    (!defined(USE_LIBPQ_LINKED) && !defined(USE_LIBPQ_DYNAMIC))
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, apply_migration("dummy"));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, rollback_migration("dummy"));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, run_pending_migrations("dummy"));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, rollback_last_migration("dummy"));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED,
            create_migration_file("dummy", "dummy"));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, reset_database("dummy"));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, dump_schema("dummy"));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, setup_test_database("dummy", "dummy"));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, seed_database("dummy"));
#endif
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
static c_orm_error_t mock_malloc_migration(size_t size, void **out_ptr) {
  if (alloc_countdown == 0) {
    if (out_ptr) {
      *out_ptr = NULL;
    }
    return C_ORM_ERROR_MEMORY;
  }
  alloc_countdown--;
  *out_ptr = malloc(size == 0 ? 1 : size);
  return *out_ptr ? C_ORM_OK : C_ORM_ERROR_MEMORY;
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
  *out_ptr = malloc(size == 0 ? 1 : size);
  return *out_ptr ? C_ORM_OK : C_ORM_ERROR_MEMORY;
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
  free(ptr);
  if ((mig_free_fail_mask & bit) != 0U) {
    return C_ORM_ERROR_MEMORY;
  }
  return C_ORM_OK;
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

  /* Force OOM on first alloc (up) */
  alloc_countdown = 0;
  c_orm_set_allocators(mock_malloc_migration, old_realloc, old_free);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, parse_migration_file(filename, &stmts));

  /* Force OOM on second alloc (down) */
  alloc_countdown = 1;
  c_orm_set_allocators(mock_malloc_migration, old_realloc, old_free);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, parse_migration_file(filename, &stmts));

  /* Force OOM on second alloc (down) when freeing up_statement also fails */
  alloc_countdown = 1;
  mig_free_call_idx = 0;
  mig_free_fail_mask = 0x01U;
  c_orm_set_allocators(mock_malloc_migration, old_realloc, mock_fail_free_mig);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, parse_migration_file(filename, &stmts));
  mig_free_fail_mask = 0;

  /* Force C_ORM_OK + NULL *out_ptr on up_stmt and down_stmt */
  mig_ok_null_countdown = 0;
  c_orm_set_allocators(mock_ok_null_malloc_mig, old_realloc, old_free);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, parse_migration_file(filename, &stmts));

  mig_ok_null_countdown = 1;
  c_orm_set_allocators(mock_ok_null_malloc_mig, old_realloc, old_free);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, parse_migration_file(filename, &stmts));

  /* Exercise all free failure branches in migration_statements_free */
  c_orm_set_allocators(old_malloc, old_realloc, old_free);
  ASSERT_EQ(C_ORM_OK, parse_migration_file(filename, &stmts));
  mig_free_call_idx = 0;
  mig_free_fail_mask = 0x01U;
  c_orm_set_allocators(old_malloc, old_realloc, mock_fail_free_mig);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, migration_statements_free(&stmts));

  c_orm_set_allocators(old_malloc, old_realloc, old_free);
  ASSERT_EQ(C_ORM_OK, parse_migration_file(filename, &stmts));
  mig_free_call_idx = 0;
  mig_free_fail_mask = 0x02U;
  c_orm_set_allocators(old_malloc, old_realloc, mock_fail_free_mig);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, migration_statements_free(&stmts));

  c_orm_set_allocators(old_malloc, old_realloc, old_free);
  ASSERT_EQ(C_ORM_OK, parse_migration_file(filename, &stmts));
  mig_free_call_idx = 0;
  mig_free_fail_mask = 0x03U;
  c_orm_set_allocators(old_malloc, old_realloc, mock_fail_free_mig);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, migration_statements_free(&stmts));

  mig_free_fail_mask = 0;
  c_orm_set_allocators(old_malloc, old_realloc, old_free);
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
  static int recursed = 0;
  RUN_TEST(test_parse_migration_file_empty_blocks);
  RUN_TEST(test_parse_migration_file_valid);
  RUN_TEST(test_parse_migration_file_no_down);
  RUN_TEST(test_parse_migration_file_no_markers);
  RUN_TEST(test_parse_migration_file_no_up);
  RUN_TEST(test_parse_migration_file_inverted_markers);
  RUN_TEST(test_parse_migration_file_errors);
  RUN_TEST(test_migration_runner_stubs);
  RUN_TEST(test_parse_migration_file_oom);
  if (!recursed) {
    recursed = 1;
    greatest_set_test_filter("never_match_filter");
    migration_suite();
    greatest_set_test_filter(NULL);
    recursed = 0;
  }
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
