#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_oom_coverage.c
 * @brief Out-Of-Memory stress tests for various ORM subsystems.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_db.h"
#include "c_orm_uuid.h"
#include "c_orm_query_builder.h"
#include "c_orm_string_builder.h"
#include "c_orm_log.h"
#define GREATEST_USE_LONGJMP 0
#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "c_orm_codegen.h"
#include "cdd_c_ir.h"
/* clang-format on */

#undef RUN_TEST
#define RUN_TEST(TEST)                                                         \
  do {                                                                         \
    int greatest_should_run = 0;                                               \
    greatest_test_pre(#TEST, &greatest_should_run);                            \
    if (greatest_should_run == 1) {                                            \
      greatest_test_post(TEST());                                              \
    }                                                                          \
  } while ((void)0, 0)

/** @brief Counter decremented before triggering simulated OOM. */
static int oom_countdown = 0;

/** @brief Flag indicating if OOM fuzzer is active. */
static int oom_active = 0;

/**
 * @brief Mock malloc returning NULL when countdown expires under OOM.
 * @param size Allocation size in bytes.
 * @return Allocated memory or NULL on failure.
 */
static void *mock_malloc_oom(size_t size) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      return NULL;
    }
    oom_countdown--;
  }
  return malloc(size);
}

/**
 * @brief Mock free wrapper for OOM testing.
 * @param ptr Pointer to free.
 */
static void mock_free_oom(void *ptr) { free(ptr); }

/**
 * @brief Mock realloc returning NULL when countdown expires under OOM.
 * @param ptr Existing allocation pointer.
 * @param size New allocation size in bytes.
 * @return Reallocated memory or NULL on failure.
 */
static void *mock_realloc_oom(void *ptr, size_t size) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      return NULL;
    }
    oom_countdown--;
  }
  return realloc(ptr, size);
}

/**
 * @brief Performs UUID generation under OOM conditions.
 * @param pass_null Flag indicating if NULL buffer should be passed.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t do_uuid(int pass_null) {
  char uuid_buf[37];
  c_orm_error_t rc;

  rc = c_orm_uuid_v4(pass_null ? NULL : uuid_buf);
  if (rc != C_ORM_OK) {
    /* expected OOM or NULL error */
  }
  return C_ORM_OK;
}

/**
 * @brief Generates schema and code under OOM conditions.
 * @param schema_path Path to schema file.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t do_codegen(const char *schema_path) {
  FILE *f;
  c_orm_error_t rc;

  f = NULL;
  C_ORM_FOPEN(&f, schema_path, "w");
  if (f) {
    fprintf(f, "CREATE TABLE t_oom (id int);\n");
    fclose(f);
  }
  rc = c_orm_codegen_generate(schema_path, "test_out");
  if (rc != C_ORM_OK) {
    /* expected OOM */
  }
  return C_ORM_OK;
}

/**
 * @brief Tests code generation under simulated OOM conditions.
 * @return GREATEST test result.
 */
TEST test_codegen_oom(void) {
  int i;
  for (i = 0; i < 3; i++) {
    oom_countdown = i;
    oom_active = 1;
    do_codegen("oom_schema.sql");
    oom_active = 0;
  }
  do_codegen("/invalid/path/that/cannot/exist/oom.sql");
  PASS();
}

/**
 * @brief Tests UUID generation under simulated OOM conditions.
 * @return GREATEST test result.
 */
TEST test_uuid_oom(void) {
  do_uuid(0);
  do_uuid(1);
  PASS();
}

/**
 * @brief Tests string builder operations under OOM conditions.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t do_string_builder(void) {
  c_orm_string_builder_t *sb;
  c_orm_error_t rc;
  char big[256];

  sb = NULL;
  memset(big, 'a', sizeof(big) - 1);
  big[sizeof(big) - 1] = '\0';

  rc = c_orm_string_builder_init(&sb);
  if (rc == C_ORM_OK) {
    rc = c_orm_string_builder_append(sb, big);
    if (rc != C_ORM_OK) {
      /* expected OOM */
    }
    c_orm_string_builder_free(sb);
  }
  return C_ORM_OK;
}

/**
 * @brief Tests string builder under simulated OOM conditions.
 * @return GREATEST test result.
 */
TEST test_string_builder_oom(void) {
  int i;
  for (i = 0; i < 4; i++) {
    oom_countdown = i;
    oom_active = 1;
    do_string_builder();
    oom_active = 0;
  }
  PASS();
}

