/**
 * @file test_codegen_coverage.c
 * @brief Unit tests for C-ORM code generator error paths and edge cases.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_safe_crt.h"
#include "c_orm_sql.h"
#include "c_orm_codegen.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include "test_assert_zero_branch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32) || defined(_MSC_VER)
#include <direct.h>
#define TEST_MKDIR(p) _mkdir(p)
#define TEST_RMDIR(p) _rmdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define TEST_MKDIR(p) mkdir(p, 0777)
#define TEST_RMDIR(p) rmdir(p)
#endif
/* clang-format on */

/** @brief Counter decremented to trigger mock allocation failure. */
static int mock_malloc_fail_count = -1;

/** @brief Counter of total mock allocation calls. */
static int mock_malloc_calls = 0;

/**
 * @brief Mock malloc returning error at targeted call count.
 * @param size Parameter size.
 * @param out_ptr Output pointer receiving allocated address.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on failure.
 */
static c_orm_error_t mock_malloc(size_t size, void **out_ptr) {
  mock_malloc_calls++;
  if (mock_malloc_fail_count == mock_malloc_calls) {
    if (out_ptr) {
      *out_ptr = NULL;
    }
    return C_ORM_ERROR_MEMORY;
  }
  return c_orm_default_malloc(size, out_ptr);
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

/**
 * @brief Tests codegen error handling when SQL schema parsing fails.
 * @return GREATEST test result.
 */
TEST test_codegen_parse_fail(void) {
  c_orm_error_t rc;

  rc = write_test_file("dummy.sql", "INVALID SQL SYNTAX;\n");
  ASSERT_EQ(C_ORM_OK, rc);
  TEST_MKDIR("test_out");
  rc = c_orm_codegen_generate("dummy.sql", "test_out");
  ASSERT(rc != C_ORM_OK);
  PASS();
}

/**
 * @brief Tests codegen error handling on empty schema read.
 * @return GREATEST test result.
 */
TEST test_codegen_fread_fail(void) {
  c_orm_error_t rc;

  rc = write_test_file("empty_schema.sql", NULL);
  ASSERT_EQ(C_ORM_OK, rc);
  TEST_MKDIR("test_out");
  rc = c_orm_codegen_generate("empty_schema.sql", "test_out");
  ASSERT(rc != C_ORM_OK);
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
  ASSERT_EQ(C_ORM_OK, rc);
  rc = write_test_file("/invalid/path/that/does/not/exist/fail.sql", NULL);
  ASSERT(rc != C_ORM_OK);
  rc = c_orm_codegen_generate("dummy.sql", "/invalid/path/that/does/not/exist");
  ASSERT(rc != C_ORM_OK);

  /* Test read error by passing a directory as schema file */
  rc = c_orm_codegen_generate(".", "test_out");
  ASSERT(rc != C_ORM_OK);
  PASS();
}

/**
 * @brief Tests codegen error handling when output C file creation fails.
 * @return GREATEST test result.
 */
TEST test_codegen_fopen_c_fail(void) {
  c_orm_error_t rc;

  rc = write_test_file("dummy.sql", "CREATE TABLE t (id INT);\n");
  ASSERT_EQ(C_ORM_OK, rc);
  TEST_MKDIR("test_conflict");
  TEST_MKDIR("test_conflict/Models.c");
  rc = c_orm_codegen_generate("dummy.sql", "test_conflict");
  ASSERT(rc != C_ORM_OK);
  remove("test_conflict/Models.h");
  TEST_RMDIR("test_conflict/Models.c");
  TEST_RMDIR("test_conflict");
  PASS();
}

/**
 * @brief Tests codegen resilience under simulated malloc failures.
 * @return GREATEST test result.
 */
TEST test_codegen_malloc_fail(void) {
  int i;
  int total_calls;
  c_orm_error_t rc;
  c_orm_malloc_fn_t old_malloc;

  old_malloc = c_orm_malloc;
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(mock_malloc, c_orm_realloc, c_orm_free));
  mock_malloc_calls = 0;
  mock_malloc_fail_count = 1;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, mock_malloc(16, NULL));

  rc = write_test_file("dummy.sql", "CREATE TABLE t (id INT);\n");
  ASSERT_EQ(C_ORM_OK, rc);
  TEST_MKDIR("test_out");
  mock_malloc_calls = 0;
  mock_malloc_fail_count = -1;
  rc = c_orm_codegen_generate("dummy.sql", "test_out");
  ASSERT_EQ(C_ORM_OK, rc);
  total_calls = mock_malloc_calls;

  for (i = 1; i <= total_calls; i++) {
    mock_malloc_calls = 0;
    mock_malloc_fail_count = i;
    TEST_MKDIR("test_out");
    rc = c_orm_codegen_generate("dummy.sql", "test_out");
    ASSERT(rc != C_ORM_OK);
  }
  mock_malloc_fail_count = -1;
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free));
  PASS();
}

