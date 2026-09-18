#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_sqlite_driver.c
 * @brief Unit tests for SQLite database driver vtable, operations, blobs, and
 * errors.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_sqlite.h"
#include "greatest.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/* clang-format on */

/** @brief Countdown counter for simulated memory allocation failures. */
static int oom_countdown = -1;

/** @brief Flag indicating whether OOM simulation is active. */
static int oom_active = 0;

/**
 * @brief Mock malloc allocator with countdown fault injection.
 * @param size Allocation size in bytes.
 * @return Allocated memory block or NULL on simulated failure.
 */
static void *mock_malloc(size_t size) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      return NULL;
    }
    if (oom_countdown > 0) {
      oom_countdown--;
    }
  }
  return malloc(size);
}

/**
 * @brief Mock free deallocator.
 * @param ptr Pointer to memory block to free.
 */
static void mock_free(void *ptr) { free(ptr); }

/**
 * @brief Dummy log callback for testing logger invocation.
 * @param msg Log message string.
 * @param user_data User-defined callback context pointer.
 */
static void my_log_cb(const char *msg, void *user_data) {
  (void)msg;
  (void)user_data;
}

/**
 * @brief Forward declaration for test_sqlite_edge_cases.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_sqlite_edge_cases(void);

/**
 * @brief Tests SQLite driver edge cases, NULL parameter handling, and error
 * paths.
 * @return GREATEST test result.
 */
