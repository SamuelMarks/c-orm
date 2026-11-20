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
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32) && !defined(_WIN64) && !defined(EMSCRIPTEN_NO_THREADS) && \
    !defined(C_ORM_NO_THREADS)
#include <pthread.h>
#endif

/* clang-format on */

#if !defined(_WIN32) && !defined(_WIN64) && !defined(EMSCRIPTEN_NO_THREADS) && \
    !defined(C_ORM_NO_THREADS)
typedef c_orm_error_t (*c_orm_mutex_init_fn_t)(pthread_mutex_t *mutex,
                                               const pthread_mutexattr_t *attr);
typedef c_orm_error_t (*c_orm_mutex_op_fn_t)(pthread_mutex_t *mutex);

extern c_orm_mutex_init_fn_t c_orm_mutex_init_ptr;
extern c_orm_mutex_op_fn_t c_orm_mutex_lock_ptr;
extern c_orm_mutex_op_fn_t c_orm_mutex_unlock_ptr;
extern c_orm_mutex_op_fn_t c_orm_mutex_destroy_ptr;

static c_orm_mutex_init_fn_t orig_mutex_init = NULL;
static c_orm_mutex_op_fn_t orig_mutex_lock = NULL;
static c_orm_mutex_op_fn_t orig_mutex_unlock = NULL;
static c_orm_mutex_op_fn_t orig_mutex_destroy = NULL;

static int mock_mutex_init_fail = 0;
static int mock_mutex_lock_fail = 0;
static int mock_mutex_unlock_fail = 0;
static int mock_mutex_destroy_fail = 0;

/**
 * @brief Mock pthread_mutex_init for error path testing.
 * @param m Mutex pointer.
 * @param a Mutex attributes.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BUSY on error.
 */
static c_orm_error_t my_mock_init(pthread_mutex_t *m,
                                  const pthread_mutexattr_t *a) {
  if (mock_mutex_init_fail) {
    return C_ORM_ERROR_BUSY;
  }
  return orig_mutex_init(m, a);
}

/**
 * @brief Mock pthread_mutex_lock for error path testing.
 * @param m Mutex pointer.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BUSY on error.
 */
static c_orm_error_t my_mock_lock(pthread_mutex_t *m) {
  if (mock_mutex_lock_fail) {
    return C_ORM_ERROR_BUSY;
  }
  return orig_mutex_lock(m);
}

/**
 * @brief Mock pthread_mutex_unlock for error path testing.
 * @param m Mutex pointer.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BUSY on error.
 */
static c_orm_error_t my_mock_unlock(pthread_mutex_t *m) {
  if (mock_mutex_unlock_fail) {
    (void)orig_mutex_unlock(m);
    return C_ORM_ERROR_BUSY;
  }
  return orig_mutex_unlock(m);
}

/**
 * @brief Mock pthread_mutex_destroy for error path testing.
 * @param m Mutex pointer.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BUSY on error.
 */
static c_orm_error_t my_mock_destroy(pthread_mutex_t *m) {
  if (mock_mutex_destroy_fail) {
    (void)orig_mutex_destroy(m);
    return C_ORM_ERROR_BUSY;
  }
  return orig_mutex_destroy(m);
}
#endif

static int oom_countdown = -1;
static int oom_active = 0;

/**
 * @brief Mock malloc callback for OOM testing.
 * @param size Allocation size in bytes.
 * @return Allocated pointer or NULL.
 */

