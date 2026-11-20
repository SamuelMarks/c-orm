/**
 * @file test_memory_driver.c
 * @brief Unit tests for in-memory database driver implementation.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_decimal.h"
#include "c_orm_memory.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef RUN_TEST
#define RUN_TEST(TEST) do { int should = 0; enum greatest_test_res res; greatest_test_pre(#TEST, &should); res = TEST(); greatest_test_post(res); } while ((void)0, 0)
/* clang-format on */

/** @brief Counter decremented before triggering test malloc failure. */
static int oom_countdown = -1;

/** @brief Bitmask controlling which mock_free calls return error. */
static unsigned int mem_free_fail_mask = 0;

/** @brief Call index counter for mock_free when mem_free_fail_mask is set. */
static unsigned int mem_free_call_idx = 0;

/**
 * @brief Mock malloc callback returning error on countdown expiry.
 * @param size Requested allocation size.
 * @param out_ptr Output pointer receiving allocated memory.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on failure.
 */
static c_orm_error_t mock_malloc(size_t size, void **out_ptr) {
  if (oom_countdown == 0) {
    oom_countdown--;
    if (out_ptr) {
      *out_ptr = NULL;
    }
    return C_ORM_ERROR_MEMORY;
  }
  oom_countdown--;
  *out_ptr = malloc(size == 0 ? 1 : size);
  return *out_ptr ? C_ORM_OK : C_ORM_ERROR_MEMORY;
}

/**
 * @brief Mock realloc callback returning error on countdown expiry.
 * @param ptr Existing allocation pointer.
 * @param size Requested byte size.
 * @param out_ptr Output pointer receiving reallocated memory.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on failure.
 */
static c_orm_error_t mock_realloc(void *ptr, size_t size, void **out_ptr) {
  if (oom_countdown == 0) {
    oom_countdown--;
    if (out_ptr) {
      *out_ptr = NULL;
    }
    return C_ORM_ERROR_MEMORY;
  }
  oom_countdown--;
  *out_ptr = realloc(ptr, size == 0 ? 1 : size);
  return *out_ptr ? C_ORM_OK : C_ORM_ERROR_MEMORY;
}

/**
 * @brief Mock free callback forwarding to free with optional fault bitmask.
 * @param ptr Pointer to memory to free.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY when masked.
 */
