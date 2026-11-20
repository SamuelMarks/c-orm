/**
 * @file test_sqlite_driver.c
 * @brief Unit tests for SQLite database driver vtable, operations, blobs, and
 * errors.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_decimal.h"
#include "c_orm_sqlite.h"
#include "c_orm_api.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>

#undef ASSERT_EQ_FMT
#define ASSERT_EQ_FMT(exp, got, fmt) do { greatest_info.assertions++; if (!((exp) == (got))) { fprintf(stderr, "\nFAIL %s:%d: %s != %s\n", __FILE__, __LINE__, #exp, #got); return GREATEST_TEST_RES_FAIL; } } while ((void)0, 0)
#undef ASSERT_EQ
#define ASSERT_EQ(exp, got) do { greatest_info.assertions++; if (!((exp) == (got))) { fprintf(stderr, "\nFAIL %s:%d: %s != %s\n", __FILE__, __LINE__, #exp, #got); return GREATEST_TEST_RES_FAIL; } } while ((void)0, 0)
#undef ASSERT
#define ASSERT(cond) do { greatest_info.assertions++; if (!((cond) != 0)) { fprintf(stderr, "\nFAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return GREATEST_TEST_RES_FAIL; } } while ((void)0, 0)
#undef ASSERT_STR_EQ
#define ASSERT_STR_EQ(exp, got) do { greatest_info.assertions++; if (strcmp((exp), (got)) != 0) { fprintf(stderr, "\nFAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (exp), (got)); return GREATEST_TEST_RES_FAIL; } } while ((void)0, 0)
#undef ASSERT_NEQ
#define ASSERT_NEQ(exp, got) do { greatest_info.assertions++; if (!((exp) != (got))) { fprintf(stderr, "\nFAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #exp, #got); return GREATEST_TEST_RES_FAIL; } } while ((void)0, 0)
#undef RUN_TEST
#define RUN_TEST(TEST) do { int should = 0; enum greatest_test_res res; greatest_test_pre(#TEST, &should); res = TEST(); greatest_test_post(res); } while((void)0, 0)
#undef CHECK_CALL
#define CHECK_CALL(res) do { do { enum greatest_test_res _rc = (res); greatest_info.assertions++; if (_rc != GREATEST_TEST_RES_PASS) { return _rc; } } while ((void)0, 0); } while ((void)0, 0)

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* clang-format on */

/**
 * @brief Executes test_c_orm_sqlite_blob_open_errors.
 */
TEST test_c_orm_sqlite_blob_open_errors(void) {
  void *handle;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_sqlite_blob_open(NULL, "db", "table", "col", 1, 0, &handle));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_sqlite_blob_open(NULL, "db", "table", "col", 1, 0, NULL));
  PASS();
}

/**
 * @brief Executes test_c_orm_sqlite_blob_read_errors.
 */
TEST test_c_orm_sqlite_blob_read_errors(void) {
  char buf[10] = {0};
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_sqlite_blob_read(NULL, buf, 10, 0));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_sqlite_blob_read(NULL, NULL, 10, 0));
  PASS();
}

/**
 * @brief Executes test_c_orm_sqlite_blob_write_errors.
 */
TEST test_c_orm_sqlite_blob_write_errors(void) {
  char buf[10] = {0};
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_sqlite_blob_write(NULL, buf, 10, 0));
  PASS();
}

/**
 * @brief Executes test_c_orm_sqlite_blob_operations.
 */
