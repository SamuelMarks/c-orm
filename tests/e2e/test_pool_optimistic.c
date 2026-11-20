/**
 * @file test_pool_optimistic.c
 * @brief Unit, integration, and fault-injection tests for connection pooling
 * (`c_orm_pool_*`) and optimistic concurrency control
 * (`c_orm_update_optimistic`).
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_api.h"
#include "c_orm_db.h"
#include "c_orm_sqlite.h"
#include "c_orm_memory.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/** @brief Countdown for simulated malloc failure (`-1` = disabled). */
static int g_pool_oom_countdown = -1;

/**
 * @brief Fault-injecting malloc callback for pool and optimistic update OOM
 * tests.
 * @param size Requested byte count.
 * @param out_ptr Output pointer receiving allocated memory.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY when countdown triggers.
 */
static c_orm_error_t pool_test_malloc(size_t size, void **out_ptr) {
  void *p;
  if (g_pool_oom_countdown >= 0) {
    if (g_pool_oom_countdown == 0) {
      *out_ptr = NULL;
      return C_ORM_ERROR_MEMORY;
    }
    g_pool_oom_countdown--;
  }
  p = malloc(size == 0 ? 1 : size);
  *out_ptr = p;
  return C_ORM_OK;
}

/**
 * @brief Fault-injecting realloc callback for OOM tests.
 * @param ptr Existing pointer.
 * @param size New byte count.
 * @param out_ptr Output pointer receiving reallocated memory.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t pool_test_realloc(void *ptr, size_t size, void **out_ptr) {
  void *p = realloc(ptr, size == 0 ? 1 : size);
  *out_ptr = p;
  return C_ORM_OK;
}

/**
 * @brief Standard free callback for OOM tests.
 * @param ptr Pointer to free.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t pool_test_free(void *ptr) {
  free(ptr);
  return C_ORM_OK;
}

/** @brief Saved real disconnect vtable pointer during mock disconnect test. */
static c_orm_error_t (*g_real_disconnect)(c_orm_db_t *db) = NULL;

/**
 * @brief Mock disconnect callback that cleans up underlying handle and returns
 * an error.
 * @param db Database connection handle.
 * @return Always returns C_ORM_ERROR_CONNECTION.
 */
static c_orm_error_t mock_failing_disconnect(c_orm_db_t *db) {
  c_orm_error_t rc = g_real_disconnect(db);
  return rc != C_ORM_OK ? rc : C_ORM_ERROR_CONNECTION;
}

/**
 * @brief Tests connection pool lifecycle, reuse, timeout, and validation paths.
 * @return Greatest test status.
 */
