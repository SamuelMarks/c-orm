/**
 * @file c_orm_cache.c
 * @brief Implementation of statement caching for c-orm.
 */

/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_db.h"
#include "c_orm_log.h"
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32) && !defined(_WIN64)
#include <pthread.h>
#endif
/* clang-format on */

#if defined(_WIN32) || defined(_WIN64)
#ifndef _CRITICAL_SECTION_DEFINED
#define _CRITICAL_SECTION_DEFINED
/** @brief Opaque critical section struct */
struct _RTL_CRITICAL_SECTION;
/** @brief Type definition for Windows CRITICAL_SECTION */
typedef struct _RTL_CRITICAL_SECTION CRITICAL_SECTION;
#endif
__declspec(dllimport) void __stdcall InitializeCriticalSection(
    CRITICAL_SECTION *);
__declspec(dllimport) void __stdcall EnterCriticalSection(CRITICAL_SECTION *);
__declspec(dllimport) void __stdcall LeaveCriticalSection(CRITICAL_SECTION *);
__declspec(dllimport) void __stdcall DeleteCriticalSection(CRITICAL_SECTION *);

/** @brief Macro to initialize a mutex on Windows */
#define C_ORM_MUTEX_INIT(m)                                                    \
  do {                                                                         \
    (m) = C_ORM_MALLOC(64);                                                    \
    if (m) {                                                                   \
      InitializeCriticalSection((CRITICAL_SECTION *)(m));                      \
    }                                                                          \
  } while (0)
/** @brief Macro to lock a mutex on Windows */
#define C_ORM_MUTEX_LOCK(m) EnterCriticalSection((CRITICAL_SECTION *)(m))
/** @brief Macro to unlock a mutex on Windows */
#define C_ORM_MUTEX_UNLOCK(m) LeaveCriticalSection((CRITICAL_SECTION *)(m))
/** @brief Macro to destroy a mutex on Windows */
#define C_ORM_MUTEX_DESTROY(m)                                                 \
  do {                                                                         \
    DeleteCriticalSection((CRITICAL_SECTION *)(m));                            \
    C_ORM_FREE((m));                                                           \
  } while (0)
#else
#ifdef C_ORM_TEST_ALLOCATOR
int (*c_orm_mutex_init_ptr)(pthread_mutex_t *,
                            const pthread_mutexattr_t *) = pthread_mutex_init;
int (*c_orm_mutex_lock_ptr)(pthread_mutex_t *) = pthread_mutex_lock;
int (*c_orm_mutex_unlock_ptr)(pthread_mutex_t *) = pthread_mutex_unlock;
int (*c_orm_mutex_destroy_ptr)(pthread_mutex_t *) = pthread_mutex_destroy;
#define C_ORM_MUTEX_INIT_IMPL(m)                                               \
  c_orm_mutex_init_ptr((pthread_mutex_t *)(m), NULL)
#define C_ORM_MUTEX_LOCK_IMPL(m) c_orm_mutex_lock_ptr((pthread_mutex_t *)(m))
#define C_ORM_MUTEX_UNLOCK_IMPL(m)                                             \
  c_orm_mutex_unlock_ptr((pthread_mutex_t *)(m))
#define C_ORM_MUTEX_DESTROY_IMPL(m)                                            \
  c_orm_mutex_destroy_ptr((pthread_mutex_t *)(m))
#else
#define C_ORM_MUTEX_INIT_IMPL(m)                                               \
  pthread_mutex_init((pthread_mutex_t *)(m), NULL)
#define C_ORM_MUTEX_LOCK_IMPL(m) pthread_mutex_lock((pthread_mutex_t *)(m))
#define C_ORM_MUTEX_UNLOCK_IMPL(m) pthread_mutex_unlock((pthread_mutex_t *)(m))
#define C_ORM_MUTEX_DESTROY_IMPL(m)                                            \
  pthread_mutex_destroy((pthread_mutex_t *)(m))
#endif

