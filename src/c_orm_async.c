/**
 * @file c_orm_async.c
 * @brief Modality-aware asynchronous CRUD API for c-orm.
 */

/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_db.h"
#include "c_orm_log.h"
#include <stdlib.h>
/* clang-format on */

/** @brief Operation kind discriminator for `c_orm_async_job_t`. */
typedef enum {
  C_ORM_ASYNC_OP_INSERT = 0,
  C_ORM_ASYNC_OP_FIND_ALL,
  C_ORM_ASYNC_OP_FIND_BY_ID_INT32,
  C_ORM_ASYNC_OP_UPDATE,
  C_ORM_ASYNC_OP_DELETE,
  C_ORM_ASYNC_OP_EXEC_RAW
} c_orm_async_op_t;

/**
 * @brief Self-contained task descriptor dispatched through `db->modality`.
 */
typedef struct c_orm_async_job {
  /** @brief Operation kind. */
  c_orm_async_op_t op;
  /** @brief Target database connection. */
  c_orm_db_t *db;
  /** @brief Table metadata pointer (NULL for raw SQL). */
  const c_orm_table_meta_t *meta;
  /** @brief Input struct pointer for insert/update/delete. */
  const void *in_struct;
  /** @brief Output struct or array pointer for find operations. */
  void *out_ptr;
  /** @brief Raw SQL query string for `C_ORM_ASYNC_OP_EXEC_RAW`. */
  const char *sql;
  /** @brief 32-bit primary key for `C_ORM_ASYNC_OP_FIND_BY_ID_INT32`. */
  int32_t id32;
  /** @brief Optional user completion callback. */
  c_orm_async_cb_t cb;
  /** @brief Opaque user context pointer passed to `cb`. */
  void *ctx;
} c_orm_async_job_t;

/**
 * @brief Executes the underlying synchronous database operation for `job`.
 * @param job Populated async job descriptor.
 * @return C_ORM_OK on success, or database operation error code.
 */
static c_orm_error_t c_orm_async_exec_op(const c_orm_async_job_t *job) {
  if (job->op == C_ORM_ASYNC_OP_INSERT) {
    return c_orm_insert(job->db, job->meta, job->in_struct);
  }
  if (job->op == C_ORM_ASYNC_OP_FIND_ALL) {
    return c_orm_find_all(job->db, job->meta, job->out_ptr);
  }
  if (job->op == C_ORM_ASYNC_OP_FIND_BY_ID_INT32) {
    return c_orm_find_by_id_int32(job->db, job->meta, job->id32, job->out_ptr);
  }
  if (job->op == C_ORM_ASYNC_OP_UPDATE) {
    return c_orm_update(job->db, job->meta, job->in_struct);
  }
  if (job->op == C_ORM_ASYNC_OP_DELETE) {
    return c_orm_delete(job->db, job->meta, job->in_struct);
  }
  return c_orm_execute_raw(job->db, job->sql);
}

/**
 * @brief Executes an `c_orm_async_job_t` and delivers its result to `job->cb`.
 * @param task_ctx Pointer to `c_orm_async_job_t`.
 * @return C_ORM_OK on success, or operation/callback error code.
 */
static c_orm_error_t c_orm_async_run_job(void *task_ctx) {
  c_orm_async_job_t *job = (c_orm_async_job_t *)task_ctx;
  c_orm_error_t rc;

  rc = c_orm_async_exec_op(job);
  if (rc != C_ORM_OK) {
    if (job->cb) {
      c_orm_error_t cb_rc = job->cb(rc, job->ctx);
      if (cb_rc != C_ORM_OK) {
        return cb_rc;
      }
    }
    return rc;
  }
  if (job->cb) {
    return job->cb(C_ORM_OK, job->ctx);
  }
  return C_ORM_OK;
}

/**
 * @brief Executes a heap-allocated `c_orm_async_job_t` on an external executor
 * and frees the descriptor upon completion.
 * @param task_ctx Heap-allocated `c_orm_async_job_t` pointer.
 * @return C_ORM_OK on success, or operation/callback/free error code.
 */
static c_orm_error_t c_orm_async_run_heap_job(void *task_ctx) {
  c_orm_error_t rc;
  rc = c_orm_async_run_job(task_ctx);
  if (rc != C_ORM_OK) {
    c_orm_system_free(task_ctx);
    return rc;
  }
  return c_orm_system_free(task_ctx);
}