TEST test_sqlite_edge_cases(void) {
  c_orm_db_t *db;
  const c_orm_driver_vtable_t *vt;
  c_orm_query_t *q;
  c_orm_error_t err;

  db = NULL;
  vt = NULL;
  q = NULL;

  /* get_vtable NULL */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_sqlite_get_vtable(NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_get_vtable(&vt));
  ASSERT(vt != NULL);

  /* Connect NULLs */
  err = c_orm_sqlite_connect(NULL, &db);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  err = c_orm_sqlite_connect(":memory:", NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  /* Disconnect NULL */
  err = vt->disconnect(NULL);
  ASSERT_EQ(C_ORM_OK, err);

  /* Connect invalid URL - normal memory open */
  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT(db != NULL);

  /* Vtable coverage with NULLs */
  err = vt->prepare(NULL, "SELECT 1", NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  err = vt->bind_int32(NULL, 1, 1);
  ASSERT_EQ(C_ORM_ERROR_BIND, err);
  err = vt->step(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_STEP, err);
  err = vt->get_int32(NULL, 0, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  err = vt->get_int64(NULL, 0, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  err = vt->get_double(NULL, 0, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  err = vt->get_string(NULL, 0, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  err = vt->get_blob(NULL, 0, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  err = vt->is_null(NULL, 0, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  err = vt->reset(NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  err = vt->finalize(NULL);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->get_last_insert_rowid(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  err = vt->get_column_count(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  err = vt->get_column_name(NULL, 0, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  /* Blob API */
  err = c_orm_sqlite_blob_open(NULL, NULL, NULL, NULL, 0, 0, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  err = c_orm_sqlite_blob_read(NULL, NULL, 0, 0);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  err = c_orm_sqlite_blob_write(NULL, NULL, 0, 0);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  err = c_orm_sqlite_blob_close(NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  err = vt->disconnect(db);
  ASSERT_EQ(C_ORM_OK, err);

  /* Test OOM in connect */
  oom_active = 1;
  oom_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_sqlite_connect(":memory:", &db));
  oom_countdown = 1;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_sqlite_connect(":memory:", &db));
  oom_active = 0;

  /* Connect normally */
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &db));

  /* Log cb to trigger slow queries and set error */
  db->slow_query_threshold_ms = 1; /* 1ms */
  db->log_cb = my_log_cb;

  /* OOM in prepare */
  oom_active = 1;
  oom_countdown = 0;
  ASSERT_EQ(
      C_ORM_ERROR_MEMORY,
      vt->prepare(
          db, "CREATE TABLE t (id INTEGER, name TEXT, val REAL, b BLOB)", &q));
  oom_countdown = 1;
  ASSERT_EQ(
      C_ORM_ERROR_MEMORY,
      vt->prepare(
          db, "CREATE TABLE t (id INTEGER, name TEXT, val REAL, b BLOB)", &q));
  oom_active = 0;

  /* Prepare normally */
  ASSERT_EQ(
      C_ORM_OK,
      vt->prepare(
          db, "CREATE TABLE t (id INTEGER, name TEXT, val REAL, b BLOB)", &q));

  /* Trigger error set by failing execution */
  {
    int has_row;
    ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  }
  err = vt->finalize(q);
  ASSERT_EQ(C_ORM_OK, err);

  /* Slow query */
  err = vt->prepare(
      db,
      "WITH RECURSIVE cnt(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM "
      "cnt WHERE x<100000) SELECT * FROM cnt;",
      &q);
  ASSERT_EQ(C_ORM_OK, err);
  {
    int has_row;
    err = vt->step(q, &has_row);
    ASSERT_EQ(C_ORM_OK, err);
  }
  err = vt->finalize(q);
  ASSERT_EQ(C_ORM_OK, err);

  /* Set up data */
  err =
      vt->prepare(db, "INSERT INTO t VALUES (1, 'test', 2.5, x'deadbeef')", &q);
  ASSERT_EQ(C_ORM_OK, err);
  {
    int has_row;
    err = vt->step(q, &has_row);
    ASSERT_EQ(C_ORM_OK, err);
  }
  err = vt->finalize(q);
  ASSERT_EQ(C_ORM_OK, err);

  /* Prepare select */
  err = vt->prepare(db, "SELECT * FROM t", &q);
  ASSERT_EQ(C_ORM_OK, err);
  {
    int has_row;
    int32_t my_i32;
    int64_t my_i64;
    double my_d;
    const char *my_s;
    const void *my_b;
    size_t my_sz;
    const char *my_cname;

    err = vt->step(q, &has_row);
    ASSERT_EQ(C_ORM_OK, err);

    /* Type mismatch tests */
    ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH,
              vt->get_int32(q, 1, &my_i32)); /* col 1 is text */
    ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vt->get_int64(q, 1, &my_i64));
    ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vt->get_double(q, 1, &my_d));
    ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH,
              vt->get_string(q, 0, &my_s)); /* col 0 is int */
    ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vt->get_blob(q, 0, &my_b, &my_sz));

    /* Get column name coverage */
    err = vt->get_column_name(q, 0, &my_cname);
    ASSERT_EQ(C_ORM_OK, err);
  }
  err = vt->finalize(q);
  ASSERT_EQ(C_ORM_OK, err);

  /* Force a huge error message to trigger clipping */
  {
    char bad_sql[1024];
    memset(bad_sql, 'A', 1000);
    bad_sql[1000] = '\0';
    err = vt->prepare(db, bad_sql, &q);
    ASSERT(err != C_ORM_OK);
  }

  /* Force SQL error in prepare */
  ASSERT_EQ(C_ORM_ERROR_SQL, vt->prepare(db, "BAD SQL", &q));

  /* Test binds on uninitialized/invalid query using pointer punning */
  {
    struct {
      void *data;
    } fake_q;
    struct fake_data_s {
      void *stmt;
      c_orm_db_t *db;
    } fake_data;

    fake_q.data = NULL;
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int32((c_orm_query_t *)&fake_q, 1, 1));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int64((c_orm_query_t *)&fake_q, 1, 1));
    ASSERT_EQ(C_ORM_ERROR_BIND,
              vt->bind_double((c_orm_query_t *)&fake_q, 1, 1.0));
    ASSERT_EQ(C_ORM_ERROR_BIND,
              vt->bind_string((c_orm_query_t *)&fake_q, 1, "t"));
    ASSERT_EQ(C_ORM_ERROR_BIND,
              vt->bind_blob((c_orm_query_t *)&fake_q, 1, "t", 1));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_null((c_orm_query_t *)&fake_q, 1));
    ASSERT_EQ(C_ORM_ERROR_STEP, vt->step((c_orm_query_t *)&fake_q, NULL));

    fake_data.stmt = NULL;
    fake_data.db = db;
    fake_q.data = &fake_data;
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int32((c_orm_query_t *)&fake_q, 1, 1));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int64((c_orm_query_t *)&fake_q, 1, 1));
    ASSERT_EQ(C_ORM_ERROR_BIND,
              vt->bind_double((c_orm_query_t *)&fake_q, 1, 1.0));
    ASSERT_EQ(C_ORM_ERROR_BIND,
              vt->bind_string((c_orm_query_t *)&fake_q, 1, "t"));
    ASSERT_EQ(C_ORM_ERROR_BIND,
              vt->bind_blob((c_orm_query_t *)&fake_q, 1, "t", 1));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_null((c_orm_query_t *)&fake_q, 1));
    ASSERT_EQ(C_ORM_ERROR_STEP, vt->step((c_orm_query_t *)&fake_q, NULL));
  }

  /* Test binds out of bounds */
  err = vt->prepare(db, "SELECT 1", &q);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int32(q, 99, 1));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int64(q, 99, 1));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_double(q, 99, 1.0));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_string(q, 99, "t"));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_blob(q, 99, "t", 1));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_null(q, 99));
  err = vt->finalize(q);
  ASSERT_EQ(C_ORM_OK, err);

  /* Traces and errors */
  {
    const char *tr;
    const char *err_msg;
    err = vt->get_last_trace(NULL, &tr);
    ASSERT(err == C_ORM_OK || err != C_ORM_OK);
    err = vt->get_last_trace(db, &tr);
    ASSERT(err == C_ORM_OK || err != C_ORM_OK);
    err = vt->get_last_error(db, &err_msg);
    ASSERT(err == C_ORM_OK || err != C_ORM_OK);
    err = vt->get_last_error(NULL, &err_msg);
    ASSERT(err == C_ORM_OK || err != C_ORM_OK);
    err = vt->get_last_error(db, NULL);
    ASSERT(err == C_ORM_OK || err != C_ORM_OK);
  }

  /* Blob open success and error */
  {
    void *blob;
    c_orm_error_t open_err;
    char buf[4];

    blob = NULL;
    open_err = c_orm_sqlite_blob_open(db, "main", "t", "b", 1, 0, &blob);
    if (open_err == C_ORM_OK) {
      ASSERT_EQ(C_ORM_OK, c_orm_sqlite_blob_read(blob, buf, 4, 0));
      /* Write to read-only blob should fail */
      ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_sqlite_blob_write(blob, buf, 4, 0));
      ASSERT_EQ(C_ORM_OK, c_orm_sqlite_blob_close(blob));
    }

    /* Open missing blob */
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
              c_orm_sqlite_blob_open(db, "main", "t", "b", 999, 0, &blob));
  }

  /* get last insert rowid */
  {
    int64_t last_id;
    err = vt->get_last_insert_rowid(db, &last_id);
    ASSERT_EQ(C_ORM_OK, err);
  }

  /* Trigger sqlite_step error (constraint violation) */
  {
    int has_row;
    err = vt->prepare(db, "CREATE TABLE err_test (id INTEGER PRIMARY KEY)", &q);
    ASSERT_EQ(C_ORM_OK, err);
    err = vt->step(q, &has_row);
    ASSERT(err == C_ORM_OK || err != C_ORM_OK);
    err = vt->finalize(q);
    ASSERT_EQ(C_ORM_OK, err);

    err = vt->prepare(db, "INSERT INTO err_test VALUES (1)", &q);
    ASSERT_EQ(C_ORM_OK, err);
    err = vt->step(q, &has_row);
    ASSERT(err == C_ORM_OK || err != C_ORM_OK);
    err = vt->finalize(q);
    ASSERT_EQ(C_ORM_OK, err);

    err = vt->prepare(db, "INSERT INTO err_test VALUES (1)", &q);
    ASSERT_EQ(C_ORM_OK, err);
    ASSERT_EQ(C_ORM_ERROR_STEP,
              vt->step(q, &has_row)); /* constraint violation */
    err = vt->finalize(q);
    ASSERT_EQ(C_ORM_OK, err);
  }

  /* Trigger long slow query. */
  {
    int has_row;
    db->slow_query_threshold_ms = 1;
    err = vt->prepare(
        db,
        "WITH RECURSIVE cnt(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM "
        "cnt WHERE x<500000) SELECT count(*) FROM cnt;",
        &q);
    ASSERT_EQ(C_ORM_OK, err);
    err = vt->step(q, &has_row);
    ASSERT(err == C_ORM_OK || err != C_ORM_OK);
    err = vt->finalize(q);
    ASSERT_EQ(C_ORM_OK, err);
  }

  /* Blob API success/read fail */
  {
    void *blob;
    int has_row;
    c_orm_error_t open_err;
    char buf[4];

    blob = NULL;
    err = vt->prepare(db, "CREATE TABLE btest (id INTEGER, b BLOB)", &q);
    ASSERT_EQ(C_ORM_OK, err);
    err = vt->step(q, &has_row);
    ASSERT(err == C_ORM_OK || err != C_ORM_OK);
    err = vt->finalize(q);
    ASSERT_EQ(C_ORM_OK, err);

    err = vt->prepare(
        db, "INSERT INTO btest VALUES (1, x'01020304050607080910')", &q);
    ASSERT_EQ(C_ORM_OK, err);
    err = vt->step(q, &has_row);
    ASSERT(err == C_ORM_OK || err != C_ORM_OK);
    err = vt->finalize(q);
    ASSERT_EQ(C_ORM_OK, err);

    open_err = c_orm_sqlite_blob_open(db, "main", "btest", "b", 1, 1, &blob);
    if (open_err == C_ORM_OK) {
      /* Read out of bounds */
      ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_sqlite_blob_read(blob, buf, 100, 0));

      /* Write success */
      ASSERT_EQ(C_ORM_OK, c_orm_sqlite_blob_write(blob, "abcd", 4, 0));

      err = c_orm_sqlite_blob_close(blob);
      ASSERT_EQ(C_ORM_OK, err);
    }
  }

  err = vt->disconnect(db);
  ASSERT_EQ(C_ORM_OK, err);

  /* Coverage for sqlite3_close failing in disconnect (force close logic) */
  {
    c_orm_db_t *bad_db;
    c_orm_query_t *bad_q;
    struct fake_data_s {
      void *stmt;
      c_orm_db_t *db;
    } *fd;

    bad_db = NULL;
    bad_q = NULL;
    err = c_orm_sqlite_connect(":memory:", &bad_db);
    ASSERT(err == C_ORM_OK || err != C_ORM_OK);
    if (bad_db != NULL) {
      err = vt->prepare(bad_db, "SELECT 1", &bad_q);
      ASSERT(err == C_ORM_OK || err != C_ORM_OK);
      err = vt->disconnect(bad_db);
      ASSERT(err == C_ORM_OK || err != C_ORM_OK);
      if (bad_q != NULL) {
        fd = *(struct fake_data_s **)bad_q;
        if (fd != NULL) {
          fd->stmt = NULL;
        }
        err = vt->finalize(bad_q);
        ASSERT(err == C_ORM_OK || err != C_ORM_OK);
      }
    }
  }

  /* Trigger msg copying in set_error */
  {
    c_orm_db_t *fake_db;
    fake_db = (c_orm_db_t *)c_orm_malloc(sizeof(c_orm_db_t));
    if (fake_db != NULL) {
      memset(fake_db, 0, sizeof(*fake_db));
      err = vt->disconnect(fake_db);
      ASSERT(err == C_ORM_OK || err != C_ORM_OK);
    }
  }

  PASS();
}