TEST test_pool_lifecycle_and_validation(void) {
  c_orm_pool_t *pool = NULL;
  c_orm_pool_t *pool_long = NULL;
  c_orm_pool_t *pool_zero = NULL;
  c_orm_db_t *db1 = NULL;
  c_orm_db_t *db2 = NULL;
  c_orm_db_t *db3 = NULL;
  c_orm_db_t *foreign_db = NULL;

  /* Validation error branches */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_pool_init(NULL, 0, 2, 10, &pool));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_pool_init("memory://", 0, 2, 10, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_pool_init("memory://", -1, 2, 10, &pool));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_pool_init("memory://", 0, 0, 10, &pool));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_pool_init("memory://", 3, 2, 10, &pool));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_pool_init("memory://", 0, 2, -1, &pool));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_pool_acquire(NULL, &db1));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_pool_release(NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_pool_destroy(NULL));

  /* Pre-warm failure on invalid URL */
  ASSERT_NEQ(C_ORM_OK, c_orm_pool_init("badscheme://invalid", 1, 2, 0, &pool));
  /* Lazy acquire failure on invalid URL when min_conns == 0 */
  ASSERT_EQ(C_ORM_OK, c_orm_pool_init("badscheme://invalid", 0, 1, 0, &pool));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_pool_acquire(pool, NULL));
  ASSERT_NEQ(C_ORM_OK, c_orm_pool_acquire(pool, &db1));
  ASSERT_EQ(C_ORM_OK, c_orm_pool_destroy(pool));
  pool = NULL;

  /* Normal lifecycle with min_conns = 1, max_conns = 3 (leaves slot 2 NULL) */
  ASSERT_EQ(C_ORM_OK, c_orm_pool_init("memory://", 1, 3, 3, &pool));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_pool_release(pool, NULL));

  /* Acquire pre-warmed slot 0 and lazy-opened slot 1 */
  ASSERT_EQ(C_ORM_OK, c_orm_pool_acquire(pool, &db1));
  ASSERT(db1 != NULL);
  ASSERT_EQ(C_ORM_OK,
            c_orm_execute_raw(db1, "CREATE TABLE t_pool (id INT PRIMARY KEY)"));
  ASSERT_EQ(C_ORM_OK, c_orm_pool_acquire(pool, &db2));
  ASSERT(db2 != NULL);
  ASSERT(db1 != db2);

  /* Release, double-release check, foreign handle check, and reuse */
  ASSERT_EQ(C_ORM_OK, c_orm_pool_release(pool, db1));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_pool_release(pool, db1));

  ASSERT_EQ(C_ORM_OK, c_orm_memory_connect("memory://", &foreign_db));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, c_orm_pool_release(pool, foreign_db));
  ASSERT_EQ(C_ORM_OK, foreign_db->vtable->disconnect(foreign_db));

  ASSERT_EQ(C_ORM_OK, c_orm_pool_acquire(pool, &db3));
  ASSERT(db3 == db1);
  ASSERT_EQ(C_ORM_OK, c_orm_pool_release(pool, db2));
  ASSERT_EQ(C_ORM_OK, c_orm_pool_release(pool, db3));
  /* Destroy while slot 2 is still NULL */
  ASSERT_EQ(C_ORM_OK, c_orm_pool_destroy(pool));

  /* Exhaustion with short timeout <= 5ms and > 5ms and 0ms */
  ASSERT_EQ(C_ORM_OK, c_orm_pool_init("memory://", 1, 1, 3, &pool_zero));
  ASSERT_EQ(C_ORM_OK, c_orm_pool_acquire(pool_zero, &db1));
  ASSERT_EQ(C_ORM_ERROR_TIMEOUT, c_orm_pool_acquire(pool_zero, &db2));
  ASSERT_EQ(C_ORM_OK, c_orm_pool_release(pool_zero, db1));
  ASSERT_EQ(C_ORM_OK, c_orm_pool_destroy(pool_zero));

  ASSERT_EQ(C_ORM_OK, c_orm_pool_init("memory://", 1, 1, 8, &pool_long));
  ASSERT_EQ(C_ORM_OK, c_orm_pool_acquire(pool_long, &db1));
  ASSERT_EQ(C_ORM_ERROR_TIMEOUT, c_orm_pool_acquire(pool_long, &db2));
  ASSERT_EQ(C_ORM_OK, c_orm_pool_release(pool_long, db1));
  ASSERT_EQ(C_ORM_OK, c_orm_pool_destroy(pool_long));

  PASS();
}

/**
 * @brief Tests OOM and mutex/disconnect fault-injection paths on connection
 * pool.
 * @return Greatest test status.
 */