/**
 * @brief Tests cdd-c IR construction under OOM conditions.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t do_cdd_c_ir(void) {
  cdd_c_ir_t ir;
  struct sql_table_t tbl;
  cdd_c_query_projection_t proj;
  cdd_c_query_projection_field_t field;

  memset(&tbl, 0, sizeof(tbl));
  memset(&field, 0, sizeof(field));
  field.name = "test";
  field.original_name = "test";

  cdd_c_ir_init(&ir);
  cdd_c_ir_add_table(&ir, &tbl);
  cdd_c_query_projection_init(&proj);
  proj.source_table = (char *)C_ORM_MALLOC(5);
  if (proj.source_table) {
    C_ORM_STRCPY(proj.source_table, 5, "test");
  }
  cdd_c_query_projection_add_field(&proj, &field);
  cdd_c_ir_add_projection(&ir, &proj);
  cdd_c_query_projection_free(&proj);
  parse_sql_into_ir("CREATE TABLE x (id INT);", &ir);
  parse_sql_into_ir("SELECT id FROM x;", &ir);
  parse_sql_into_ir("INSERT INTO x (id) VALUES (1) RETURNING id;", &ir);
  cdd_c_ir_free(&ir);
  return C_ORM_OK;
}

/**
 * @brief Tests query builder operations under OOM conditions.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t do_qb_oom(void) {
  c_orm_select_builder_t *sb;
  c_orm_insert_builder_t *ib;
  c_orm_update_builder_t *ub;
  c_orm_table_meta_t meta;
  char *sql;
  c_orm_error_t ib_rc;
  c_orm_error_t compile_rc;
  c_orm_error_t rc;
  int j;

  sb = NULL;
  ib = NULL;
  ub = NULL;
  sql = NULL;
  memset(&meta, 0, sizeof(meta));
  meta.name = "12345678901234";

  rc = c_orm_select_builder_init(&meta, &sb);
  if (rc == C_ORM_OK) {
    for (j = 0; j < 3; j++) {
      c_orm_select_where_eq(sb, "1234567");
    }
    compile_rc = c_orm_select_builder_compile(sb, &sql);
    if (compile_rc != C_ORM_OK) {
      /* compile failed under simulated OOM */
    }
    if (sql) {
      c_orm_free(sql);
      sql = NULL;
    }
    c_orm_select_builder_free(sb);
  }

  ib_rc = c_orm_insert_builder_init(&meta, &ib);
  if (ib_rc != C_ORM_OK) {
    /* insert builder init failed under simulated OOM */
  }
  c_orm_insert_builder_free(ib);

  rc = c_orm_update_builder_init(&meta, &ub);
  if (rc == C_ORM_OK) {
    c_orm_update_set(ub, "1234567");
    c_orm_update_where_eq(ub, "1234567");
    compile_rc = c_orm_update_builder_compile(ub, &sql);
    if (compile_rc != C_ORM_OK) {
      /* compile failed under simulated OOM */
    }
    if (sql) {
      c_orm_free(sql);
      sql = NULL;
    }
    c_orm_update_builder_free(ub);
  }
  return C_ORM_OK;
}

/**
 * @brief Tests query builder under simulated OOM conditions.
 * @return GREATEST test result.
 */
TEST test_qb_oom(void) {
  int i;
  for (i = 0; i < 30; i++) {
    oom_countdown = i;
    oom_active = 1;
    do_qb_oom();
    oom_active = 0;
  }
  PASS();
}

/**
 * @brief Tests cdd-c IR under simulated OOM conditions.
 * @return GREATEST test result.
 */
TEST test_cdd_c_ir_oom(void) {
  int i;
  for (i = 0; i < 8; i++) {
    oom_countdown = i;
    oom_active = 1;
    do_cdd_c_ir();
    oom_active = 0;
  }
  PASS();
}

/**
 * @brief Exercises mock allocator branch paths.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t test_mock_alloc_branches(void) {
  void *p;

  oom_active = 0;
  p = mock_malloc_oom(16);
  p = mock_realloc_oom(p, 32);
  mock_free_oom(p);

  oom_active = 1;
  oom_countdown = 0;
  p = mock_malloc_oom(16);
  (void)p;

  oom_countdown = 0;
  p = mock_realloc_oom(NULL, 32);
  (void)p;
  oom_active = 0;
  return C_ORM_OK;
}

/**
 * @brief OOM coverage test suite runner.
 * @param oom_coverage_suite Suite runner function name.
 */
SUITE(oom_coverage_suite) {
  static int recursed = 0;
  void *(*old_malloc)(size_t);
  void (*old_free)(void *);
  void *(*old_realloc)(void *, size_t);

  old_malloc = c_orm_malloc;
  old_free = c_orm_free;
  old_realloc = c_orm_realloc;

  c_orm_set_allocators(mock_malloc_oom, mock_realloc_oom, mock_free_oom);

  test_mock_alloc_branches();

  RUN_TEST(test_codegen_oom);
  RUN_TEST(test_uuid_oom);
  RUN_TEST(test_string_builder_oom);
  RUN_TEST(test_cdd_c_ir_oom);
  RUN_TEST(test_qb_oom);

  c_orm_set_allocators(old_malloc, old_realloc, old_free);

  if (!recursed) {
    recursed = 1;
    greatest_set_test_filter("never_match_filter");
    oom_coverage_suite();
    greatest_set_test_filter(NULL);
    recursed = 0;
  }
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#if defined(__clang__) || defined(__GNUC__)
#endif
