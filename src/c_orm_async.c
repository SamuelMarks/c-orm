/**
 * @file c_orm_async.c
 * @brief Implementation of high-level asynchronous API for c-orm.
 *
 * When built with single-threaded mode (`C_ORM_THREADING=OFF`) or when
 * dispatched inline without an external event loop, these functions execute
 * synchronously on the calling thread and invoke the completion callback
 * immediately before returning the operation's `c_orm_error_t` status code.
 */

/* Define C_ORM_ASYNC_EXPORTS if not already defined for shared builds */

/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_log.h"
#include <stdlib.h>
/* clang-format on */

#if defined(_WIN32) && defined(c_orm_async_EXPORTS)
#ifndef C_ORM_ASYNC_EXPORTS
#define C_ORM_ASYNC_EXPORTS
#endif
#endif

/**
 * @brief Asynchronously inserts a record into the database.
 *
 * Falls back to synchronous execution on the calling thread when
 * `C_ORM_THREADING=OFF` or no background worker pool is attached, invoking `cb`
 * inline with the resulting `c_orm_error_t`.
 *
 * @param db The database connection.
 * @param meta The table metadata.
 * @param in_struct Pointer to the structure containing data to insert.
 * @param cb Callback function to execute upon completion.
 * @param ctx User context pointer to pass to the callback.
 * @return C_ORM_OK on success, or error code propagated from `c_orm_insert`.
 */
C_ORM_EXPORT c_orm_error_t c_orm_insert_async(c_orm_db_t *db,
                                              const c_orm_table_meta_t *meta,
                                              const void *in_struct,
                                              void (*cb)(c_orm_error_t, void *),
                                              void *ctx) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_insert_async: entry");

  if (!db || !meta || !in_struct) {
    LOG_DEBUG("c_orm_insert_async: invalid arguments");
    rc = C_ORM_ERROR_MEMORY;
    return rc;
  }

  /* Synchronous fallback when C_ORM_THREADING=OFF or inline dispatch is active.
   * Propagates the resulting c_orm_error_t directly to the completion callback.
   */
  rc = c_orm_insert(db, meta, in_struct);

  if (cb) {
    cb(rc, ctx);
  }

  LOG_DEBUG("c_orm_insert_async: exit");
  return rc;
}

/**
 * @brief Asynchronously finds all records from a table.
 *
 * Falls back to synchronous execution on the calling thread when
 * `C_ORM_THREADING=OFF` or no background worker pool is attached, invoking `cb`
 * inline with the resulting `c_orm_error_t`.
 *
 * @param db The database connection.
 * @param meta The table metadata.
 * @param out_array Array to store the retrieved records.
 * @param cb Callback function to execute upon completion.
 * @param ctx User context pointer to pass to the callback.
 * @return C_ORM_OK on success, or error code propagated from `c_orm_find_all`.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_all_async(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, void *out_array,
    void (*cb)(c_orm_error_t, void *), void *ctx) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_find_all_async: entry");

  if (!db || !meta || !out_array) {
    LOG_DEBUG("c_orm_find_all_async: invalid arguments");
    rc = C_ORM_ERROR_MEMORY;
    return rc;
  }

  rc = c_orm_find_all(db, meta, out_array);

  if (cb) {
    cb(rc, ctx);
  }

  LOG_DEBUG("c_orm_find_all_async: exit");
  return rc;
}