TEST test_pool_fault_injection(void) {
  c_orm_pool_t *pool = NULL;
  c_orm_db_t *db1 = NULL;
  c_orm_db_t *db2 = NULL;
  c_orm_db_t *foreign_db = NULL;
  c_orm_driver_vtable_t custom_vt;
  const c_orm_driver_vtable_t *orig_vt;
  int step;

  ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(pool_test_malloc, pool_test_realloc,
                                           pool_test_free));
  {
    void *tmp = NULL;
    g_pool_oom_countdown = -1;
    ASSERT_EQ(C_ORM_OK, c_orm_system_malloc(0, &tmp));
    ASSERT_EQ(C_ORM_OK, c_orm_system_realloc(tmp, 0, &tmp));
    ASSERT_EQ(C_ORM_OK, c_orm_system_realloc(tmp, 8, &tmp));
    ASSERT_EQ(C_ORM_OK, c_orm_system_free(tmp));
  }
  for (step = 0; step < 6; ++step) {
    g_pool_oom_countdown = step;
    pool = NULL;
    ASSERT_NEQ(C_ORM_OK, c_orm_pool_init("memory://", 1, 2, 0, &pool));
  }
  g_pool_oom_countdown = -1;
  ASSERT_EQ(C_ORM_OK, c_orm_reset_allocators());

  /* Mutex init fail */
  c_orm_mock_cache_init_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_pool_init("memory://", 0, 2, 0, &pool));
  c_orm_mock_cache_init_fail = 0;

  /* Mutex lock & unlock failures across acquire / release / destroy */
  ASSERT_EQ(C_ORM_OK, c_orm_pool_init("memory://", 1, 2, 0, &pool));

  c_orm_mock_cache_lock_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_pool_acquire(pool, &db1));
  c_orm_mock_cache_lock_fail = 0;

  /* Unlock failure on reused idle slot */
  c_orm_mock_cache_unlock_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_pool_acquire(pool, &db1));
  c_orm_mock_cache_unlock_fail = 0;

  /* Acquire slot 0 normally, then unlock failure on newly opened slot 1 */
  ASSERT_EQ(C_ORM_OK, c_orm_pool_acquire(pool, &db1));
  c_orm_mock_cache_unlock_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_pool_acquire(pool, &db2));
  c_orm_mock_cache_unlock_fail = 0;

  /* Slot 1 was connected and rolled back to idle; acquire it now so pool is
   * full */
  ASSERT_EQ(C_ORM_OK, c_orm_pool_acquire(pool, &db2));

  /* Unlock failure when pool is exhausted */
  c_orm_mock_cache_unlock_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_pool_acquire(pool, &foreign_db));
  c_orm_mock_cache_unlock_fail = 0;

  /* Release lock/unlock failure paths */
  c_orm_mock_cache_lock_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_pool_release(pool, db1));
  c_orm_mock_cache_lock_fail = 0;

  c_orm_mock_cache_unlock_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_pool_release(pool, db1));
  c_orm_mock_cache_unlock_fail = 0;

  /* db1 is now idle; double-release with unlock failure */
  c_orm_mock_cache_unlock_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_pool_release(pool, db1));
  c_orm_mock_cache_unlock_fail = 0;

  /* Foreign db release with unlock failure */
  ASSERT_EQ(C_ORM_OK, c_orm_memory_connect("memory://", &foreign_db));
  c_orm_mock_cache_unlock_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_pool_release(pool, foreign_db));
  c_orm_mock_cache_unlock_fail = 0;
  ASSERT_EQ(C_ORM_OK, foreign_db->vtable->disconnect(foreign_db));

  /* Mock disconnect failure and mutex destroy failure in c_orm_pool_destroy */
  orig_vt = db2->vtable;
  custom_vt = *orig_vt;
  g_real_disconnect = orig_vt->disconnect;
  custom_vt.disconnect = mock_failing_disconnect;
  db2->vtable = &custom_vt;

  c_orm_mock_cache_destroy_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_pool_destroy(pool));
  c_orm_mock_cache_destroy_fail = 0;

  /* Test disconnect failure alone without mutex destroy failure */
  ASSERT_EQ(C_ORM_OK, c_orm_pool_init("memory://", 1, 1, 0, &pool));
  ASSERT_EQ(C_ORM_OK, c_orm_pool_acquire(pool, &db1));
  orig_vt = db1->vtable;
  custom_vt = *orig_vt;
  g_real_disconnect = orig_vt->disconnect;
  custom_vt.disconnect = mock_failing_disconnect;
  db1->vtable = &custom_vt;
  ASSERT_EQ(C_ORM_ERROR_CONNECTION, c_orm_pool_destroy(pool));

  PASS();
}

/**
 * @brief Optimistic model with int32 PK and non-nullable int32 version.
 */
typedef struct OptItem32 {
  /** @brief Primary key ID. */
  int32_t id;
  /** @brief Item title string. */
  char *title;
  /** @brief Optimistic lock version counter. */
  int32_t version;
} OptItem32;

static const c_orm_column_meta_t g_opt32_cols[] = {
    {"id", C_ORM_TYPE_INT32, offsetof(OptItem32, id), true, false, NULL, false,
     false, 0, 0, 0, 0},
    {"title", C_ORM_TYPE_STRING, offsetof(OptItem32, title), false, false, NULL,
     false, false, 0, 0, 0, 0},
    {"version", C_ORM_TYPE_INT32, offsetof(OptItem32, version), false, false,
     NULL, false, false, 0, 0, 0, 0}};