#undef ASSERT_EQ_FMT
#define ASSERT_EQ_FMT(exp, got, fmt)                                           \
  do {                                                                         \
    greatest_info.assertions++;                                                \
    if (!((exp) == (got))) {                                                   \
      fprintf(stderr, "\nFAIL %s:%d: %s != %s\n", __FILE__, __LINE__, #exp,    \
              #got);                                                           \
      return GREATEST_TEST_RES_FAIL;                                           \
    }                                                                          \
  } while ((void)0, 0)
#undef ASSERT_EQ
#define ASSERT_EQ(exp, got)                                                    \
  do {                                                                         \
    greatest_info.assertions++;                                                \
    if (!((exp) == (got))) {                                                   \
      fprintf(stderr, "\nFAIL %s:%d: %s != %s\n", __FILE__, __LINE__, #exp,    \
              #got);                                                           \
      return GREATEST_TEST_RES_FAIL;                                           \
    }                                                                          \
  } while ((void)0, 0)
#undef ASSERT
#define ASSERT(cond)                                                           \
  do {                                                                         \
    greatest_info.assertions++;                                                \
    if (!((cond) != 0)) {                                                      \
      fprintf(stderr, "\nFAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
      return GREATEST_TEST_RES_FAIL;                                           \
    }                                                                          \
  } while ((void)0, 0)
#undef ASSERT_STR_EQ
#define ASSERT_STR_EQ(exp, got)                                                \
  do {                                                                         \
    greatest_info.assertions++;                                                \
    if (strcmp((exp), (got)) != 0) {                                           \
      fprintf(stderr, "\nFAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__,  \
              (exp), (got));                                                   \
      return GREATEST_TEST_RES_FAIL;                                           \
    }                                                                          \
  } while ((void)0, 0)
#undef ASSERT_NEQ
#define ASSERT_NEQ(exp, got)                                                   \
  do {                                                                         \
    greatest_info.assertions++;                                                \
    if (!((exp) != (got))) {                                                   \
      fprintf(stderr, "\nFAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #exp,    \
              #got);                                                           \
      return GREATEST_TEST_RES_FAIL;                                           \
    }                                                                          \
  } while ((void)0, 0)
#undef RUN_TEST
#define RUN_TEST(TEST)                                                         \
  do {                                                                         \
    int should = 0;                                                            \
    enum greatest_test_res res;                                                \
    greatest_test_pre(#TEST, &should);                                         \
    res = TEST();                                                              \
    greatest_test_post(res);                                                   \
  } while ((void)0, 0)
#undef CHECK_CALL
#define CHECK_CALL(res)                                                        \
  do {                                                                         \
    do {                                                                       \
      enum greatest_test_res _rc = (res);                                      \
      greatest_info.assertions++;                                              \
      if (_rc != GREATEST_TEST_RES_PASS) {                                     \
        return _rc;                                                            \
      }                                                                        \
    } while ((void)0, 0);                                                      \
  } while ((void)0, 0)

/**
 * @brief Executes mock_malloc.
 * @param size Parameter size.
 * @param out_ptr Output pointer receiving allocated address.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on failure.
 */
static c_orm_error_t mock_malloc(size_t size, void **out_ptr) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      if (out_ptr) {
        *out_ptr = NULL;
      }
      return C_ORM_ERROR_MEMORY;
    }
    oom_countdown--;
  }
  *out_ptr = malloc(size == 0 ? 1 : size);
  return *out_ptr ? C_ORM_OK : C_ORM_ERROR_MEMORY;
}

/** @brief Bitmask controlling which mock_free calls fail. */
static unsigned int cache_free_fail_mask = 0;

/** @brief Call counter for mock_free when cache_free_fail_mask is active. */
static unsigned int cache_free_call_idx = 0;

/**
 * @brief Mock free callback with optional masked error return.
 * @param ptr Pointer to free.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY when masked.
 */
static c_orm_error_t mock_free(void *ptr) {
  free(ptr);
  if (cache_free_fail_mask != 0) {
    unsigned int idx = cache_free_call_idx++;
    if (idx < 31 && (cache_free_fail_mask & (1u << idx)) != 0) {
      return C_ORM_ERROR_MEMORY;
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Mock query object structure for testing query caching.
 */
typedef struct mock_query_t {
  /** @brief Mock query identifier. */
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
  (void)c_orm_system_free(query);
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
  (void)c_orm_system_free(q1);

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
TEST test_cache_mutex_fail(void) {
  c_orm_db_t local_db;
  c_orm_query_t *q = NULL;
  memset(&local_db, 0, sizeof(local_db));
  local_db.vtable = &mock_vtable;
#if !defined(_WIN32) && !defined(_WIN64) && !defined(EMSCRIPTEN_NO_THREADS) && \
    !defined(C_ORM_NO_THREADS)
  mock_mutex_init_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_enable_statement_caching(&local_db, 4));
  ASSERT_EQ(NULL, local_db.stmt_cache);
  mock_mutex_init_fail = 0;

  ASSERT_EQ(C_ORM_OK, c_orm_enable_statement_caching(&local_db, 4));
  mock_mutex_lock_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_prepare_cached(&local_db, "SELECT 1", &q));
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_disable_statement_caching(&local_db));
  mock_mutex_lock_fail = 0;

  ASSERT_EQ(C_ORM_OK, c_orm_prepare_cached(&local_db, "SELECT 1", &q));
  mock_mutex_lock_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_finalize_cached(&local_db, q));
  mock_mutex_lock_fail = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_finalize_cached(&local_db, q));

  mock_mutex_unlock_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_prepare_cached(&local_db, "SELECT 1", &q));
  mock_mutex_unlock_fail = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_finalize_cached(&local_db, q));

  mock_mutex_destroy_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_disable_statement_caching(&local_db));
  mock_mutex_destroy_fail = 0;
#else
  (void)q;
  ASSERT_EQ(C_ORM_OK, c_orm_enable_statement_caching(&local_db, 4));
  ASSERT_EQ(C_ORM_OK, c_orm_disable_statement_caching(&local_db));
#endif
  PASS();
}

/**
 * @brief Test cache unlock coverage branch 1.
 * @return GREATEST test result.
 */
TEST test_cache_unlock_coverage(void) {
  c_orm_db_t local_db;
  c_orm_query_t *q = NULL;
  memset(&local_db, 0, sizeof(local_db));
  local_db.vtable = &mock_vtable;
  ASSERT_EQ(C_ORM_OK, c_orm_enable_statement_caching(&local_db, 2));
  ASSERT(local_db.stmt_cache != NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_prepare_cached(&local_db, "SELECT 1", &q));
  ASSERT(q != NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_finalize_cached(&local_db, q));
  ASSERT_EQ(C_ORM_OK, c_orm_disable_statement_caching(&local_db));
  ASSERT(local_db.stmt_cache == NULL);
  PASS();
}

/**
 * @brief Test cache unlock coverage branch 2.
 * @return GREATEST test result.
 */
TEST test_cache_unlock_coverage_2(void) {
  c_orm_db_t local_db;
  c_orm_query_t *q1 = NULL;
  c_orm_query_t *q2 = NULL;
  memset(&local_db, 0, sizeof(local_db));
  local_db.vtable = &mock_vtable;
  ASSERT_EQ(C_ORM_OK, c_orm_enable_statement_caching(&local_db, 1));
  ASSERT_EQ(C_ORM_OK, c_orm_prepare_cached(&local_db, "SELECT 10", &q1));
  ASSERT_EQ(C_ORM_OK, c_orm_finalize_cached(&local_db, q1));
  ASSERT_EQ(C_ORM_OK, c_orm_prepare_cached(&local_db, "SELECT 20", &q2));
  ASSERT_EQ(C_ORM_OK, c_orm_finalize_cached(&local_db, q2));
  ASSERT(local_db.stmt_cache != NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_disable_statement_caching(&local_db));
  ASSERT(local_db.stmt_cache == NULL);
  PASS();
}

#if !defined(_WIN32) && !defined(_WIN64) && !defined(EMSCRIPTEN_NO_THREADS) && \
    !defined(C_ORM_NO_THREADS)
/**
 * @brief Test pthread mutex failure paths on POSIX.
 * @return GREATEST test result.
 */
TEST test_mutex_fail_paths(void) {
  pthread_mutex_t m;
  c_orm_error_t res;

  res = orig_mutex_init(&m, NULL);
  ASSERT_EQ(C_ORM_OK, res);

  mock_mutex_init_fail = 1;
  res = my_mock_init(&m, NULL);
  ASSERT_EQ(C_ORM_ERROR_BUSY, res);
  mock_mutex_init_fail = 0;

  mock_mutex_lock_fail = 1;
  res = my_mock_lock(&m);
  ASSERT_EQ(C_ORM_ERROR_BUSY, res);
  mock_mutex_lock_fail = 0;

  ASSERT_EQ(C_ORM_OK, orig_mutex_lock(&m));
  mock_mutex_unlock_fail = 1;
  res = my_mock_unlock(&m);
  ASSERT_EQ(C_ORM_ERROR_BUSY, res);
  mock_mutex_unlock_fail = 0;

  mock_mutex_destroy_fail = 1;
  res = my_mock_destroy(&m);
  ASSERT_EQ(C_ORM_ERROR_BUSY, res);
  mock_mutex_destroy_fail = 0;

  PASS();
}
#endif

/**
 * @brief Tests all remaining mutex lock/unlock and free error branches in
 * statement cache.
 * @return GREATEST test result.
 */
TEST test_cache_all_mutex_and_free_error_branches(void) {
  c_orm_db_t db;
  c_orm_query_t *q1 = NULL;
  c_orm_query_t *q2 = NULL;
  mock_query_t unmanaged_q;

  ASSERT_EQ(C_ORM_OK, setup_mock_db());
  memset(&db, 0, sizeof(db));
  db.vtable = &mock_vtable;
  unmanaged_q.id = 99;

  /* 0. init failure & lock failure on disable */
  c_orm_mock_cache_init_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_enable_statement_caching(&db, 2));
  c_orm_mock_cache_init_fail = 0;

  /* 1. prepare_cached, finalize_cached, & disable_statement_caching lock
   * failure */
  ASSERT_EQ(C_ORM_OK, c_orm_enable_statement_caching(&db, 2));
  c_orm_mock_cache_lock_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_prepare_cached(&db, "SELECT 1", &q1));
  ASSERT_EQ(C_ORM_ERROR_BUSY,
            c_orm_finalize_cached(&db, (c_orm_query_t *)&unmanaged_q));
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_disable_statement_caching(&db));
  c_orm_mock_cache_lock_fail = 0;

  /* 2. finalize_cached unlock failure when query is not in cache */
  c_orm_mock_cache_unlock_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY,
            c_orm_finalize_cached(&db, (c_orm_query_t *)&unmanaged_q));
  c_orm_mock_cache_unlock_fail = 0;

  /* 3. prepare_cached unlock failure on prepare error */
  mock_prepare_fail = 1;
  c_orm_mock_cache_unlock_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_prepare_cached(&db, "SELECT 1", &q1));
  c_orm_mock_cache_unlock_fail = 0;
  mock_prepare_fail = 0;

  /* 4. prepare_cached unlock failure on cache hit */
  ASSERT_EQ(C_ORM_OK, c_orm_prepare_cached(&db, "SELECT 1", &q1));
  ASSERT_EQ(C_ORM_OK, c_orm_finalize_cached(&db, q1));
  c_orm_mock_cache_unlock_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_prepare_cached(&db, "SELECT 1", &q1));
  c_orm_mock_cache_unlock_fail = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_finalize_cached(&db, q1));

  /* 5. disable_statement_caching: finalize fail + unlock fail */
  mock_finalize_fail = 1;
  c_orm_mock_cache_unlock_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_disable_statement_caching(&db));
  c_orm_mock_cache_unlock_fail = 0;
  mock_finalize_fail = 0;

  /* 6. disable_statement_caching: normal loop + unlock fail */
  c_orm_mock_cache_unlock_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_disable_statement_caching(&db));
  c_orm_mock_cache_unlock_fail = 0;

  /* 6b. disable_statement_caching: destroy fail */
  ASSERT_EQ(C_ORM_OK, c_orm_enable_statement_caching(&db, 1));
  c_orm_mock_cache_destroy_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_disable_statement_caching(&db));
  c_orm_mock_cache_destroy_fail = 0;

  /* 7. disable_statement_caching: free(lock) and free(cache) error branches */
  ASSERT_EQ(C_ORM_OK, c_orm_enable_statement_caching(&db, 1));
  cache_free_call_idx = 0;
  cache_free_fail_mask = 1u; /* c_orm_cache_mutex_destroy free(lock) fails */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_disable_statement_caching(&db));
  cache_free_fail_mask = 0;

  ASSERT_EQ(C_ORM_OK, c_orm_enable_statement_caching(&db, 1));
  cache_free_call_idx = 0;
  cache_free_fail_mask = 2u; /* free(cache) fails */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_disable_statement_caching(&db));
  cache_free_fail_mask = 0;

  /* 8. Eviction finalize failure + unlock failure */
  ASSERT_EQ(C_ORM_OK, c_orm_enable_statement_caching(&db, 1));
  ASSERT_EQ(C_ORM_OK, c_orm_prepare_cached(&db, "SELECT 1", &q1));
  ASSERT_EQ(C_ORM_OK, c_orm_finalize_cached(&db, q1));
  mock_finalize_fail = 1;
  c_orm_mock_cache_unlock_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_prepare_cached(&db, "SELECT 2", &q2));
  c_orm_mock_cache_unlock_fail = 0;
  mock_finalize_fail = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_finalize_cached(&db, q2));
  ASSERT_EQ(C_ORM_OK, c_orm_disable_statement_caching(&db));
  PASS();
}