TEST test_c_orm_sqlite_blob_operations(void) {
#ifdef C_ORM_ENABLE_SQLITE
  c_orm_db_t *db = NULL;
  c_orm_query_t *query = NULL;
  void *blob_handle = NULL;
  char write_buf[] = "test";
  char read_buf[5] = {0};
  int has_row = 0;

  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &db));
  ASSERT_EQ(C_ORM_OK,
            db->vtable->prepare(
                db, "CREATE TABLE test_blobs (id INTEGER PRIMARY KEY, b BLOB);",
                &query));
  ASSERT_EQ(C_ORM_OK, db->vtable->step(query, &has_row));
  ASSERT_EQ(C_ORM_OK, db->vtable->finalize(query));
  query = NULL;

  ASSERT_EQ(C_ORM_OK,
            db->vtable->prepare(
                db, "INSERT INTO test_blobs (id, b) VALUES (1, zeroblob(10));",
                &query));
  ASSERT_EQ(C_ORM_OK, db->vtable->step(query, &has_row));
  ASSERT_EQ(C_ORM_OK, db->vtable->finalize(query));

  /* Open blob with explicit main */
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_blob_open(db, "main", "test_blobs", "b", 1,
                                             1, &blob_handle));
  ASSERT(blob_handle != NULL);

  /* Test NULL buffer */
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_sqlite_blob_read(blob_handle, NULL, 4, 0));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_sqlite_blob_write(blob_handle, NULL, 4, 0));

  /* Write blob */
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_blob_write(blob_handle, write_buf, 4, 0));

  /* Read blob */
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_blob_read(blob_handle, read_buf, 4, 0));
  ASSERT_STR_EQ(write_buf, read_buf);

  /* Errors with valid blob_handle */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_sqlite_blob_write(blob_handle, write_buf, 1000, 0));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_sqlite_blob_read(blob_handle, read_buf, 1000, 0));

  /* Close blob */
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_blob_close(blob_handle));

  /* Blob open error */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_sqlite_blob_open(db, "main", "nonexistent_table", "b", 1, 1,
                                   &blob_handle));

  /* Open blob with NULL db_name to cover branch */
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_blob_open(db, NULL, "test_blobs", "b", 1, 1,
                                             &blob_handle));
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_blob_close(blob_handle));

  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));
#endif
  PASS();
}

/**
 * @brief Executes test_c_orm_sqlite_blob_close_errors.
 */
TEST test_c_orm_sqlite_blob_close_errors(void) {
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_sqlite_blob_close(NULL));
  PASS();
}

/**
 * @brief Executes test_c_orm_sqlite_connect_errors.
 */
TEST test_c_orm_sqlite_connect_errors(void) {
  c_orm_db_t *db = NULL;
#ifdef C_ORM_ENABLE_SQLITE
  const char *str;
  void *orig_driver_data = NULL;
#endif
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_sqlite_connect(NULL, &db));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_sqlite_connect(":memory:", NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_sqlite_connect(NULL, NULL));

#ifdef C_ORM_ENABLE_SQLITE
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &db));

  orig_driver_data = db->driver_data;

  /* Trigger error branch in sqlite_get_last_error */
  db->driver_data = NULL;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, db->vtable->get_last_error(db, &str));
  db->driver_data = orig_driver_data;

  /* Null args to get_last_error */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, db->vtable->get_last_error(db, NULL));

  /* Valid call to get_last_error */
  ASSERT_EQ(C_ORM_OK, db->vtable->get_last_error(db, &str));

  /* Trigger set_error with long string */
  {
    c_orm_query_t *query = NULL;
    char long_sql[1024];
    memset(long_sql, 'A', 600);
    long_sql[600] = '\0';
    db->vtable->prepare(db, long_sql, &query); /* will fail */
  }

  /* Test disconnect when driver_data is NULL by creating a dummy db */
  {
    c_orm_db_t *db2 = NULL;
    ASSERT_EQ(C_ORM_OK,
              c_orm_system_calloc(1, sizeof(c_orm_db_t), (void **)&db2));
    db2->driver_data = NULL;
    db2->vtable = db->vtable;
    ASSERT_EQ(C_ORM_OK, db2->vtable->disconnect(db2)); /* Will free db2 */
  }

  /* Properly disconnect the original db to avoid leaking sqlite connection */
  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));
#endif

  PASS();
}

/**
 * @brief Executes test_c_orm_sqlite_operations_errors.
 */
