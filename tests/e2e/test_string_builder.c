/* clang-format off */
#include "c_orm_log.h"
#include "c_orm_string_builder.h"
#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

#ifdef C_ORM_TEST_ALLOCATOR

static int malloc_fail_countdown = -1;
static void *my_test_malloc(size_t size) {
  if (malloc_fail_countdown == 0) {
    malloc_fail_countdown--;
    return NULL;
  }
  malloc_fail_countdown--;
  return malloc(size);
}

static int realloc_fail_countdown = -1;
static void *my_test_realloc(void *ptr, size_t size) {
  if (realloc_fail_countdown == 0) {
    realloc_fail_countdown--;
    return NULL;
  }
  if (realloc_fail_countdown > 0) {
    realloc_fail_countdown--;
  }
  return realloc(ptr, size);
}
#endif

TEST test_c_orm_string_builder(void) {
  c_orm_string_builder_t *sb = NULL;
  const char *str = NULL;
  size_t len = 0;
  int rc;

#ifdef C_ORM_TEST_ALLOCATOR
  void *(*old_malloc)(size_t) = c_orm_malloc;
  void *(*old_realloc)(void *, size_t) = c_orm_realloc;
  c_orm_malloc = my_test_malloc;
  c_orm_realloc = my_test_realloc;

  malloc_fail_countdown = 0;
  rc = c_orm_string_builder_init(&sb);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  malloc_fail_countdown = 1;
  rc = c_orm_string_builder_init(&sb);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  malloc_fail_countdown = -1;
  realloc_fail_countdown = -1;
#endif

  rc = c_orm_string_builder_init(NULL);
  ASSERT_NEQ(0, rc);

  rc = c_orm_string_builder_init(&sb);
  ASSERT_EQ(0, rc);
  ASSERT(sb != NULL);

#ifdef C_ORM_TEST_ALLOCATOR
  rc = c_orm_string_builder_append(sb, "Initial");
  ASSERT_EQ(0, rc);
  realloc_fail_countdown = 0;
  rc = c_orm_string_builder_append(
      sb, " This is a very long string that should definitely force a "
          "reallocation of the underlying buffer because it exceeds the "
          "initial capacity of 64 bytes.");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_string_builder_append(sb, " More text");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_string_builder_get(sb, &str);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  realloc_fail_countdown = -1;
  c_orm_string_builder_free(sb);
  rc = c_orm_string_builder_init(&sb);
  ASSERT_EQ(0, rc);
#endif

  rc = c_orm_string_builder_append(NULL, "test");
  ASSERT_NEQ(0, rc);
  rc = c_orm_string_builder_append(sb, NULL);
  ASSERT_NEQ(0, rc);

  rc = c_orm_string_builder_append(sb, "");
  ASSERT_EQ(0, rc);

  rc = c_orm_string_builder_append(sb, "Hello, ");
  ASSERT_EQ(0, rc);

  rc = c_orm_string_builder_get(NULL, &str);
  ASSERT_NEQ(0, rc);
  rc = c_orm_string_builder_get(sb, NULL);
  ASSERT_NEQ(0, rc);

  rc = c_orm_string_builder_get(sb, &str);
  ASSERT_EQ(0, rc);
  ASSERT_STR_EQ("Hello, ", str);

  rc = c_orm_string_builder_len(NULL, &len);
  ASSERT_NEQ(0, rc);
  rc = c_orm_string_builder_len(sb, NULL);
  ASSERT_NEQ(0, rc);

  rc = c_orm_string_builder_len(sb, &len);
  ASSERT_EQ(0, rc);
  ASSERT_EQ(7, (int)len);

  rc = c_orm_string_builder_append(sb, "World!");
  ASSERT_EQ(0, rc);

  c_orm_log_debug("Test %s", "log");

  rc = c_orm_string_builder_get(sb, &str);
  ASSERT_EQ(0, rc);
  ASSERT_STR_EQ("Hello, World!", str);

  /* Force reallocation */
  rc = c_orm_string_builder_append(
      sb, " This is a very long string that should definitely force a "
          "reallocation of the underlying buffer because it exceeds the "
          "initial capacity of 64 bytes.");
  ASSERT_EQ(0, rc);

  c_orm_string_builder_free(sb);
  c_orm_string_builder_free(NULL);

#ifdef C_ORM_TEST_ALLOCATOR
  c_orm_malloc = old_malloc;
  c_orm_realloc = old_realloc;
#endif

  PASS();
}

SUITE(string_builder_suite) { RUN_TEST(test_c_orm_string_builder); }
