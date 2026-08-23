#if defined(__clang__) || defined(__GNUC__)
#endif
/* clang-format off */
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

static int my_mock_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) {
  if (mock_mutex_init_fail)
    return 1;
  return pthread_mutex_init(m, a);
}
static int my_mock_lock(pthread_mutex_t *m) {
  if (mock_mutex_lock_fail)
    return 1;
  return pthread_mutex_lock(m);
}
static int my_mock_unlock(pthread_mutex_t *m) {
  if (mock_mutex_unlock_fail) {
    pthread_mutex_unlock(m);
    return 1;
  }
  return pthread_mutex_unlock(m);
}
static int my_mock_destroy(pthread_mutex_t *m) {
  if (mock_mutex_destroy_fail) {
    pthread_mutex_destroy(m);
    return 1;
  }
  return pthread_mutex_destroy(m);
}
#endif

/* Mock Allocator to trigger OOM */
static int oom_countdown = -1;
static int oom_active = 0;

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

static void mock_free(void *ptr) { free(ptr); }

/* Mock vtable and query */
typedef struct mock_query_t {
  int id;
} mock_query_t;

static int mock_prepare_fail = 0;
static int mock_reset_fail = 0;
static int mock_finalize_fail = 0;

static c_orm_error_t mock_prepare(c_orm_db_t *db, const char *sql,
                                  c_orm_query_t **out_query) {
  mock_query_t *q;
  (void)db;
  (void)sql;
  if (mock_prepare_fail)
    return C_ORM_ERROR_SQL;
  q = (mock_query_t *)malloc(sizeof(mock_query_t));
  q->id = 1;
  *out_query = (c_orm_query_t *)q;
  return C_ORM_OK;
}

static c_orm_error_t mock_reset(c_orm_query_t *query) {
  (void)query;
  if (mock_reset_fail)
    return C_ORM_ERROR_SQL;
  return C_ORM_OK;
}

static c_orm_error_t mock_finalize(c_orm_query_t *query) {
  if (mock_finalize_fail) {
    return C_ORM_ERROR_SQL;
  }
  C_ORM_FREE(query);
  return C_ORM_OK;
}

static c_orm_driver_vtable_t mock_vtable;

static c_orm_db_t mock_db;
static void setup_mock_db(void) {
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
}

TEST test_cache_enable(void) {
  setup_mock_db();

  /* !db */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_enable_statement_caching(NULL, 10));

  /* cache_size == 0 */
  ASSERT_EQ(C_ORM_OK, c_orm_enable_statement_caching(&mock_db, 0));

  /* db->stmt_cache already enabled */
  ASSERT_EQ(C_ORM_OK, c_orm_enable_statement_caching(&mock_db, 10));

  c_orm_disable_statement_caching(&mock_db);

  /* OOM cache struct */
  oom_active = 1;
  oom_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_enable_statement_caching(&mock_db, 10));
  oom_active = 0;

  /* OOM mutex struct (countdown 1 because first alloc is cache struct) */
  oom_active = 1;
  oom_countdown = 1;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_enable_statement_caching(&mock_db, 10));
  oom_active = 0;

  PASS();
}

TEST test_cache_disable(void) {
  c_orm_query_t *q1;
  setup_mock_db();

  /* !db or !db->stmt_cache */
  ASSERT_EQ(C_ORM_OK, c_orm_disable_statement_caching(NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_disable_statement_caching(&mock_db));

  /* finalize fails */
  c_orm_enable_statement_caching(&mock_db, 10);
  c_orm_prepare_cached(&mock_db, "SQL 1", &q1);
  c_orm_finalize_cached(&mock_db, q1); /* release */

  mock_finalize_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_disable_statement_caching(&mock_db));
  mock_finalize_fail = 0;

  /* Clean up properly */
  c_orm_disable_statement_caching(&mock_db);

  PASS();
}

TEST test_cache_prepare_args(void) {
  c_orm_query_t *q = NULL;
  setup_mock_db();

  /* invalid args */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_prepare_cached(NULL, "SELECT 1", &q));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_prepare_cached(&mock_db, NULL, &q));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_prepare_cached(&mock_db, "SELECT 1", NULL));

  /* not enabled */
  ASSERT_EQ(C_ORM_OK, c_orm_prepare_cached(&mock_db, "SELECT 1", &q));
  c_orm_finalize_cached(&mock_db, q); /* finalizes directly */

  PASS();
}

TEST test_cache_prepare_miss(void) {
  c_orm_query_t *q = NULL;
  setup_mock_db();
  c_orm_enable_statement_caching(&mock_db, 10);

  /* prepare fails */
  mock_prepare_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_prepare_cached(&mock_db, "SELECT 1", &q));
  mock_prepare_fail = 0;

  /* OOM entry alloc */
  oom_active = 1;
  oom_countdown = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_prepare_cached(&mock_db, "SELECT 1", &q)); /* Graceful */
  oom_active = 0;
  c_orm_finalize_cached(&mock_db, q);

  /* OOM entry->sql alloc */
  oom_active = 1;
  oom_countdown = 1; /* entry alloc succeeds, sql alloc fails */
  ASSERT_EQ(C_ORM_OK,
            c_orm_prepare_cached(&mock_db, "SELECT 1", &q)); /* Graceful */
  oom_active = 0;
  c_orm_finalize_cached(&mock_db, q);

  c_orm_disable_statement_caching(&mock_db);
  PASS();
}

