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
/**
 * @brief Default malloc callback wrapper.
 * @param size Number of bytes to allocate.
 * @return Pointer to allocated memory, or NULL on failure.
 */
static void *default_malloc(size_t size) { return malloc(size); }

/**
 * @brief Default free callback wrapper.
 * @param ptr Pointer to memory to free.
 */
static void default_free(void *ptr) { free(ptr); }

/**
 * @brief Default realloc callback wrapper.
 * @param ptr Pointer to previously allocated memory.
 * @param size New size in bytes.
 * @return Pointer to reallocated memory, or NULL on failure.
 */
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

/**
 * @brief Configures custom dynamic memory allocators for testing.
 * @param m Malloc callback function.
 * @param r Realloc callback function.
 * @param f Free callback function.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_set_allocators(void *(*m)(size_t),
                                                void *(*r)(void *, size_t),
                                                void (*f)(void *)) {
  c_orm_malloc = m;
  c_orm_realloc = r;
  c_orm_free = f;
  return C_ORM_OK;
}
#endif

/**
 * @brief Duplicates a null-terminated string using c-orm memory allocator.
 * @param s Source null-terminated string to duplicate.
 * @param out_dup Output pointer to receive duplicated string.
 * @return C_ORM_OK on success, or error code on failure.
 */
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