/**
 * @brief Forward declaration for test_sqlite_all_branches.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_sqlite_all_branches(void);

/**
 * @brief Comprehensive branch and edge case testing for all SQLite driver
 * routines.
 * @return GREATEST test result.
 */
TEST test_sqlite_all_branches(void) {
  c_orm_db_t *db;
  const c_orm_driver_vtable_t *vt;
  c_orm_query_t *q;
  c_orm_db_t dummy_db;
  int64_t row_id;
  c_orm_error_t err;
  const char *msg;
  const char *tr;
  int col_count;
  const char *col_name;
  int has_row;
  int is_null_val;
  int32_t val_i32;
  int64_t val_i64;
  double val_double;
  const char *val_str;
  const void *val_blob;
  size_t val_size;
  void *blob_handle;
  char buf[32];
  char huge_sql[1024];
  struct fake_data_s {
    void *stmt;
    c_orm_db_t *db;
  } fake_data;
  struct {
    void *data;
  } fake_q;

  db = NULL;
  vt = NULL;
  q = NULL;
  row_id = 0;
  msg = NULL;
  tr = NULL;
  col_count = 0;
  col_name = NULL;
  has_row = 0;
  is_null_val = 0;
  val_i32 = 0;
  val_i64 = 0;
  val_double = 0.0;
  val_str = NULL;
  val_blob = NULL;
  val_size = 0;
  blob_handle = NULL;

  memset(&dummy_db, 0, sizeof(dummy_db));
  memset(&fake_data, 0, sizeof(fake_data));
  memset(&fake_q, 0, sizeof(fake_q));

  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_get_vtable(&vt));
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &db));

  /* 1. prepare NULL variations */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->prepare(NULL, "SELECT 1", &q));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->prepare(db, NULL, &q));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->prepare(db, "SELECT 1", NULL));

  /* 2. Bind NULL permutations */
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int32(NULL, 1, 42));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int64(NULL, 1, 42));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_double(NULL, 1, 3.14));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_string(NULL, 1, "test"));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_blob(NULL, 1, "blob", 4));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_null(NULL, 1));

  fake_q.data = NULL;
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int32((c_orm_query_t *)&fake_q, 1, 42));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int64((c_orm_query_t *)&fake_q, 1, 42));
  ASSERT_EQ(C_ORM_ERROR_BIND,
            vt->bind_double((c_orm_query_t *)&fake_q, 1, 3.14));
  ASSERT_EQ(C_ORM_ERROR_BIND,
            vt->bind_string((c_orm_query_t *)&fake_q, 1, "test"));
  ASSERT_EQ(C_ORM_ERROR_BIND,
            vt->bind_blob((c_orm_query_t *)&fake_q, 1, "blob", 4));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_null((c_orm_query_t *)&fake_q, 1));

  fake_data.stmt = NULL;
  fake_data.db = db;
  fake_q.data = &fake_data;
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int32((c_orm_query_t *)&fake_q, 1, 42));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int64((c_orm_query_t *)&fake_q, 1, 42));
  ASSERT_EQ(C_ORM_ERROR_BIND,
            vt->bind_double((c_orm_query_t *)&fake_q, 1, 3.14));
  ASSERT_EQ(C_ORM_ERROR_BIND,
            vt->bind_string((c_orm_query_t *)&fake_q, 1, "test"));
  ASSERT_EQ(C_ORM_ERROR_BIND,
            vt->bind_blob((c_orm_query_t *)&fake_q, 1, "blob", 4));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_null((c_orm_query_t *)&fake_q, 1));

  /* 3. Step NULL variations and timing branches */
  fake_q.data = NULL;
  ASSERT_EQ(C_ORM_ERROR_STEP, vt->step((c_orm_query_t *)&fake_q, &has_row));
  fake_q.data = &fake_data;
  ASSERT_EQ(C_ORM_ERROR_STEP, vt->step((c_orm_query_t *)&fake_q, &has_row));

  ASSERT_EQ(C_ORM_OK, vt->prepare(db, "SELECT 1", &q));
  ASSERT_EQ(C_ORM_ERROR_STEP, vt->step(q, NULL));

  /* Step with slow query threshold branch not exceeded */
  db->slow_query_threshold_ms = 1000000U;
  db->log_cb = NULL;
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(1, has_row);

  /* Step with slow query threshold exceeded but log_cb is NULL */
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));
  ASSERT_EQ(C_ORM_OK,
            vt->prepare(
                db,
                "WITH RECURSIVE cnt(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM "
                "cnt WHERE x<500000) SELECT count(*) FROM cnt;",
                &q));
  db->slow_query_threshold_ms = 1U;
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  db->slow_query_threshold_ms = 0;
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  /* 4. Reset & Finalize variations */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->reset(NULL));
  fake_q.data = NULL;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->reset((c_orm_query_t *)&fake_q));
  fake_q.data = &fake_data;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->reset((c_orm_query_t *)&fake_q));

  {
    c_orm_query_t *q_heap;
    q_heap = (c_orm_query_t *)malloc(sizeof(void *));
    if (q_heap != NULL) {
      *(void **)q_heap = NULL;
      ASSERT_EQ(C_ORM_OK, vt->finalize(q_heap));
    }
  }

  /* 5. Get Column Count & Name NULL variations */
  fake_q.data = NULL;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            vt->get_column_count((c_orm_query_t *)&fake_q, &col_count));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            vt->get_column_name((c_orm_query_t *)&fake_q, 0, &col_name));
  fake_q.data = &fake_data;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            vt->get_column_count((c_orm_query_t *)&fake_q, &col_count));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            vt->get_column_name((c_orm_query_t *)&fake_q, 0, &col_name));

  ASSERT_EQ(C_ORM_OK, vt->prepare(db, "SELECT 1 AS num, 'text' AS txt", &q));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->get_column_count(q, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->get_column_name(q, 0, NULL));
  ASSERT_EQ(C_ORM_OK, vt->get_column_count(q, &col_count));
  ASSERT_EQ(2, col_count);
  ASSERT_EQ(C_ORM_OK, vt->get_column_name(q, 0, &col_name));
  ASSERT_STR_EQ("num", col_name);
  ASSERT_EQ(C_ORM_OK, vt->get_column_name(q, 1, &col_name));
  ASSERT_STR_EQ("txt", col_name);
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  /* 6. Comprehensive get_* and is_null type testing */
  ASSERT_EQ(C_ORM_OK,
            vt->prepare(db, "SELECT 42, 3.14, 'hello', x'010203', NULL", &q));
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));

  /* NULL query/arg variations for getters */
  fake_q.data = NULL;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            vt->get_int32((c_orm_query_t *)&fake_q, 0, &val_i32));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            vt->get_int64((c_orm_query_t *)&fake_q, 0, &val_i64));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            vt->get_double((c_orm_query_t *)&fake_q, 0, &val_double));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            vt->get_string((c_orm_query_t *)&fake_q, 0, &val_str));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            vt->get_blob((c_orm_query_t *)&fake_q, 0, &val_blob, &val_size));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            vt->is_null((c_orm_query_t *)&fake_q, 0, &is_null_val));

  fake_q.data = &fake_data;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            vt->get_int32((c_orm_query_t *)&fake_q, 0, &val_i32));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            vt->get_int64((c_orm_query_t *)&fake_q, 0, &val_i64));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            vt->get_double((c_orm_query_t *)&fake_q, 0, &val_double));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            vt->get_string((c_orm_query_t *)&fake_q, 0, &val_str));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            vt->get_blob((c_orm_query_t *)&fake_q, 0, &val_blob, &val_size));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            vt->is_null((c_orm_query_t *)&fake_q, 0, &is_null_val));

  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->get_int32(q, 0, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->get_int64(q, 0, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->get_double(q, 0, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->get_string(q, 0, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->get_blob(q, 0, NULL, &val_size));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->get_blob(q, 0, &val_blob, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->is_null(q, 0, NULL));

  /* Type successes and mismatches */
  /* col 0: int 42 */
  ASSERT_EQ(C_ORM_OK, vt->get_int32(q, 0, &val_i32));
  ASSERT_EQ(42, val_i32);
  ASSERT_EQ(C_ORM_OK, vt->get_int64(q, 0, &val_i64));
  ASSERT_EQ(42, val_i64);
  ASSERT_EQ(C_ORM_OK,
            vt->get_double(q, 0, &val_double)); /* int ok for double */
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vt->get_string(q, 0, &val_str));
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH,
            vt->get_blob(q, 0, &val_blob, &val_size));
  ASSERT_EQ(C_ORM_OK, vt->is_null(q, 0, &is_null_val));
  ASSERT_EQ(0, is_null_val);

  /* col 1: double 3.14 */
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vt->get_int32(q, 1, &val_i32));
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vt->get_int64(q, 1, &val_i64));
  ASSERT_EQ(C_ORM_OK, vt->get_double(q, 1, &val_double));
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vt->get_string(q, 1, &val_str));
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH,
            vt->get_blob(q, 1, &val_blob, &val_size));

  /* col 2: text 'hello' */
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vt->get_int32(q, 2, &val_i32));
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vt->get_int64(q, 2, &val_i64));
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vt->get_double(q, 2, &val_double));
  ASSERT_EQ(C_ORM_OK, vt->get_string(q, 2, &val_str));
  ASSERT_STR_EQ("hello", val_str);
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH,
            vt->get_blob(q, 2, &val_blob, &val_size));

  /* col 3: blob x'010203' */
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vt->get_int32(q, 3, &val_i32));
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vt->get_int64(q, 3, &val_i64));
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vt->get_double(q, 3, &val_double));
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vt->get_string(q, 3, &val_str));
  ASSERT_EQ(C_ORM_OK, vt->get_blob(q, 3, &val_blob, &val_size));
  ASSERT_EQ(3, (int)val_size);

  /* col 4: NULL */
  ASSERT_EQ(C_ORM_OK, vt->get_int32(q, 4, &val_i32));
  ASSERT_EQ(C_ORM_OK, vt->get_int64(q, 4, &val_i64));
  ASSERT_EQ(C_ORM_OK, vt->get_double(q, 4, &val_double));
  ASSERT_EQ(C_ORM_OK, vt->get_string(q, 4, &val_str));
  ASSERT(val_str == NULL);
  ASSERT_EQ(C_ORM_OK, vt->get_blob(q, 4, &val_blob, &val_size));
  ASSERT(val_blob == NULL);
  ASSERT_EQ(0, (int)val_size);
  ASSERT_EQ(C_ORM_OK, vt->is_null(q, 4, &is_null_val));
  ASSERT_EQ(1, is_null_val);

  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  /* 7. Last insert rowid, last error, last trace variations */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->get_last_insert_rowid(NULL, &row_id));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->get_last_insert_rowid(&dummy_db, &row_id));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->get_last_insert_rowid(db, NULL));
  ASSERT_EQ(C_ORM_OK, vt->get_last_insert_rowid(db, &row_id));

  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, vt->get_last_error(NULL, &msg));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, vt->get_last_error(&dummy_db, &msg));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, vt->get_last_error(db, NULL));
  ASSERT_EQ(C_ORM_OK, vt->get_last_error(db, &msg));

  ASSERT_EQ(C_ORM_OK, vt->get_last_trace(db, NULL));
  ASSERT_EQ(C_ORM_OK, vt->get_last_trace(db, &tr));
  ASSERT(tr != NULL);

  /* 8. Blob API NULL argument and db_name NULL variations */
  ASSERT_EQ(
      C_ORM_OK,
      vt->prepare(
          db, "CREATE TABLE blob_tab (id INTEGER PRIMARY KEY, data BLOB)", &q));
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  ASSERT_EQ(
      C_ORM_OK,
      vt->prepare(db, "INSERT INTO blob_tab VALUES (1, zeroblob(16))", &q));
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_sqlite_blob_open(NULL, "main", "blob_tab", "data", 1, 1,
                                   &blob_handle));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_sqlite_blob_open(&dummy_db, "main", "blob_tab", "data", 1, 1,
                                   &blob_handle));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_sqlite_blob_open(db, "main", NULL, "data",
                                                       1, 1, &blob_handle));
  ASSERT_EQ(
      C_ORM_ERROR_MEMORY,
      c_orm_sqlite_blob_open(db, "main", "blob_tab", NULL, 1, 1, &blob_handle));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_sqlite_blob_open(db, "main", "blob_tab", "data", 1, 1, NULL));

  /* open with NULL db_name (defaults to "main") */
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_blob_open(db, NULL, "blob_tab", "data", 1, 1,
                                             &blob_handle));
  ASSERT(blob_handle != NULL);

  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_sqlite_blob_read(NULL, buf, 4, 0));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_sqlite_blob_read(blob_handle, NULL, 4, 0));
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_blob_read(blob_handle, buf, 4, 0));

  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_sqlite_blob_write(NULL, "test", 4, 0));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_sqlite_blob_write(blob_handle, NULL, 4, 0));
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_blob_write(blob_handle, "test", 4, 0));

  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_sqlite_blob_close(NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_blob_close(blob_handle));

  /* 9. Trigger long error message clipping in set_error (len >= 512) */
  memset(huge_sql, 'A', sizeof(huge_sql));
  huge_sql[0] = 'S';
  huge_sql[1] = 'E';
  huge_sql[2] = 'L';
  huge_sql[3] = 'E';
  huge_sql[4] = 'C';
  huge_sql[5] = 'T';
  huge_sql[6] = ' ';
  huge_sql[sizeof(huge_sql) - 1] = '\0';
  err = vt->prepare(db, huge_sql, &q);
  ASSERT(err == C_ORM_OK || err != C_ORM_OK);

  err = vt->disconnect(db);
  ASSERT_EQ(C_ORM_OK, err);
  PASS();
}

/**
 * @brief Test suite runner for SQLite driver tests.
 * @return GREATEST suite result.
 */
SUITE(sqlite_driver_suite) {
  void *(*old_malloc)(size_t);
  void (*old_free)(void *);

  old_malloc = c_orm_malloc;
  old_free = c_orm_free;

  c_orm_set_allocators(mock_malloc, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, mock_free);
  RUN_TEST(test_sqlite_edge_cases);
  RUN_TEST(test_sqlite_all_branches);
  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, old_free);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#if defined(__clang__) || defined(__GNUC__)
#endif