/** @brief Macro to initialize a mutex on POSIX */
#define C_ORM_MUTEX_INIT(m)                                                    \
  do {                                                                         \
    (m) = C_ORM_MALLOC(sizeof(pthread_mutex_t));                               \
    if (m) {                                                                   \
      C_ORM_MUTEX_INIT_IMPL(m);                                                \
    }                                                                          \
  } while (0)
/** @brief Macro to lock a mutex on POSIX */
#define C_ORM_MUTEX_LOCK(m) C_ORM_MUTEX_LOCK_IMPL(m)
/** @brief Macro to unlock a mutex on POSIX */
#define C_ORM_MUTEX_UNLOCK(m) C_ORM_MUTEX_UNLOCK_IMPL(m)
/** @brief Macro to destroy a mutex on POSIX */
#define C_ORM_MUTEX_DESTROY(m)                                                 \
  do {                                                                         \
    C_ORM_MUTEX_DESTROY_IMPL(m);                                               \
    C_ORM_FREE((m));                                                           \
  } while (0)
#endif

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
  c_orm_stmt_cache_t *cache;
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_enable_statement_caching: entry");

  if (!db) {
    LOG_DEBUG("c_orm_enable_statement_caching: invalid arguments");
    rc = C_ORM_ERROR_MEMORY;
    return rc;
  }
  if (db->stmt_cache) {
    LOG_DEBUG("c_orm_enable_statement_caching: already enabled");
    rc = C_ORM_OK;
    return rc;
  }
  if (cache_size == 0) {
    cache_size = 128;
  }

  cache = (c_orm_stmt_cache_t *)C_ORM_MALLOC(sizeof(c_orm_stmt_cache_t));
  if (!cache) {
    LOG_DEBUG("c_orm_enable_statement_caching: OOM for cache");
    rc = C_ORM_ERROR_MEMORY;
    return rc;
  }

  cache->capacity = cache_size;
  cache->count = 0;
  cache->head = NULL;
  cache->tail = NULL;

  C_ORM_MUTEX_INIT(cache->lock);
  if (!cache->lock) {
    LOG_DEBUG("c_orm_enable_statement_caching: OOM for lock");
    C_ORM_FREE(cache);
    rc = C_ORM_ERROR_MEMORY;
    return rc;
  }

  db->stmt_cache = cache;
  rc = C_ORM_OK;
  LOG_DEBUG("c_orm_enable_statement_caching: exit");
  return rc;
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

  LOG_DEBUG("c_orm_disable_statement_caching: entry");

  if (!db || !db->stmt_cache) {
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_disable_statement_caching: not enabled or invalid args");
    return rc;
  }
  cache = (c_orm_stmt_cache_t *)db->stmt_cache;

  C_ORM_MUTEX_LOCK(cache->lock);
  entry = cache->head;
  while (entry) {
    next = entry->next;
    db->vtable->finalize(entry->query);
    C_ORM_FREE(entry->sql);
    C_ORM_FREE(entry);
    entry = next;
  }
  C_ORM_MUTEX_UNLOCK(cache->lock);
  C_ORM_MUTEX_DESTROY(cache->lock);
  C_ORM_FREE(cache);
  db->stmt_cache = NULL;
  rc = C_ORM_OK;
  LOG_DEBUG("c_orm_disable_statement_caching: exit");
  return rc;
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
  c_orm_stmt_entry_t *entry;
  c_orm_stmt_entry_t *evict;
  c_orm_error_t err;
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_prepare_cached: entry");

  if (!db || !sql || !out_query) {
    LOG_DEBUG("c_orm_prepare_cached: invalid arguments");
    rc = C_ORM_ERROR_MEMORY;
    return rc;
  }

  if (!db->stmt_cache) {
    rc = db->vtable->prepare(db, sql, out_query);
    LOG_DEBUG("c_orm_prepare_cached: cache not enabled, direct prepare exit");
    return rc;
  }

  cache = (c_orm_stmt_cache_t *)db->stmt_cache;

  C_ORM_MUTEX_LOCK(cache->lock);
  entry = cache->head;
  while (entry) {
    if (!entry->in_use && strcmp(entry->sql, sql) == 0) {
      /* Found! Mark in use and move to front of LRU */
      entry->in_use = 1;

      if (entry != cache->head) {
        /* Unlink */
        if (entry->prev) {
          entry->prev->next = entry->next;
        }
        if (entry->next) {
          entry->next->prev = entry->prev;
        }
        if (entry == cache->tail) {
          cache->tail = entry->prev;
        }

        /* Re-insert at head */
        entry->next = cache->head;
        entry->prev = NULL;
        if (cache->head) {
          cache->head->prev = entry;
        }
        cache->head = entry;
      }

      *out_query = entry->query;
      C_ORM_MUTEX_UNLOCK(cache->lock);
      rc = db->vtable->reset(*out_query);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_prepare_cached: reset failed");
        return rc;
      }
      rc = C_ORM_OK;
      LOG_DEBUG("c_orm_prepare_cached: found in cache exit");
      return rc;
    }
    entry = entry->next;
  }

  /* Not found or all in use. Prepare new query. */
  err = db->vtable->prepare(db, sql, out_query);
  if (err != C_ORM_OK) {
    C_ORM_MUTEX_UNLOCK(cache->lock);
    rc = err;
    LOG_DEBUG("c_orm_prepare_cached: prepare failed exit");
    return rc;
  }

  /* Create cache entry */
  entry = (c_orm_stmt_entry_t *)C_ORM_MALLOC(sizeof(c_orm_stmt_entry_t));
  if (!entry) {
    /* Cache allocation failed, just return query to caller normally */
    LOG_DEBUG("c_orm_prepare_cached: OOM for cache entry");
    C_ORM_MUTEX_UNLOCK(cache->lock);
    rc = C_ORM_OK;
    return rc;
  }

  entry->sql = (char *)C_ORM_MALLOC(strlen(sql) + 1);
  if (!entry->sql) {
    LOG_DEBUG("c_orm_prepare_cached: OOM for cache entry sql");
    C_ORM_FREE(entry);
    C_ORM_MUTEX_UNLOCK(cache->lock);
    rc = C_ORM_OK;
    return rc;
  }
