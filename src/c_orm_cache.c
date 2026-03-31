#include "c_orm_api.h"
#include "c_orm_db.h"
#include <stdlib.h>
#include <string.h>

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
  struct c_orm_stmt_entry *hash_next;
} c_orm_stmt_entry_t;

typedef struct {
  size_t capacity;
  size_t size;
  c_orm_stmt_entry_t *head;
  c_orm_stmt_entry_t *tail;
  c_orm_stmt_entry_t **hash_table;
  size_t hash_size;
  void *lock;
} c_orm_stmt_cache_t;

#if 0
static size_t hash_sql(const char *sql) {
  size_t hash = 5381;
  int c;
  while ((c = *sql++)) {
    hash = ((hash << 5) + hash) + c;
  }
  return hash;
}
#endif

C_ORM_EXPORT c_orm_error_t c_orm_enable_statement_caching(c_orm_db_t *db,
                                                          size_t cache_size) {
  c_orm_stmt_cache_t *cache;
  size_t i;
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
  cache->size = 0;
  cache->head = NULL;
  cache->tail = NULL;
  cache->hash_size = cache_size * 2;
  cache->hash_table = (c_orm_stmt_entry_t **)malloc(
      cache->hash_size * sizeof(c_orm_stmt_entry_t *));
  if (!cache->hash_table) {
    free(cache);
    return C_ORM_ERROR_MEMORY;
  }
  for (i = 0; i < cache->hash_size; i++) {
    cache->hash_table[i] = NULL;
  }
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
  free(cache->hash_table);
  C_ORM_MUTEX_UNLOCK(cache->lock);
  C_ORM_MUTEX_DESTROY(cache->lock);
  free(cache);
  db->stmt_cache = NULL;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_prepare_cached(c_orm_db_t *db, const char *sql,
                                                c_orm_query_t **out_query) {
  if (!db || !sql || !out_query)
    return C_ORM_ERROR_MEMORY;

  return db->vtable->prepare(db, sql, out_query);
}

C_ORM_EXPORT c_orm_error_t c_orm_finalize_cached(c_orm_db_t *db,
                                                 c_orm_query_t *query) {
  if (!db)
    return C_ORM_ERROR_MEMORY;
  return db->vtable->finalize(query);
}
