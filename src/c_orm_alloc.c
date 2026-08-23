#if defined(__clang__) || defined(__GNUC__)
#endif
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
static void *default_malloc(size_t size) { return malloc(size); }
static void default_free(void *ptr) { free(ptr); }
static void *default_realloc(void *ptr, size_t size) {
  return realloc(ptr, size);
}

/**
 * @brief Global pointer for malloc override.
 */
C_ORM_EXPORT void *(*c_orm_malloc)(size_t size) = default_malloc;

/**
 * @brief Global pointer for free override.
 */
C_ORM_EXPORT void (*c_orm_free)(void *ptr) = default_free;

/**
 * @brief Global pointer for realloc override.
 */
C_ORM_EXPORT void *(*c_orm_realloc)(void *ptr, size_t size) = default_realloc;

C_ORM_EXPORT c_orm_error_t c_orm_set_allocators(void *(*m)(size_t),
                                                void *(*r)(void *, size_t),
                                                void (*f)(void *)) {
  c_orm_malloc = m;
  c_orm_realloc = r;
  c_orm_free = f;
  return C_ORM_OK;
}
#endif

C_ORM_EXPORT c_orm_error_t c_orm_strdup(const char *s, char **out_dup) {
  size_t len;
  char *dup;
  if (!out_dup)
    return C_ORM_ERROR_VALIDATION;
  if (!s) {
    *out_dup = NULL;
    return C_ORM_OK;
  }
  len = strlen(s);
  dup = (char *)C_ORM_MALLOC(len + 1);
  if (dup) {
    memcpy(dup, s, len + 1);
  }
  *out_dup = dup;
  return dup ? C_ORM_OK : C_ORM_ERROR_MEMORY;
}

#if defined(__clang__) || defined(__GNUC__)
#endif