static const c_orm_table_meta_t g_opt32_meta = {
    "opt_items32",
    g_opt32_cols,
    3,
    sizeof(OptItem32),
    "SELECT * FROM opt_items32",
    "SELECT * FROM opt_items32 WHERE id = ?",
    "INSERT INTO opt_items32 (id, title, version) VALUES (?, ?, ?)",
    "UPDATE opt_items32 SET title = ?, version = ? WHERE id = ?",
    "DELETE FROM opt_items32 WHERE id = ?",
    NULL,
    false,
    false,
    0,
    0,
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    NULL,
    0};

/**
 * @brief Optimistic model with int64 PK and nullable int32 version pointer.
 */
typedef struct OptItem64Null32 {
  /** @brief 64-bit Primary key ID. */
  int64_t id;
  /** @brief Nullable 32-bit version pointer. */
  int32_t *version;
} OptItem64Null32;

static const c_orm_column_meta_t g_opt64_null32_cols[] = {
    {"id", C_ORM_TYPE_INT64, offsetof(OptItem64Null32, id), true, false, NULL,
     false, false, 0, 0, 0, 0},
    {"version", C_ORM_TYPE_INT32, offsetof(OptItem64Null32, version), false,
     true, NULL, false, false, 0, 0, 0, 0}};

static const c_orm_table_meta_t g_opt64_null32_meta = {
    "opt_items64",
    g_opt64_null32_cols,
    2,
    sizeof(OptItem64Null32),
    "SELECT * FROM opt_items64",
    NULL,
    "INSERT INTO opt_items64 (id, version) VALUES (?, ?)",
    NULL,
    NULL,
    NULL,
    false,
    false,
    0,
    0,
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    NULL,
    0};

/**
 * @brief Optimistic model with string PK and int64 version (non-nullable and
 * nullable metadata variants).
 */
typedef struct OptItemStr64 {
  /** @brief String primary key. */
  char *code;
  /** @brief Non-nullable 64-bit version. */
  int64_t version;
  /** @brief Nullable 64-bit version pointer. */
  int64_t *opt_ver;
} OptItemStr64;

static const c_orm_column_meta_t g_opt_str64_cols[] = {
    {"code", C_ORM_TYPE_STRING, offsetof(OptItemStr64, code), true, false, NULL,
     false, false, 0, 0, 0, 0},
    {"version", C_ORM_TYPE_INT64, offsetof(OptItemStr64, version), false, false,
     NULL, false, false, 0, 0, 0, 0},
    {"opt_ver", C_ORM_TYPE_INT64, offsetof(OptItemStr64, opt_ver), false, true,
     NULL, false, false, 0, 0, 0, 0}};

static const c_orm_table_meta_t g_opt_str64_meta = {
    "opt_str64",
    g_opt_str64_cols,
    3,
    sizeof(OptItemStr64),
    "SELECT * FROM opt_str64",
    NULL,
    "INSERT INTO opt_str64 (code, version, opt_ver) VALUES (?, ?, ?)",
    NULL,
    NULL,
    NULL,
    false,
    false,
    0,
    0,
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    NULL,
    0};

/** @brief Counter tracking hook invocations during optimistic update tests. */
static int g_opt_hook_calls = 0;
/** @brief Which 1-based hook call should fail (`0` = none). */
static int g_opt_hook_fail_on = 0;

/**
 * @brief Lifecycle hook callback for optimistic update testing.
 * @param record Struct pointer.
 * @param db Database connection.
 * @return C_ORM_OK or C_ORM_ERROR_HOOK_FAILED when configured to fail.
 */
static c_orm_error_t opt_test_hook(void *record, void *db) {
  (void)record;
  (void)db;
  g_opt_hook_calls++;
  if (g_opt_hook_fail_on == g_opt_hook_calls) {
    return C_ORM_ERROR_VALIDATION;
  }
  return C_ORM_OK;
}

/**
 * @brief Tests c_orm_update_optimistic end-to-end across int32/int64/string PKs
 * and version columns, mismatch detection, validation, and hooks.
 * @return Greatest test status.
 */
