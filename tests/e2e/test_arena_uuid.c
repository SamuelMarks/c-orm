/**
 * @file test_arena_uuid.c
 * @brief Unit tests for memory arena and UUID generation coverage.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_ast.h"
#include "c_orm_uuid.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include "test_assert_zero_branch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

#ifdef C_ORM_TEST_ALLOCATOR

/** @brief Counter decremented before triggering test malloc failure. */
static int malloc_fail_countdown = -1;

/**
 * @brief Mock malloc callback for testing allocator failures.
 * @param size Parameter size.
 * @param out_ptr Output pointer receiving allocated address.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on failure.
 */
static c_orm_error_t my_test_malloc(size_t size, void **out_ptr) {
  if (malloc_fail_countdown == 0) {
    malloc_fail_countdown--;
    if (out_ptr) {
      *out_ptr = NULL;
    }
    return C_ORM_ERROR_MEMORY;
  }
  malloc_fail_countdown--;
  return c_orm_default_malloc(size, out_ptr);
}

/** @brief Countdown triggering C_ORM_OK with NULL *out_ptr for defensive branch
 * testing. */
static int malloc_ok_null_countdown = -1;

/**
 * @brief Mock malloc returning C_ORM_OK with NULL pointer when countdown hits
 * 0.
 * @param size Requested allocation size.
 * @param out_ptr Output pointer receiving allocated address.
 * @return C_ORM_OK or C_ORM_ERROR_MEMORY.
 */
static c_orm_error_t my_ok_null_malloc(size_t size, void **out_ptr) {
  if (malloc_ok_null_countdown == 0) {
    malloc_ok_null_countdown--;
    *out_ptr = NULL;
    return C_ORM_OK;
  }
  if (malloc_ok_null_countdown > 0) {
    malloc_ok_null_countdown--;
  }
  return c_orm_default_malloc(size, out_ptr);
}

/** @brief Bitmask controlling which my_fail_free invocations return error. */
static unsigned int free_fail_mask = 0;
/** @brief Invocation counter for my_fail_free. */
static unsigned int free_call_idx = 0;

/**
 * @brief Mock free callback that frees memory while returning a simulated error
 * on masked call indices.
 * @param ptr Pointer to free.
 * @return C_ORM_OK or C_ORM_ERROR_MEMORY when masked.
 */
static c_orm_error_t my_fail_free(void *ptr) {
  unsigned int bit = 1U << free_call_idx++;
  c_orm_default_free(ptr);
  if ((free_fail_mask & bit) != 0U) {
    return C_ORM_ERROR_MEMORY;
  }
  return C_ORM_OK;
}
#endif

/**
 * @brief Tests memory arena creation, allocation, strdup, and cleanup.
 * @return GREATEST test result.
 */
TEST test_arena_coverage(void) {
  c_orm_arena_t *arena = NULL;
  void *ptr = NULL;
  c_orm_error_t rc;

#ifdef C_ORM_TEST_ALLOCATOR
  c_orm_malloc_fn_t old_malloc = c_orm_malloc;
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(my_test_malloc, c_orm_realloc, c_orm_free));

  malloc_fail_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, my_test_malloc(16, NULL));

  /* Test OOM in arena_new */
  malloc_fail_countdown = 0;
  rc = c_orm_arena_new(&arena);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  /* Test OOM in arena_alloc */
  malloc_fail_countdown = -1;
  rc = c_orm_arena_new(&arena);
  ASSERT_EQ(C_ORM_OK, rc);

  malloc_fail_countdown = 0;
  rc = c_orm_arena_alloc(arena, 10, &ptr);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  /* Test OOM in arena_strdup */
  {
    const char *str_oom = NULL;
    malloc_fail_countdown = 0;
    rc = c_orm_arena_strdup(arena, "fail", &str_oom);
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  }

  ASSERT_EQ(C_ORM_OK, c_orm_arena_free(arena));

  /* Reset */
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free));
#endif

  rc = c_orm_arena_new(NULL);
  ASSERT_NEQ(0, rc);

  rc = c_orm_arena_new(&arena);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(arena != NULL);

  rc = c_orm_arena_alloc(NULL, 10, &ptr);
  ASSERT_NEQ(0, rc);

  rc = c_orm_arena_alloc(arena, 0, &ptr);
  ASSERT_NEQ(0, rc);

  rc = c_orm_arena_alloc(arena, 10, NULL);
  ASSERT_NEQ(0, rc);

  /* allocate normal */
  rc = c_orm_arena_alloc(arena, 10, &ptr);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(ptr != NULL);

  /* allocate large */
  rc = c_orm_arena_alloc(arena, 5000, &ptr);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(ptr != NULL);

  /* arena_strdup coverage */
  {
    const char *dup_str = NULL;
    rc = c_orm_arena_strdup(NULL, "test", &dup_str);
    ASSERT_NEQ(0, rc);

    rc = c_orm_arena_strdup(arena, "test", NULL);
    ASSERT_NEQ(0, rc);

    rc = c_orm_arena_strdup(arena, NULL, &dup_str);
    ASSERT_EQ(C_ORM_OK, rc);
    ASSERT(dup_str == NULL);

    rc = c_orm_arena_strdup(arena, "hello", &dup_str);
    ASSERT_EQ(C_ORM_OK, rc);
    ASSERT_EQ(C_ORM_OK, rc);
    ASSERT_STR_EQ("hello", dup_str);
  }

