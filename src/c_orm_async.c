/**
 * @file c_orm_async.c
 * @brief Implementation of high-level asynchronous API for c-orm.
 */

/* clang-format off */
/* Define C_ORM_ASYNC_EXPORTS if not already defined for shared builds */
#if defined(_WIN32) && defined(c_orm_async_EXPORTS)
  #ifndef C_ORM_ASYNC_EXPORTS
    #define C_ORM_ASYNC_EXPORTS
  #endif
#endif
#include "c_orm_api.h"
#include <stdlib.h>
/* clang-format on */

C_ORM_EXPORT c_orm_error_t c_orm_insert_async(c_orm_db_t *db,
                                              const c_orm_table_meta_t *meta,
                                              const void *in_struct,
                                              void (*cb)(c_orm_error_t, void *),
                                              void *ctx) {
  if (!db || !meta || !in_struct)
    return C_ORM_ERROR_MEMORY;
  /* Phase 5 Async simulation stub mapped for libuv loops via queue injection */
  if (cb)
    cb(C_ORM_ERROR_NOT_IMPLEMENTED, ctx);
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

C_ORM_EXPORT c_orm_error_t c_orm_find_all_async(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, void *out_array,
    void (*cb)(c_orm_error_t, void *), void *ctx) {
  if (!db || !meta || !out_array)
    return C_ORM_ERROR_MEMORY;
  /* Async fetch loop enqueue simulation */
  if (cb)
    cb(C_ORM_ERROR_NOT_IMPLEMENTED, ctx);
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}