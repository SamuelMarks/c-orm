/**
 * @file test_query_projection.c
 * @brief Unit tests for query projection structures and field operations.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "query_projection.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include "test_assert_zero_branch.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Executes mock_realloc_qp.
 * @param ptr Parameter ptr.
 * @param size Parameter size.
 * @param out_ptr Output pointer receiving NULL.
 * @return C_ORM_ERROR_MEMORY on failure.
 */
static c_orm_error_t mock_realloc_qp(void *ptr, size_t size, void **out_ptr) {
  (void)ptr;
  (void)size;
  *out_ptr = NULL;
  return C_ORM_ERROR_MEMORY;
}

/**
 * @brief Mock malloc callback returning C_ORM_ERROR_MEMORY.
 * @param size Requested size.
 * @param out_ptr Output pointer receiving NULL.
 * @return C_ORM_ERROR_MEMORY on failure.
 */
static c_orm_error_t mock_malloc_qp(size_t size, void **out_ptr) {
  (void)size;
  *out_ptr = NULL;
  return C_ORM_ERROR_MEMORY;
}

/**
 * @brief Tests query projection initialization and deallocation.
 * @return GREATEST test result.
 */
TEST test_query_projection_init_free(void) {
  cdd_c_query_projection_t proj;

  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, cdd_c_query_projection_init(NULL));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, cdd_c_query_projection_free(NULL));

  ASSERT_EQ(0, cdd_c_query_projection_init(&proj));
  ASSERT_EQ(0, proj.n_fields);
  ASSERT_EQ(0, proj.capacity);
  ASSERT_EQ(NULL, proj.fields);

  ASSERT_EQ(0, cdd_c_query_projection_free(&proj));

  PASS();
}

/**
 * @brief Tests adding fields to a query projection, including realloc failures.
 * @return GREATEST test result.
 */
TEST test_query_projection_add_field(void) {
  static char c_mut_s0[] = "test_field";
  static char c_mut_s1[] = "test_field_orig";

  cdd_c_query_projection_t proj;
  cdd_c_query_projection_field_t field;
  c_orm_realloc_fn_t old_realloc;

  old_realloc = c_orm_realloc;

  ASSERT_EQ(0, cdd_c_query_projection_init(&proj));

  memset(&field, 0, sizeof(field));
  field.name = c_mut_s0;
  field.original_name = c_mut_s1;
  field.type = 4;

  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            cdd_c_query_projection_add_field(NULL, &field));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, cdd_c_query_projection_add_field(&proj, NULL));

  /* Success path */
  ASSERT_EQ(0, cdd_c_query_projection_add_field(&proj, &field));
  ASSERT_EQ(1, proj.n_fields);
  ASSERT_STR_EQ("test_field", proj.fields[0].name);

  /* OOM realloc */
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(c_orm_malloc, mock_realloc_qp, c_orm_free));
  proj.capacity = 1; /* force realloc on next add */
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            cdd_c_query_projection_add_field(&proj, &field));
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(c_orm_malloc, old_realloc, c_orm_free));

  ASSERT_EQ(0, cdd_c_query_projection_free(&proj));
  PASS();
}

/**
 * @brief Tests OOM handling during string duplication of field names.
 * @return GREATEST test result.
 */
TEST test_query_projection_duplicate_string_oom(void) {
  static char c_mut_s2[] = "test_field";
  static char c_mut_s3[] = "test_field_orig";

  cdd_c_query_projection_t proj;
  cdd_c_query_projection_field_t field;
  c_orm_malloc_fn_t old_malloc;

  old_malloc = c_orm_malloc;

  ASSERT_EQ(0, cdd_c_query_projection_init(&proj));

  memset(&field, 0, sizeof(field));
  field.name = c_mut_s2;
  field.original_name = c_mut_s3;

  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(mock_malloc_qp, c_orm_realloc, c_orm_free));
  /* OOM on c_orm_strdup name */
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            cdd_c_query_projection_add_field(&proj, &field));
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free));

  ASSERT_EQ(0, cdd_c_query_projection_free(&proj));
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(c_orm_malloc, c_orm_realloc, c_orm_free));
  PASS();
}

/**
 * @brief Tests handling of NULL string fields in query projection.
 * @return GREATEST test result.
 */