TEST test_optimistic_update_crud_and_validation(void) {
  c_orm_db_t *db = NULL;
  OptItem32 item32;
  OptItem32 fetched32;
  OptItem64Null32 item64;
  OptItemStr64 item_str;
  int32_t v32;
  int64_t v64;
  c_orm_table_meta_t bad_meta;
  c_orm_column_meta_t bad_cols[2];

  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &db));
  ASSERT_EQ(C_ORM_OK,
            c_orm_execute_raw(db, "CREATE TABLE opt_items32 (id INTEGER "
                                  "PRIMARY KEY, title TEXT, version INTEGER)"));
  ASSERT_EQ(C_ORM_OK,
            c_orm_execute_raw(
                db, "CREATE TABLE opt_items64 (id INTEGER PRIMARY KEY, version "
                    "INTEGER)"));
  ASSERT_EQ(C_ORM_OK,
            c_orm_execute_raw(db, "CREATE TABLE opt_str64 (code TEXT PRIMARY "
                                  "KEY, version INTEGER, opt_ver INTEGER)"));

  /* Parameter & metadata validation checks */
  memset(&item32, 0, sizeof(item32));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_update_optimistic(NULL, &g_opt32_meta, "version", &item32));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_update_optimistic(db, NULL, "version", &item32));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_update_optimistic(db, &g_opt32_meta, NULL, &item32));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_update_optimistic(db, &g_opt32_meta, "version", NULL));

  bad_meta = g_opt32_meta;
  bad_meta.is_view = true;
  ASSERT_EQ(C_ORM_ERROR_READ_ONLY,
            c_orm_update_optimistic(db, &bad_meta, "version", &item32));

  bad_meta = g_opt32_meta;
  bad_meta.name = NULL;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_update_optimistic(db, &bad_meta, "version", &item32));

  bad_meta = g_opt32_meta;
  bad_meta.columns = NULL;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_update_optimistic(db, &bad_meta, "version", &item32));

  bad_meta = g_opt32_meta;
  bad_meta.num_columns = 0;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_update_optimistic(db, &bad_meta, "version", &item32));

  /* Missing version column and version_column == PK */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_update_optimistic(db, &g_opt32_meta, "nonexistent", &item32));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_update_optimistic(db, &g_opt32_meta, "id", &item32));
  /* Unsupported version column type (STRING) */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_update_optimistic(db, &g_opt32_meta, "title", &item32));

  /* Missing PK column */
  bad_cols[0] = g_opt32_cols[1];
  bad_cols[1] = g_opt32_cols[2];
  bad_meta = g_opt32_meta;
  bad_meta.columns = bad_cols;
  bad_meta.num_columns = 2;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_update_optimistic(db, &bad_meta, "version", &item32));

  /* Unsupported PK type (FLOAT) */
  bad_cols[0] = g_opt32_cols[0];
  bad_cols[0].type = C_ORM_TYPE_FLOAT;
  bad_cols[1] = g_opt32_cols[2];
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_update_optimistic(db, &bad_meta, "version", &item32));

  /* NULL string PK and NULL nullable version pointers */
  memset(&item_str, 0, sizeof(item_str));
  ASSERT_EQ(
      C_ORM_ERROR_VALIDATION,
      c_orm_update_optimistic(db, &g_opt_str64_meta, "version", &item_str));
  item_str.code = "acct-1";
  ASSERT_EQ(
      C_ORM_ERROR_VALIDATION,
      c_orm_update_optimistic(db, &g_opt_str64_meta, "opt_ver", &item_str));

  memset(&item64, 0, sizeof(item64));
  item64.id = 100;
  item64.version = NULL;
  ASSERT_EQ(
      C_ORM_ERROR_VALIDATION,
      c_orm_update_optimistic(db, &g_opt64_null32_meta, "version", &item64));

  /* 1. OptItem32: insert v1, update to v2, detect conflict on stale v2 */
  item32.id = 1;
  item32.title = "Initial";
  item32.version = 1;
  ASSERT_EQ(C_ORM_OK, c_orm_insert(db, &g_opt32_meta, &item32));

  item32.title = "Updated V2";
  item32.version = 2;
  ASSERT_EQ(C_ORM_OK,
            c_orm_update_optimistic(db, &g_opt32_meta, "version", &item32));

  memset(&fetched32, 0, sizeof(fetched32));
  ASSERT_EQ(C_ORM_OK, c_orm_find_by_id_int32(db, &g_opt32_meta, 1, &fetched32));
  ASSERT_STR_EQ("Updated V2", fetched32.title);
  ASSERT_EQ(2, fetched32.version);
  ASSERT_EQ(C_ORM_OK, c_orm_free_columns(&g_opt32_meta, &fetched32));

  /* Stale update with version = 2 again fails with C_ORM_ERROR_NOT_FOUND */
  item32.title = "Stale Overwrite";
  item32.version = 2;
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_update_optimistic(db, &g_opt32_meta, "version", &item32));

  /* 2. OptItem64Null32: int64 PK + nullable int32* version */
  v32 = 1;
  item64.id = 500;
  item64.version = &v32;
  ASSERT_EQ(C_ORM_OK, c_orm_insert(db, &g_opt64_null32_meta, &item64));
  v32 = 2;
  ASSERT_EQ(C_ORM_OK, c_orm_update_optimistic(db, &g_opt64_null32_meta,
                                              "version", &item64));
  ASSERT_EQ(
      C_ORM_ERROR_NOT_FOUND,
      c_orm_update_optimistic(db, &g_opt64_null32_meta, "version", &item64));

  /* 3. OptItemStr64: string PK + int64 version and nullable int64* version */
  v64 = 10;
  item_str.code = "acct-1";
  item_str.version = 10;
  item_str.opt_ver = &v64;
  ASSERT_EQ(C_ORM_OK, c_orm_insert(db, &g_opt_str64_meta, &item_str));

  item_str.version = 11;
  ASSERT_EQ(C_ORM_OK, c_orm_update_optimistic(db, &g_opt_str64_meta, "version",
                                              &item_str));
  v64 = 11;
  ASSERT_EQ(C_ORM_OK, c_orm_update_optimistic(db, &g_opt_str64_meta, "opt_ver",
                                              &item_str));

  /* 4. Lifecycle hooks (success + failure on each hook stage) */
  bad_meta = g_opt32_meta;
  bad_meta.hooks[C_ORM_HOOK_BEFORE_SAVE] = opt_test_hook;
  bad_meta.hooks[C_ORM_HOOK_BEFORE_UPDATE] = opt_test_hook;
  bad_meta.hooks[C_ORM_HOOK_AFTER_UPDATE] = opt_test_hook;
  bad_meta.hooks[C_ORM_HOOK_AFTER_SAVE] = opt_test_hook;

  g_opt_hook_calls = 0;
  g_opt_hook_fail_on = 1;
  item32.version = 3;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_update_optimistic(db, &bad_meta, "version", &item32));

  g_opt_hook_calls = 0;
  g_opt_hook_fail_on = 2;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_update_optimistic(db, &bad_meta, "version", &item32));

  g_opt_hook_calls = 0;
  g_opt_hook_fail_on = 3;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_update_optimistic(db, &bad_meta, "version", &item32));

  /* DB is now at version 3 because AFTER_UPDATE ran post-step; advance to 4 */
  g_opt_hook_calls = 0;
  g_opt_hook_fail_on = 4;
  item32.version = 4;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_update_optimistic(db, &bad_meta, "version", &item32));

  g_opt_hook_calls = 0;
  g_opt_hook_fail_on = 0;
  item32.version = 5;
  ASSERT_EQ(C_ORM_OK,
            c_orm_update_optimistic(db, &bad_meta, "version", &item32));
  ASSERT_EQ(4, g_opt_hook_calls);

  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));
  PASS();
}