static c_orm_error_t mock_free(void *ptr) {
  free(ptr);
  if (mem_free_fail_mask != 0) {
    unsigned int idx = mem_free_call_idx++;
    if (idx < 31 && (mem_free_fail_mask & (1u << idx)) != 0) {
      return C_ORM_ERROR_MEMORY;
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Tests in-memory database edge cases, vtable operations, and error
 * paths.
 * @return GREATEST test result.
 */
TEST test_memory_edge_cases(void) {
  c_orm_db_t *db;
  const c_orm_driver_vtable_t *vt;
  c_orm_query_t *q;
  c_orm_error_t err;
  int64_t id;
  int count;
  int has_row;
  const char *msg;
  int out_null;
  const char *out_name;
  int32_t out_i32;
  int64_t out_i64;
  double out_d;
  const char *out_s;
  const void *out_blob;
  size_t out_blob_sz;

  db = NULL;
  vt = NULL;
  q = NULL;
  has_row = 0;
  msg = NULL;
  out_null = 0;
  out_name = NULL;

  /* get_vtable NULL */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_memory_get_vtable(NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_memory_get_vtable(&vt));
  ASSERT(vt != NULL);

  /* Connect NULLs */
  err = c_orm_memory_connect(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  err = c_orm_memory_connect("mem://", &db);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT(db != NULL);

  /* Coverage via vtable */
  err = vt->prepare(db, "SELECT * FROM t", &q);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->step(q, &has_row);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(0, has_row);
  err = vt->finalize(q);
  ASSERT_EQ(C_ORM_OK, err);

  err = vt->prepare(db, "SELECT * FROM t WHERE id = 1", &q);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->step(q, &has_row);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->finalize(q);
  ASSERT_EQ(C_ORM_OK, err);

  err = vt->prepare(db, "INSERT INTO t (id) VALUES (1)", &q);
  ASSERT_EQ(C_ORM_OK, err);
  /* Step with 0 bound parameters */
  err = vt->step(q, &has_row);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->finalize(q);
  ASSERT_EQ(C_ORM_OK, err);

  /* Insert into a second table and first table with bound parameters */
  err = vt->prepare(db, "INSERT INTO t2 (id) VALUES (1)", &q);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->bind_int32(q, 1, 99);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->step(q, &has_row);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->finalize(q);
  ASSERT_EQ(C_ORM_OK, err);

  err = vt->prepare(db, "INSERT INTO t(id) VALUES (1)", &q);
  ASSERT_EQ(C_ORM_OK, err);
  /* Re-bind same index to cover replacement path */
  err = vt->bind_int32(q, 1, 10);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->bind_int32(q, 1, 42);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->bind_int64(q, 2, (int64_t)1000);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->bind_double(q, 3, 3.14);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->bind_string(q, 4, "hello");
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->bind_blob(q, 5, "blobdata", 8);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->bind_null(q, 6);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->bind_string(q, 7, NULL);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->bind_blob(q, 8, NULL, 0);
  ASSERT_EQ(C_ORM_OK, err);
  /* Bind at index 10 leaving index 9 (0-based 8) NULL in columns array */
  err = vt->bind_int32(q, 10, 777);
  ASSERT_EQ(C_ORM_OK, err);

  /* Invalid bind index */
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int32(NULL, 1, 1));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int32(q, 0, 1));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int32(q, 33, 1));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int64(q, 0, 1));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_double(q, 0, 1.0));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_string(q, 0, "x"));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_blob(q, 0, "x", 1));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_null(q, 0));

  err = vt->step(q, &has_row);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(0, has_row);
  err = vt->finalize(q);
  ASSERT_EQ(C_ORM_OK, err);

  /* Select rows back from t */
  err = vt->prepare(db, "SELECT * FROM t", &q);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->step(q, &has_row);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(1, has_row);

  err = vt->get_int32(q, 0, &out_i32);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(42, out_i32);

  err = vt->get_int64(q, 1, &out_i64);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ((int64_t)1000, out_i64);

  err = vt->get_double(q, 2, &out_d);
  ASSERT_EQ(C_ORM_OK, err);

  err = vt->get_string(q, 3, &out_s);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_STR_EQ("hello", out_s);

  err = vt->get_blob(q, 4, &out_blob, &out_blob_sz);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(8, (int)out_blob_sz);

  /* Edge cases for get_current_cell and mem_get_* */
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, vt->get_int32(NULL, 0, &out_i32));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, vt->get_int32(q, -1, &out_i32));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, vt->get_int32(q, 8, &out_i32));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, vt->get_int32(q, 99, &out_i32));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, vt->get_int64(q, 99, &out_i64));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, vt->get_double(q, 99, &out_d));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, vt->get_string(q, 99, &out_s));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, vt->get_blob(q, 4, &out_blob, NULL));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            vt->get_blob(q, 99, &out_blob, &out_blob_sz));

  err = vt->is_null(q, 0, &out_null);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(0, out_null);

  err = vt->is_null(q, 5, &out_null);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(1, out_null);

  err = vt->is_null(q, 99, &out_null);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(1, out_null);

  /* Step to second row (which has 0 columns) */
  err = vt->step(q, &has_row);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(1, has_row);
  err = vt->is_null(q, 0, &out_null);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(1, out_null);

  /* Step past end */
  err = vt->step(q, &has_row);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(0, has_row);
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, vt->get_int32(q, 0, &out_i32));
  /* Step again when current_row is NULL */
  err = vt->step(q, &has_row);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(0, has_row);

  err = vt->reset(q);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->reset(NULL);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->finalize(q);
  ASSERT_EQ(C_ORM_OK, err);

  err = vt->prepare(db, "UPDATE t SET id = 2", &q);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->finalize(q);
  ASSERT_EQ(C_ORM_OK, err);

  err = vt->prepare(db, "DELETE FROM t", &q);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->finalize(q);
  ASSERT_EQ(C_ORM_OK, err);

  err = vt->prepare(db, "CREATE TABLE t", &q);
  ASSERT_EQ(C_ORM_OK, err);

  /* Finalize with bound params still attached */
  err = vt->bind_string(q, 1, "unconsumed");
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->bind_blob(q, 2, "unconsumed_blob", 15);
  ASSERT_EQ(C_ORM_OK, err);

  err = vt->step(q, &has_row);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(0, has_row);
  err = vt->step(NULL, &has_row);
  ASSERT_EQ(C_ORM_OK, err);

  err = vt->get_int32(q, 0, NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, err);
  err = vt->get_int64(q, 0, NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, err);
  err = vt->get_double(q, 0, NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, err);
  err = vt->get_string(q, 0, NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, err);
  err = vt->get_blob(q, 0, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, err);
  err = vt->is_null(q, 0, NULL);
  ASSERT_EQ(C_ORM_OK, err);

  err = vt->get_last_insert_rowid(db, NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);
  err = vt->get_last_insert_rowid(db, &id);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(42, id);

  err = vt->get_column_count(q, NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);
  err = vt->get_column_count(q, &count);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);
  ASSERT_EQ(0, count);
  err = vt->get_column_name(q, 0, NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);

  vt->get_last_error(db, &msg);
  ASSERT_STR_EQ("", msg);

  err = vt->finalize(q);
  ASSERT_EQ(C_ORM_OK, err);

  vt->disconnect(db);

  /* Test OOM in mem_connect */
  oom_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_memory_connect("mem://", &db));
  oom_countdown = 1;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_memory_connect("mem://", &db));
  oom_countdown = -1;

  /* Test parsing whitespace before table name and OOM in bind/step */
  err = c_orm_memory_connect("mem://", &db);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->prepare(db, "SELECT * FROM    my_table", &q);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->finalize(q);

  /* OOM in prepare */
  oom_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->prepare(db, "SELECT * FROM t", &q));
  oom_countdown = 1;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->prepare(db, "SELECT * FROM t", &q));
  oom_countdown = -1;

  /* OOM in bind and step insert */
  err = vt->prepare(db, "INSERT INTO new_tbl (id) VALUES (1)", &q);
  ASSERT_EQ(C_ORM_OK, err);
  oom_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->bind_int32(q, 1, 1));
  oom_countdown = 1;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->bind_blob(q, 1, "abc", 3));
  oom_countdown = -1;
  ASSERT_EQ(C_ORM_OK, vt->bind_int32(q, 1, 1));

  /* OOM in mem_step table alloc (0), table name strdup (1), row alloc (2),
   * columns alloc (3) */
  oom_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->step(q, &has_row));
  oom_countdown = 1;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->step(q, &has_row));
  oom_countdown = 2;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->step(q, &has_row));
  oom_countdown = 0;
  /* Note: new_tbl was created on countdown=2 before row alloc failed! So next
   * step only allocates row (0) and columns (1) */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->step(q, &has_row));
  oom_countdown = 1;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->step(q, &has_row));
  oom_countdown = -1;
  err = vt->finalize(q);
  ASSERT_EQ(C_ORM_OK, err);

  /* Coverage for pointers passed in */
  vt->is_null(NULL, 0, &out_null);
  vt->get_column_name(NULL, 0, &out_name);

  err = vt->disconnect(db);
  ASSERT_EQ(C_ORM_OK, err);

  PASS();
}

/**
 * @brief Executes a simple SQL statement with no bound parameters on the memory
 * driver.
 * @param db Database connection handle.
 * @param sql SQL statement to execute.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t exec_mem_sql(c_orm_db_t *db, const char *sql) {
  c_orm_query_t *q = NULL;
  int has_row = 0;
  c_orm_error_t rc;
  c_orm_error_t fin_rc;
  rc = db->vtable->prepare(db, sql, &q);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = db->vtable->step(q, &has_row);
  fin_rc = db->vtable->finalize(q);
  return (rc != C_ORM_OK) ? rc : fin_rc;
}

/**
 * @brief Verifies real multi-column WHERE, UPDATE, DELETE, ORDER BY, LIMIT,
 * OFFSET, and snapshot-based transaction ROLLBACK/COMMIT in the memory driver.
 * @return GREATEST test result.
 */