TEST test_c_orm_sqlite_operations_errors(void) {
#ifdef C_ORM_ENABLE_SQLITE
  const c_orm_driver_vtable_t *vtable = NULL;
  c_orm_db_t *db = NULL;
  c_orm_query_t *query = NULL;
  int has_row = 0;
  int32_t i32;
  int64_t i64;
  double dbl;
  const char *str;
  const void *blob;
  void *rw_blob = NULL;
  size_t size;
  int is_null;
  int col_count = 0;

  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_get_vtable(&vtable));

  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &db));

  /* Test missing branches for blob methods */
  {
    void *orig_driver_data = db->driver_data;
    db->driver_data = NULL;
    ASSERT_EQ(
        C_ORM_ERROR_MEMORY,
        c_orm_sqlite_blob_open(db, "main", "test_blobs", "b", 1, 0, &rw_blob));
    db->driver_data = orig_driver_data;

    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              c_orm_sqlite_blob_open(db, "main", NULL, "b", 1, 0, &rw_blob));
    ASSERT_EQ(
        C_ORM_ERROR_MEMORY,
        c_orm_sqlite_blob_open(db, "main", "test_blobs", NULL, 1, 0, &rw_blob));
    ASSERT_EQ(
        C_ORM_ERROR_MEMORY,
        c_orm_sqlite_blob_open(db, "main", "test_blobs", "b", 1, 0, NULL));

    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_sqlite_blob_read(rw_blob, NULL, 5, 0));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_sqlite_blob_write(rw_blob, NULL, 5, 0));
  }

  /* NULL db argument for operations */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->prepare(NULL, "SELECT 1", &query));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->prepare(db, NULL, &query));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->prepare(db, "SELECT 1", NULL));

  /* Test NULL query->data and NULL query->data->stmt */
  ASSERT_EQ(C_ORM_OK, vtable->disconnect(NULL));

  /* Test NULL query */
  ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_int32(NULL, 1, 1));
  ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_int64(NULL, 1, 1));
  ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_double(NULL, 1, 1.0));
  ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_string(NULL, 1, "test"));
  ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_blob(NULL, 1, "test", 4));
  ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_null(NULL, 1));
  ASSERT_EQ(C_ORM_ERROR_STEP, vtable->step(NULL, &has_row));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_int32(NULL, 0, &i32));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_int64(NULL, 0, &i64));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_double(NULL, 0, &dbl));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_string(NULL, 0, &str));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_blob(NULL, 0, &blob, &size));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->is_null(NULL, 0, &is_null));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_column_count(NULL, &col_count));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_column_name(NULL, 0, &str));
  ASSERT_EQ(C_ORM_OK, vtable->finalize(NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->reset(NULL));

  /* Test NULL query->data and NULL query->data->stmt */
  {
    void *q_data_ptr = NULL;

    ASSERT_EQ(C_ORM_OK, vtable->prepare(db, "SELECT 1", &query));

    q_data_ptr = *(void **)query;
    *(void **)query = NULL;
    ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_int32(query, 1, 1));
    ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_int64(query, 1, 1));
    ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_double(query, 1, 1.0));
    ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_string(query, 1, "test"));
    ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_blob(query, 1, "test", 4));
    ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_null(query, 1));
    ASSERT_EQ(C_ORM_ERROR_STEP, vtable->step(query, &has_row));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_int32(query, 0, &i32));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_int64(query, 0, &i64));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_double(query, 0, &dbl));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_string(query, 0, &str));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_blob(query, 0, &blob, &size));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->is_null(query, 0, &is_null));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_column_count(query, &col_count));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_column_name(query, 0, &str));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->reset(query));

    *(void **)query = q_data_ptr;
    {
      void *orig_stmt = *(void **)q_data_ptr;
      *(void **)q_data_ptr = NULL;
      ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_int32(query, 1, 1));
      ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_int64(query, 1, 1));
      ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_double(query, 1, 1.0));
      ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_string(query, 1, "test"));
      ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_blob(query, 1, "test", 4));
      ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_null(query, 1));
      ASSERT_EQ(C_ORM_ERROR_STEP, vtable->step(query, &has_row));
      ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_int32(query, 0, &i32));
      ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_int64(query, 0, &i64));
      ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_double(query, 0, &dbl));
      ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_string(query, 0, &str));
      ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_blob(query, 0, &blob, &size));
      ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->is_null(query, 0, &is_null));
      ASSERT_EQ(C_ORM_ERROR_MEMORY,
                vtable->get_column_count(query, &col_count));
      ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_column_name(query, 0, &str));
      ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->reset(query));

      *(void **)q_data_ptr = orig_stmt;
    }

    ASSERT_EQ(C_ORM_OK, vtable->finalize(query));

    /* Test finalize with NULL data to cover the branch */
    {
      c_orm_query_t *dummy = NULL;
      ASSERT_EQ(C_ORM_OK, c_orm_system_malloc(sizeof(void *), (void **)&dummy));
      *(void **)dummy = NULL;
      ASSERT_EQ(C_ORM_OK, vtable->finalize(dummy));
    }
  }

  /* Test NULL DB or args on db-level methods */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, vtable->get_last_error(db, NULL));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, vtable->get_last_error(NULL, &str));
  ASSERT_EQ(C_ORM_OK, vtable->get_last_trace(db, NULL));
  ASSERT_EQ(C_ORM_OK, vtable->get_last_trace(db, &str));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_last_insert_rowid(db, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_last_insert_rowid(NULL, &i64));

  /* Missing branch for invalid db object but non-null db */
  {
    void *orig_driver_data = db->driver_data;
    db->driver_data = NULL;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_last_insert_rowid(db, &i64));
    db->driver_data = orig_driver_data;
  }

  /* Test valid query but invalid output pointers */
  ASSERT_EQ(C_ORM_OK, vtable->prepare(db, "SELECT 1", &query));
  ASSERT_EQ(C_ORM_ERROR_STEP, vtable->step(query, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_int32(query, 0, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_int64(query, 0, NULL));

  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_double(query, 0, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_string(query, 0, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_blob(query, 0, NULL, &size));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_blob(query, 0, &blob, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_column_name(query, 0, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->get_column_count(query, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vtable->is_null(query, 0, NULL));

  /* Test out of bounds index to trigger bind errors */
  ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_int32(query, 999, 1));
  ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_int64(query, 999, 1));
  ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_double(query, 999, 1.0));
  ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_string(query, 999, "test"));
  ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_blob(query, 999, "test", 4));
  ASSERT_EQ(C_ORM_ERROR_BIND, vtable->bind_null(query, 999));

  ASSERT_EQ(C_ORM_OK, vtable->finalize(query));

  /* Test type mismatch errors */
  ASSERT_EQ(C_ORM_OK,
            vtable->prepare(db, "SELECT 'string', 1, 1.0, NULL", &query));
  ASSERT_EQ(C_ORM_OK, vtable->step(query, &has_row));
  ASSERT(has_row == 1);

  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vtable->get_int32(query, 0, &i32));
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vtable->get_int64(query, 0, &i64));
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vtable->get_double(query, 0, &dbl));
  ASSERT_EQ(C_ORM_OK,
            vtable->get_double(query, 1, &dbl)); /* Widens SQLITE_INTEGER 1 */
  ASSERT_EQ(1.0, dbl);
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vtable->get_string(query, 1, &str));
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH,
            vtable->get_blob(query, 1, &blob, &size));

  ASSERT_EQ(C_ORM_OK, vtable->get_int32(query, 3, &i32));
  ASSERT_EQ(0, i32);
  ASSERT_EQ(C_ORM_OK, vtable->get_int64(query, 3, &i64));
  ASSERT_EQ(0, i64);
  ASSERT_EQ(C_ORM_OK, vtable->get_double(query, 3, &dbl));
  ASSERT_EQ(0.0, dbl);
  ASSERT_EQ(C_ORM_OK, vtable->get_string(query, 3, &str));
  ASSERT_EQ(NULL, str);
  ASSERT_EQ(C_ORM_OK, vtable->get_blob(query, 3, &blob, &size));
  ASSERT_EQ(NULL, blob);
  ASSERT_EQ(0, size);

  ASSERT_EQ(C_ORM_OK, vtable->is_null(query, 3, &is_null));
  ASSERT_EQ(1, is_null);
  ASSERT_EQ(C_ORM_OK, vtable->is_null(query, 1, &is_null));
  ASSERT_EQ(0, is_null);

  ASSERT_EQ(C_ORM_OK, vtable->finalize(query));
  ASSERT_EQ(C_ORM_OK, vtable->disconnect(db));