#ifdef C_ORM_TEST_ALLOCATOR
  /* Defensive NULL-with-OK branches and free error percolation */
  {
    c_orm_arena_t *a2 = NULL;
    void *p2 = NULL;
    c_orm_free_fn_t old_free = c_orm_free;
    c_orm_malloc_fn_t saved_malloc = c_orm_malloc;

    ASSERT_EQ(C_ORM_OK,
              c_orm_set_allocators(my_ok_null_malloc, c_orm_realloc, old_free));
    malloc_ok_null_countdown = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_arena_new(&a2));

    malloc_ok_null_countdown = 1;
    ASSERT_EQ(C_ORM_OK, c_orm_arena_new(&a2));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_arena_alloc(a2, 16, &p2));

    malloc_ok_null_countdown = -1;
    /* Allocate two blocks so while(block) iterates multiple times */
    ASSERT_EQ(C_ORM_OK, c_orm_arena_alloc(a2, 3000, &p2));
    ASSERT_EQ(C_ORM_OK, c_orm_arena_alloc(a2, 3000, &p2));

    /* Fail both block frees + arena free (covers rc==OK and rc!=OK branches) */
    free_call_idx = 0;
    free_fail_mask = 0x07U;
    ASSERT_EQ(C_ORM_OK,
              c_orm_set_allocators(saved_malloc, c_orm_realloc, my_fail_free));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_arena_free(a2));

    /* Fail only final arena struct free (block frees succeed, arena free fails)
     */
    a2 = NULL;
    ASSERT_EQ(C_ORM_OK, c_orm_arena_new(&a2));
    ASSERT_EQ(C_ORM_OK, c_orm_arena_alloc(a2, 32, &p2));
    free_call_idx = 0;
    free_fail_mask = 0x02U;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_arena_free(a2));

    free_fail_mask = 0;
    ASSERT_EQ(C_ORM_OK,
              c_orm_set_allocators(saved_malloc, c_orm_realloc, old_free));
  }
#endif

  ASSERT_EQ(C_ORM_OK, c_orm_arena_free(NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_arena_free(arena));

  PASS();
}

/**
 * @brief Tests UUID generation functionality and argument validation.
 * @return GREATEST test result.
 */
TEST test_uuid_coverage(void) {
  c_orm_error_t rc;
  char buf[37];

  rc = c_orm_uuid_v4(NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_uuid_v4(buf);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ((size_t)36, strlen(buf));
  ASSERT_EQ('-', buf[8]);
  ASSERT_EQ('4', buf[14]);

#if defined(C_ORM_TEST_ALLOCATOR) && !defined(_WIN32) && !defined(_WIN64)
  c_orm_mock_urandom_path = "/nonexistent_c_orm_urandom_404";
  ASSERT_EQ(C_ORM_ERROR_CRYPTO, c_orm_uuid_v4(buf));

  c_orm_mock_urandom_path = "/dev/null";
  ASSERT_EQ(C_ORM_ERROR_CRYPTO, c_orm_uuid_v4(buf));

  c_orm_mock_urandom_path = NULL;
#endif

  PASS();
}

/**
 * @brief Arena and UUID test suite runner.
 * @param arena_uuid_suite Suite runner function name.
 */
SUITE(arena_uuid_suite) {
  RUN_TEST(test_arena_coverage);
  RUN_TEST(test_uuid_coverage);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
