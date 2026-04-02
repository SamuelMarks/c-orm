/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_db.h"
#include <stdlib.h>
#include <string.h>
/* clang-format on */

#if defined(_WIN32) || defined(_WIN64)
#ifndef _CRITICAL_SECTION_DEFINED
#define _CRITICAL_SECTION_DEFINED
struct _RTL_CRITICAL_SECTION;
typedef struct _RTL_CRITICAL_SECTION CRITICAL_SECTION;
#endif
__declspec(dllimport) void __stdcall InitializeCriticalSection(
    CRITICAL_SECTION *);
__declspec(dllimport) void __stdcall EnterCriticalSection(CRITICAL_SECTION *);
__declspec(dllimport) void __stdcall LeaveCriticalSection(CRITICAL_SECTION *);
__declspec(dllimport) void __stdcall DeleteCriticalSection(CRITICAL_SECTION *);

#define C_ORM_MUTEX_INIT(m)                                                    \
  do {                                                                         \
    (m) = malloc(64);                                                          \
    InitializeCriticalSection((CRITICAL_SECTION *)(m));                        \
  } while (0)
#define C_ORM_MUTEX_LOCK(m) EnterCriticalSection((CRITICAL_SECTION *)(m))
#define C_ORM_MUTEX_UNLOCK(m) LeaveCriticalSection((CRITICAL_SECTION *)(m))
#define C_ORM_MUTEX_DESTROY(m)                                                 \
  do {                                                                         \
    DeleteCriticalSection((CRITICAL_SECTION *)(m));                            \
    free((m));                                                                 \
  } while (0)
#else
#include <pthread.h>
#define C_ORM_MUTEX_INIT(m)                                                    \
  do {                                                                         \
    (m) = malloc(sizeof(pthread_mutex_t));                                     \
    pthread_mutex_init((pthread_mutex_t *)(m), NULL);                          \
  } while (0)
#define C_ORM_MUTEX_LOCK(m) pthread_mutex_lock((pthread_mutex_t *)(m))
#define C_ORM_MUTEX_UNLOCK(m) pthread_mutex_unlock((pthread_mutex_t *)(m))
#define C_ORM_MUTEX_DESTROY(m)                                                 \
  do {                                                                         \
    pthread_mutex_destroy((pthread_mutex_t *)(m));                             \
    free((m));                                                                 \
  } while (0)
#endif

typedef struct c_orm_stmt_entry {
  char *sql;
  c_orm_query_t *query;
  struct c_orm_stmt_entry *next;
  struct c_orm_stmt_entry *prev;
  int in_use;
} c_orm_stmt_entry_t;

typedef struct {
  size_t capacity;
  size_t count;
  c_orm_stmt_entry_t *head;
  c_orm_stmt_entry_t *tail;
  void *lock;
} c_orm_stmt_cache_t;

C_ORM_EXPORT c_orm_error_t c_orm_enable_statement_caching(c_orm_db_t *db,
                                                          size_t cache_size) {
  c_orm_stmt_cache_t *cache;

  if (!db)
    return C_ORM_ERROR_MEMORY;
  if (db->stmt_cache)
    return C_ORM_OK; /* Already enabled */
  if (cache_size == 0)
    cache_size = 128;

  cache = (c_orm_stmt_cache_t *)malloc(sizeof(c_orm_stmt_cache_t));
  if (!cache)
    return C_ORM_ERROR_MEMORY;

  cache->capacity = cache_size;
  cache->count = 0;
  cache->head = NULL;
  cache->tail = NULL;

  C_ORM_MUTEX_INIT(cache->lock);
  db->stmt_cache = cache;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_disable_statement_caching(c_orm_db_t *db) {
  c_orm_stmt_cache_t *cache;
  c_orm_stmt_entry_t *entry, *next;

  if (!db || !db->stmt_cache)
    return C_ORM_OK;
  cache = (c_orm_stmt_cache_t *)db->stmt_cache;

  C_ORM_MUTEX_LOCK(cache->lock);
  entry = cache->head;
  while (entry) {
    next = entry->next;
    db->vtable->finalize(entry->query);
    free(entry->sql);
    free(entry);
    entry = next;
  }
  C_ORM_MUTEX_UNLOCK(cache->lock);
  C_ORM_MUTEX_DESTROY(cache->lock);
  free(cache);
  db->stmt_cache = NULL;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_prepare_cached(c_orm_db_t *db, const char *sql,
                                                c_orm_query_t **out_query) {
  c_orm_stmt_cache_t *cache;
  c_orm_stmt_entry_t *entry;
  c_orm_error_t err;

  if (!db || !sql || !out_query)
    return C_ORM_ERROR_MEMORY;

  if (!db->stmt_cache) {
    return db->vtable->prepare(db, sql, out_query);
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
        if (entry->prev)
          entry->prev->next = entry->next;
        if (entry->next)
          entry->next->prev = entry->prev;
        if (entry == cache->tail)
          cache->tail = entry->prev;

        /* Re-insert at head */
        entry->next = cache->head;
        entry->prev = NULL;
        if (cache->head)
          cache->head->prev = entry;
        cache->head = entry;
      }

      *out_query = entry->query;
      C_ORM_MUTEX_UNLOCK(cache->lock);
      db->vtable->reset(*out_query);
      return C_ORM_OK;
    }
    entry = entry->next;
  }

  /* Not found or all in use. Prepare new query. */
  err = db->vtable->prepare(db, sql, out_query);
  if (err != C_ORM_OK) {
    C_ORM_MUTEX_UNLOCK(cache->lock);
    return err;
  }

  /* Create cache entry */
  entry = (c_orm_stmt_entry_t *)malloc(sizeof(c_orm_stmt_entry_t));
  if (!entry) {
    /* Cache allocation failed, just return query to caller normally */
    C_ORM_MUTEX_UNLOCK(cache->lock);
    return C_ORM_OK;
  }

  entry->sql = (char *)malloc(strlen(sql) + 1);
  if (!entry->sql) {
    free(entry);
    C_ORM_MUTEX_UNLOCK(cache->lock);
    return C_ORM_OK;
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
    c_orm_stmt_entry_t *evict = cache->tail;
    while (evict) {
      if (!evict->in_use) {
        if (evict->prev)
          evict->prev->next = evict->next;
        if (evict->next)
          evict->next->prev = evict->prev;
        if (evict == cache->head)
          cache->head = evict->next;
        if (evict == cache->tail)
          cache->tail = evict->prev;

        db->vtable->finalize(evict->query);
        free(evict->sql);
        free(evict);
        cache->count--;
        break;
      }
      evict = evict->prev;
    }
  }

  C_ORM_MUTEX_UNLOCK(cache->lock);
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_finalize_cached(c_orm_db_t *db,
                                                 c_orm_query_t *query) {
  c_orm_stmt_cache_t *cache;
  c_orm_stmt_entry_t *entry;

  if (!db || !query)
    return C_ORM_ERROR_MEMORY;

  if (!db->stmt_cache) {
    return db->vtable->finalize(query);
  }

  cache = (c_orm_stmt_cache_t *)db->stmt_cache;

  C_ORM_MUTEX_LOCK(cache->lock);
  entry = cache->head;
  while (entry) {
    if (entry->query == query) {
      entry->in_use = 0; /* Release back to pool */
      C_ORM_MUTEX_UNLOCK(cache->lock);
      return C_ORM_OK;
    }
    entry = entry->next;
  }
  C_ORM_MUTEX_UNLOCK(cache->lock);

  /* Query not managed by cache, finalize normally */
  return db->vtable->finalize(query);
}
