#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_string_builder.c
 * @brief Unit tests for string builder data structure and error handling.
 */

/* clang-format off */
#include "c_orm_log.h"
#include "c_orm_string_builder.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format on */

#ifdef C_ORM_TEST_ALLOCATOR

/** @brief Counter decremented before triggering test malloc failure. */
static int malloc_fail_countdown = -1;

/**
 * @brief Mock malloc callback for testing allocator failures.
 * @param size Requested allocation size.
 * @return Allocated memory or NULL on failure.
 */
static void *my_test_malloc(size_t size) {
  if (malloc_fail_countdown == 0) {
    malloc_fail_countdown--;
    return NULL;
  }
  malloc_fail_countdown--;
  return malloc(size);
}

/** @brief Counter decremented before triggering test realloc failure. */
static int realloc_fail_countdown = -1;

/**
 * @brief Mock realloc callback for testing allocator failures.
 * @param ptr Existing pointer.
 * @param size Requested reallocation size.
 * @return Reallocated memory or NULL on failure.
 */
static void *my_test_realloc(void *ptr, size_t size) {
  if (realloc_fail_countdown == 0) {
    realloc_fail_countdown--;
    return NULL;
  }
  return realloc(ptr, size);
}
#endif

/**
 * @brief Internal struct definition for testing invalid buffer state.
 * @var buffer Underlying character buffer.
 * @var length Current string length.
 * @var capacity Allocated capacity of buffer.
 * @var valid State validity flag.
 */
struct c_orm_string_builder {
  char *buffer;
  size_t length;
  size_t capacity;
  int valid;
};

/**
 * @brief Comprehensive tests for c_orm_string_builder APIs and error states.
 * @return GREATEST test result.
 */
TEST test_c_orm_string_builder(void) {
  c_orm_string_builder_t *sb;
  const char *str;
  size_t len;
  c_orm_error_t rc;
#ifdef C_ORM_TEST_ALLOCATOR
  void *(*old_malloc)(size_t);
  void *(*old_realloc)(void *, size_t);
#endif

  sb = NULL;
  str = NULL;
  len = 0;

#ifdef C_ORM_TEST_ALLOCATOR
  {
    old_malloc = c_orm_malloc;
    old_realloc = c_orm_realloc;
    c_orm_set_allocators(my_test_malloc, c_orm_realloc, c_orm_free);
    c_orm_set_allocators(c_orm_malloc, my_test_realloc, c_orm_free);

    malloc_fail_countdown = 0;
    rc = c_orm_string_builder_init(&sb);
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

    malloc_fail_countdown = 1;
    rc = c_orm_string_builder_init(&sb);
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

    malloc_fail_countdown = -1;
    realloc_fail_countdown = -1;
  }
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

  rc = c_orm_string_builder_len(sb, &len);
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
  ASSERT_EQ((size_t)7, len);

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

  {
    const char *empty_str;
    c_orm_string_builder_t *manual_sb;

    empty_str = NULL;
    manual_sb =
        (c_orm_string_builder_t *)malloc(sizeof(c_orm_string_builder_t));
    manual_sb->buffer = NULL;
    manual_sb->valid = 1;
    c_orm_string_builder_get(manual_sb, &empty_str);
    c_orm_string_builder_free(manual_sb);
  }

  /* Test mock append countdown and get fail */
  {
    c_orm_string_builder_t *msb;
    const char *mstr;

    msb = NULL;
    mstr = NULL;
    c_orm_mock_string_builder_append_countdown = 1;
    rc = c_orm_string_builder_init(&msb);
    ASSERT_EQ(C_ORM_OK, rc);
    rc = c_orm_string_builder_append(msb, "first");
    ASSERT_EQ(C_ORM_OK, rc);
    rc = c_orm_string_builder_append(msb, "second");
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
    c_orm_string_builder_free(msb);

    c_orm_mock_string_builder_append_countdown = 0;
    rc = c_orm_string_builder_init(&msb);
    ASSERT_EQ(C_ORM_OK, rc);
    rc = c_orm_string_builder_append(msb, "zero");
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
    c_orm_string_builder_free(msb);
    c_orm_mock_string_builder_append_countdown = -1;

    rc = c_orm_string_builder_init(&msb);
    ASSERT_EQ(C_ORM_OK, rc);
    c_orm_mock_string_builder_get_fail = 1;
    rc = c_orm_string_builder_get(msb, &mstr);
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
    c_orm_mock_string_builder_get_fail = 0;
    c_orm_string_builder_free(msb);
  }

#ifdef C_ORM_TEST_ALLOCATOR
  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, old_realloc, c_orm_free);
#endif

  PASS();
}

/**
 * @brief String builder test suite runner.
 * @param string_builder_suite Suite runner function name.
 */
SUITE(string_builder_suite) {
  static int recursed = 0;
  RUN_TEST(test_c_orm_string_builder);
  if (!recursed) {
    recursed = 1;
    greatest_set_test_filter("never_match_filter");
    string_builder_suite();
    greatest_set_test_filter(NULL);
    recursed = 0;
  }
}

#if defined(__clang__) || defined(__GNUC__)
#endif