/**
 * @brief Mock state controlling driver failures in optimistic update tests.
 */
typedef struct OptMockState {
  /** @brief Prepare invocation counter. */
  int prepare_call;
  /** @brief 1-based prepare call index to fail (`0` = none). */
  int fail_prepare_on;
  /** @brief Bind invocation counter. */
  int bind_call;
  /** @brief 1-based bind call index to fail (`0` = none). */
  int fail_bind_on;
  /** @brief Step invocation counter. */
  int step_call;
  /** @brief 1-based step call index to fail (`0` = none). */
  int fail_step_on;
  /** @brief Finalize invocation counter. */
  int finalize_call;
  /** @brief 1-based finalize call index to fail (`0` = none). */
  int fail_finalize_on;
  /** @brief Simulated row indicator returned by step. */
  int has_row_val;
} OptMockState;

/** @brief Active mock driver state instance. */
static OptMockState g_opt_mock;

/**
 * @brief Mock prepare callback for optimistic update error-path coverage.
 * @param db Database connection.
 * @param sql SQL string.
 * @param out_query Output statement pointer.
 * @return C_ORM_OK or C_ORM_ERROR_SQL.
 */
static c_orm_error_t opt_mock_prepare(c_orm_db_t *db, const char *sql,
                                      c_orm_query_t **out_query) {
  (void)db;
  (void)sql;
  g_opt_mock.prepare_call++;
  if (g_opt_mock.prepare_call == g_opt_mock.fail_prepare_on) {
    return C_ORM_ERROR_SQL;
  }
  *out_query = (c_orm_query_t *)0x1;
  return C_ORM_OK;
}

