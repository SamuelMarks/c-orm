/**
 * @file c_orm_pool.c
 * @brief Thread-safe database connection pool implementation for c-orm.
 */

/* clang-format off */
#if !defined(_WIN32) && !defined(_WIN64) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L
#endif
#include "c_orm_safe_crt.h"
#include "c_orm_api.h"
#include "c_orm_db.h"
#include "c_orm_log.h"
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32) || defined(_WIN64)
extern __declspec(dllimport) void __stdcall Sleep(unsigned long dwMilliseconds);
#else
#include <time.h>
#endif
/* clang-format on */

/**
 * @brief Internal structure representing a managed connection pool.
 * @var url Duplicated database connection URL string.
 * @var min_conns Minimum connections pre-warmed at initialization.
 * @var max_conns Maximum concurrent connections permitted in the pool.
 * @var timeout_ms Maximum wait duration in milliseconds when pool is full.
 * @var conns Array of managed database connection pointers.
 * @var in_use Array of lease flags (1 = leased, 0 = available).
 * @var lock Synchronization mutex protecting pool slot arrays.
 */
struct c_orm_pool {
  /** @brief Duplicated database connection URL string. */
  char *url;
  /** @brief Minimum connections pre-warmed at initialization. */
  int min_conns;
  /** @brief Maximum concurrent connections permitted in the pool. */
  int max_conns;
  /** @brief Maximum wait duration in milliseconds when pool is full. */
  int timeout_ms;
  /** @brief Array of managed database connection pointers. */
  c_orm_db_t **conns;
  /** @brief Array of lease flags (1 = leased, 0 = available). */
  unsigned char *in_use;
  /** @brief Synchronization mutex protecting pool slot arrays. */
  void *lock;
  /** @brief Count of times connection acquisition timed out due to exhaustion.
   */
  size_t exhaustion_count;
  /** @brief Count of acquire attempts that had to wait for an idle slot. */
  size_t wait_count;
  /** @brief Accumulated wait duration in milliseconds across acquisitions. */
  double total_wait_time_ms;
};

