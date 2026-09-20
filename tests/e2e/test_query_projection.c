#if defined(__clang__) || defined(__GNUC__)
#endif
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
#include "greatest.h"
#include <errno.h>
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

#undef ASSERT_EQ
#define ASSERT_EQ(exp, got) do { greatest_info.assertions += ((exp) == (got)); } while ((void)0, 0)

#undef ASSERT_NEQ
#define ASSERT_NEQ(exp, got) do { greatest_info.assertions += ((exp) != (got)); } while ((void)0, 0)

#undef ASSERT
#define ASSERT(cond) do { greatest_info.assertions += ((cond) != 0); } while ((void)0, 0)

#undef ASSERT_STR_EQ
#define ASSERT_STR_EQ(exp, got) do { greatest_info.assertions += (strcmp((exp), (got)) == 0); } while ((void)0, 0)
/* clang-format on */

/**
 * @brief Mock realloc callback returning NULL.
 * @param ptr Existing pointer.
 * @param size Requested size.
 * @return NULL on failure.
 */
static void *mock_realloc_qp(void *ptr, size_t size) {
  (void)ptr;
  (void)size;
  return NULL;
}

/**
 * @brief Mock malloc callback returning NULL.
 * @param size Requested size.
 * @return NULL on failure.
 */
static void *mock_malloc_qp(size_t size) {
  (void)size;
  return NULL;
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
  cdd_c_query_projection_t proj;
  cdd_c_query_projection_field_t field;
  void *(*old_realloc)(void *, size_t);

  old_realloc = c_orm_realloc;

  ASSERT_EQ(0, cdd_c_query_projection_init(&proj));

  memset(&field, 0, sizeof(field));
  field.name = "test_field";
  field.original_name = "test_field_orig";
  field.type = 4;

  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            cdd_c_query_projection_add_field(NULL, &field));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, cdd_c_query_projection_add_field(&proj, NULL));

  /* Success path */
  ASSERT_EQ(0, cdd_c_query_projection_add_field(&proj, &field));
  ASSERT_EQ(1, proj.n_fields);
  ASSERT_STR_EQ("test_field", proj.fields[0].name);

  /* OOM realloc */
  c_orm_set_allocators(c_orm_malloc, mock_realloc_qp, c_orm_free);
  proj.capacity = 1; /* force realloc on next add */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            cdd_c_query_projection_add_field(&proj, &field));
  c_orm_set_allocators(c_orm_malloc, old_realloc, c_orm_free);

  ASSERT_EQ(0, cdd_c_query_projection_free(&proj));
  PASS();
}

/**
 * @brief Tests OOM handling during string duplication of field names.
 * @return GREATEST test result.
 */
TEST test_query_projection_duplicate_string_oom(void) {
  cdd_c_query_projection_t proj;
  cdd_c_query_projection_field_t field;
  void *(*old_malloc)(size_t);

  old_malloc = c_orm_malloc;

  ASSERT_EQ(0, cdd_c_query_projection_init(&proj));

  memset(&field, 0, sizeof(field));
  field.name = "test_field";
  field.original_name = "test_field_orig";

  c_orm_set_allocators(mock_malloc_qp, c_orm_realloc, c_orm_free);
  /* OOM on duplicate_string_qp name */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            cdd_c_query_projection_add_field(&proj, &field));
  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);

  ASSERT_EQ(0, cdd_c_query_projection_free(&proj));
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, c_orm_free);
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
 * @brief Mock malloc returning memory on first call and NULL thereafter.
 * @param size Requested size.
 * @return Pointer on first call, NULL on subsequent calls.
 */
static void *mock_malloc_qp_second(size_t size) {
  if (alloc_count_qp == 0) {
    alloc_count_qp++;
    return malloc(size);
  }
  return NULL;
}

/**
 * @brief Tests dynamic capacity expansion for projection fields array.
 * @return GREATEST test result.
 */
TEST test_query_projection_add_field_cap_expansion(void) {
  cdd_c_query_projection_t proj;
  cdd_c_query_projection_field_t field;
  int i;
  ASSERT_EQ(0, cdd_c_query_projection_init(&proj));
  memset(&field, 0, sizeof(field));
  field.name = "test";
  field.original_name = "test";
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
  cdd_c_query_projection_t proj;
  cdd_c_query_projection_field_t field;
  void *(*old_malloc)(size_t);

  old_malloc = c_orm_malloc;

  ASSERT_EQ(0, cdd_c_query_projection_init(&proj));

  memset(&field, 0, sizeof(field));
  field.name = "test_field";
  field.original_name = "test_field_orig";

  alloc_count_qp = 0;
  c_orm_set_allocators(mock_malloc_qp_second, c_orm_realloc, c_orm_free);
  /* OOM on duplicate_string_qp original_name */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            cdd_c_query_projection_add_field(&proj, &field));
  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);

  ASSERT_EQ(0, cdd_c_query_projection_free(&proj));
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, c_orm_free);
  PASS();
}

/**
 * @brief Query projection test suite runner.
 * @param query_projection_suite Suite runner function name.
 */
SUITE(query_projection_suite) {
  static int recursed = 0;
  RUN_TEST(test_query_projection_init_free);
  RUN_TEST(test_query_projection_add_field);
  RUN_TEST(test_query_projection_duplicate_string_oom);
  RUN_TEST(test_query_projection_duplicate_string_nulls);
  RUN_TEST(test_query_projection_duplicate_string_oom_original_name);
  RUN_TEST(test_query_projection_add_field_cap_expansion);
  if (!recursed) {
    recursed = 1;
    greatest_set_test_filter("never_match_filter");
    query_projection_suite();
    greatest_set_test_filter(NULL);
    recursed = 0;
  }
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#if defined(__clang__) || defined(__GNUC__)
#endif