#endif
  PASS();
}

/**
 * @brief Executes test_sqlite_vtable.
 */
TEST test_sqlite_vtable(void) {
  const c_orm_driver_vtable_t *vtable = NULL;
  c_orm_error_t err;

  err = c_orm_sqlite_get_vtable(&vtable);
#ifdef C_ORM_ENABLE_SQLITE
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT(vtable != NULL);
  ASSERT(vtable->connect != NULL);
  ASSERT(vtable->disconnect != NULL);
  ASSERT(vtable->prepare != NULL);
  ASSERT(vtable->bind_int32 != NULL);
  ASSERT(vtable->bind_int64 != NULL);
  ASSERT(vtable->bind_double != NULL);
  ASSERT(vtable->bind_string != NULL);
  ASSERT(vtable->bind_blob != NULL);
  ASSERT(vtable->bind_null != NULL);
  ASSERT(vtable->step != NULL);
  ASSERT(vtable->get_int32 != NULL);
  ASSERT(vtable->get_int64 != NULL);
  ASSERT(vtable->get_double != NULL);
  ASSERT(vtable->get_string != NULL);
  ASSERT(vtable->get_blob != NULL);
  ASSERT(vtable->get_column_name != NULL);
  ASSERT(vtable->finalize != NULL);
  ASSERT(vtable->get_last_insert_rowid != NULL);
  ASSERT(vtable->get_last_error != NULL);

  /* Call get_vtable with NULL */
  err = c_orm_sqlite_get_vtable(NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, err);
#else
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);
  ASSERT(vtable == NULL);

  err = c_orm_sqlite_get_vtable(NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);
