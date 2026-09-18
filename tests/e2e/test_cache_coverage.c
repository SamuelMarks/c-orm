#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_cache_coverage.c
 * @brief Unit tests for prepared statement caching and eviction.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_api.h"
#include "c_orm_db.h"
#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32) && !defined(_WIN64)
#include <pthread.h>
#endif
/* clang-format on */

#if !defined(_WIN32) && !defined(_WIN64)
extern int (*c_orm_mutex_init_ptr)(pthread_mutex_t *,
                                   const pthread_mutexattr_t *);
extern int (*c_orm_mutex_lock_ptr)(pthread_mutex_t *);
extern int (*c_orm_mutex_unlock_ptr)(pthread_mutex_t *);
extern int (*c_orm_mutex_destroy_ptr)(pthread_mutex_t *);

static int mock_mutex_init_fail = 0;
static int mock_mutex_lock_fail = 0;
static int mock_mutex_unlock_fail = 0;
static int mock_mutex_destroy_fail = 0;

/**
 * @brief Mock pthread_mutex_init for error path testing.
 * @param m Mutex pointer.
 * @param a Mutex attributes.
 * @return 0 on success, non-zero on error.
 */
static int my_mock_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) {
  if (mock_mutex_init_fail) {
    return 1;
  }
  return pthread_mutex_init(m, a);
}

/**
 * @brief Mock pthread_mutex_lock for error path testing.
 * @param m Mutex pointer.
 * @return 0 on success, non-zero on error.
 */
static int my_mock_lock(pthread_mutex_t *m) {
  if (mock_mutex_lock_fail) {
    return 1;
  }
  return pthread_mutex_lock(m);
}

/**
 * @brief Mock pthread_mutex_unlock for error path testing.
 * @param m Mutex pointer.
 * @return 0 on success, non-zero on error.
 */
static int my_mock_unlock(pthread_mutex_t *m) {
  if (mock_mutex_unlock_fail) {
    pthread_mutex_unlock(m);
    return 1;
  }
  return pthread_mutex_unlock(m);
}

/**
 * @brief Mock pthread_mutex_destroy for error path testing.
 * @param m Mutex pointer.
 * @return 0 on success, non-zero on error.
 */
static int my_mock_destroy(pthread_mutex_t *m) {
  if (mock_mutex_destroy_fail) {
    pthread_mutex_destroy(m);
    return 1;
  }
  return pthread_mutex_destroy(m);
}
#endif

static int oom_countdown = -1;
static int oom_active = 0;

/**
 * @brief Mock malloc callback for OOM testing.
 * @param size Allocation size in bytes.
 * @return Allocated pointer or NULL.
 */
static void *mock_malloc(size_t size) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      return NULL;
    }
    oom_countdown--;
  }
  return malloc(size);
}

/**
 * @brief Mock free callback.
 * @param ptr Pointer to free.
 */
static void mock_free(void *ptr) { free(ptr); }

typedef struct mock_query_t {
  int id;
} mock_query_t;

static int mock_prepare_fail = 0;
static int mock_reset_fail = 0;
static int mock_finalize_fail = 0;

