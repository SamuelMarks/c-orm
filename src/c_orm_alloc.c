/* clang-format off */
#include "c_orm_meta.h"
#include <stdlib.h>
/* clang-format on */

#ifdef C_ORM_TEST_ALLOCATOR
void *(*c_orm_malloc)(size_t size) = malloc;
void (*c_orm_free)(void *ptr) = free;
void *(*c_orm_realloc)(void *ptr, size_t size) = realloc;
#endif