/**
 * @brief Destroy the connection pool and disconnect all managed connections.
 * @param pool The pool to destroy.
 * @return C_ORM_OK on success, or the last driver/mutex cleanup error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_pool_destroy(c_orm_pool_t *pool) {
  void *lock;
  int i;
  c_orm_error_t drc = C_ORM_OK;
  c_orm_error_t saved_rc = C_ORM_OK;
  c_orm_error_t lock_rc = C_ORM_OK;

  LOG_DEBUG("c_orm_pool_destroy: entry");
  if (!pool) {
    LOG_DEBUG("c_orm_pool_destroy: pool is NULL");
    return C_ORM_ERROR_VALIDATION;
  }

  lock = pool->lock;
  pool->lock = NULL;

  if (pool->conns) {
    for (i = 0; i < pool->max_conns; ++i) {
      if (pool->conns[i]) {
        c_orm_db_t *conn = pool->conns[i];
        pool->conns[i] = NULL;
        drc = conn->vtable->disconnect(conn);
        if (drc != C_ORM_OK) {
          saved_rc = drc;
          goto pool_next_slot;
        }
      }
    pool_next_slot:;
    }
  }

  c_orm_system_free(pool->in_use);
  c_orm_system_free(pool->conns);
  c_orm_system_free(pool->url);
  c_orm_system_free(pool);

  if (lock) {
    lock_rc = c_orm_mutex_destroy(lock);
    if (lock_rc != C_ORM_OK) {
      return lock_rc;
    }
  }

  LOG_DEBUG("c_orm_pool_destroy: exit");
  drc = saved_rc;
  return drc;
}

/**
 * @brief Initializes a thread-safe connection pool with wait timeouts and retry
 * logic.
 * @param url Connection string.
 * @param min_conns Minimum number of connections to keep alive.
 * @param max_conns Maximum number of connections.
 * @param timeout_ms Milliseconds to wait before failing to acquire a
 * connection.
 * @param out_pool Pointer to receive the initialized pool.
 * @return C_ORM_OK on success, or an error code on validation/allocation/driver
 * failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_pool_init(const char *url, int min_conns,
                                           int max_conns, int timeout_ms,
                                           c_orm_pool_t **out_pool) {
  c_orm_pool_t *pool = NULL;
  c_orm_error_t rc;
  int i;

  LOG_DEBUG("c_orm_pool_init: entry");
  if (!url || !out_pool || min_conns < 0 || max_conns <= 0 ||
      min_conns > max_conns || timeout_ms < 0) {
    LOG_DEBUG("c_orm_pool_init: invalid arguments");
    return C_ORM_ERROR_VALIDATION;
  }

  *out_pool = NULL;

  rc = c_orm_system_calloc(1, sizeof(c_orm_pool_t), (void **)&pool);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_pool_init: pool allocation failed");
    return rc;
  }

  pool->min_conns = min_conns;
  pool->max_conns = max_conns;
  pool->timeout_ms = timeout_ms;

  rc = c_orm_strdup(url, &pool->url);
  if (rc != C_ORM_OK) {
    c_orm_pool_destroy(pool);
    return rc;
  }

  rc = c_orm_system_calloc((size_t)max_conns, sizeof(c_orm_db_t *),
                           (void **)&pool->conns);
  if (rc != C_ORM_OK) {
    c_orm_pool_destroy(pool);
    return rc;
  }

  rc = c_orm_system_calloc((size_t)max_conns, sizeof(unsigned char),
                           (void **)&pool->in_use);
  if (rc != C_ORM_OK) {
    c_orm_pool_destroy(pool);
    return rc;
  }

  rc = c_orm_mutex_create(&pool->lock);
  if (rc != C_ORM_OK) {
    c_orm_pool_destroy(pool);
    return rc;
  }

  for (i = 0; i < min_conns; ++i) {
    rc = c_orm_connect_url(pool->url, &pool->conns[i]);
    if (rc != C_ORM_OK) {
      c_orm_pool_destroy(pool);
      return rc;
    }
  }

  *out_pool = pool;
  LOG_DEBUG("c_orm_pool_init: exit");
  return C_ORM_OK;
}

/**
 * @brief Acquire a session-based connection from the pool.
 * @param pool The connection pool.
 * @param out_db Pointer to receive the acquired db connection session.
 * @return C_ORM_OK on success, C_ORM_ERROR_TIMEOUT when exhausted past timeout,
 * or an error code on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_pool_acquire(c_orm_pool_t *pool,
                                              c_orm_db_t **out_db) {
  c_orm_error_t rc;
  c_orm_error_t unlock_rc;
  int elapsed_ms = 0;
  int i;

  LOG_DEBUG("c_orm_pool_acquire: entry");
  if (!pool || !out_db) {
    LOG_DEBUG("c_orm_pool_acquire: invalid arguments");
    return C_ORM_ERROR_VALIDATION;
  }

  *out_db = NULL;

  for (;;) {
    rc = c_orm_mutex_lock(pool->lock);
    if (rc != C_ORM_OK) {
      return rc;
    }

    for (i = 0; i < pool->max_conns; ++i) {
      if (!pool->in_use[i] && pool->conns[i] != NULL) {
        pool->in_use[i] = 1;
        unlock_rc = c_orm_mutex_unlock(pool->lock);
        if (unlock_rc != C_ORM_OK) {
          pool->in_use[i] = 0;
          return unlock_rc;
        }
        *out_db = pool->conns[i];
        LOG_DEBUG("c_orm_pool_acquire: reused idle slot");
        return C_ORM_OK;
      }
    }

    for (i = 0; i < pool->max_conns; ++i) {
      if (!pool->in_use[i]) {
        rc = c_orm_connect_url(pool->url, &pool->conns[i]);
        if (rc != C_ORM_OK) {
          unlock_rc = c_orm_mutex_unlock(pool->lock);
          if (unlock_rc != C_ORM_OK) {
            return unlock_rc;
          }
          return rc;
        }
        pool->in_use[i] = 1;
        unlock_rc = c_orm_mutex_unlock(pool->lock);
        if (unlock_rc != C_ORM_OK) {
          pool->in_use[i] = 0;
          return unlock_rc;
        }
        *out_db = pool->conns[i];
        LOG_DEBUG("c_orm_pool_acquire: opened new slot");
        return C_ORM_OK;
      }
    }

    if (elapsed_ms >= pool->timeout_ms) {
      pool->exhaustion_count++;
      unlock_rc = c_orm_mutex_unlock(pool->lock);
      if (unlock_rc != C_ORM_OK) {
        return unlock_rc;
      }
      LOG_DEBUG("c_orm_pool_acquire: timeout waiting for connection");
      return C_ORM_ERROR_TIMEOUT;
    }

    {
      int step_ms = pool->timeout_ms - elapsed_ms;
      if (step_ms > 5) {
        step_ms = 5;
      }
      if (elapsed_ms == 0) {
        pool->wait_count++;
      }
      pool->total_wait_time_ms += (double)step_ms;
      elapsed_ms += step_ms;

      unlock_rc = c_orm_mutex_unlock(pool->lock);
      if (unlock_rc != C_ORM_OK) {
        return unlock_rc;
      }

#if defined(_WIN32) || defined(_WIN64)
      Sleep((unsigned long)step_ms);
#else
      {
        struct timespec req;
        req.tv_sec = (time_t)(step_ms / 1000);
        req.tv_nsec = (long)(step_ms % 1000) * 1000000L;
        nanosleep(&req, NULL);
      }
#endif
    }
  }
}

/**
 * @brief Safely return a session-based connection to the pool.
 * @param pool The connection pool.
 * @param db The connection to release.
 * @return C_ORM_OK on success, C_ORM_ERROR_VALIDATION if already idle or NULL,
 * or C_ORM_ERROR_NOT_FOUND if db is not managed by pool.
 */
