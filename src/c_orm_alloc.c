/**
 * @file c_orm_alloc.c
 * @brief Memory allocation wrappers for c-orm.
 */

/* clang-format off */
#include "c_orm_meta.h"
#include <stdlib.h>
#include <string.h>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif
/* clang-format on */

/**
 * @brief Default malloc callback wrapper returning c_orm_error_t.
 * @param size Number of bytes to allocate.
 * @param out_ptr Output pointer receiving allocated memory, or NULL on failure.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_default_malloc(size_t size, void **out_ptr) {
  void *p;
#if defined(_MSC_VER) && defined(_DEBUG)
  {
    static int crt_configured = 0;
    if (!crt_configured) {
      crt_configured = 1;
      _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
      _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
      _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
      _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
      _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
      _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    }
  }
#endif
  if (!out_ptr) {
    return C_ORM_ERROR_MEMORY;
  }
  if (size > (((size_t)-1) >> 1)) {
    *out_ptr = NULL;
    return C_ORM_ERROR_MEMORY;
  }
  p = malloc(size == 0 ? 1 : size);
  *out_ptr = p;
  return p ? C_ORM_OK : C_ORM_ERROR_MEMORY;
}

/**
 * @brief Default free callback wrapper returning c_orm_error_t.
 * @param ptr Pointer to memory to free.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_default_free(void *ptr) {
  free(ptr);
  return C_ORM_OK;
}

/**
 * @brief Default realloc callback wrapper returning c_orm_error_t.
 * @param ptr Pointer to previously allocated memory.
 * @param size New size in bytes.
 * @param out_ptr Output pointer receiving reallocated memory, or NULL on
 * failure.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_default_realloc(void *ptr, size_t size,
                                                 void **out_ptr) {
  void *p;
  if (!out_ptr) {
    return C_ORM_ERROR_MEMORY;
  }
  if (size > (((size_t)-1) >> 1)) {
    *out_ptr = NULL;
    return C_ORM_ERROR_MEMORY;
  }
  p = realloc(ptr, size == 0 ? 1 : size);
  *out_ptr = p;
  return p ? C_ORM_OK : C_ORM_ERROR_MEMORY;
}

#ifdef C_ORM_TEST_ALLOCATOR
/**
 * @brief Global pointer for malloc override.
 */
C_ORM_EXPORT c_orm_malloc_fn_t c_orm_malloc = c_orm_default_malloc;

/**
 * @brief Global pointer for free override.
 */
C_ORM_EXPORT c_orm_free_fn_t c_orm_free = c_orm_default_free;

/**
 * @brief Global pointer for realloc override.
 */
C_ORM_EXPORT c_orm_realloc_fn_t c_orm_realloc = c_orm_default_realloc;

/**
 * @brief Configures custom dynamic memory allocators for testing.
 * @param m Malloc callback function.
 * @param r Realloc callback function.
 * @param f Free callback function.
 * @return C_ORM_OK on success, or C_ORM_ERROR_VALIDATION if any callback is
 * NULL.
 */
C_ORM_EXPORT c_orm_error_t c_orm_set_allocators(c_orm_malloc_fn_t m,
                                                c_orm_realloc_fn_t r,
                                                c_orm_free_fn_t f) {
  if (!m || !r || !f) {
    return C_ORM_ERROR_VALIDATION;
  }
  c_orm_malloc = m;
  c_orm_realloc = r;
  c_orm_free = f;
  return C_ORM_OK;
}

/**
 * @brief Resets custom dynamic memory allocators back to system defaults.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_reset_allocators(void) {
  c_orm_malloc = c_orm_default_malloc;
  c_orm_realloc = c_orm_default_realloc;
  c_orm_free = c_orm_default_free;
  return C_ORM_OK;
}
#endif

/**
 * @brief Free system memory allocated by c_orm_system_malloc/calloc/realloc.
 * @param ptr Pointer to allocated memory block to free.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_system_free(void *ptr) {
#ifdef C_ORM_TEST_ALLOCATOR
  return c_orm_free(ptr);
#else
  return c_orm_default_free(ptr);
#endif
}

/**
 * @brief Allocate system memory block.
 * @param size Number of bytes to allocate.
 * @param out_ptr Pointer to receive allocated memory pointer.
 * @return C_ORM_OK on success, or error code on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_system_malloc(size_t size, void **out_ptr) {
  c_orm_error_t rc;
  if (!out_ptr) {
    return C_ORM_ERROR_MEMORY;
  }
  *out_ptr = NULL;
#ifdef C_ORM_TEST_ALLOCATOR
  rc = c_orm_malloc(size, out_ptr);
#else
  rc = c_orm_default_malloc(size, out_ptr);
#endif
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (!*out_ptr) {
    return C_ORM_ERROR_MEMORY;
  }
  return C_ORM_OK;
}

/**
 * @brief Allocate and zero-initialize system memory.
 * @param nmemb Number of elements.
 * @param size Size of each element in bytes.
 * @param out_ptr Pointer to receive allocated memory pointer.
 * @return C_ORM_OK on success, or error code on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_system_calloc(size_t nmemb, size_t size,
                                               void **out_ptr) {
  c_orm_error_t rc;
  size_t total;
  if (!out_ptr) {
    return C_ORM_ERROR_MEMORY;
  }
  *out_ptr = NULL;
  if (size != 0 && nmemb > ((size_t)-1) / size) {
    return C_ORM_ERROR_OVERFLOW;
  }
  total = nmemb * size;
  rc = c_orm_system_malloc(total, out_ptr);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (total > 0) {
    memset(*out_ptr, 0, total);
  }
  return C_ORM_OK;
}

/**
 * @brief Reallocate system memory block.
 * @param ptr Existing memory pointer or NULL.
 * @param size New size in bytes.
 * @param out_ptr Pointer to receive reallocated memory pointer.
 * @return C_ORM_OK on success, or error code on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_system_realloc(void *ptr, size_t size,
                                                void **out_ptr) {
  c_orm_error_t rc;
  if (!out_ptr) {
    return C_ORM_ERROR_MEMORY;
  }
  *out_ptr = NULL;
#ifdef C_ORM_TEST_ALLOCATOR
  rc = c_orm_realloc(ptr, size, out_ptr);
#else
  rc = c_orm_default_realloc(ptr, size, out_ptr);
#endif
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (!*out_ptr) {
    return C_ORM_ERROR_MEMORY;
  }
  return C_ORM_OK;
}

/**
 * @brief Duplicates a null-terminated string using c-orm memory allocator.
 * @param s Source null-terminated string to duplicate.
 * @param out_dup Output pointer to receive duplicated string.
 * @return C_ORM_OK on success, or error code on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_strdup(const char *s, char **out_dup) {
  size_t len;
  char *dup = NULL;
  c_orm_error_t rc;
  if (!out_dup) {
    return C_ORM_ERROR_VALIDATION;
  }
  if (!s) {
    *out_dup = NULL;
    return C_ORM_OK;
  }
  len = strlen(s);
  rc = c_orm_system_malloc(len + 1, (void **)&dup);
  if (rc != C_ORM_OK) {
    *out_dup = NULL;
    return rc;
  }
  memcpy(dup, s, len + 1);
  *out_dup = dup;
  return C_ORM_OK;
}
