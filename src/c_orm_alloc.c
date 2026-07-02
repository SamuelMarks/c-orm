/**
 * @file c_orm_alloc.c
 * @brief Memory allocation wrappers for c-orm.
 */

/* clang-format off */
#include "c_orm_meta.h"
#include <stdlib.h>
#include <string.h>
/* clang-format on */

#ifdef C_ORM_TEST_ALLOCATOR
/**
 * @brief Global pointer for malloc override.
 */
C_ORM_EXPORT void *(*c_orm_malloc)(size_t size) = malloc;

/**
 * @brief Global pointer for free override.
 */
C_ORM_EXPORT void (*c_orm_free)(void *ptr) = free;

/**
 * @brief Global pointer for realloc override.
 */
C_ORM_EXPORT void *(*c_orm_realloc)(void *ptr, size_t size) = realloc;

C_ORM_EXPORT void c_orm_set_allocators(void *(*m)(size_t),
                                       void *(*r)(void *, size_t),
                                       void (*f)(void *)) {
  c_orm_malloc = m;
  c_orm_realloc = r;
  c_orm_free = f;
}
#endif

C_ORM_EXPORT char *c_orm_strdup(const char *s) {
  size_t len;
  char *dup;
  if (!s) {
    return NULL;
  }
  len = strlen(s);
  dup = (char *)C_ORM_MALLOC(len + 1);
  if (dup) {
    memcpy(dup, s, len + 1);
  }
  return dup;
}