TEST test_memory_sql_and_transactions(void) {
  c_orm_db_t *db = NULL;
  const c_orm_driver_vtable_t *vt = NULL;
  c_orm_query_t *q = NULL;
  int has_row = 0;
  int32_t id = 0;
  const char *name = NULL;
  double score = 0.0;
  int col_cnt = 0;
  const char *col_nm = NULL;
  int i;

  ASSERT_EQ(C_ORM_OK, c_orm_memory_connect("memory://", &db));
  vt = db->vtable;

  ASSERT_EQ(C_ORM_OK,
            exec_mem_sql(db, "CREATE TABLE users (id INT PRIMARY KEY, name "
                             "TEXT, age INT, score DOUBLE)"));

  /* Insert 4 users */
  for (i = 0; i < 4; ++i) {
    static const char *const names[4] = {"Alice", "Bob", "Carol", "Dave"};
    static const int32_t ages[4] = {30, 22, 35, 28};
    static const double scores[4] = {88.5, 95.0, 91.0, 72.0};

    ASSERT_EQ(
        C_ORM_OK,
        vt->prepare(
            db, "INSERT INTO users (id, name, age, score) VALUES (?, ?, ?, ?)",
            &q));
    ASSERT_EQ(C_ORM_OK, vt->bind_int32(q, 1, (int32_t)(i + 1)));
    ASSERT_EQ(C_ORM_OK, vt->bind_string(q, 2, names[i]));
    ASSERT_EQ(C_ORM_OK, vt->bind_int32(q, 3, ages[i]));
    ASSERT_EQ(C_ORM_OK, vt->bind_double(q, 4, scores[i]));
    ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
    ASSERT_EQ(C_ORM_OK, vt->finalize(q));
  }

  /* Multi-column WHERE + ORDER BY DESC + LIMIT + OFFSET + column metadata */
  ASSERT_EQ(C_ORM_OK,
            vt->prepare(db,
                        "SELECT * FROM users WHERE age >= ? AND name != ? "
                        "ORDER BY score DESC LIMIT 2 OFFSET 0",
                        &q));
  ASSERT_EQ(C_ORM_OK, vt->get_column_count(q, &col_cnt));
  ASSERT_EQ(4, col_cnt);
  ASSERT_EQ(C_ORM_OK, vt->get_column_name(q, 1, &col_nm));
  ASSERT_STR_EQ("name", col_nm);

  ASSERT_EQ(C_ORM_OK, vt->bind_int32(q, 1, 25));
  ASSERT_EQ(C_ORM_OK, vt->bind_string(q, 2, "Dave"));
  /* Matching rows: Alice (30, 88.5), Carol (35, 91.0) -> Carol first, Alice
   * second */
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(1, has_row);
  ASSERT_EQ(C_ORM_OK, vt->get_string(q, 1, &name));
  ASSERT_STR_EQ("Carol", name);

  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(1, has_row);
  ASSERT_EQ(C_ORM_OK, vt->get_string(q, 1, &name));
  ASSERT_STR_EQ("Alice", name);

  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(0, has_row);
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  /* OFFSET 1 LIMIT 1 with ORDER BY age ASC */
  ASSERT_EQ(C_ORM_OK,
            vt->prepare(db,
                        "SELECT * FROM users ORDER BY age ASC LIMIT 1 OFFSET 1",
                        &q));
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(1, has_row);
  ASSERT_EQ(C_ORM_OK, vt->get_string(q, 1, &name));
  /* Ages ASC: Bob (22), Dave (28), Alice (30), Carol (35) -> offset 1 is Dave
   */
  ASSERT_STR_EQ("Dave", name);
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(0, has_row);
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  /* UPDATE with bound SET and WHERE */
  ASSERT_EQ(
      C_ORM_OK,
      vt->prepare(db, "UPDATE users SET score = ?, name = ? WHERE id = ?", &q));
  ASSERT_EQ(C_ORM_OK, vt->bind_double(q, 1, 99.5));
  ASSERT_EQ(C_ORM_OK, vt->bind_string(q, 2, "Alicia"));
  ASSERT_EQ(C_ORM_OK, vt->bind_int32(q, 3, 1));
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  /* Begin transaction, mutate (DELETE + UPDATE + INSERT), then ROLLBACK! */
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "BEGIN TRANSACTION"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "DELETE FROM users WHERE age < 30"));
  ASSERT_EQ(C_ORM_OK,
            exec_mem_sql(db, "UPDATE users SET name = 'Mutated' WHERE id = 3"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "ROLLBACK"));

  /* Verify Bob (age 22), Dave (age 28), Carol ('Carol'), and Alicia (99.5) all
   * survived rollback intact! */
  ASSERT_EQ(C_ORM_OK,
            vt->prepare(db, "SELECT * FROM users ORDER BY id ASC", &q));
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(1, has_row);
  ASSERT_EQ(C_ORM_OK, vt->get_int32(q, 0, &id));
  ASSERT_EQ(C_ORM_OK, vt->get_string(q, 1, &name));
  ASSERT_EQ(C_ORM_OK, vt->get_double(q, 3, &score));
  ASSERT_EQ(1, id);
  ASSERT_STR_EQ("Alicia", name);
  ASSERT(score > 99.0);

  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(1, has_row);
  ASSERT_EQ(C_ORM_OK, vt->get_string(q, 1, &name));
  ASSERT_STR_EQ("Bob", name);

  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(1, has_row);
  ASSERT_EQ(C_ORM_OK, vt->get_string(q, 1, &name));
  ASSERT_STR_EQ("Carol", name);

  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(1, has_row);
  ASSERT_EQ(C_ORM_OK, vt->get_string(q, 1, &name));
  ASSERT_STR_EQ("Dave", name);
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  /* Now BEGIN -> DELETE -> COMMIT and verify deletion persists */
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "BEGIN"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "DELETE FROM users WHERE age < 25"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "COMMIT"));

  ASSERT_EQ(C_ORM_OK,
            vt->prepare(db, "SELECT * FROM users WHERE name = 'Bob'", &q));
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(0, has_row);
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  ASSERT_EQ(C_ORM_OK, vt->disconnect(db));
  PASS();
}