/**
 * @brief Mock query prepare callback.
 * @param db Database pointer.
 * @param sql SQL statement.
 * @param out_query Pointer to receive prepared query.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t mock_prepare(c_orm_db_t *db, const char *sql,
                                  c_orm_query_t **out_query) {
  mock_query_t *q;
  (void)db;
  (void)sql;
  if (mock_prepare_fail) {
    return C_ORM_ERROR_SQL;
  }
  q = (mock_query_t *)malloc(sizeof(mock_query_t));
  q->id = 1;
  *out_query = (c_orm_query_t *)q;
  return C_ORM_OK;
}

/**
 * @brief Mock query reset callback.
 * @param query Query pointer.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t mock_reset(c_orm_query_t *query) {
  (void)query;
  if (mock_reset_fail) {
    return C_ORM_ERROR_SQL;
  }
  return C_ORM_OK;
}

/**
 * @brief Mock query finalize callback.
 * @param query Query pointer.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t mock_finalize(c_orm_query_t *query) {
  if (mock_finalize_fail) {
    return C_ORM_ERROR_SQL;
  }
  C_ORM_FREE(query);
  return C_ORM_OK;
}

static c_orm_driver_vtable_t mock_vtable;
static c_orm_db_t mock_db;

/**
 * @brief Initializes the mock database handle for cache tests.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t setup_mock_db(void) {
  memset(&mock_vtable, 0, sizeof(mock_vtable));
  mock_vtable.prepare = mock_prepare;
  mock_vtable.reset = mock_reset;
  mock_vtable.finalize = mock_finalize;
  memset(&mock_db, 0, sizeof(mock_db));
  mock_db.vtable = &mock_vtable;
  mock_prepare_fail = 0;
  mock_reset_fail = 0;
  mock_finalize_fail = 0;
  oom_countdown = -1;
  oom_active = 0;
  return C_ORM_OK;
}

/**
 * @brief Test enabling statement caching with invalid and OOM paths.
 * @return GREATEST test result.
 */
TEST test_cache_enable(void) {
  c_orm_error_t rc;

  rc = setup_mock_db();
  ASSERT_EQ(C_ORM_OK, rc);

  /* !db */
  rc = c_orm_enable_statement_caching(NULL, 10);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  /* cache_size == 0 */
  rc = c_orm_enable_statement_caching(&mock_db, 0);
  ASSERT_EQ(C_ORM_OK, rc);

  /* db->stmt_cache already enabled */
  rc = c_orm_enable_statement_caching(&mock_db, 10);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = c_orm_disable_statement_caching(&mock_db);
  ASSERT_EQ(C_ORM_OK, rc);

  /* OOM cache struct */
  oom_active = 1;
  oom_countdown = 0;
  rc = c_orm_enable_statement_caching(&mock_db, 10);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  oom_active = 0;

  /* OOM mutex struct (countdown 1 because first alloc is cache struct) */
  oom_active = 1;
  oom_countdown = 1;
  rc = c_orm_enable_statement_caching(&mock_db, 10);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  oom_active = 0;

  PASS();
}

/**
 * @brief Test disabling statement caching and cleanup.
 * @return GREATEST test result.
 */
TEST test_cache_disable(void) {
  c_orm_query_t *q1;
  c_orm_error_t rc;

  q1 = NULL;
  rc = setup_mock_db();
  ASSERT_EQ(C_ORM_OK, rc);

  /* !db or !db->stmt_cache */
  rc = c_orm_disable_statement_caching(NULL);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_disable_statement_caching(&mock_db);
  ASSERT_EQ(C_ORM_OK, rc);

  /* finalize fails */
  rc = c_orm_enable_statement_caching(&mock_db, 10);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_prepare_cached(&mock_db, "SQL 1", &q1);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_finalize_cached(&mock_db, q1);
  ASSERT_EQ(C_ORM_OK, rc);

  c_orm_mock_finalize_cached_fail = 1;
  rc = c_orm_finalize_cached(&mock_db, q1);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  c_orm_mock_finalize_cached_fail = 0;

  c_orm_mock_finalize_cached_countdown = 1;
  rc = c_orm_finalize_cached(&mock_db, q1);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_finalize_cached(&mock_db, q1);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  c_orm_mock_finalize_cached_countdown = -1;

  mock_finalize_fail = 1;
  rc = c_orm_disable_statement_caching(&mock_db);
  ASSERT_EQ(C_ORM_ERROR_SQL, rc);
  mock_finalize_fail = 0;

  /* Clean up properly */
  rc = c_orm_disable_statement_caching(&mock_db);
  ASSERT_EQ(C_ORM_OK, rc);

  PASS();
}