/**
 * @brief Mock bind_int32 callback for optimistic update error-path coverage.
 * @param query Query handle.
 * @param index Parameter index.
 * @param val Integer value.
 * @return C_ORM_OK or C_ORM_ERROR_BIND.
 */
static c_orm_error_t opt_mock_bind_int32(c_orm_query_t *query, int index,
                                         int32_t val) {
  (void)query;
  (void)index;
  (void)val;
  g_opt_mock.bind_call++;
  if (g_opt_mock.bind_call == g_opt_mock.fail_bind_on) {
    return C_ORM_ERROR_BIND;
  }
  return C_ORM_OK;
}

/**
 * @brief Mock bind_string callback for optimistic update error-path coverage.
 * @param query Query handle.
 * @param index Parameter index.
 * @param val String value.
 * @return C_ORM_OK or C_ORM_ERROR_BIND.
 */
static c_orm_error_t opt_mock_bind_string(c_orm_query_t *query, int index,
                                          const char *val) {
  (void)query;
  (void)index;
  (void)val;
  g_opt_mock.bind_call++;
  if (g_opt_mock.bind_call == g_opt_mock.fail_bind_on) {
    return C_ORM_ERROR_BIND;
  }
  return C_ORM_OK;
}

/**
 * @brief Mock step callback for optimistic update error-path coverage.
 * @param query Query handle.
 * @param out_has_row Output row indicator.
 * @return C_ORM_OK or C_ORM_ERROR_STEP.
 */
static c_orm_error_t opt_mock_step(c_orm_query_t *query, int *out_has_row) {
  (void)query;
  g_opt_mock.step_call++;
  if (g_opt_mock.step_call == g_opt_mock.fail_step_on) {
    return C_ORM_ERROR_STEP;
  }
  *out_has_row = g_opt_mock.has_row_val;
  return C_ORM_OK;
}

/**
 * @brief Mock finalize callback for optimistic update error-path coverage.
 * @param query Query handle.
 * @return C_ORM_OK or C_ORM_ERROR_SQL.
 */
static c_orm_error_t opt_mock_finalize(c_orm_query_t *query) {
  (void)query;
  g_opt_mock.finalize_call++;
  if (g_opt_mock.finalize_call == g_opt_mock.fail_finalize_on) {
    return C_ORM_ERROR_SQL;
  }
  return C_ORM_OK;
}

/**
 * @brief Tests OOM, Safe CRT, and driver vtable error branches in
 * c_orm_update_optimistic.
 * @return Greatest test status.
 */
