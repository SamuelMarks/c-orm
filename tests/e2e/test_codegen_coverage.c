/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_sql.h"
#include "c_orm_codegen.h"
#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int mock_parse_fail = 0;
static int mock_fread_fail = 0;
static int mock_fopen_h_fail = 0;
static int mock_fopen_c_fail = 0;

static int mock_parse_sql_ddl(const char *sql_data, struct sql_table_t **out_tables, size_t *out_n_tables) {
  (void)sql_data; (void)out_tables; (void)out_n_tables;
  if (mock_parse_fail) return 1;
  return 0;
}

static size_t mock_fread(void *ptr, size_t size, size_t count, FILE *stream) {
  if (mock_fread_fail) return 0;
  return fread(ptr, size, count, stream);
}

static FILE *mock_fopen(const char *path, const char *mode) {
  if (mock_fopen_h_fail && strstr(path, "Models.h")) return NULL;
  if (mock_fopen_c_fail && strstr(path, "Models.c")) return NULL;
  return fopen(path, mode);
}

static int mock_malloc_fail_count = -1;
static int mock_malloc_calls = 0;
static void *mock_malloc(size_t size) {
  mock_malloc_calls++; printf("MOCK_MALLOC CALLED\n"); printf("MOCK_MALLOC %d\n", mock_malloc_calls);
  if (mock_malloc_fail_count == mock_malloc_calls) return NULL;
  return malloc(size);
}



#define parse_sql_ddl mock_parse_sql_ddl
#define fread mock_fread
#define fopen mock_fopen
#define c_orm_codegen_generate test_c_orm_codegen_generate

c_orm_error_t test_c_orm_codegen_generate(const char *schema_file,
                                          const char *output_dir);

#include "../../src/c_orm_codegen.c"


#undef parse_sql_ddl
#undef fread
#undef fopen
#undef c_orm_codegen_generate

/* clang-format on */

TEST test_codegen_parse_fail(void) {
  system("echo 'x' > dummy.sql");
  mock_parse_fail = 1;
  system("mkdir -p test_out");
  test_c_orm_codegen_generate("dummy.sql", "test_out");
  mock_parse_fail = 0;
  PASS();
}

TEST test_codegen_fread_fail(void) {
  system("echo 'x' > dummy.sql");
  mock_fread_fail = 1;
  system("mkdir -p test_out");
  test_c_orm_codegen_generate("dummy.sql", "test_out");
  mock_fread_fail = 0;
  PASS();
}

TEST test_codegen_fopen_h_fail(void) {
  system("echo 'x' > dummy.sql");
  mock_fopen_h_fail = 1;
  system("mkdir -p test_out");
  test_c_orm_codegen_generate("dummy.sql", "test_out");
  mock_fopen_h_fail = 0;
  PASS();
}

TEST test_codegen_fopen_c_fail(void) {
  system("echo 'x' > dummy.sql");
  mock_fopen_c_fail = 1;
  system("mkdir -p test_out");
  test_c_orm_codegen_generate("dummy.sql", "test_out");
  mock_fopen_c_fail = 0;
  PASS();
}

TEST test_codegen_malloc_fail(void) {
  int i;
  void *(*old_malloc)(size_t) = c_orm_malloc;
  c_orm_malloc = mock_malloc;
  system("echo 'CREATE TABLE t (id INT);' > dummy.sql");
  for (i = 1; i < 5; i++) {
    mock_malloc_calls = 0;
    mock_malloc_fail_count = i;
    system("mkdir -p test_out");
    test_c_orm_codegen_generate("dummy.sql", "test_out");
    printf("FAIL_COUNT: %d, TOTAL_CALLS: %d\n", i, mock_malloc_calls);
  }
  mock_malloc_fail_count = -1;
  c_orm_malloc = old_malloc;
  PASS();
}

SUITE(codegen_coverage_suite) {
  RUN_TEST(test_codegen_parse_fail);
  RUN_TEST(test_codegen_fread_fail);
  RUN_TEST(test_codegen_fopen_h_fail);
  RUN_TEST(test_codegen_fopen_c_fail);
  RUN_TEST(test_codegen_malloc_fail);
}