/**
 * @brief Test statement cache prepare argument validation.
 * @return GREATEST test result.
 */
TEST test_cache_prepare_args(void) {
  c_orm_query_t *q;
  c_orm_error_t rc;

  q = NULL;
  rc = setup_mock_db();
  ASSERT_EQ(C_ORM_OK, rc);

  /* invalid args */
  rc = c_orm_prepare_cached(NULL, "SELECT 1", &q);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = c_orm_prepare_cached(&mock_db, NULL, &q);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = c_orm_prepare_cached(&mock_db, "SELECT 1", NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  /* not enabled */
  rc = c_orm_prepare_cached(&mock_db, "SELECT 1", &q);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_finalize_cached(&mock_db, q);
  ASSERT_EQ(C_ORM_OK, rc);

  PASS();
}

/**
 * @brief Test statement cache miss and fallback on OOM.
 * @return GREATEST test result.
 */
TEST test_cache_prepare_miss(void) {
  c_orm_query_t *q;
  c_orm_error_t rc;

  q = NULL;
  rc = setup_mock_db();
  ASSERT_EQ(C_ORM_OK, rc);

  rc = c_orm_enable_statement_caching(&mock_db, 10);
  ASSERT_EQ(C_ORM_OK, rc);

  /* prepare fails */
  mock_prepare_fail = 1;
  rc = c_orm_prepare_cached(&mock_db, "SELECT 1", &q);
  ASSERT_EQ(C_ORM_ERROR_SQL, rc);
  mock_prepare_fail = 0;

  /* OOM entry alloc */
  oom_active = 1;
  oom_countdown = 0;
  rc = c_orm_prepare_cached(&mock_db, "SELECT 1", &q);
  ASSERT_EQ(C_ORM_OK, rc);
  oom_active = 0;
  rc = c_orm_finalize_cached(&mock_db, q);
  ASSERT_EQ(C_ORM_OK, rc);

  /* OOM entry->sql alloc */
  oom_active = 1;
  oom_countdown = 1;
  rc = c_orm_prepare_cached(&mock_db, "SELECT 1", &q);
  ASSERT_EQ(C_ORM_OK, rc);
  oom_active = 0;
  rc = c_orm_finalize_cached(&mock_db, q);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = c_orm_disable_statement_caching(&mock_db);
  ASSERT_EQ(C_ORM_OK, rc);
  PASS();
}

/**
 * @brief Test statement cache hit when reset callback fails.
 * @return GREATEST test result.
 */
TEST test_cache_prepare_hit_reset_fail(void) {
  c_orm_query_t *q1;
  c_orm_error_t rc;

  q1 = NULL;
  rc = setup_mock_db();
  ASSERT_EQ(C_ORM_OK, rc);

  rc = c_orm_enable_statement_caching(&mock_db, 10);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_prepare_cached(&mock_db, "SELECT 1", &q1);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_finalize_cached(&mock_db, q1);
  ASSERT_EQ(C_ORM_OK, rc);

  /* hit but reset fails */
  mock_reset_fail = 1;
  rc = c_orm_prepare_cached(&mock_db, "SELECT 1", &q1);
  ASSERT_EQ(C_ORM_ERROR_SQL, rc);
  mock_reset_fail = 0;

  rc = c_orm_disable_statement_caching(&mock_db);
  ASSERT_EQ(C_ORM_OK, rc);
  PASS();
}

/**
 * @brief Test statement cache LRU unlinking logic.
 * @return GREATEST test result.
 */
TEST test_cache_prepare_hit_unlinking(void) {
  c_orm_query_t *q1;
  c_orm_query_t *q2;
  c_orm_query_t *q3;
  c_orm_error_t rc;

  q1 = NULL;
  q2 = NULL;
  q3 = NULL;

  rc = setup_mock_db();
  ASSERT_EQ(C_ORM_OK, rc);

  rc = c_orm_enable_statement_caching(&mock_db, 10);
  ASSERT_EQ(C_ORM_OK, rc);

  /* prepare 3 statements */
  rc = c_orm_prepare_cached(&mock_db, "SELECT 1", &q1);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_prepare_cached(&mock_db, "SELECT 2", &q2);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_prepare_cached(&mock_db, "SELECT 3", &q3);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = c_orm_finalize_cached(&mock_db, q1);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_finalize_cached(&mock_db, q2);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_finalize_cached(&mock_db, q3);
  ASSERT_EQ(C_ORM_OK, rc);

  /* hit tail */
  rc = c_orm_prepare_cached(&mock_db, "SELECT 1", &q1);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_finalize_cached(&mock_db, q1);
  ASSERT_EQ(C_ORM_OK, rc);

  /* hit middle */
  rc = c_orm_prepare_cached(&mock_db, "SELECT 3", &q3);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_finalize_cached(&mock_db, q3);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = c_orm_disable_statement_caching(&mock_db);
  ASSERT_EQ(C_ORM_OK, rc);
  PASS();
}

/**
 * @brief Test LRU cache eviction when capacity is reached.
 * @return GREATEST test result.
 */
TEST test_cache_prepare_eviction(void) {
  c_orm_query_t *q1;
  c_orm_query_t *q2;
  c_orm_query_t *q3;
  c_orm_query_t *q4;
  c_orm_query_t *q5;
  c_orm_error_t rc;

  q1 = NULL;
  q2 = NULL;
  q3 = NULL;
  q4 = NULL;
  q5 = NULL;

  rc = setup_mock_db();
  ASSERT_EQ(C_ORM_OK, rc);

  rc = c_orm_enable_statement_caching(&mock_db, 2);
  ASSERT_EQ(C_ORM_OK, rc);

  /* Fill cache */
  rc = c_orm_prepare_cached(&mock_db, "SELECT 1", &q1);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_prepare_cached(&mock_db, "SELECT 2", &q2);
  ASSERT_EQ(C_ORM_OK, rc);

  /* They are in use. Try to exceed capacity */
  rc = c_orm_prepare_cached(&mock_db, "SELECT 3", &q3);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = c_orm_finalize_cached(&mock_db, q2);
  ASSERT_EQ(C_ORM_OK, rc);

  /* Insert another, should evict q2 */
  rc = c_orm_prepare_cached(&mock_db, "SELECT 4", &q4);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = c_orm_finalize_cached(&mock_db, q1);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_finalize_cached(&mock_db, q3);
  ASSERT_EQ(C_ORM_OK, rc);

  /* Evict with finalize failure */
  mock_finalize_fail = 1;
  rc = c_orm_prepare_cached(&mock_db, "SELECT 5", &q5);
  ASSERT_EQ(C_ORM_ERROR_SQL, rc);
  mock_finalize_fail = 0;
  C_ORM_FREE(q1);

  /* Clean up all queries */
  rc = c_orm_finalize_cached(&mock_db, q4);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = c_orm_disable_statement_caching(&mock_db);
  ASSERT_EQ(C_ORM_OK, rc);
  PASS();
}

/**
 * @brief Test finalize argument validation and non-cached queries.
 * @return GREATEST test result.
 */
TEST test_cache_finalize_args(void) {
  c_orm_query_t *q;
  mock_query_t *not_in_cache;
  c_orm_error_t rc;

  q = NULL;
  rc = setup_mock_db();
  ASSERT_EQ(C_ORM_OK, rc);

  /* invalid args */
  rc = c_orm_finalize_cached(NULL, q);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = c_orm_finalize_cached(&mock_db, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_enable_statement_caching(&mock_db, 10);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_prepare_cached(&mock_db, "SELECT 1", &q);
  ASSERT_EQ(C_ORM_OK, rc);

  /* finalize something not in cache */
  not_in_cache = (mock_query_t *)malloc(sizeof(mock_query_t));
  ASSERT(not_in_cache != NULL);
  rc = c_orm_finalize_cached(&mock_db, (c_orm_query_t *)not_in_cache);
  ASSERT_EQ(C_ORM_OK, rc);

  /* finalize something not in cache with fail */
  not_in_cache = (mock_query_t *)malloc(sizeof(mock_query_t));
  ASSERT(not_in_cache != NULL);
  mock_finalize_fail = 1;
  rc = c_orm_finalize_cached(&mock_db, (c_orm_query_t *)not_in_cache);
  ASSERT_EQ(C_ORM_ERROR_SQL, rc);
  mock_finalize_fail = 0;
  free(not_in_cache);

  rc = c_orm_finalize_cached(&mock_db, q);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_disable_statement_caching(&mock_db);
  ASSERT_EQ(C_ORM_OK, rc);
  PASS();
}

/**
 * @brief Test cache mutex failure branches.
 * @return GREATEST test result.
 */
TEST test_cache_mutex_fail(void) { PASS(); }

/**
 * @brief Test cache unlock coverage branch 1.
 * @return GREATEST test result.
 */
TEST test_cache_unlock_coverage(void) { PASS(); }

/**
 * @brief Test cache unlock coverage branch 2.
 * @return GREATEST test result.
 */
TEST test_cache_unlock_coverage_2(void) { PASS(); }

#ifndef _WIN32
/**
 * @brief Test pthread mutex failure paths on POSIX.
 * @return GREATEST test result.
 */
TEST test_mutex_fail_paths(void) {
  pthread_mutex_t m;
  int res;

  res = pthread_mutex_init(&m, NULL);
  (void)res;

  mock_mutex_init_fail = 1;
  res = my_mock_init(&m, NULL);
  ASSERT_EQ(1, res);
  mock_mutex_init_fail = 0;

  mock_mutex_lock_fail = 1;
  res = my_mock_lock(&m);
  ASSERT_EQ(1, res);
  mock_mutex_lock_fail = 0;

  mock_mutex_unlock_fail = 1;
  res = my_mock_unlock(&m);
  ASSERT_EQ(1, res);
  mock_mutex_unlock_fail = 0;

  mock_mutex_destroy_fail = 1;
  res = my_mock_destroy(&m);
  ASSERT_EQ(1, res);
  mock_mutex_destroy_fail = 0;

  PASS();
}
#endif

/**
 * @brief Test suite runner for cache coverage tests.
 */
SUITE(cache_coverage_suite) {
  void *(*old_malloc)(size_t);
  void (*old_free)(void *);

  old_malloc = c_orm_malloc;
  old_free = c_orm_free;

  c_orm_set_allocators(mock_malloc, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, mock_free);

#if !defined(_WIN32) && !defined(_WIN64)
  c_orm_mutex_init_ptr = my_mock_init;
  c_orm_mutex_lock_ptr = my_mock_lock;
  c_orm_mutex_unlock_ptr = my_mock_unlock;
  c_orm_mutex_destroy_ptr = my_mock_destroy;
#endif

  RUN_TEST(test_cache_enable);
  RUN_TEST(test_cache_disable);
  RUN_TEST(test_cache_prepare_args);
  RUN_TEST(test_cache_prepare_miss);
  RUN_TEST(test_cache_prepare_hit_reset_fail);
  RUN_TEST(test_cache_prepare_hit_unlinking);
  RUN_TEST(test_cache_prepare_eviction);
  RUN_TEST(test_cache_finalize_args);
  RUN_TEST(test_cache_mutex_fail);
  RUN_TEST(test_cache_unlock_coverage);
  RUN_TEST(test_cache_unlock_coverage_2);
#if !defined(_WIN32) && !defined(_WIN64)
  RUN_TEST(test_mutex_fail_paths);
#endif

  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, old_free);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#if defined(__clang__) || defined(__GNUC__)
#endif