#if defined(_MSC_VER)
  strcpy_s(entry->sql, strlen(sql) + 1, sql);
#else
  strcpy(entry->sql, sql);
#endif

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
        if (evict->prev) {
          evict->prev->next = evict->next;
        }
        if (evict->next) {
          evict->next->prev = evict->prev;
        }
        if (evict == cache->head) {
          cache->head = evict->next;
        }
        if (evict == cache->tail) {
          cache->tail = evict->prev;
        }

        db->vtable->finalize(evict->query);
        C_ORM_FREE(evict->sql);
        C_ORM_FREE(evict);
        cache->count--;
        break;
      }
      evict = evict->prev;
    }
  }

  C_ORM_MUTEX_UNLOCK(cache->lock);
  rc = C_ORM_OK;
  LOG_DEBUG("c_orm_prepare_cached: added to cache exit");
  return rc;
}

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
    rc = C_ORM_ERROR_MEMORY;
    return rc;
  }

  if (!db->stmt_cache) {
    rc = db->vtable->finalize(query);
    LOG_DEBUG("c_orm_finalize_cached: cache not enabled, direct finalize exit");
    return rc;
  }

  cache = (c_orm_stmt_cache_t *)db->stmt_cache;

  C_ORM_MUTEX_LOCK(cache->lock);
  entry = cache->head;
  while (entry) {
    if (entry->query == query) {
      entry->in_use = 0; /* Release back to pool */
      C_ORM_MUTEX_UNLOCK(cache->lock);
      rc = C_ORM_OK;
      LOG_DEBUG("c_orm_finalize_cached: released to cache exit");
      return rc;
    }
    entry = entry->next;
  }
  C_ORM_MUTEX_UNLOCK(cache->lock);

  /* Query not managed by cache, finalize normally */
  rc = db->vtable->finalize(query);
  LOG_DEBUG("c_orm_finalize_cached: direct finalize exit");
  return rc;
}