/**
 * @brief Dispatches `job` according to `db->modality` and `db->modality_ctx`.
 * @param db Database connection.
 * @param job Populated async job descriptor.
 * @return C_ORM_OK on success, or error code from executor or job.
 */
static c_orm_error_t c_orm_async_dispatch(c_orm_db_t *db,
                                          c_orm_async_job_t *job) {
  if (db->modality != C_ORM_MODALITY_SYNC && db->modality_ctx != NULL) {
    c_orm_async_executor_t *exec = (c_orm_async_executor_t *)db->modality_ctx;
    c_orm_async_job_t *heap_job = NULL;
    c_orm_error_t rc;
    if (!exec->submit) {
      return C_ORM_ERROR_VALIDATION;
    }
    rc = c_orm_system_malloc(sizeof(c_orm_async_job_t), (void **)&heap_job);
    if (rc != C_ORM_OK) {
      return rc;
    }
    *heap_job = *job;
    return exec->submit(c_orm_async_run_heap_job, heap_job, exec->user_data);
  }

  if (db->modality == C_ORM_MODALITY_THREAD_POOL) {
    c_orm_error_t rc;
    c_orm_error_t job_rc;
    c_orm_error_t unlock_rc;

    if (!db->modality_lock) {
      rc = c_orm_mutex_create(&db->modality_lock);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
    rc = c_orm_mutex_lock(db->modality_lock);
    if (rc != C_ORM_OK) {
      return rc;
    }
    job_rc = c_orm_async_run_job(job);
    if (job_rc != C_ORM_OK) {
      unlock_rc = c_orm_mutex_unlock(db->modality_lock);
      if (unlock_rc != C_ORM_OK) {
        return unlock_rc;
      }
      return job_rc;
    }
    return c_orm_mutex_unlock(db->modality_lock);
  }

  return c_orm_async_run_job(job);
}

/**
 * @brief Async execution wrapper for record insertion.
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_struct Pointer to input struct data.
 * @param cb Callback function executed upon completion.
 * @param ctx User context passed to `cb`.
 * @return C_ORM_OK on success, or error code from `c_orm_insert` / `cb`.
 */
C_ORM_EXPORT c_orm_error_t c_orm_insert_async(c_orm_db_t *db,
                                              const c_orm_table_meta_t *meta,
                                              const void *in_struct,
                                              c_orm_async_cb_t cb, void *ctx) {
  c_orm_async_job_t job;
  LOG_DEBUG("c_orm_insert_async: entry");
  if (!db || !meta || !in_struct) {
    return C_ORM_ERROR_MEMORY;
  }
  job.op = C_ORM_ASYNC_OP_INSERT;
  job.db = db;
  job.meta = meta;
  job.in_struct = in_struct;
  job.out_ptr = NULL;
  job.sql = NULL;
  job.id32 = 0;
  job.cb = cb;
  job.ctx = ctx;
  return c_orm_async_dispatch(db, &job);
}

/**
 * @brief Asynchronous fetch all records wrapper.
 * @param db Database connection.
 * @param meta Table metadata.
 * @param out_array Pointer to output `Generic_Array`.
 * @param cb Callback function executed upon completion.
 * @param ctx User context passed to `cb`.
 * @return C_ORM_OK on success, or error code from `c_orm_find_all` / `cb`.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_all_async(c_orm_db_t *db,
                                                const c_orm_table_meta_t *meta,
                                                void *out_array,
                                                c_orm_async_cb_t cb,
                                                void *ctx) {
  c_orm_async_job_t job;
  LOG_DEBUG("c_orm_find_all_async: entry");
  if (!db || !meta || !out_array) {
    return C_ORM_ERROR_MEMORY;
  }
  job.op = C_ORM_ASYNC_OP_FIND_ALL;
  job.db = db;
  job.meta = meta;
  job.in_struct = NULL;
  job.out_ptr = out_array;
  job.sql = NULL;
  job.id32 = 0;
  job.cb = cb;
  job.ctx = ctx;
  return c_orm_async_dispatch(db, &job);
}

/**
 * @brief Asynchronous fetch by 32-bit integer primary key wrapper.
 * @param db Database connection.
 * @param meta Table metadata.
 * @param id 32-bit integer primary key value.
 * @param out_struct Output struct pointer to hydrate.
 * @param cb Optional completion callback.
 * @param ctx User context passed to `cb`.
 * @return C_ORM_OK on success, or error code from `c_orm_find_by_id_int32` /
 * `cb`.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_by_id_int32_async(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, int32_t id,
    void *out_struct, c_orm_async_cb_t cb, void *ctx) {
  c_orm_async_job_t job;
  LOG_DEBUG("c_orm_find_by_id_int32_async: entry");
  if (!db || !meta || !out_struct) {
    return C_ORM_ERROR_MEMORY;
  }
  job.op = C_ORM_ASYNC_OP_FIND_BY_ID_INT32;
  job.db = db;
  job.meta = meta;
  job.in_struct = NULL;
  job.out_ptr = out_struct;
  job.sql = NULL;
  job.id32 = id;
  job.cb = cb;
  job.ctx = ctx;
  return c_orm_async_dispatch(db, &job);
}

/**
 * @brief Asynchronous record update wrapper.
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_struct Struct instance containing updated column values and PK.
 * @param cb Optional completion callback.
 * @param ctx User context passed to `cb`.
 * @return C_ORM_OK on success, or error code from `c_orm_update` / `cb`.
 */
C_ORM_EXPORT c_orm_error_t c_orm_update_async(c_orm_db_t *db,
                                              const c_orm_table_meta_t *meta,
                                              const void *in_struct,
                                              c_orm_async_cb_t cb, void *ctx) {
  c_orm_async_job_t job;
  LOG_DEBUG("c_orm_update_async: entry");
  if (!db || !meta || !in_struct) {
    return C_ORM_ERROR_MEMORY;
  }
  job.op = C_ORM_ASYNC_OP_UPDATE;
  job.db = db;
  job.meta = meta;
  job.in_struct = in_struct;
  job.out_ptr = NULL;
  job.sql = NULL;
  job.id32 = 0;
  job.cb = cb;
  job.ctx = ctx;
  return c_orm_async_dispatch(db, &job);
}

/**
 * @brief Asynchronous record deletion wrapper.
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_struct Struct instance identifying the row to delete.
 * @param cb Optional completion callback.
 * @param ctx User context passed to `cb`.
 * @return C_ORM_OK on success, or error code from `c_orm_delete` / `cb`.
 */
C_ORM_EXPORT c_orm_error_t c_orm_delete_async(c_orm_db_t *db,
                                              const c_orm_table_meta_t *meta,
                                              const void *in_struct,
                                              c_orm_async_cb_t cb, void *ctx) {
  c_orm_async_job_t job;
  LOG_DEBUG("c_orm_delete_async: entry");
  if (!db || !meta || !in_struct) {
    return C_ORM_ERROR_MEMORY;
  }
  job.op = C_ORM_ASYNC_OP_DELETE;
  job.db = db;
  job.meta = meta;
  job.in_struct = in_struct;
  job.out_ptr = NULL;
  job.sql = NULL;
  job.id32 = 0;
  job.cb = cb;
  job.ctx = ctx;
  return c_orm_async_dispatch(db, &job);
}

/**
 * @brief Asynchronous raw SQL execution wrapper.
 * @param db Database connection.
 * @param sql Raw SQL statement string.
 * @param cb Optional completion callback.
 * @param ctx User context passed to `cb`.
 * @return C_ORM_OK on success, or error code from `c_orm_execute_raw` / `cb`.
 */
C_ORM_EXPORT c_orm_error_t c_orm_execute_raw_async(c_orm_db_t *db,
                                                   const char *sql,
                                                   c_orm_async_cb_t cb,
                                                   void *ctx) {
  c_orm_async_job_t job;
  LOG_DEBUG("c_orm_execute_raw_async: entry");
  if (!db || !sql) {
    return C_ORM_ERROR_MEMORY;
  }
  job.op = C_ORM_ASYNC_OP_EXEC_RAW;
  job.db = db;
  job.meta = NULL;
  job.in_struct = NULL;
  job.out_ptr = NULL;
  job.sql = sql;
  job.id32 = 0;
  job.cb = cb;
  job.ctx = ctx;
  return c_orm_async_dispatch(db, &job);
}