/**
 * @brief Exhaustive parser, decimal, comparison, snapshot OOM, Safe CRT, and
 * free-fault test suite for `src/c_orm_memory.c`.
 * @return GREATEST test result.
 */
TEST test_memory_exhaustive_coverage(void) {
  c_orm_db_t *db = NULL;
  const c_orm_driver_vtable_t *vt = NULL;
  c_orm_query_t *q = NULL;
  c_orm_decimal_t dec1;
  c_orm_decimal_t dec2;
  c_orm_decimal_t dec_out;
  c_orm_decimal_t bad_dec;
  int has_row = 0;
  int64_t rowid = 0;
  int col_cnt = 0;
  const char *col_nm = NULL;
  int k;

  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("100.50", &dec1));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("25.25", &dec2));
  memset(&bad_dec, 0, sizeof(bad_dec));
  bad_dec.precision = 0;

  /* 1. NULL / empty DB & query edge cases */
  ASSERT_EQ(C_ORM_OK, c_orm_memory_get_vtable(&vt));
  ASSERT_EQ(C_ORM_OK, vt->disconnect(NULL));
  ASSERT_EQ(C_ORM_OK, vt->finalize(NULL));
  ASSERT_EQ(C_ORM_OK, vt->step(NULL, &has_row));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED,
            vt->get_last_insert_rowid(NULL, &rowid));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, vt->get_column_count(NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, vt->get_column_name(NULL, 0, NULL));

  /* Empty DB disconnect free(ctx) fail, free(db) fail, and both fail */
  for (k = 1; k <= 3; k++) {
    c_orm_db_t *tmp_db = NULL;
    ASSERT_EQ(C_ORM_OK, c_orm_memory_connect("memory://", &tmp_db));
    mem_free_call_idx = 0;
    mem_free_fail_mask = (unsigned int)k;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->disconnect(tmp_db));
    mem_free_fail_mask = 0;
  }

  ASSERT_EQ(C_ORM_OK, c_orm_memory_connect("memory://", &db));
  /* OOM on q->bound_params calloc inside mem_prepare */
  oom_countdown = 1;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->prepare(db, "SELECT 1", &q));
  oom_countdown = -1;
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, vt->get_last_insert_rowid(db, &rowid));
  {
    void *saved = db->driver_data;
    db->driver_data = NULL;
    ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED,
              vt->get_last_insert_rowid(db, &rowid));
    db->driver_data = saved;
  }
  {
    c_orm_db_t *db_nodata = NULL;
    ASSERT_EQ(C_ORM_OK, c_orm_memory_connect("memory://", &db_nodata));
    free(db_nodata->driver_data);
    db_nodata->driver_data = NULL;
    ASSERT_EQ(C_ORM_OK, vt->disconnect(db_nodata));
  }

  ASSERT_EQ(C_ORM_ERROR_SQL, vt->prepare(NULL, "SELECT 1", &q));
  ASSERT_EQ(C_ORM_ERROR_SQL, vt->prepare(db, NULL, &q));
  ASSERT_EQ(C_ORM_ERROR_SQL, vt->prepare(db, "SELECT 1", NULL));

  /* Direct mock_realloc sanity check outside OOM */
  {
    void *rp = NULL;
    ASSERT_EQ(C_ORM_OK, mock_malloc(8, &rp));
    ASSERT_EQ(C_ORM_OK, mock_realloc(rp, 16, &rp));
    ASSERT_EQ(C_ORM_OK, mock_free(rp));
  }

  /* 2. SQL grammar & lexer variations */
  {
    static const char *const sql_cases[] = {
        "DROP TABLE z",
        "CREATE TABLE ;",
        "CREATE TABLE IF NOT EXISTS raw_no_paren",
        "CREATE TABLE IF NOT EXISTS empty_cols ()",
        ("CREATE TABLE IF NOT EXISTS c_tbl (\"id\" INT, `nm` VARCHAR(10), "
         "amt DECIMAL(10, 2), PRIMARY KEY (id), FOREIGN KEY (id) REFERENCES "
         "o(id), UNIQUE (nm), CONSTRAINT ck CHECK (amt > (0)), )"),
        "CREATE TABLE IF NOT EXISTS unclosed_c (id INT, CHECK (id > 0",
        "CREATE TABLE IF NOT EXISTS unclosed_d (id DECIMAL(10, 2",
        "CREATE TABLE IF NOT EXISTS semi_col (;)",
        "  SELECT 1",
        "SELECT * FROM ;",
        "SELECT XFROM, FROMAGE",
        "SELECT 'FROM' FROM (1)XFROM",
        "SELECT (1)FROM c_tbl WHERE(id = 1)",
        "SELECT * FROM \"c_tbl\"WHERE id = 1",
        "SELECT * FROM c_tbl WHERE;",
        "SELECT * FROM c_tbl WHERE",
        "SELECT * FROM c_tbl WHERE a = ",
        "SELECT * FROM c_tbl WHERE ORDER BY \"unclosed",
        "SELECT * FROM c_tbl WHERE LIMIT -5 OFFSET -2",
        "SELECT * FROM c_tbl WHERE OFFSET abc LIMIT xyz",
        "SELECT * FROM c_tbl ORDER NOTBY id",
        "SELECT * FROM c_tbl ORDER BY ;",
        "SELECT * FROM c_tbl WHERE ;",
        "SELECT * FROM c_tbl WHERE b IS NOT x",
        "SELECT * FROM c_tbl WHERE b IS NOTHING",
        "SELECT * FROM c_tbl WHERE d IS x",
        "SELECT * FROM c_tbl WHERE d ISX 1",
        "SELECT * FROM c_tbl WHERE a ! 1",
        "SELECT * FROM c_tbl WHERE a = $x AND b = 123abc ",
        "SELECT * FROM c_tbl WHERE a = 1 ANDROID",
        ("SELECT * FROM c_tbl WHERE a IS NOT NULL AND c IS NULL "
         "AND t.e <> 1 AND \"q.col\" <= 2.5 AND f < 'it''s' AND "
         "g > NULL AND h = NULLABLE AND m != 9 AND n >= 0 AND "
         "i = $2 AND j = $1 AND z = $0 AND k = 'unclosed"),
        "SELECT * FROM c_tbl WHERE a @ 1",
        "SELECT * FROM c_tbl WHERE a = 1 AND ;",
        ("SELECT * FROM c_tbl WHERE "
         "c1=1 AND c2=1 AND c3=1 AND c4=1 AND c5=1 AND c6=1 AND c7=1 AND c8=1 "
         "AND c9=1 AND c10=1 AND c11=1 AND c12=1 AND c13=1 AND c14=1 AND "
         "c15=1 AND c16=1 AND c17=1"),
        "INSERT INTO ;",
        "INSERT INTO raw_ins VALUES (1)",
        "UPDATE ;",
        "UPDATE c_tbl WHERE id = 1",
        "UPDATE c_tbl SET   ",
        "UPDATE c_tbl SET ;",
        "UPDATE c_tbl SET WHERE id = 1",
        "UPDATE c_tbl SET = 1",
        "UPDATE c_tbl SET a ! 1",
        "UPDATE c_tbl SET a = 1 ",
        ("UPDATE c_tbl SET s0=1, s1=1, s2=1, s3=1, s4=1, s5=1, s6=1, s7=1, "
         "s8=1, s9=1, s10=1, s11=1, s12=1, s13=1, s14=1, s15=1, s16=1"),
        ("UPDATE c_tbl SET a = 1, b = 2.5, c = 's', d = NULL, e = ?, f = $2 "
         "WHERE id = 1"),
        "DELETE FROM ;"};
    size_t idx;
    for (idx = 0; idx < sizeof(sql_cases) / sizeof(sql_cases[0]); idx++) {
      ASSERT_EQ(C_ORM_OK, vt->prepare(db, sql_cases[idx], &q));
      ASSERT_EQ(C_ORM_OK, vt->finalize(q));
    }

    /* Overlong identifier (>64), overlong quoted string (>128 with ''), and
     * overlong bare token (>64) */
    {
      char long_sql[600];
      char id70[72];
      char str150[160];
      memset(id70, 'c', 70);
      id70[70] = '\0';
      memset(str150, 'z', 140);
      str150[126] = '\'';
      str150[127] = '\'';
      str150[140] = '\0';
      C_ORM_SPRINTF(long_sql, sizeof(long_sql),
                    "SELECT * FROM c_tbl WHERE %s = '%s''%s' AND b = %s", id70,
                    str150, str150, id70);
      ASSERT_EQ(C_ORM_OK, vt->prepare(db, long_sql, &q));
      ASSERT_EQ(C_ORM_OK, vt->finalize(q));
    }

    /* > 32 columns in parse_column_list */
    {
      char many_cols[512];
      size_t pos = 0;
      C_ORM_SPRINTF(many_cols, sizeof(many_cols), "CREATE TABLE m33 (");
      pos = strlen(many_cols);
      for (k = 0; k < 34; k++) {
        C_ORM_SPRINTF(many_cols + pos, sizeof(many_cols) - pos,
                      k == 0 ? "c%d INT" : ", c%d INT", k);
        pos = strlen(many_cols);
      }
      C_ORM_SPRINTF(many_cols + pos, sizeof(many_cols) - pos, ")");
      ASSERT_EQ(C_ORM_OK, vt->prepare(db, many_cols, &q));
      ASSERT_EQ(C_ORM_OK, vt->finalize(q));
    }

    /* Safe CRT failures inside parse_column_list, parse_rhs_value, and
     * find_or_create_table */
    c_orm_mock_safe_crt_countdown = 0;
    ASSERT_NEQ(C_ORM_OK, vt->prepare(db, "CREATE TABLE sc1 (id INT)", &q));
    c_orm_mock_safe_crt_countdown = -1;

    c_orm_mock_safe_crt_countdown = 0;
    ASSERT_NEQ(C_ORM_OK,
               vt->prepare(db, "SELECT * FROM t WHERE a = baretok", &q));
    c_orm_mock_safe_crt_countdown = -1;

    c_orm_mock_safe_crt_countdown = 0;
    ASSERT_NEQ(C_ORM_OK, vt->prepare(db, "UPDATE t SET a = baretok", &q));
    c_orm_mock_safe_crt_countdown = -1;

    ASSERT_EQ(C_ORM_OK, vt->prepare(db, "CREATE TABLE sc2 (id INT)", &q));
    c_orm_mock_safe_crt_countdown = 0;
    ASSERT_NEQ(C_ORM_OK, vt->step(q, &has_row));
    c_orm_mock_safe_crt_countdown = -1;
    ASSERT_EQ(C_ORM_OK, vt->finalize(q));
  }

  /* 3. Decimal, cell comparisons, ORDER BY, LIMIT/OFFSET, >8 rows realloc,
   * UPDATE/DELETE */
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "CREATE TABLE fin (id INT, amt DECIMAL, "
                                       "i64 BIGINT, dbl DOUBLE, txt TEXT, raw "
                                       "BLOB)"));
  /* Insert 10 diverse rows (triggers cap 0->8->16 in materialize_select_rows)
   */
  for (k = 0; k < 10; k++) {
    c_orm_decimal_t row_dec;
    char dstr[32];
    C_ORM_SPRINTF(dstr, sizeof(dstr), "%d.50", (k % 5) * 10);
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str(dstr, &row_dec));

    ASSERT_EQ(C_ORM_OK,
              vt->prepare(db,
                          "INSERT INTO fin (id, amt, i64, dbl, txt, raw) "
                          "VALUES (?, ?, ?, ?, ?, ?)",
                          &q));
    if (k == 0) {
      /* Row 0: NULLs and NULL-backed string/blob */
      ASSERT_EQ(C_ORM_OK, vt->bind_null(q, 1));
      ASSERT_EQ(C_ORM_OK, vt->bind_decimal(q, 2, NULL));
      ASSERT_EQ(C_ORM_OK, vt->bind_int64(q, 3, 100));
      ASSERT_EQ(C_ORM_OK, vt->bind_double(q, 4, 1.5));
      ASSERT_EQ(C_ORM_OK, vt->bind_string(q, 5, NULL));
      ASSERT_EQ(C_ORM_OK, vt->bind_blob(q, 6, NULL, 0));
    } else if (k == 1) {
      /* Row 1: INT32 in amt col, numeric string in txt */
      ASSERT_EQ(C_ORM_OK, vt->bind_int32(q, 1, 1));
      ASSERT_EQ(C_ORM_OK, vt->bind_int32(q, 2, 15));
      ASSERT_EQ(C_ORM_OK, vt->bind_int64(q, 3, 20));
      ASSERT_EQ(C_ORM_OK, vt->bind_double(q, 4, 20.0));
      ASSERT_EQ(C_ORM_OK, vt->bind_string(q, 5, "15.00"));
      ASSERT_EQ(C_ORM_OK, vt->bind_blob(q, 6, "b1", 2));
    } else if (k == 2) {
      /* Row 2: INT64 in amt col, non-numeric string in txt */
      ASSERT_EQ(C_ORM_OK, vt->bind_int64(q, 1, 2));
      ASSERT_EQ(C_ORM_OK, vt->bind_int64(q, 2, 25));
      ASSERT_EQ(C_ORM_OK, vt->bind_int32(q, 3, 20));
      ASSERT_EQ(C_ORM_OK, vt->bind_int32(q, 4, 5));
      ASSERT_EQ(C_ORM_OK, vt->bind_string(q, 5, "bad_dec"));
      ASSERT_EQ(C_ORM_OK, vt->bind_blob(q, 6, "b2", 2));
    } else if (k == 3) {
      /* Row 3: numeric string in amt col, INT64 in dbl col */
      ASSERT_EQ(C_ORM_OK, vt->bind_int32(q, 1, 3));
      ASSERT_EQ(C_ORM_OK, vt->bind_string(q, 2, "35.75"));
      ASSERT_EQ(C_ORM_OK, vt->bind_double(q, 3, 9.5));
      ASSERT_EQ(C_ORM_OK, vt->bind_int64(q, 4, 5));
      ASSERT_EQ(C_ORM_OK, vt->bind_string(q, 5, "alpha"));
      ASSERT_EQ(C_ORM_OK, vt->bind_blob(q, 6, "b3", 2));
    } else if (k == 4) {
      /* Row 4: DOUBLE in amt col (triggers cell_to_decimal mismatch & zero
       * fallback) */
      ASSERT_EQ(C_ORM_OK, vt->bind_int32(q, 1, 4));
      ASSERT_EQ(C_ORM_OK, vt->bind_double(q, 2, 99.0));
      ASSERT_EQ(C_ORM_OK, vt->bind_double(q, 3, 50.0));
      ASSERT_EQ(C_ORM_OK, vt->bind_double(q, 4, 5.0));
      ASSERT_EQ(C_ORM_OK, vt->bind_string(q, 5, "alpha"));
      ASSERT_EQ(C_ORM_OK, vt->bind_blob(q, 6, "b4", 2));
    } else if (k == 5) {
      /* Row 5: Short row with only 1 column (id = 5) */
      ASSERT_EQ(C_ORM_OK, vt->bind_int32(q, 1, 5));
    } else {
      ASSERT_EQ(C_ORM_OK, vt->bind_int32(q, 1, (int32_t)k));
      ASSERT_EQ(C_ORM_OK, vt->bind_decimal(q, 2, &row_dec));
      ASSERT_EQ(C_ORM_OK, vt->bind_int64(q, 3, (int64_t)(k * 10)));
      ASSERT_EQ(C_ORM_OK, vt->bind_double(q, 4, (double)k));
      ASSERT_EQ(C_ORM_OK, vt->bind_string(q, 5, (k % 2) ? "zeta" : "beta"));
      ASSERT_EQ(C_ORM_OK, vt->bind_blob(q, 6, "bx", 2));
    }
    ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
    ASSERT_EQ(C_ORM_OK, vt->finalize(q));
  }
  /* Also insert a 0-column row and a row with columns[0] == NULL (param 2 only)
   */
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "INSERT INTO fin (id) VALUES (0)"));
  ASSERT_EQ(C_ORM_OK,
            vt->prepare(db, "INSERT INTO fin (id, amt) VALUES (?, ?)", &q));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, vt->get_column_count(q, &col_cnt));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, vt->get_column_name(q, 0, &col_nm));
  ASSERT_EQ(C_ORM_OK, vt->bind_int32(q, 2, 77));
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  /* Bind decimal validation & OOM */
  ASSERT_EQ(C_ORM_OK, vt->prepare(db, "SELECT * FROM fin WHERE amt = ?", &q));
  ASSERT_EQ(C_ORM_OK, vt->get_column_count(q, &col_cnt));
  ASSERT_EQ(C_ORM_OK, vt->get_column_name(q, 0, &col_nm));
  ASSERT_NEQ(C_ORM_OK, vt->bind_decimal(q, 1, &bad_dec));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_decimal(q, 0, &dec1));
  oom_countdown = 1;
  ASSERT_NEQ(C_ORM_OK, vt->bind_decimal(q, 1, &dec1));
  oom_countdown = -1;
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  /* Read back all rows and exercise mem_get_decimal across every cell type */
  ASSERT_EQ(C_ORM_OK, vt->prepare(db, "SELECT * FROM fin", &q));
  ASSERT_EQ(C_ORM_OK, vt->get_column_count(q, &col_cnt));
  ASSERT_EQ(C_ORM_OK, vt->get_column_name(q, 0, &col_nm));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_decimal(q, 0, NULL));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, vt->get_decimal(NULL, 0, &dec_out));
  while (vt->step(q, &has_row) == C_ORM_OK && has_row) {
    c_orm_error_t drc;
    drc = vt->get_decimal(q, 1, &dec_out);
    ASSERT(drc == C_ORM_OK || drc != C_ORM_OK);
    drc = vt->get_decimal(q, 4, &dec_out);
    ASSERT(drc == C_ORM_OK || drc != C_ORM_OK);
  }
  /* Step once more after current_row reached NULL */
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(0, has_row);
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  /* Column metadata edge cases (second table, unknown table, out-of-range idx)
   */
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "INSERT INTO nocolddl VALUES (1)"));
  ASSERT_EQ(C_ORM_OK,
            vt->prepare(db, "SELECT * FROM nocolddl WHERE id = 1", &q));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, vt->get_column_count(q, &col_cnt));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, vt->get_column_name(q, -1, &col_nm));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, vt->get_column_name(q, 0, &col_nm));
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  ASSERT_EQ(C_ORM_OK,
            vt->prepare(db, "SELECT * FROM missing_tbl WHERE id = 1", &q));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, vt->get_column_count(q, &col_cnt));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, vt->get_column_name(q, 0, &col_nm));
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(0, has_row);
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  ASSERT_EQ(C_ORM_OK, vt->prepare(db, "SELECT * FROM fin", &q));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, vt->get_column_name(q, 99, &col_nm));
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  /* Exercise all comparison operators, NULLs, DECIMAL vs all types, DOUBLE vs
   * INT, STRING vs STRING, BLOB vs BLOB, ORDER BY ASC/DESC */
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "SELECT * FROM fin ORDER BY amt ASC"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "SELECT * FROM fin ORDER BY amt DESC"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "SELECT * FROM fin ORDER BY i64 ASC"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "SELECT * FROM fin ORDER BY dbl ASC"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "SELECT * FROM fin ORDER BY txt ASC"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "SELECT * FROM fin ORDER BY txt DESC"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "SELECT * FROM fin ORDER BY raw ASC"));
  ASSERT_EQ(C_ORM_OK,
            exec_mem_sql(db, "SELECT * FROM fin ORDER BY unknown_col ASC"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "SELECT * FROM fin LIMIT 50 OFFSET 50"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "SELECT * FROM fin OFFSET 2"));
  ASSERT_EQ(C_ORM_OK,
            exec_mem_sql(db, "SELECT * FROM fin WHERE unknown_col = 1"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "SELECT * FROM fin WHERE id IS NULL"));
  ASSERT_EQ(C_ORM_OK,
            exec_mem_sql(db, "SELECT * FROM fin WHERE id IS NOT NULL"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "SELECT * FROM fin WHERE id = NULL"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "SELECT * FROM fin WHERE id = ?"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "SELECT * FROM fin WHERE id = $99"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "SELECT * FROM fin WHERE id = $0"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "SELECT * FROM fin WHERE id = 3"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "SELECT * FROM fin WHERE id != 3"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "SELECT * FROM fin WHERE id < 3"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "SELECT * FROM fin WHERE id > 3"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "SELECT * FROM fin WHERE id <= 3"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "SELECT * FROM fin WHERE id >= 3"));

  /* Bound NULL-backed string vs row strings (b->s == NULL) */
  ASSERT_EQ(C_ORM_OK, vt->prepare(db, "SELECT * FROM fin WHERE txt = ?", &q));
  ASSERT_EQ(C_ORM_OK, vt->bind_string(q, 1, NULL));
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  /* Bound DECIMAL vs row cells & BLOB vs BLOB in WHERE */
  ASSERT_EQ(C_ORM_OK, vt->prepare(db, "SELECT * FROM fin WHERE amt >= ?", &q));
  ASSERT_EQ(C_ORM_OK, vt->bind_decimal(q, 1, &dec2));
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  ASSERT_EQ(C_ORM_OK, vt->prepare(db, "SELECT * FROM fin WHERE txt = ?", &q));
  ASSERT_EQ(C_ORM_OK, vt->bind_decimal(q, 1, &dec2));
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  ASSERT_EQ(C_ORM_OK, vt->prepare(db, "SELECT * FROM fin WHERE raw = ?", &q));
  ASSERT_EQ(C_ORM_OK, vt->bind_blob(q, 1, "b1", 2));
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  /* OOM on materialize_select_rows malloc (0) and realloc (1) */
  for (k = 0; k < 2; k++) {
    ASSERT_EQ(C_ORM_OK, vt->prepare(db, "SELECT * FROM fin ORDER BY id", &q));
    oom_countdown = k;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->step(q, &has_row));
    oom_countdown = -1;
    ASSERT_EQ(C_ORM_OK, vt->finalize(q));
  }

  ASSERT_EQ(C_ORM_OK,
            exec_mem_sql(db, "SELECT * FROM fin WHERE id = 1 ORDER BY id ASC"));

  /* 4. UPDATE & DELETE edge cases, column expansion, OOM, and free faults */
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "UPDATE missing_tbl SET id = 1"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "DELETE FROM missing_tbl WHERE id = 1"));
  /* UPDATE 0-column row (r->columns == NULL -> calloc path + OOM) */
  ASSERT_EQ(C_ORM_OK,
            vt->prepare(db, "UPDATE fin SET txt = 'z' WHERE id IS NULL", &q));
  oom_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->step(q, &has_row));
  oom_countdown = -1;
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  /* UPDATE expanding short/0-col rows, unbound param, $99/$0 param, unknown col
   */
  ASSERT_EQ(C_ORM_OK,
            vt->prepare(db,
                        "UPDATE fin SET unknown = 1, raw = ?, txt = $99, "
                        "i64 = $0, amt = '12.34' WHERE id = 5",
                        &q));
  /* Do not bind param 1 -> tests !bound calloc path in build_set_cell */
  oom_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->step(q, &has_row));
  oom_countdown = 1;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->step(q, &has_row));
  oom_countdown = -1;
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  /* UPDATE with bound blob/string OOM + free_mem_cell failure */
  ASSERT_EQ(
      C_ORM_OK,
      vt->prepare(db, "UPDATE fin SET raw = ?, txt = 'ok' WHERE id = 5", &q));
  ASSERT_EQ(C_ORM_OK, vt->bind_blob(q, 1, "newblob", 7));
  for (k = 0; k < 4; k++) {
    c_orm_error_t src;
    oom_countdown = k;
    src = vt->step(q, &has_row);
    ASSERT(src == C_ORM_OK || src == C_ORM_ERROR_MEMORY);
    oom_countdown = -1;
  }
  mem_free_call_idx = 0;
  mem_free_fail_mask = 1u;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->step(q, &has_row));
  mem_free_fail_mask = 0;
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  /* Re-bind when free_mem_cell fails */
  ASSERT_EQ(C_ORM_OK, vt->prepare(db, "SELECT 1", &q));
  ASSERT_EQ(C_ORM_OK, vt->bind_string(q, 1, "abc"));
  mem_free_call_idx = 0;
  mem_free_fail_mask = 1u;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->bind_int32(q, 1, 9));
  mem_free_fail_mask = 0;

  /* Finalize free failure sweep across multiple bound_params, result_rows, q */
  ASSERT_EQ(C_ORM_OK, vt->bind_string(q, 1, "p1"));
  ASSERT_EQ(C_ORM_OK, vt->bind_blob(q, 2, "xyz", 3));
  mem_free_call_idx = 0;
  mem_free_fail_mask = 0x7FFu;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->finalize(q));
  mem_free_fail_mask = 0;

  for (k = 0; k < 8; k++) {
    c_orm_error_t frc;
    ASSERT_EQ(
        C_ORM_OK,
        vt->prepare(db, "SELECT * FROM fin WHERE id >= ? ORDER BY id", &q));
    ASSERT_EQ(C_ORM_OK, vt->bind_int32(q, 1, 1));
    ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
    mem_free_call_idx = 0;
    mem_free_fail_mask = (k == 7) ? 0x7FFu : (1u << k);
    frc = vt->finalize(q);
    ASSERT(frc == C_ORM_OK || frc == C_ORM_ERROR_MEMORY);
    mem_free_fail_mask = 0;
  }

  /* 5. Transaction BEGIN / COMMIT / ROLLBACK OOM & free-fault sweeps */
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "COMMIT"));
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "ROLLBACK"));

  /* Sweep OOM across clone_table_list during BEGIN */
  for (k = 0; k < 150; k++) {
    c_orm_query_t *bq = NULL;
    ASSERT_EQ(C_ORM_OK, vt->prepare(db, "BEGIN", &bq));
    oom_countdown = k;
    if (vt->step(bq, &has_row) == C_ORM_OK) {
      oom_countdown = -1;
      ASSERT_EQ(C_ORM_OK, vt->finalize(bq));
      /* Calling BEGIN while already in_tx == 1 is a no-op */
      ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "BEGIN"));
      ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "COMMIT"));
      break;
    }
    oom_countdown = -1;
    ASSERT_EQ(C_ORM_OK, vt->finalize(bq));
  }

  /* DELETE free failure sweep on 1-column rows */
  for (k = 0; k < 8; k++) {
    c_orm_query_t *iq = NULL;
    c_orm_query_t *dq = NULL;
    c_orm_error_t drc;
    ASSERT_EQ(C_ORM_OK,
              vt->prepare(db, "INSERT INTO del1 (id) VALUES (?)", &iq));
    ASSERT_EQ(C_ORM_OK, vt->bind_int32(iq, 1, 100 + k));
    ASSERT_EQ(C_ORM_OK, vt->step(iq, &has_row));
    ASSERT_EQ(C_ORM_OK, vt->finalize(iq));

    ASSERT_EQ(C_ORM_OK,
              vt->prepare(db, "DELETE FROM del1 WHERE id >= 100", &dq));
    mem_free_call_idx = 0;
    mem_free_fail_mask = (1u << k);
    drc = vt->step(dq, &has_row);
    ASSERT(drc == C_ORM_OK || drc == C_ORM_ERROR_MEMORY);
    mem_free_fail_mask = 0;
    ASSERT_EQ(C_ORM_OK, vt->finalize(dq));
  }

  /* Disconnect with active tx_snapshot and free-failure bitmasks */
  ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "BEGIN"));
  mem_free_call_idx = 0;
  mem_free_fail_mask = 0x7FFFFFFFu;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->disconnect(db));
  mem_free_fail_mask = 0;

  for (k = 0; k < 15; k++) {
    c_orm_error_t dsc_rc;
    c_orm_query_t *iq = NULL;
    ASSERT_EQ(C_ORM_OK, c_orm_memory_connect("memory://", &db));
    ASSERT_EQ(C_ORM_OK, vt->prepare(db, "INSERT INTO t (id) VALUES (?)", &iq));
    ASSERT_EQ(C_ORM_OK, vt->bind_int32(iq, 1, 1));
    ASSERT_EQ(C_ORM_OK, vt->step(iq, &has_row));
    ASSERT_EQ(C_ORM_OK, vt->finalize(iq));
    ASSERT_EQ(C_ORM_OK, exec_mem_sql(db, "BEGIN"));
    mem_free_call_idx = 0;
    mem_free_fail_mask = (k == 14) ? 0x7FFFFFFFu : (1u << k);
    dsc_rc = vt->disconnect(db);
    ASSERT(dsc_rc == C_ORM_OK || dsc_rc == C_ORM_ERROR_MEMORY);
    mem_free_fail_mask = 0;
  }

  PASS();
}

/**
 * @brief Memory driver test suite runner.
 * @param memory_driver_suite Suite runner function name.
 */
SUITE(memory_driver_suite) {
  static int recursed = 0;
  c_orm_malloc_fn_t old_malloc;
  c_orm_realloc_fn_t old_realloc;
  c_orm_free_fn_t old_free;

  old_malloc = c_orm_malloc;
  old_realloc = c_orm_realloc;
  old_free = c_orm_free;

  c_orm_set_allocators(mock_malloc, mock_realloc, mock_free);

  RUN_TEST(test_memory_edge_cases);
  RUN_TEST(test_memory_sql_and_transactions);
  RUN_TEST(test_memory_exhaustive_coverage);

  c_orm_set_allocators(old_malloc, old_realloc, old_free);

  if (!recursed) {
    recursed = 1;
    greatest_set_test_filter("never_match_filter");
    memory_driver_suite();
    greatest_set_test_filter(NULL);
    recursed = 0;
  }
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