TEST test_query_projection_duplicate_string_nulls(void) {
  cdd_c_query_projection_t proj;
  cdd_c_query_projection_field_t field;

  ASSERT_EQ(0, cdd_c_query_projection_init(&proj));

  memset(&field, 0, sizeof(field));
  field.name = NULL;
  field.original_name = NULL;

  /* Success with NULL strings */
  ASSERT_EQ(0, cdd_c_query_projection_add_field(&proj, &field));
  ASSERT_EQ(1, proj.n_fields);
  ASSERT_EQ(NULL, proj.fields[0].name);
  ASSERT_EQ(NULL, proj.fields[0].original_name);

  ASSERT_EQ(0, cdd_c_query_projection_free(&proj));
  PASS();
}

/** @brief Counter for mock allocator invocations. */
static int alloc_count_qp = 0;

/**
 * @brief Mock malloc returning memory on first call and error thereafter.
 * @param size Requested size.
 * @param out_ptr Output pointer receiving allocated address or NULL.
 * @return C_ORM_OK on first call, C_ORM_ERROR_MEMORY on subsequent calls.
 */
static c_orm_error_t mock_malloc_qp_second(size_t size, void **out_ptr) {
  if (alloc_count_qp == 0) {
    alloc_count_qp++;
    return c_orm_default_malloc(size, out_ptr);
  }
  *out_ptr = NULL;
  return C_ORM_ERROR_MEMORY;
}

/**
 * @brief Tests dynamic capacity expansion for projection fields array.
 * @return GREATEST test result.
 */
TEST test_query_projection_add_field_cap_expansion(void) {
  static char c_mut_s4[] = "test";
  static char c_mut_s5[] = "test";

  cdd_c_query_projection_t proj;
  cdd_c_query_projection_field_t field;
  int i;
  ASSERT_EQ(0, cdd_c_query_projection_init(&proj));
  memset(&field, 0, sizeof(field));
  field.name = c_mut_s4;
  field.original_name = c_mut_s5;
  for (i = 0; i < 5; i++) {
    ASSERT_EQ(0, cdd_c_query_projection_add_field(&proj, &field));
  }
  ASSERT_EQ(0, cdd_c_query_projection_free(&proj));
  PASS();
}

/**
 * @brief Tests OOM handling during string duplication of original field name.
 * @return GREATEST test result.
 */
TEST test_query_projection_duplicate_string_oom_original_name(void) {
  static char c_mut_s6[] = "test_field";
  static char c_mut_s7[] = "test_field_orig";

  cdd_c_query_projection_t proj;
  cdd_c_query_projection_field_t field;
  c_orm_malloc_fn_t old_malloc;

  old_malloc = c_orm_malloc;

  ASSERT_EQ(0, cdd_c_query_projection_init(&proj));

  memset(&field, 0, sizeof(field));
  field.name = c_mut_s6;
  field.original_name = c_mut_s7;

  alloc_count_qp = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(mock_malloc_qp_second, c_orm_realloc,
                                           c_orm_free));
  /* OOM on c_orm_strdup original_name */
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            cdd_c_query_projection_add_field(&proj, &field));
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free));

  ASSERT_EQ(0, cdd_c_query_projection_free(&proj));
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(c_orm_malloc, c_orm_realloc, c_orm_free));
  PASS();
}

/** @brief Bitmask controlling which mock_fail_free_qp calls return an error. */
static unsigned int qp_free_fail_mask = 0;

/** @brief Invocation index for mock_fail_free_qp. */
static unsigned int qp_free_call_idx = 0;

/**
 * @brief Mock realloc returning C_ORM_OK with NULL *out_ptr to exercise
 * defensive NULL check branch.
 * @param ptr Existing pointer.
 * @param size Requested size.
 * @param out_ptr Output pointer receiving NULL.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_ok_null_realloc_qp(void *ptr, size_t size,
                                             void **out_ptr) {
  (void)ptr;
  (void)size;
  *out_ptr = NULL;
  return C_ORM_OK;
}

/**
 * @brief Mock malloc returning C_ORM_OK with NULL *out_ptr to exercise
 * defensive NULL check branch.
 * @param size Requested size.
 * @param out_ptr Output pointer receiving NULL.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_ok_null_malloc_qp(size_t size, void **out_ptr) {
  (void)size;
  *out_ptr = NULL;
  return C_ORM_OK;
}

/**
 * @brief Mock free callback that frees memory while returning a simulated error
 * on masked call indices.
 * @param ptr Pointer to free.
 * @return C_ORM_OK or C_ORM_ERROR_MEMORY when masked.
 */
static c_orm_error_t mock_fail_free_qp(void *ptr) {
  unsigned int bit = 1U << qp_free_call_idx++;
  c_orm_default_free(ptr);
  if ((qp_free_fail_mask & bit) != 0U) {
    return C_ORM_ERROR_MEMORY;
  }
  return C_ORM_OK;
}