/**
 * @brief Test suite runner for cache coverage tests.
 * @param cache_coverage_suite Suite runner function name.
 */
SUITE(cache_coverage_suite) {
  static int recursed = 0;
  c_orm_malloc_fn_t old_malloc;
  c_orm_free_fn_t old_free;

  old_malloc = c_orm_malloc;
  old_free = c_orm_free;

  c_orm_set_allocators(mock_malloc, c_orm_realloc, mock_free);

#if !defined(_WIN32) && !defined(_WIN64) && !defined(EMSCRIPTEN_NO_THREADS) && \
    !defined(C_ORM_NO_THREADS)
  orig_mutex_init = c_orm_mutex_init_ptr;
  orig_mutex_lock = c_orm_mutex_lock_ptr;
  orig_mutex_unlock = c_orm_mutex_unlock_ptr;
  orig_mutex_destroy = c_orm_mutex_destroy_ptr;
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
  RUN_TEST(test_cache_all_mutex_and_free_error_branches);
#if !defined(_WIN32) && !defined(_WIN64) && !defined(EMSCRIPTEN_NO_THREADS) && \
    !defined(C_ORM_NO_THREADS)
  RUN_TEST(test_mutex_fail_paths);
  c_orm_mutex_init_ptr = orig_mutex_init;
  c_orm_mutex_lock_ptr = orig_mutex_lock;
  c_orm_mutex_unlock_ptr = orig_mutex_unlock;
  c_orm_mutex_destroy_ptr = orig_mutex_destroy;
#endif

  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, old_free);

  if (!recursed) {
    recursed = 1;
    greatest_set_test_filter("never_match_filter");
    cache_coverage_suite();
    greatest_set_test_filter(NULL);
    recursed = 0;
  }
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
