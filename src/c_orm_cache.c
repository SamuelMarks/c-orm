/**
 * @file c_orm_cache.c
 * @brief Implementation of statement caching for c-orm.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_api.h"
#include "c_orm_db.h"
#include "c_orm_log.h"
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32) && !defined(_WIN64) && !defined(EMSCRIPTEN_NO_THREADS) && !defined(C_ORM_NO_THREADS)
#include <pthread.h>
#endif
/* clang-format on */

#ifdef C_ORM_TEST_ALLOCATOR
/** @brief Test flag simulating cache mutex init failure. */
C_ORM_EXPORT int c_orm_mock_cache_init_fail = 0;
/** @brief Test flag simulating cache mutex lock failure. */
C_ORM_EXPORT int c_orm_mock_cache_lock_fail = 0;
/** @brief Test flag simulating cache mutex unlock failure. */
C_ORM_EXPORT int c_orm_mock_cache_unlock_fail = 0;
/** @brief Test flag simulating cache mutex destroy failure. */
C_ORM_EXPORT int c_orm_mock_cache_destroy_fail = 0;
#endif

#if defined(EMSCRIPTEN_NO_THREADS) || defined(C_ORM_NO_THREADS)
/**
 * @brief Initializes a no-op single-threaded mutex token.
 * @param out_lock Output pointer receiving allocated dummy lock token.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_cache_mutex_init(void **out_lock) {
  c_orm_error_t rc;
  rc = c_orm_system_malloc(1, out_lock);
  if (rc != C_ORM_OK) {
    return C_ORM_ERROR_MEMORY;
  }
#ifdef C_ORM_TEST_ALLOCATOR
  if (c_orm_mock_cache_init_fail) {
    (void)c_orm_system_free(*out_lock);
    *out_lock = NULL;
    return C_ORM_ERROR_BUSY;
  }
#endif
  return C_ORM_OK;
}

/**
 * @brief Acquires a no-op single-threaded mutex.
 * @param lock Mutex pointer.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BUSY when mocked.
 */
static c_orm_error_t c_orm_cache_mutex_lock(void *lock) {
  (void)lock;
#ifdef C_ORM_TEST_ALLOCATOR
  if (c_orm_mock_cache_lock_fail) {
    return C_ORM_ERROR_BUSY;
  }
#endif
  return C_ORM_OK;
}

/**
 * @brief Releases a no-op single-threaded mutex.
 * @param lock Mutex pointer.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BUSY when mocked.
 */
static c_orm_error_t c_orm_cache_mutex_unlock(void *lock) {
  (void)lock;
#ifdef C_ORM_TEST_ALLOCATOR
  if (c_orm_mock_cache_unlock_fail) {
    return C_ORM_ERROR_BUSY;
  }
#endif
  return C_ORM_OK;
}

/**
 * @brief Destroys and frees a no-op single-threaded mutex token.
 * @param lock Mutex pointer.
 * @return C_ORM_OK on success, or error code when mocked.
 */
static c_orm_error_t c_orm_cache_mutex_destroy(void *lock) {
  c_orm_error_t frc;
  frc = c_orm_system_free(lock);
#ifdef C_ORM_TEST_ALLOCATOR
  if (c_orm_mock_cache_destroy_fail) {
    return C_ORM_ERROR_BUSY;
  }
#endif
  return frc;
}
#elif defined(_WIN32) || defined(_WIN64)
/**
 * @brief Exact ABI layout of Windows RTL_CRITICAL_SECTION (24 bytes on x86,
 * 40 bytes on x64/ARM64, pointer-aligned) without including windows.h.
 * @var DebugInfo Pointer to debug info record.
 * @var LockCount Lock contention count.
 * @var RecursionCount Owning thread recursion count.
 * @var OwningThread Handle to owning thread.
 * @var LockSemaphore Contention event handle.
 * @var SpinCount Busy-wait spin count.
 */