C_ORM_EXPORT c_orm_error_t c_orm_pool_release(c_orm_pool_t *pool,
                                              c_orm_db_t *db) {
  c_orm_error_t rc;
  c_orm_error_t unlock_rc;
  int i;

  LOG_DEBUG("c_orm_pool_release: entry");
  if (!pool || !db) {
    LOG_DEBUG("c_orm_pool_release: invalid arguments");
    return C_ORM_ERROR_VALIDATION;
  }

  rc = c_orm_mutex_lock(pool->lock);
  if (rc != C_ORM_OK) {
    return rc;
  }

  for (i = 0; i < pool->max_conns; ++i) {
    if (pool->conns[i] == db) {
      if (!pool->in_use[i]) {
        unlock_rc = c_orm_mutex_unlock(pool->lock);
        if (unlock_rc != C_ORM_OK) {
          return unlock_rc;
        }
        return C_ORM_ERROR_VALIDATION;
      }
      pool->in_use[i] = 0;
      LOG_DEBUG("c_orm_pool_release: exit");
      return c_orm_mutex_unlock(pool->lock);
    }
  }

  unlock_rc = c_orm_mutex_unlock(pool->lock);
  if (unlock_rc != C_ORM_OK) {
    return unlock_rc;
  }
  return C_ORM_ERROR_NOT_FOUND;
}

C_ORM_EXPORT c_orm_error_t c_orm_pool_get_telemetry(
    c_orm_pool_t *pool, c_orm_pool_telemetry_t *out_telemetry) {
  c_orm_error_t rc;
  int i;
  size_t active = 0;
  size_t idle = 0;
  size_t slow = 0;

  if (!pool || !out_telemetry) {
    return C_ORM_ERROR_VALIDATION;
  }

  rc = c_orm_mutex_lock(pool->lock);
  if (rc != C_ORM_OK) {
    return rc;
  }

  for (i = 0; i < pool->max_conns; ++i) {
    if (pool->conns[i] != NULL) {
      if (pool->in_use[i]) {
        active++;
      } else {
        idle++;
      }
      slow += pool->conns[i]->telemetry.slow_queries_logged;
    }
  }

  out_telemetry->active_connections = active;
  out_telemetry->idle_connections = idle;
  out_telemetry->exhaustion_count = pool->exhaustion_count;
  out_telemetry->average_wait_time_ms =
      pool->wait_count > 0
          ? (pool->total_wait_time_ms / (double)pool->wait_count)
          : 0.0;
  out_telemetry->slow_queries_logged = slow;

  return c_orm_mutex_unlock(pool->lock);
}
