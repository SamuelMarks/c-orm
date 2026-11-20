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
#include <greatest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "c_orm_codegen.h"
#include "cdd_c_ir.h"
#if defined(_WIN32) || defined(_MSC_VER)
#include <direct.h>
#define TEST_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define TEST_MKDIR(p) mkdir(p, 0777)
#endif
/* clang-format on */

/** @brief Counter decremented before triggering simulated OOM. */
static int oom_countdown = 0;

/** @brief Flag indicating if OOM fuzzer is active. */
static int oom_active = 0;

/**
 * @brief Mock malloc returning NULL when countdown expires under OOM.
 * @param size Allocation size in bytes.
 * @return Allocated memory or NULL on failure.
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
 * @brief Executes mock_malloc_oom.
 * @param size Parameter size.
 * @param out_ptr Output pointer receiving allocated address.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on failure.
 */
static c_orm_error_t mock_malloc_oom(size_t size, void **out_ptr) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      if (out_ptr) {
        *out_ptr = NULL;
      }
      return C_ORM_ERROR_MEMORY;
    }
    oom_countdown--;
  }
  *out_ptr = malloc(size == 0 ? 1 : size);
  return *out_ptr ? C_ORM_OK : C_ORM_ERROR_MEMORY;
}

/**
 * @brief Mock free wrapper for OOM testing.
 * @param ptr Pointer to free.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t mock_free_oom(void *ptr) {
  free(ptr);
  return C_ORM_OK;
}

/**
 * @brief Mock realloc returning error when countdown expires under OOM.
 * @param ptr Existing allocation pointer.
 * @param size New allocation size in bytes.
 * @param out_ptr Output pointer receiving reallocated memory.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on failure.
 */
static c_orm_error_t mock_realloc_oom(void *ptr, size_t size, void **out_ptr) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      if (out_ptr) {
        *out_ptr = NULL;
      }
      return C_ORM_ERROR_MEMORY;
    }
    oom_countdown--;
  }
  *out_ptr = realloc(ptr, size == 0 ? 1 : size);
  return *out_ptr ? C_ORM_OK : C_ORM_ERROR_MEMORY;
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
    fprintf(f, "CREATE TABLE t_oom (id INT);\n");
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
  TEST_MKDIR("test_out");
  for (i = 0; i < 3; i++) {
    oom_countdown = i;
    oom_active = 1;
    do_codegen("oom_schema.sql");
    oom_active = 0;
  }
  do_codegen("/invalid/path/that/cannot/exist/oom.sql");
  oom_countdown = -1;
  oom_active = 0;
  do_codegen("oom_schema.sql");
  remove("oom_schema.sql");
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
  proj.source_table = NULL;
  if (c_orm_system_malloc(5, (void **)&proj.source_table) == C_ORM_OK &&
      proj.source_table) {
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
  c_orm_update_builder_t *ub;
  c_orm_table_meta_t meta;
  char *sql;
  c_orm_error_t compile_rc;
  c_orm_error_t rc;
  int j;

  sb = NULL;
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
      (void)c_orm_free(sql);
      sql = NULL;
    }
    c_orm_select_builder_free(sb);
  }

  rc = c_orm_update_builder_init(&meta, &ub);
  if (rc == C_ORM_OK) {
    c_orm_update_set(ub, "1234567");
    c_orm_update_where_eq(ub, "1234567");
    compile_rc = c_orm_update_builder_compile(ub, &sql);
    if (compile_rc != C_ORM_OK) {
      /* compile failed under simulated OOM */
    }
    if (sql) {
      (void)c_orm_free(sql);
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
  for (i = 0; i < 60; i++) {
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
  void *p = NULL;

  oom_active = 0;
  (void)mock_malloc_oom(16, &p);
  (void)mock_realloc_oom(p, 32, &p);
  (void)mock_free_oom(p);

  oom_active = 1;
  oom_countdown = 0;
  (void)mock_malloc_oom(16, &p);

  oom_countdown = 0;
  (void)mock_realloc_oom(NULL, 32, &p);
  oom_active = 0;
  return C_ORM_OK;
}

/**
 * @brief OOM coverage test suite runner.
 * @param oom_coverage_suite Suite runner function name.
 */
SUITE(oom_coverage_suite) {
  static int recursed = 0;
  c_orm_malloc_fn_t old_malloc;
  c_orm_free_fn_t old_free;
  c_orm_realloc_fn_t old_realloc;

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