typedef struct c_orm_win32_critical_section {
  /** @brief Pointer to debug info record. */
  void *DebugInfo;
  /** @brief Lock contention count. */
  long LockCount;
  /** @brief Owning thread recursion count. */
  long RecursionCount;
  /** @brief Handle to owning thread. */
  void *OwningThread;
  /** @brief Contention event handle. */
  void *LockSemaphore;
  /** @brief Busy-wait spin count. */
  size_t SpinCount;
} c_orm_win32_critical_section_t;

__declspec(dllimport) void __stdcall InitializeCriticalSection(
    c_orm_win32_critical_section_t *);
__declspec(dllimport) void __stdcall EnterCriticalSection(
    c_orm_win32_critical_section_t *);
__declspec(dllimport) void __stdcall LeaveCriticalSection(
    c_orm_win32_critical_section_t *);
__declspec(dllimport) void __stdcall DeleteCriticalSection(
    c_orm_win32_critical_section_t *);

/**
 * @brief Initializes a Windows CRITICAL_SECTION mutex.
 * @param out_lock Output pointer receiving allocated CRITICAL_SECTION.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on allocation failure.
 */
static c_orm_error_t c_orm_cache_mutex_init(void **out_lock) {
  c_orm_error_t rc;
  rc = c_orm_system_malloc(sizeof(c_orm_win32_critical_section_t), out_lock);
  if (rc != C_ORM_OK) {
    return C_ORM_ERROR_MEMORY;
  }
#ifdef C_ORM_TEST_ALLOCATOR
  if (c_orm_mock_cache_init_fail) {
    (void)c_orm_system_free(*out_lock);
    *out_lock = NULL;
    return C_ORM_ERROR_BUSY;
  }
#endif
  InitializeCriticalSection((c_orm_win32_critical_section_t *)(*out_lock));
  return C_ORM_OK;
}

/**
 * @brief Acquires a Windows CRITICAL_SECTION mutex.
 * @param lock Mutex pointer.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t c_orm_cache_mutex_lock(void *lock) {
#ifdef C_ORM_TEST_ALLOCATOR
  if (c_orm_mock_cache_lock_fail) {
    return C_ORM_ERROR_BUSY;
  }
#endif
  EnterCriticalSection((c_orm_win32_critical_section_t *)lock);
  return C_ORM_OK;
}

/**
 * @brief Releases a Windows CRITICAL_SECTION mutex.
 * @param lock Mutex pointer.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t c_orm_cache_mutex_unlock(void *lock) {
  LeaveCriticalSection((c_orm_win32_critical_section_t *)lock);
#ifdef C_ORM_TEST_ALLOCATOR
  if (c_orm_mock_cache_unlock_fail) {
    return C_ORM_ERROR_BUSY;
  }
#endif
  return C_ORM_OK;
}

/**
 * @brief Destroys and frees a Windows CRITICAL_SECTION mutex.
 * @param lock Mutex pointer.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t c_orm_cache_mutex_destroy(void *lock) {
  c_orm_error_t frc;
  if (!lock) {
    return C_ORM_OK;
  }
  DeleteCriticalSection((c_orm_win32_critical_section_t *)lock);
  frc = c_orm_system_free(lock);
#ifdef C_ORM_TEST_ALLOCATOR
  if (c_orm_mock_cache_destroy_fail) {
    return C_ORM_ERROR_BUSY;
  }
#endif
  return frc;
}
#else
/**
 * @brief Callback signature for POSIX mutex initialization returning
 * c_orm_error_t.
 * @param mutex Mutex pointer to initialize.
 * @param attr Optional mutex attributes pointer.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BUSY on failure.
 */
typedef c_orm_error_t (*c_orm_mutex_init_fn_t)(pthread_mutex_t *mutex,
                                               const pthread_mutexattr_t *attr);