#endif
  PASS();
}

/**
 * @brief Dummy query log callback returning C_ORM_OK.
 * @param msg Log message.
 * @param user_data Opaque user data.
 * @return C_ORM_OK.
 */
static c_orm_error_t dummy_log_cb(const char *msg, void *user_data) {
  (void)msg;
  (void)user_data;
  return C_ORM_OK;
}

/**
 * @brief Executes test_c_orm_sqlite_slow_query.
 */
TEST test_c_orm_sqlite_slow_query(void) {
#ifdef C_ORM_ENABLE_SQLITE
  c_orm_db_t *db = NULL;
  c_orm_query_t *query = NULL;
  int has_row = 0;
  c_orm_pool_telemetry_t tel;

  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &db));

  /* Configure slow query logging with threshold 1 */
  c_orm_set_slow_query_threshold(db, 1);
  c_orm_set_log_callback(db, dummy_log_cb, NULL);

  /* Execute a slow query to exceed 1ms */
  ASSERT_EQ(C_ORM_OK, db->vtable->prepare(
                          db,
                          "WITH RECURSIVE r(i) AS (VALUES(0) UNION ALL SELECT "
                          "i+1 FROM r LIMIT 200000) SELECT MAX(i) FROM r;",
                          &query));
  ASSERT_EQ(C_ORM_OK, db->vtable->step(query, &has_row));
  ASSERT_EQ(C_ORM_OK, db->vtable->finalize(query));

  /* Test when log_cb is NULL but threshold is met to cover branch */
  c_orm_set_log_callback(db, NULL, NULL);
  ASSERT_EQ(C_ORM_OK, db->vtable->prepare(
                          db,
                          "WITH RECURSIVE r(i) AS (VALUES(0) UNION ALL SELECT "
                          "i+1 FROM r LIMIT 200000) SELECT MAX(i) FROM r;",
                          &query));
  ASSERT_EQ(C_ORM_OK, db->vtable->step(query, &has_row));
  ASSERT_EQ(C_ORM_OK, db->vtable->finalize(query));

  ASSERT_EQ(C_ORM_OK, c_orm_get_telemetry(db, &tel));
  ASSERT(tel.slow_queries_logged >= 2);

  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));
#endif
  PASS();
}

/** @brief Bitmask controlling which sqlite_test_free calls fail. */
static unsigned int sqlite_free_fail_mask = 0;

/** @brief Call index counter for sqlite_test_free when masked. */
static unsigned int sqlite_free_call_idx = 0;

/**
 * @brief Mock free callback with optional masked error return.
 * @param ptr Pointer to free.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY when masked.
 */
static c_orm_error_t sqlite_test_free(void *ptr) {
  free(ptr);
  if (sqlite_free_fail_mask != 0) {
    unsigned int idx = sqlite_free_call_idx++;
    if (idx < 31 && (sqlite_free_fail_mask & (1u << idx)) != 0) {
      return C_ORM_ERROR_MEMORY;
    }
  }
  return C_ORM_OK;
}

/** @brief Countdown for failing_log_cb. */
static int g_log_fail_countdown = -1;

/**
 * @brief Log callback that fails when g_log_fail_countdown reaches 0.
 * @param msg Log message.
 * @param user_data Opaque user data.
 * @return C_ORM_OK or C_ORM_ERROR_UNKNOWN.
 */