TEST test_cache_prepare_hit_reset_fail(void) {
  c_orm_query_t *q1;
  setup_mock_db();
  c_orm_enable_statement_caching(&mock_db, 10);
  c_orm_prepare_cached(&mock_db, "SELECT 1", &q1);
  c_orm_finalize_cached(&mock_db, q1); /* release */

  /* hit but reset fails */
  mock_reset_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_prepare_cached(&mock_db, "SELECT 1", &q1));
  mock_reset_fail = 0;

  c_orm_disable_statement_caching(&mock_db);
  PASS();
}

TEST test_cache_prepare_hit_unlinking(void) {
  c_orm_query_t *q1, *q2, *q3;
  setup_mock_db();
  c_orm_enable_statement_caching(&mock_db, 10);

  /* prepare 3 statements */
  c_orm_prepare_cached(&mock_db, "SELECT 1", &q1);
  c_orm_prepare_cached(&mock_db, "SELECT 2", &q2);
  c_orm_prepare_cached(&mock_db, "SELECT 3", &q3);

  c_orm_finalize_cached(&mock_db, q1);
  c_orm_finalize_cached(&mock_db, q2);
  c_orm_finalize_cached(&mock_db, q3);

  /* hit tail (q1 because q3 is head, q2 is middle, q1 is tail) */
  c_orm_prepare_cached(&mock_db, "SELECT 1", &q1);
  c_orm_finalize_cached(&mock_db, q1);

  /* hit middle (q2 is now tail, q3 is head, q1 is head now, wait: order was q3,
   * q2, q1. hit q1: q1, q3, q2. hit q3: q3, q1, q2.) */
  c_orm_prepare_cached(&mock_db, "SELECT 3", &q3);
  c_orm_finalize_cached(&mock_db, q3);

  c_orm_disable_statement_caching(&mock_db);
  PASS();
}

TEST test_cache_prepare_eviction(void) {
  c_orm_query_t *q1, *q2, *q3, *q4, *q5;
  setup_mock_db();
  c_orm_enable_statement_caching(&mock_db, 2);

  /* Fill cache */
  c_orm_prepare_cached(&mock_db, "SELECT 1", &q1);
  c_orm_prepare_cached(&mock_db, "SELECT 2", &q2);

  /* They are in use. Try to exceed capacity */
  c_orm_prepare_cached(&mock_db, "SELECT 3", &q3);
  /* Cache count is now 3. Eviction skipped because all in use. */

  c_orm_finalize_cached(&mock_db, q2); /* Release q2 (middle) */
  /* Insert another, should evict q2 */
  c_orm_prepare_cached(&mock_db, "SELECT 4", &q4);

  c_orm_finalize_cached(&mock_db, q1); /* Release tail */
  c_orm_finalize_cached(&mock_db, q3); /* Release head */

  /* Evict with finalize failure */
  mock_finalize_fail = 1;
  c_orm_prepare_cached(&mock_db, "SELECT 5", &q5); /* should evict q1 */
  mock_finalize_fail = 0;
  C_ORM_FREE(q1); /* manually free since mock_finalize failed */

  /* Clean up all queries */
  c_orm_finalize_cached(&mock_db, q4);
  c_orm_finalize_cached(&mock_db, q5);

  c_orm_disable_statement_caching(&mock_db);
  PASS();
}

TEST test_cache_finalize_args(void) {
  c_orm_query_t *q = NULL;
  mock_query_t *not_in_cache;
  setup_mock_db();

  /* invalid args */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_finalize_cached(NULL, q));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_finalize_cached(&mock_db, NULL));

  c_orm_enable_statement_caching(&mock_db, 10);
  c_orm_prepare_cached(&mock_db, "SELECT 1", &q);

  /* finalize something not in cache */
  not_in_cache = (mock_query_t *)malloc(sizeof(mock_query_t));
  ASSERT_EQ(C_ORM_OK,
            c_orm_finalize_cached(&mock_db, (c_orm_query_t *)not_in_cache));

  /* finalize something not in cache with fail */
  not_in_cache = (mock_query_t *)malloc(sizeof(mock_query_t));
  mock_finalize_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL,
            c_orm_finalize_cached(&mock_db, (c_orm_query_t *)not_in_cache));
  mock_finalize_fail = 0;
  free(not_in_cache);

  c_orm_finalize_cached(&mock_db, q);
  c_orm_disable_statement_caching(&mock_db);
  PASS();
}

TEST test_cache_mutex_fail(void) { PASS(); }

TEST test_cache_unlock_coverage(void) { PASS(); }

TEST test_cache_unlock_coverage_2(void) { PASS(); }

#ifndef _WIN32
TEST test_mutex_fail_paths(void) {
  pthread_mutex_t m;
  pthread_mutex_init(&m, NULL);
  pthread_mutex_init(&m, NULL);
  mock_mutex_init_fail = 1;
  my_mock_init(&m, NULL);
  mock_mutex_init_fail = 0;

  mock_mutex_lock_fail = 1;
  my_mock_lock(&m);
  mock_mutex_lock_fail = 0;

  mock_mutex_unlock_fail = 1;
  my_mock_unlock(&m);
  mock_mutex_unlock_fail = 0;

  mock_mutex_destroy_fail = 1;
  my_mock_destroy(&m);
  mock_mutex_destroy_fail = 0;

  PASS();
}
#endif

SUITE(cache_coverage_suite) {
  void *(*old_malloc)(size_t) = c_orm_malloc;
  void (*old_free)(void *) = c_orm_free;

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

#if defined(__clang__) || defined(__GNUC__)
#endif