/**
 * @brief Callback signature for POSIX mutex lock/unlock/destroy returning
 * c_orm_error_t.
 * @param mutex Mutex pointer.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BUSY on failure.
 */
typedef c_orm_error_t (*c_orm_mutex_op_fn_t)(pthread_mutex_t *mutex);

/**
 * @brief Default POSIX pthread_mutex_init wrapper returning c_orm_error_t.
 * @param mutex Mutex pointer to initialize.
 * @param attr Optional mutex attributes pointer.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BUSY on failure.
 */
static c_orm_error_t
c_orm_default_pthread_init(pthread_mutex_t *mutex,
                           const pthread_mutexattr_t *attr) {
  return pthread_mutex_init(mutex, attr) == 0 ? C_ORM_OK : C_ORM_ERROR_BUSY;
}

/**
 * @brief Default POSIX pthread_mutex_lock wrapper returning c_orm_error_t.
 * @param mutex Mutex pointer.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BUSY on failure.
 */
static c_orm_error_t c_orm_default_pthread_lock(pthread_mutex_t *mutex) {
  return pthread_mutex_lock(mutex) == 0 ? C_ORM_OK : C_ORM_ERROR_BUSY;
}

/**
 * @brief Default POSIX pthread_mutex_unlock wrapper returning c_orm_error_t.
 * @param mutex Mutex pointer.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BUSY on failure.
 */
static c_orm_error_t c_orm_default_pthread_unlock(pthread_mutex_t *mutex) {
  return pthread_mutex_unlock(mutex) == 0 ? C_ORM_OK : C_ORM_ERROR_BUSY;
}

/**
 * @brief Default POSIX pthread_mutex_destroy wrapper returning c_orm_error_t.
 * @param mutex Mutex pointer.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BUSY on failure.
 */
static c_orm_error_t c_orm_default_pthread_destroy(pthread_mutex_t *mutex) {
  return pthread_mutex_destroy(mutex) == 0 ? C_ORM_OK : C_ORM_ERROR_BUSY;
}

#ifdef C_ORM_TEST_ALLOCATOR
/** @brief Test hook for mutex initialization. */
C_ORM_EXPORT c_orm_mutex_init_fn_t c_orm_mutex_init_ptr =
    c_orm_default_pthread_init;
/** @brief Test hook for mutex locking. */
C_ORM_EXPORT c_orm_mutex_op_fn_t c_orm_mutex_lock_ptr =
    c_orm_default_pthread_lock;
/** @brief Test hook for mutex unlocking. */
C_ORM_EXPORT c_orm_mutex_op_fn_t c_orm_mutex_unlock_ptr =
    c_orm_default_pthread_unlock;
/** @brief Test hook for mutex destruction. */
C_ORM_EXPORT c_orm_mutex_op_fn_t c_orm_mutex_destroy_ptr =
    c_orm_default_pthread_destroy;
#endif

/**
 * @brief Allocates and initializes a POSIX pthread mutex.
 * @param out_lock Output pointer receiving allocated pthread_mutex_t.
 * @return C_ORM_OK on success, C_ORM_ERROR_MEMORY on OOM, or C_ORM_ERROR_BUSY
 * on pthread failure.
 */
static c_orm_error_t c_orm_cache_mutex_init(void **out_lock) {
  c_orm_error_t rc;
  rc = c_orm_system_malloc(sizeof(pthread_mutex_t), out_lock);
  if (rc != C_ORM_OK) {
    return C_ORM_ERROR_MEMORY;
  }
#ifdef C_ORM_TEST_ALLOCATOR
  if (c_orm_mock_cache_init_fail) {
    (void)c_orm_system_free(*out_lock);
    *out_lock = NULL;
    return C_ORM_ERROR_BUSY;
  }
  rc = c_orm_mutex_init_ptr((pthread_mutex_t *)(*out_lock), NULL);
#else
  rc = c_orm_default_pthread_init((pthread_mutex_t *)(*out_lock), NULL);
#endif
  if (rc != C_ORM_OK) {
    (void)c_orm_system_free(*out_lock);
    *out_lock = NULL;
    return rc;
  }
  return C_ORM_OK;
}

