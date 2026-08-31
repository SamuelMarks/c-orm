#if defined(__clang__) || defined(__GNUC__)
#endif
/* clang-format off */
#include "test_utils.h"
#include "query_projection.h"
#include <greatest.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
/* clang-format on */

static void *mock_realloc_qp(void *ptr, size_t size) {
  (void)ptr;
  (void)size;
  return NULL;
}

static void *mock_malloc_qp(size_t size) {
  (void)size;
  return NULL;
}

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

TEST test_query_projection_add_field(void) {
  cdd_c_query_projection_t proj;
  cdd_c_query_projection_field_t field;
  void *(*old_realloc)(void *, size_t) = c_orm_realloc;

  ASSERT_EQ(0, cdd_c_query_projection_init(&proj));

  memset(&field, 0, sizeof(field));
  field.name = test_strdup("test_field");
  field.original_name = test_strdup("test_field_orig");
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

TEST test_query_projection_duplicate_string_oom(void) {
  cdd_c_query_projection_t proj;
  cdd_c_query_projection_field_t field;
  void *(*old_malloc)(size_t) = c_orm_malloc;

  ASSERT_EQ(0, cdd_c_query_projection_init(&proj));

  memset(&field, 0, sizeof(field));
  field.name = test_strdup("test_field");
  field.original_name = test_strdup("test_field_orig");

  c_orm_set_allocators(mock_malloc_qp, c_orm_realloc, c_orm_free);
  /* OOM on duplicate_string_qp name */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            cdd_c_query_projection_add_field(&proj, &field));
  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);

  ASSERT_EQ(0, cdd_c_query_projection_free(&proj));
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, c_orm_free);
  PASS();
}

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

static int alloc_count_qp = 0;
static void *mock_malloc_qp_second(size_t size) {
  if (alloc_count_qp == 0) {
    alloc_count_qp++;
    return c_orm_malloc(size);
  }
  return NULL;
}

TEST test_query_projection_add_field_cap_expansion(void) {
  cdd_c_query_projection_t proj;
  cdd_c_query_projection_field_t field;
  int i;
  ASSERT_EQ(0, cdd_c_query_projection_init(&proj));
  memset(&field, 0, sizeof(field));
  field.name = test_strdup("test");
  field.original_name = test_strdup("test");
  for (i = 0; i < 5; i++)
    ASSERT_EQ(0, cdd_c_query_projection_add_field(&proj, &field));
  ASSERT_EQ(0, cdd_c_query_projection_free(&proj));
  PASS();
}

TEST test_query_projection_duplicate_string_oom_original_name(void) {
  cdd_c_query_projection_t proj;
  cdd_c_query_projection_field_t field;
  void *(*old_malloc)(size_t) = c_orm_malloc;

  ASSERT_EQ(0, cdd_c_query_projection_init(&proj));

  memset(&field, 0, sizeof(field));
  field.name = test_strdup("test_field");
  field.original_name = test_strdup("test_field_orig");

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

SUITE(query_projection_suite) {
  RUN_TEST(test_query_projection_init_free);
  RUN_TEST(test_query_projection_add_field);
  RUN_TEST(test_query_projection_duplicate_string_oom);
  RUN_TEST(test_query_projection_duplicate_string_nulls);
  RUN_TEST(test_query_projection_duplicate_string_oom_original_name);
  RUN_TEST(test_query_projection_add_field_cap_expansion);
}

#if defined(__clang__) || defined(__GNUC__)
#endif