static c_orm_error_t failing_log_cb(const char *msg, void *user_data) {
  (void)msg;
  (void)user_data;
  if (g_log_fail_countdown >= 0) {
    if (g_log_fail_countdown == 0) {
      g_log_fail_countdown = -1;
      return C_ORM_ERROR_UNKNOWN;
    }
    g_log_fail_countdown--;
  }
  return C_ORM_OK;
}

/**
 * @brief Exercises SQLite decimal binding/extraction, log callback errors,
 * slow-query Safe CRT failures, and disconnect/finalize free error paths.
 * @return GREATEST test result.
 */
TEST test_c_orm_sqlite_decimal_and_error_branches(void) {
#ifdef C_ORM_ENABLE_SQLITE
  c_orm_malloc_fn_t old_m = c_orm_malloc;
  c_orm_realloc_fn_t old_r = c_orm_realloc;
  c_orm_free_fn_t old_f = c_orm_free;
  c_orm_db_t *db = NULL;
  c_orm_query_t *q = NULL;
  c_orm_decimal_t dec_in;
  c_orm_decimal_t dec_out;
  c_orm_decimal_t bad_dec;
  int has_row = 0;

  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("-98765.4321", &dec_in));
  memset(&bad_dec, 0, sizeof(bad_dec));
  bad_dec.precision = 0;

  c_orm_set_allocators(old_m, old_r, sqlite_test_free);

  /* 1. Prepare log_cb error + slow-query sprintf & log_cb errors */
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &db));
  c_orm_set_log_callback(db, failing_log_cb, NULL);
  g_log_fail_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, db->vtable->prepare(db, "SELECT 1", &q));

  c_orm_set_slow_query_threshold(db, 1);
  /* Prepare succeeds (countdown=1 -> 0), then step slow query log_cb fails */
  g_log_fail_countdown = 1;
  ASSERT_EQ(C_ORM_OK, db->vtable->prepare(
                          db,
                          "WITH RECURSIVE r(i) AS (VALUES(0) UNION ALL SELECT "
                          "i+1 FROM r LIMIT 200000) SELECT MAX(i) FROM r;",
                          &q));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, db->vtable->step(q, &has_row));
  ASSERT_EQ(C_ORM_OK, db->vtable->finalize(q));

  /* Slow query C_ORM_SPRINTF failure */
  g_log_fail_countdown = -1;
  ASSERT_EQ(C_ORM_OK, db->vtable->prepare(
                          db,
                          "WITH RECURSIVE r(i) AS (VALUES(0) UNION ALL SELECT "
                          "i+1 FROM r LIMIT 200000) SELECT MAX(i) FROM r;",
                          &q));
  c_orm_mock_safe_crt_countdown = 0;
  ASSERT_NEQ(C_ORM_OK, db->vtable->step(q, &has_row));
  c_orm_mock_safe_crt_countdown = -1;
  ASSERT_EQ(C_ORM_OK, db->vtable->finalize(q));
  c_orm_set_slow_query_threshold(db, 0);
  c_orm_set_log_callback(db, NULL, NULL);

  /* 2. Decimal bind & get across NULL, INTEGER, BLOB, FLOAT, TEXT, and errors
   */
  ASSERT_EQ(C_ORM_OK,
            db->vtable->prepare(db, "SELECT ?, ?, 42, X'DEADBEEF', 3.5", &q));
  ASSERT_EQ(C_ORM_OK, db->vtable->bind_decimal(q, 1, NULL));
  ASSERT_NEQ(C_ORM_OK, db->vtable->bind_decimal(q, 2, &bad_dec));
  ASSERT_EQ(C_ORM_OK, db->vtable->bind_decimal(q, 2, &dec_in));
  ASSERT_EQ(C_ORM_OK, db->vtable->step(q, &has_row));
  ASSERT_EQ(1, has_row);

  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, db->vtable->get_decimal(q, 0, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, db->vtable->get_decimal(NULL, 0, &dec_out));
  {
    void *fake_stmt_null_data[1];
    void *fake_data_null_stmt[2];
    void *fake_stmt_with_data[1];
    fake_stmt_null_data[0] = NULL;
    fake_data_null_stmt[0] = NULL;
    fake_data_null_stmt[1] = NULL;
    fake_stmt_with_data[0] = fake_data_null_stmt;
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              db->vtable->get_decimal((c_orm_query_t *)fake_stmt_null_data, 0,
                                      &dec_out));
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              db->vtable->get_decimal((c_orm_query_t *)fake_stmt_with_data, 0,
                                      &dec_out));
  }
  ASSERT_EQ(C_ORM_OK, db->vtable->get_decimal(q, 0, &dec_out));
  ASSERT_EQ(C_ORM_OK, db->vtable->get_decimal(q, 1, &dec_out));
  ASSERT_EQ(C_ORM_OK, db->vtable->get_decimal(q, 2, &dec_out));
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, db->vtable->get_decimal(q, 3, &dec_out));
  ASSERT_EQ(C_ORM_OK, db->vtable->get_decimal(q, 4, &dec_out));

  /* finalize free error branches (mask=1: data fails; mask=2: query fails;
   * mask=3: both fail) */
  sqlite_free_call_idx = 0;
  sqlite_free_fail_mask = 2u;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, db->vtable->finalize(q));
  sqlite_free_fail_mask = 0;

  ASSERT_EQ(C_ORM_OK, db->vtable->prepare(db, "SELECT 1", &q));
  sqlite_free_call_idx = 0;
  sqlite_free_fail_mask = 3u;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, db->vtable->finalize(q));
  sqlite_free_fail_mask = 0;

  /* 3. Disconnect error paths: caching disable failure + free(data)/free(db)
   * failures */
  ASSERT_EQ(C_ORM_OK, c_orm_enable_statement_caching(db, 4));
  ASSERT_EQ(C_ORM_OK, c_orm_prepare_cached(db, "SELECT 1", &q));
  ASSERT_EQ(C_ORM_OK, c_orm_finalize_cached(db, q));
  /* In disable_statement_caching:
   * idx 0 = sqlite_finalize(data), idx 1 = sqlite_finalize(query) -> fails!
   * Then sqlite_disconnect continues and frees data & db */
  sqlite_free_call_idx = 0;
  sqlite_free_fail_mask = 2u;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, db->vtable->disconnect(db));
  sqlite_free_fail_mask = 0;

  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &db));
  ASSERT_EQ(C_ORM_OK, c_orm_enable_statement_caching(db, 4));
  ASSERT_EQ(C_ORM_OK, c_orm_prepare_cached(db, "SELECT 1", &q));
  ASSERT_EQ(C_ORM_OK, c_orm_finalize_cached(db, q));
  sqlite_free_call_idx = 0;
  sqlite_free_fail_mask = 6u;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, db->vtable->disconnect(db));
  sqlite_free_fail_mask = 0;

  /* Disconnect where disable_statement_caching succeeds, free(data) fails */
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &db));
  sqlite_free_call_idx = 0;
  sqlite_free_fail_mask = 1u;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, db->vtable->disconnect(db));
  sqlite_free_fail_mask = 0;

  /* Disconnect where free(data) succeeds, free(db) fails */
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &db));
  sqlite_free_call_idx = 0;
  sqlite_free_fail_mask = 2u;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, db->vtable->disconnect(db));
  sqlite_free_fail_mask = 0;

  /* Disconnect where both free(data) and free(db) fail */
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &db));
  sqlite_free_call_idx = 0;
  sqlite_free_fail_mask = 3u;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, db->vtable->disconnect(db));
  sqlite_free_fail_mask = 0;

  c_orm_set_allocators(old_m, old_r, old_f);
#endif
  PASS();
}

/**
 * @brief Test suite runner for SQLite driver tests.
 * @param sqlite_driver_suite Suite runner function name.
 * @return GREATEST suite result.
 */
SUITE(sqlite_driver_suite) {
  RUN_TEST(test_sqlite_vtable);
  RUN_TEST(test_c_orm_sqlite_connect_errors);
  RUN_TEST(test_c_orm_sqlite_operations_errors);
  RUN_TEST(test_c_orm_sqlite_slow_query);
  RUN_TEST(test_c_orm_sqlite_decimal_and_error_branches);
  RUN_TEST(test_c_orm_sqlite_blob_open_errors);
  RUN_TEST(test_c_orm_sqlite_blob_operations);
  RUN_TEST(test_c_orm_sqlite_blob_read_errors);
  RUN_TEST(test_c_orm_sqlite_blob_write_errors);
  RUN_TEST(test_c_orm_sqlite_blob_close_errors);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