/**
 * @brief Acquires a POSIX pthread mutex.
 * @param lock Mutex pointer.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BUSY on failure.
 */
static c_orm_error_t c_orm_cache_mutex_lock(void *lock) {
#ifdef C_ORM_TEST_ALLOCATOR
  if (c_orm_mock_cache_lock_fail) {
    return C_ORM_ERROR_BUSY;
  }
  return c_orm_mutex_lock_ptr((pthread_mutex_t *)lock);
#else
  return c_orm_default_pthread_lock((pthread_mutex_t *)lock);
#endif
}

/**
 * @brief Releases a POSIX pthread mutex.
 * @param lock Mutex pointer.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BUSY on failure.
 */
static c_orm_error_t c_orm_cache_mutex_unlock(void *lock) {
#ifdef C_ORM_TEST_ALLOCATOR
  if (c_orm_mock_cache_unlock_fail) {
    c_orm_mutex_unlock_ptr((pthread_mutex_t *)lock);
    return C_ORM_ERROR_BUSY;
  }
  return c_orm_mutex_unlock_ptr((pthread_mutex_t *)lock);
#else
  return c_orm_default_pthread_unlock((pthread_mutex_t *)lock);
#endif
}

/**
 * @brief Destroys and frees a POSIX pthread mutex.
 * @param lock Mutex pointer.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_cache_mutex_destroy(void *lock) {
  c_orm_error_t rc;
  c_orm_error_t frc;
#ifdef C_ORM_TEST_ALLOCATOR
  rc = c_orm_mutex_destroy_ptr((pthread_mutex_t *)lock);
  if (c_orm_mock_cache_destroy_fail) {
    rc = C_ORM_ERROR_BUSY;
  }
#else
  rc = c_orm_default_pthread_destroy((pthread_mutex_t *)lock);
#endif
  frc = c_orm_system_free(lock);
  return rc != C_ORM_OK ? rc : frc;
}
#endif

/**
 * @brief Allocates and initializes a cross-platform synchronization mutex.
 * @param out_lock Output pointer receiving the allocated mutex handle.
 * @return C_ORM_OK on success, C_ORM_ERROR_MEMORY on OOM, or C_ORM_ERROR_BUSY
 * on OS primitive initialization failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_mutex_create(void **out_lock) {
  return c_orm_cache_mutex_init(out_lock);
}

/**
 * @brief Acquires a cross-platform synchronization mutex.
 * @param lock Mutex handle previously created with c_orm_mutex_create.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BUSY on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_mutex_lock(void *lock) {
  return c_orm_cache_mutex_lock(lock);
}

/**
 * @brief Releases a cross-platform synchronization mutex.
 * @param lock Mutex handle previously created with c_orm_mutex_create.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BUSY on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_mutex_unlock(void *lock) {
  return c_orm_cache_mutex_unlock(lock);
}

/**
 * @brief Destroys and frees a cross-platform synchronization mutex.
 * @param lock Mutex handle previously created with c_orm_mutex_create.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BUSY on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_mutex_destroy(void *lock) {
  return c_orm_cache_mutex_destroy(lock);
}

/**
 * @brief Structure representing a cached statement entry.
 */
typedef struct c_orm_stmt_entry {
  char *sql;            /**< @brief The SQL string associated with this query */
  c_orm_query_t *query; /**< @brief The prepared query object */
  struct c_orm_stmt_entry
      *next; /**< @brief Pointer to the next entry in the LRU list */
  struct c_orm_stmt_entry
      *prev;  /**< @brief Pointer to the previous entry in the LRU list */
  int in_use; /**< @brief Flag indicating if the entry is currently in use */
} c_orm_stmt_entry_t;

/**
 * @brief Structure representing the statement cache.
 */
