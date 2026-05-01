/**
 * @file c_orm_alloc.c
 * @brief Memory allocation wrappers for c-orm.
 */

/* clang-format off */
#include "c_orm_meta.h"
#include <stdlib.h>
/* clang-format on */

#ifdef C_ORM_TEST_ALLOCATOR
/**
 * @brief Global pointer for malloc override.
 */
void *(*c_orm_malloc)(size_t size) = malloc;

/**
 * @brief Global pointer for free override.
 */
void (*c_orm_free)(void *ptr) = free;

/**
 * @brief Global pointer for realloc override.
 */
void *(*c_orm_realloc)(void *ptr, size_t size) = realloc;
#endif