/**
 * @brief Tests defensive NULL-return allocators and free error propagation in
 * query_projection.c.
 * @return GREATEST test result.
 */
TEST test_query_projection_free_and_defensive_errors(void) {
  static char c_mut_s8[] = "f1";
  static char c_mut_s9[] = "orig1";

  cdd_c_query_projection_t proj;
  cdd_c_query_projection_field_t field;
  c_orm_malloc_fn_t saved_malloc = c_orm_malloc;
  c_orm_realloc_fn_t saved_realloc = c_orm_realloc;
  c_orm_free_fn_t saved_free = c_orm_free;
  unsigned int bit_idx;

  ASSERT_EQ(C_ORM_OK, cdd_c_query_projection_init(&proj));
  memset(&field, 0, sizeof(field));
  field.name = c_mut_s8;
  field.original_name = c_mut_s9;

  /* Defensive NULL return on realloc */
  ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(
                          saved_malloc, mock_ok_null_realloc_qp, saved_free));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            cdd_c_query_projection_add_field(&proj, &field));

  /* Defensive NULL return on malloc inside c_orm_strdup */
  ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(mock_ok_null_malloc_qp,
                                           saved_realloc, saved_free));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            cdd_c_query_projection_add_field(&proj, &field));
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(saved_malloc, saved_realloc, saved_free));
  ASSERT_EQ(C_ORM_OK, cdd_c_query_projection_free(&proj));

  /* Exercise each individual free failure (rc == C_ORM_OK -> true) across a
   * 2-field projection with source_table and target_name (7 free calls total:
   * 0:f0.name, 1:f0.orig, 2:f1.name, 3:f1.orig, 4:fields, 5:source_table,
   * 6:target_name) */
  for (bit_idx = 0; bit_idx < 7U; ++bit_idx) {
    ASSERT_EQ(C_ORM_OK, cdd_c_query_projection_init(&proj));
    ASSERT_EQ(C_ORM_OK, cdd_c_query_projection_add_field(&proj, &field));
    ASSERT_EQ(C_ORM_OK, cdd_c_query_projection_add_field(&proj, &field));
    ASSERT_EQ(C_ORM_OK, c_orm_strdup("src_tbl", &proj.source_table));
    ASSERT_EQ(C_ORM_OK,
              c_orm_strdup("tgt_map", &proj.mapping_meta.target_name));

    qp_free_call_idx = 0;
    qp_free_fail_mask = (1U << bit_idx);
    ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(saved_malloc, saved_realloc,
                                             mock_fail_free_qp));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, cdd_c_query_projection_free(&proj));
    ASSERT_EQ(C_ORM_OK,
              c_orm_set_allocators(saved_malloc, saved_realloc, saved_free));
  }

  /* Exercise all frees failing simultaneously so (frc != C_ORM_OK && rc ==
   * C_ORM_OK) evaluates (true && false) on every subsequent free */
  ASSERT_EQ(C_ORM_OK, cdd_c_query_projection_init(&proj));
  ASSERT_EQ(C_ORM_OK, cdd_c_query_projection_add_field(&proj, &field));
  ASSERT_EQ(C_ORM_OK, cdd_c_query_projection_add_field(&proj, &field));
  ASSERT_EQ(C_ORM_OK, c_orm_strdup("src_tbl", &proj.source_table));
  ASSERT_EQ(C_ORM_OK, c_orm_strdup("tgt_map", &proj.mapping_meta.target_name));

  qp_free_call_idx = 0;
  qp_free_fail_mask = 0x7FU;
  ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(saved_malloc, saved_realloc,
                                           mock_fail_free_qp));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, cdd_c_query_projection_free(&proj));
  qp_free_fail_mask = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(saved_malloc, saved_realloc, saved_free));

  PASS();
}

/**
 * @brief Query projection test suite runner.
 * @param query_projection_suite Suite runner function name.
 */
SUITE(query_projection_suite) {
  RUN_TEST(test_query_projection_init_free);
  RUN_TEST(test_query_projection_add_field);
  RUN_TEST(test_query_projection_duplicate_string_oom);
  RUN_TEST(test_query_projection_duplicate_string_nulls);
  RUN_TEST(test_query_projection_duplicate_string_oom_original_name);
  RUN_TEST(test_query_projection_add_field_cap_expansion);
  RUN_TEST(test_query_projection_free_and_defensive_errors);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