typedef struct {
  size_t capacity; /**< @brief Maximum number of entries allowed in the cache */
  size_t count;    /**< @brief Current number of entries in the cache */
  c_orm_stmt_entry_t
      *head; /**< @brief Pointer to the most recently used entry */
  c_orm_stmt_entry_t
      *tail;  /**< @brief Pointer to the least recently used entry */
  void *lock; /**< @brief Mutex lock for thread safety */
} c_orm_stmt_cache_t;

/**
 * @brief Enables statement caching for the specified database connection.
 *
 * @param db The database connection.
 * @param cache_size The maximum number of statements to cache.
 * @return c_orm_error_t result code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_enable_statement_caching(c_orm_db_t *db,
                                                          size_t cache_size) {
  c_orm_stmt_cache_t *cache = NULL;
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_enable_statement_caching: entry");

  if (!db) {
    LOG_DEBUG("c_orm_enable_statement_caching: invalid arguments");
    return C_ORM_ERROR_MEMORY;
  }
  if (db->stmt_cache) {
    LOG_DEBUG("c_orm_enable_statement_caching: already enabled");
    return C_ORM_OK;
  }
  if (cache_size == 0) {
    cache_size = 128;
  }

  rc = c_orm_system_malloc(sizeof(c_orm_stmt_cache_t), (void **)&cache);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_enable_statement_caching: OOM for cache");
    return C_ORM_ERROR_MEMORY;
  }

  cache->capacity = cache_size;
  cache->count = 0;
  cache->head = NULL;
  cache->tail = NULL;
  cache->lock = NULL;

  rc = c_orm_cache_mutex_init(&cache->lock);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_enable_statement_caching: mutex init failed");
    (void)c_orm_system_free(cache);
    return rc;
  }

  db->stmt_cache = cache;
  LOG_DEBUG("c_orm_enable_statement_caching: exit");
  return C_ORM_OK;
}

/**
 * @brief Disables statement caching for the specified database connection.
 *
 * @param db The database connection.
 * @return c_orm_error_t result code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_disable_statement_caching(c_orm_db_t *db) {
  c_orm_stmt_cache_t *cache;
  c_orm_stmt_entry_t *entry, *next;
  c_orm_error_t rc;
  c_orm_error_t unlock_rc;
  c_orm_error_t destroy_rc;
  c_orm_error_t free_rc;

  LOG_DEBUG("c_orm_disable_statement_caching: entry");

  if (!db || !db->stmt_cache) {
    LOG_DEBUG("c_orm_disable_statement_caching: not enabled or invalid args");
    return C_ORM_OK;
  }
  cache = (c_orm_stmt_cache_t *)db->stmt_cache;

  rc = c_orm_cache_mutex_lock(cache->lock);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_disable_statement_caching: mutex lock failed");
    return rc;
  }
  entry = cache->head;
  while (entry) {
    next = entry->next;
    rc = db->vtable->finalize(entry->query);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_disable_statement_caching: finalize failed");
      cache->head = entry;
      unlock_rc = c_orm_cache_mutex_unlock(cache->lock);
      if (unlock_rc != C_ORM_OK) {
        return unlock_rc;
      }
      return rc;
    }
    (void)c_orm_system_free(entry->sql);
    (void)c_orm_system_free(entry);
    entry = next;
  }
  unlock_rc = c_orm_cache_mutex_unlock(cache->lock);
  destroy_rc = c_orm_cache_mutex_destroy(cache->lock);
  free_rc = c_orm_system_free(cache);
  db->stmt_cache = NULL;
  if (unlock_rc != C_ORM_OK) {
    return unlock_rc;
  }
  if (destroy_rc != C_ORM_OK) {
    return destroy_rc;
  }
  LOG_DEBUG("c_orm_disable_statement_caching: exit");
  return free_rc;
}

/**
 * @brief Prepares a statement, utilizing the cache if enabled.
 *
 * @param db The database connection.
 * @param sql The SQL statement to prepare.
 * @param out_query Pointer to store the prepared query object.
 * @return c_orm_error_t result code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_prepare_cached(c_orm_db_t *db, const char *sql,
                                                c_orm_query_t **out_query) {
  c_orm_stmt_cache_t *cache;
  c_orm_stmt_entry_t *entry = NULL;
  c_orm_stmt_entry_t *evict;
  c_orm_error_t err;
  c_orm_error_t rc;
  c_orm_error_t unlock_rc;

  LOG_DEBUG("c_orm_prepare_cached: entry");

  if (!db || !sql || !out_query) {
    LOG_DEBUG("c_orm_prepare_cached: invalid arguments");
    return C_ORM_ERROR_MEMORY;
  }

  if (!db->stmt_cache) {
    rc = db->vtable->prepare(db, sql, out_query);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_prepare_cached: direct prepare failed");
      return rc;
    }
    LOG_DEBUG("c_orm_prepare_cached: cache not enabled, direct prepare exit");
    return rc;
  }

  cache = (c_orm_stmt_cache_t *)db->stmt_cache;

  rc = c_orm_cache_mutex_lock(cache->lock);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_prepare_cached: mutex lock failed");
    return rc;
  }
  entry = cache->head;
  while (entry) {
    if (!entry->in_use && strcmp(entry->sql, sql) == 0) {
      /* Found! Mark in use and move to front of LRU */
      entry->in_use = 1;

      if (entry != cache->head) {
        /* Unlink */
        entry->prev->next = entry->next;
        if (entry->next) {
          entry->next->prev = entry->prev;
        }
        if (entry == cache->tail) {
          cache->tail = entry->prev;
        }

        /* Re-insert at head */
        entry->next = cache->head;
        entry->prev = NULL;
        cache->head->prev = entry;
        cache->head = entry;
      }

      *out_query = entry->query;
      unlock_rc = c_orm_cache_mutex_unlock(cache->lock);
      if (unlock_rc != C_ORM_OK) {
        return unlock_rc;
      }
      rc = db->vtable->reset(*out_query);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_prepare_cached: reset failed");
        return rc;
      }
      LOG_DEBUG("c_orm_prepare_cached: found in cache exit");
      return C_ORM_OK;
    }
    entry = entry->next;
  }

  /* Not found or all in use. Prepare new query. */
  err = db->vtable->prepare(db, sql, out_query);
  if (err != C_ORM_OK) {
    unlock_rc = c_orm_cache_mutex_unlock(cache->lock);
    LOG_DEBUG("c_orm_prepare_cached: prepare failed exit");
    if (unlock_rc != C_ORM_OK) {
      return unlock_rc;
    }
    return err;
  }

  /* Create cache entry */
  entry = NULL;
  if (c_orm_system_malloc(sizeof(c_orm_stmt_entry_t), (void **)&entry) !=
      C_ORM_OK) {
    /* Cache allocation failed, just return query to caller normally */
    LOG_DEBUG("c_orm_prepare_cached: OOM for cache entry");
    return c_orm_cache_mutex_unlock(cache->lock);
  }

  entry->sql = NULL;
  if (c_orm_system_malloc(strlen(sql) + 1, (void **)&entry->sql) != C_ORM_OK) {
    LOG_DEBUG("c_orm_prepare_cached: OOM for cache entry sql");
    (void)c_orm_system_free(entry);
    return c_orm_cache_mutex_unlock(cache->lock);
  }
  C_ORM_STRCPY(entry->sql, strlen(sql) + 1, sql);

  entry->query = *out_query;
  entry->in_use = 1;

  /* Insert at head */
  entry->next = cache->head;
  entry->prev = NULL;
  if (cache->head) {
    cache->head->prev = entry;
  } else {
    cache->tail = entry;
  }
  cache->head = entry;
  cache->count++;

  /* Evict if capacity exceeded. Only evict items NOT in use! */
  if (cache->count > cache->capacity) {
    evict = cache->tail;
    while (evict) {
      if (!evict->in_use) {
        evict->prev->next = evict->next;
        if (evict->next) {
          evict->next->prev = evict->prev;
        }
        if (evict == cache->tail) {
          cache->tail = evict->prev;
        }

        rc = db->vtable->finalize(evict->query);
        (void)c_orm_system_free(evict->sql);
        (void)c_orm_system_free(evict);
        cache->count--;
        if (rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_prepare_cached: finalize failed during eviction");
          unlock_rc = c_orm_cache_mutex_unlock(cache->lock);
          if (unlock_rc != C_ORM_OK) {
            return unlock_rc;
          }
          return rc;
        }
        break;
      }
      evict = evict->prev;
    }
  }

  rc = c_orm_cache_mutex_unlock(cache->lock);
  LOG_DEBUG("c_orm_prepare_cached: added to cache exit");
  return rc;
}

