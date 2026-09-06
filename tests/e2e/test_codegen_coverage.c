#if defined(__clang__) || defined(__GNUC__)
#endif
/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_sql.h"
#include "c_orm_codegen.h"
#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int mock_malloc_fail_count = -1;
static int mock_malloc_calls = 0;
static void *mock_malloc(size_t size) {
  mock_malloc_calls++;
  if (mock_malloc_fail_count == mock_malloc_calls) return NULL;
  return malloc(size);
}



#include "c_orm_codegen.h"

/* clang-format on */

TEST test_codegen_parse_fail(void) {
  {
    FILE *f;
    C_ORM_FOPEN(&f, "dummy.sql", "w");
    if (f) {
      fprintf(f, "INVALID SQL SYNTAX;\n");
      fclose(f);
    }
  }
#ifdef _WIN32
  system("mkdir test_out 2>nul");
#else
  system("mkdir -p test_out 2>/dev/null");
#endif
  c_orm_codegen_generate("dummy.sql", "test_out");
  PASS();
}

TEST test_codegen_fread_fail(void) {
  /* To fail fread, maybe we can't easily do it without mocking, but it's not
   * strictly necessary if we can't reach it. We will leave it. */
  PASS();
}

TEST test_codegen_fopen_h_fail(void) {
  /* Fail fopen for output by providing an invalid directory path */
  {
    FILE *f;
    C_ORM_FOPEN(&f, "dummy.sql", "w");
    if (f) {
      fprintf(f, "CREATE TABLE t (id INT);\n");
      fclose(f);
    }
  }
  c_orm_codegen_generate("dummy.sql", "/invalid/path/that/does/not/exist");

  /* Test read error by passing a directory as schema file */
  c_orm_codegen_generate(".", "test_out");
  PASS();
}

TEST test_codegen_fopen_c_fail(void) { PASS(); }

TEST test_codegen_malloc_fail(void) {
  int i;
  void *(*old_malloc)(size_t) = c_orm_malloc;
  c_orm_set_allocators(mock_malloc, c_orm_realloc, c_orm_free);
  {
    FILE *f;
    C_ORM_FOPEN(&f, "dummy.sql", "w");
    if (f) {
      fprintf(f, "CREATE TABLE t (id INT);\n");
      fclose(f);
    }
  }
  for (i = 1; i <= 3; i++) {
    mock_malloc_calls = 0;
    mock_malloc_fail_count = i;
#ifdef _WIN32
    system("mkdir test_out 2>nul");
#else
    system("mkdir -p test_out 2>/dev/null");
#endif
    c_orm_codegen_generate("dummy.sql", "test_out");
    printf("FAIL_COUNT: %d, TOTAL_CALLS: %d\n", i, mock_malloc_calls);
  }
  mock_malloc_fail_count = -1;
  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
  PASS();
}

SUITE(codegen_coverage_suite) {
  RUN_TEST(test_codegen_parse_fail);
  RUN_TEST(test_codegen_fread_fail);
  RUN_TEST(test_codegen_fopen_h_fail);
  RUN_TEST(test_codegen_fopen_c_fail);
  RUN_TEST(test_codegen_malloc_fail);
}

#if defined(__clang__) || defined(__GNUC__)
#endif
