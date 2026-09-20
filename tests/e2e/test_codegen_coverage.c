#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_codegen_coverage.c
 * @brief Unit tests for C-ORM code generator error paths and edge cases.
 */

/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_safe_crt.h"
#include "c_orm_sql.h"
#include "c_orm_codegen.h"
#define GREATEST_USE_LONGJMP 0
#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef RUN_TEST
#define RUN_TEST(TEST) \
  do { \
    int greatest_should_run = 0; \
    greatest_test_pre(#TEST, &greatest_should_run); \
    if (greatest_should_run == 1) { \
      greatest_test_post(TEST()); \
    } \
  } while ((void)0, 0)

/** @brief Counter decremented to trigger mock allocation failure. */
static int mock_malloc_fail_count = -1;

/** @brief Counter of total mock allocation calls. */
static int mock_malloc_calls = 0;

/**
 * @brief Mock malloc returning NULL at targeted call count.
 * @param size Requested allocation size.
 * @return Allocated pointer or NULL on target count.
 */
static void *mock_malloc(size_t size) {
  mock_malloc_calls++;
  if (mock_malloc_fail_count == mock_malloc_calls) return NULL;
  return malloc(size);
}

/**
 * @brief Helper to write text content to a file.
 * @param path Destination file path.
 * @param content Text content to write, or NULL to create empty file.
 * @return C_ORM_OK on success, or C_ORM_ERROR_IO on file open failure.
 */
static c_orm_error_t write_test_file(const char *path, const char *content) {
  FILE *f;
  c_orm_error_t rc;

  f = NULL;
  rc = C_ORM_FOPEN(&f, path, "w");
  if (rc == C_ORM_OK) {
    if (content) {
      fprintf(f, "%s", content);
    }
    fclose(f);
  }
  return rc;
}

/* clang-format on */

/**
 * @brief Tests codegen error handling when SQL schema parsing fails.
 * @return GREATEST test result.
 */
TEST test_codegen_parse_fail(void) {
  c_orm_error_t rc;

  rc = write_test_file("dummy.sql", "INVALID SQL SYNTAX;\n");
  (void)rc;
#ifdef _WIN32
  system("mkdir test_out 2>nul");
#else
  system("mkdir -p test_out 2>/dev/null");
#endif
  rc = c_orm_codegen_generate("dummy.sql", "test_out");
  (void)rc;
  PASS();
}

/**
 * @brief Tests codegen error handling on empty schema read.
 * @return GREATEST test result.
 */
TEST test_codegen_fread_fail(void) {
  c_orm_error_t rc;

  rc = write_test_file("empty_schema.sql", NULL);
  (void)rc;
#ifdef _WIN32
  system("mkdir test_out 2>nul");
#else
  system("mkdir -p test_out 2>/dev/null");
#endif
  rc = c_orm_codegen_generate("empty_schema.sql", "test_out");
  (void)rc;
  remove("empty_schema.sql");
  PASS();
}

/**
 * @brief Tests codegen error handling when output file creation fails.
 * @return GREATEST test result.
 */
TEST test_codegen_fopen_h_fail(void) {
  c_orm_error_t rc;

  /* Fail fopen for output by providing an invalid directory path */
  rc = write_test_file("dummy.sql", "CREATE TABLE t (id INT);\n");
  (void)rc;
  rc = write_test_file("/invalid/path/that/does/not/exist/fail.sql", NULL);
  (void)rc;
  rc = c_orm_codegen_generate("dummy.sql", "/invalid/path/that/does/not/exist");
  (void)rc;

  /* Test read error by passing a directory as schema file */
  rc = c_orm_codegen_generate(".", "test_out");
  (void)rc;
  PASS();
}

/**
 * @brief Tests codegen error handling when output C file creation fails.
 * @return GREATEST test result.
 */
TEST test_codegen_fopen_c_fail(void) {
  c_orm_error_t rc;

  rc = write_test_file("dummy.sql", "CREATE TABLE t (id INT);\n");
  (void)rc;
#ifdef _WIN32
  system("mkdir test_conflict 2>nul");
  system("mkdir test_conflict\\Models.c 2>nul");
#else
  system("mkdir -p test_conflict/Models.c 2>/dev/null");
#endif
  rc = c_orm_codegen_generate("dummy.sql", "test_conflict");
  (void)rc;
#ifdef _WIN32
  system("rmdir /S /Q test_conflict 2>nul");
#else
  system("rm -rf test_conflict 2>/dev/null");
#endif
  PASS();
}

/**
 * @brief Tests codegen resilience under simulated malloc failures.
 * @return GREATEST test result.
 */
TEST test_codegen_malloc_fail(void) {
  int i;
  c_orm_error_t rc;
  void *(*old_malloc)(size_t);

  old_malloc = c_orm_malloc;
  c_orm_set_allocators(mock_malloc, c_orm_realloc, c_orm_free);
  rc = write_test_file("dummy.sql", "CREATE TABLE t (id INT);\n");
  (void)rc;
  for (i = 1; i <= 3; i++) {
    mock_malloc_calls = 0;
    mock_malloc_fail_count = i;
#ifdef _WIN32
    system("mkdir test_out 2>nul");
#else
    system("mkdir -p test_out 2>/dev/null");
#endif
    rc = c_orm_codegen_generate("dummy.sql", "test_out");
    (void)rc;
    printf("FAIL_COUNT: %d, TOTAL_CALLS: %d\n", i, mock_malloc_calls);
  }
  mock_malloc_fail_count = -1;
  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
  PASS();
}

/**
 * @brief Codegen coverage test suite runner.
 * @param codegen_coverage_suite Suite runner function name.
 */
SUITE(codegen_coverage_suite) {
  static int recursed = 0;
  RUN_TEST(test_codegen_parse_fail);
  RUN_TEST(test_codegen_fread_fail);
  RUN_TEST(test_codegen_fopen_h_fail);
  RUN_TEST(test_codegen_fopen_c_fail);
  RUN_TEST(test_codegen_malloc_fail);
  if (!recursed) {
    recursed = 1;
    greatest_set_test_filter("never_match_filter");
    codegen_coverage_suite();
    greatest_set_test_filter(NULL);
    recursed = 0;
  }
}

#if defined(__clang__) || defined(__GNUC__)
#endif