/**
 * @brief Tests codegen handling when header or source emit fails.
 * @return GREATEST test result.
 */
TEST test_codegen_emit_fail(void) {
  c_orm_error_t rc;

  rc = write_test_file("dummy.sql", "CREATE TABLE t (id INT);\n");
  ASSERT_EQ(C_ORM_OK, rc);
  TEST_MKDIR("test_out");

  c_orm_mock_codegen_header_emit_fail = 1;
  rc = c_orm_codegen_generate("dummy.sql", "test_out");
  ASSERT(rc != C_ORM_OK);
  c_orm_mock_codegen_header_emit_fail = 0;

  c_orm_mock_codegen_source_emit_fail = 1;
  rc = c_orm_codegen_generate("dummy.sql", "test_out");
  ASSERT(rc != C_ORM_OK);
  c_orm_mock_codegen_source_emit_fail = 0;

  c_orm_mock_codegen_io_fail = 1;
  rc = c_orm_codegen_generate("dummy.sql", "test_out");
  ASSERT_EQ(C_ORM_ERROR_IO, rc);
  c_orm_mock_codegen_io_fail = 2;
  rc = c_orm_codegen_generate("dummy.sql", "test_out");
  ASSERT_EQ(C_ORM_ERROR_IO, rc);
  c_orm_mock_codegen_io_fail = 0;

  ASSERT_NEQ(C_ORM_OK, c_orm_codegen_generate(NULL, "test_out"));
  ASSERT_NEQ(C_ORM_OK, c_orm_codegen_generate("dummy.sql", NULL));
  ASSERT_NEQ(C_ORM_OK, c_orm_codegen_generate("x", "test_out"));
  ASSERT_NEQ(C_ORM_OK,
             c_orm_codegen_generate("nonexistent_test.CPY", "test_out"));
  ASSERT_NEQ(C_ORM_OK,
             c_orm_codegen_generate("nonexistent_test.COB", "test_out"));
  rc = write_test_file("valid_min.cpy",
                       "000100 01 MIN-REC.\n000200    05 FLD PIC 9(4).\n");
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_NEQ(C_ORM_OK, c_orm_codegen_generate("valid_min.cpy", ""));
  remove("valid_min.cpy");

  rc = write_test_file("comment_only.sql", "-- no tables here\n");
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_NEQ(C_ORM_OK, c_orm_codegen_generate("comment_only.sql", "test_out"));
  remove("comment_only.sql");

  remove("dummy.sql");
  remove("test_out/Models.h");
  remove("test_out/Models.c");
  TEST_RMDIR("test_out");

  PASS();
}

/**
 * @brief Codegen coverage test suite runner.
 * @param codegen_coverage_suite Suite runner function name.
 */
SUITE(codegen_coverage_suite) {
  RUN_TEST(test_codegen_parse_fail);
  RUN_TEST(test_codegen_fread_fail);
  RUN_TEST(test_codegen_fopen_h_fail);
  RUN_TEST(test_codegen_fopen_c_fail);
  RUN_TEST(test_codegen_malloc_fail);
  RUN_TEST(test_codegen_emit_fail);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