#ifdef C_ORM_TEST_ALLOCATOR
/**
 * @brief Flag to simulate cached finalize failure.
 */
C_ORM_EXPORT int c_orm_mock_finalize_cached_fail = 0;

/**
 * @brief Countdown counter to simulate cached finalize failure.
 */
C_ORM_EXPORT int c_orm_mock_finalize_cached_countdown = -1;
#endif /* C_ORM_TEST_ALLOCATOR */

/**
 * @brief Finalizes a cached statement, releasing it back to the cache pool.
 *
 * @param db The database connection.
 * @param query The prepared query object to finalize.
 * @return c_orm_error_t result code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_finalize_cached(c_orm_db_t *db,
                                                 c_orm_query_t *query) {
  c_orm_stmt_cache_t *cache;
  c_orm_stmt_entry_t *entry;
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_finalize_cached: entry");

  if (!db || !query) {
    LOG_DEBUG("c_orm_finalize_cached: invalid arguments");
    return C_ORM_ERROR_MEMORY;
  }

#ifdef C_ORM_TEST_ALLOCATOR
  if (c_orm_mock_finalize_cached_fail) {
    LOG_DEBUG("c_orm_finalize_cached: mock failure");
    return C_ORM_ERROR_UNKNOWN;
  }

  if (c_orm_mock_finalize_cached_countdown >= 0) {
    if (c_orm_mock_finalize_cached_countdown == 0) {
      c_orm_mock_finalize_cached_countdown = -1;
      LOG_DEBUG("c_orm_finalize_cached: mock countdown failure");
      return C_ORM_ERROR_UNKNOWN;
    }
    c_orm_mock_finalize_cached_countdown--;
  }
#endif

  if (!db->stmt_cache) {
    rc = db->vtable->finalize(query);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_finalize_cached: direct finalize failed");
      return rc;
    }
    LOG_DEBUG("c_orm_finalize_cached: cache not enabled, direct finalize exit");
    return rc;
  }

  cache = (c_orm_stmt_cache_t *)db->stmt_cache;

  rc = c_orm_cache_mutex_lock(cache->lock);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_finalize_cached: mutex lock failed");
    return rc;
  }
  entry = cache->head;
  while (entry) {
    if (entry->query == query) {
      entry->in_use = 0; /* Release back to pool */
      rc = c_orm_cache_mutex_unlock(cache->lock);
      LOG_DEBUG("c_orm_finalize_cached: released to cache exit");
      return rc;
    }
    entry = entry->next;
  }
  rc = c_orm_cache_mutex_unlock(cache->lock);
  if (rc != C_ORM_OK) {
    return rc;
  }

  /* Query not managed by cache, finalize normally */
  rc = db->vtable->finalize(query);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_finalize_cached: normal finalize failed");
    return rc;
  }
  LOG_DEBUG("c_orm_finalize_cached: direct finalize exit");
  return rc;
}