TEST test_optimistic_update_fault_injection(void) {
  c_orm_driver_vtable_t vt;
  c_orm_db_t mock_db;
  OptItem32 item;
  int step;

  memset(&vt, 0, sizeof(vt));
  vt.prepare = opt_mock_prepare;
  vt.bind_int32 = opt_mock_bind_int32;
  vt.bind_string = opt_mock_bind_string;
  vt.step = opt_mock_step;
  vt.finalize = opt_mock_finalize;

  memset(&mock_db, 0, sizeof(mock_db));
  mock_db.vtable = &vt;
  mock_db.driver_name = "sqlite";

  item.id = 1;
  item.title = "hello";
  item.version = 2;

  /* 1. OOM countdown across check_sql, set_buf, and upd_sql allocations */
  ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(pool_test_malloc, pool_test_realloc,
                                           pool_test_free));
  for (step = 0; step < 3; ++step) {
    memset(&g_opt_mock, 0, sizeof(g_opt_mock));
    g_opt_mock.has_row_val = 1;
    g_pool_oom_countdown = step;
    ASSERT_EQ(
        C_ORM_ERROR_MEMORY,
        c_orm_update_optimistic(&mock_db, &g_opt32_meta, "version", &item));
  }
  g_pool_oom_countdown = -1;
  ASSERT_EQ(C_ORM_OK, c_orm_reset_allocators());

  /* 2. Safe CRT formatting failure on check_sql (0) and upd_sql (1) */
  memset(&g_opt_mock, 0, sizeof(g_opt_mock));
  g_opt_mock.has_row_val = 1;
  c_orm_mock_safe_crt_countdown = 0;
  ASSERT_NEQ(C_ORM_OK, c_orm_update_optimistic(&mock_db, &g_opt32_meta,
                                               "version", &item));

  memset(&g_opt_mock, 0, sizeof(g_opt_mock));
  g_opt_mock.has_row_val = 1;
  c_orm_mock_safe_crt_countdown = 1;
  ASSERT_NEQ(C_ORM_OK, c_orm_update_optimistic(&mock_db, &g_opt32_meta,
                                               "version", &item));
  c_orm_mock_safe_crt_countdown = -1;

  /* 3. Prepare failures (1 = check_sql, 2 = upd_sql) */
  for (step = 1; step <= 2; ++step) {
    memset(&g_opt_mock, 0, sizeof(g_opt_mock));
    g_opt_mock.has_row_val = 1;
    g_opt_mock.fail_prepare_on = step;
    ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_update_optimistic(&mock_db, &g_opt32_meta,
                                                       "version", &item));
  }

  /* 4. Bind failures (1 = check PK, 2 = check ver, 3 = upd col, 5 = upd PK)
   * both with normal finalize and cascading finalize failure */
  for (step = 1; step <= 6; ++step) {
    memset(&g_opt_mock, 0, sizeof(g_opt_mock));
    g_opt_mock.has_row_val = 1;
    g_opt_mock.fail_bind_on = step;
    ASSERT_EQ(C_ORM_ERROR_BIND, c_orm_update_optimistic(&mock_db, &g_opt32_meta,
                                                        "version", &item));

    memset(&g_opt_mock, 0, sizeof(g_opt_mock));
    g_opt_mock.has_row_val = 1;
    g_opt_mock.fail_bind_on = step;
    g_opt_mock.fail_finalize_on = (step <= 2) ? 1 : 2;
    ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_update_optimistic(&mock_db, &g_opt32_meta,
                                                       "version", &item));
  }

  /* 5. Step failures (1 = check step, 2 = upd step) + cascading finalize fail
   */
  for (step = 1; step <= 2; ++step) {
    memset(&g_opt_mock, 0, sizeof(g_opt_mock));
    g_opt_mock.has_row_val = 1;
    g_opt_mock.fail_step_on = step;
    ASSERT_EQ(C_ORM_ERROR_STEP, c_orm_update_optimistic(&mock_db, &g_opt32_meta,
                                                        "version", &item));

    memset(&g_opt_mock, 0, sizeof(g_opt_mock));
    g_opt_mock.has_row_val = 1;
    g_opt_mock.fail_step_on = step;
    g_opt_mock.fail_finalize_on = step;
    ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_update_optimistic(&mock_db, &g_opt32_meta,
                                                       "version", &item));
  }

  /* 6. Standalone finalize failures (1 = check finalize, 2 = upd finalize) */
  for (step = 1; step <= 2; ++step) {
    memset(&g_opt_mock, 0, sizeof(g_opt_mock));
    g_opt_mock.has_row_val = 1;
    g_opt_mock.fail_finalize_on = step;
    ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_update_optimistic(&mock_db, &g_opt32_meta,
                                                       "version", &item));
  }

  PASS();
}

/**
 * @brief Suite aggregating connection pool and optimistic locking tests.
 * @param pool_optimistic_suite Suite runner function name.
 */
SUITE(pool_optimistic_suite) {
  RUN_TEST(test_pool_lifecycle_and_validation);
  RUN_TEST(test_pool_fault_injection);
  RUN_TEST(test_optimistic_update_crud_and_validation);
  RUN_TEST(test_optimistic_update_fault_injection);
}
