/* clang-format off */
#include "c_orm_ast.h"
#include "c_orm_uuid.h"
#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
/* clang-format on */

#ifdef C_ORM_TEST_ALLOCATOR
extern void *(*c_orm_malloc)(size_t size);
extern void (*c_orm_free)(void *ptr);

static int malloc_fail_countdown = -1;
static void *my_test_malloc(size_t size) {
  if (malloc_fail_countdown == 0) {
    return NULL;
  }
  if (malloc_fail_countdown > 0) {
    malloc_fail_countdown--;
  }
  return malloc(size);
}
#endif

TEST test_arena_coverage(void) {
  c_orm_arena_t *arena = NULL;
  void *ptr = NULL;
  int rc;

#ifdef C_ORM_TEST_ALLOCATOR
  void *(*old_malloc)(size_t) = c_orm_malloc;
  c_orm_malloc = my_test_malloc;

  /* Test OOM in arena_new */
  malloc_fail_countdown = 0;
  rc = c_orm_arena_new(&arena);
  ASSERT_EQ(1, rc);

  /* Test OOM in arena_alloc */
  malloc_fail_countdown = -1;
  rc = c_orm_arena_new(&arena);
  ASSERT_EQ(0, rc);

  malloc_fail_countdown = 0;
  rc = c_orm_arena_alloc(arena, 10, &ptr);
  ASSERT_EQ(1, rc);

  /* Reset */
  c_orm_malloc = old_malloc;
#endif

  rc = c_orm_arena_new(NULL);
  ASSERT_NEQ(0, rc);

  rc = c_orm_arena_new(&arena);
  ASSERT_EQ(0, rc);
  ASSERT(arena != NULL);

  rc = c_orm_arena_alloc(NULL, 10, &ptr);
  ASSERT_NEQ(0, rc);

  rc = c_orm_arena_alloc(arena, 0, &ptr);
  ASSERT_NEQ(0, rc);

  rc = c_orm_arena_alloc(arena, 10, NULL);
  ASSERT_NEQ(0, rc);

  /* allocate normal */
  rc = c_orm_arena_alloc(arena, 10, &ptr);
  ASSERT_EQ(0, rc);
  ASSERT(ptr != NULL);

  /* allocate large */
  rc = c_orm_arena_alloc(arena, 5000, &ptr);
  ASSERT_EQ(0, rc);
  ASSERT(ptr != NULL);

  c_orm_arena_free(NULL);
  c_orm_arena_free(arena);

  PASS();
}

TEST test_uuid_coverage(void) {
  c_orm_error_t rc;
  char buf[37];

  rc = c_orm_uuid_v4(NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_uuid_v4(buf);
  ASSERT_EQ(C_ORM_OK, rc);

  PASS();
}

SUITE(arena_uuid_suite) {
  RUN_TEST(test_arena_coverage);
  RUN_TEST(test_uuid_coverage);
}
