/**
 * @file test_memory_driver.c
 * @brief Unit tests for in-memory database driver implementation.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
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

/**
 * @brief Mock malloc callback returning NULL on countdown expiry.
 * @param size Requested allocation size.
 * @return Allocated memory or NULL on failure.
 */
static void *mock_malloc(size_t size) {
  if (oom_countdown == 0) {
    oom_countdown--;
    return NULL;
  }
  oom_countdown--;
  return malloc(size);
}

/**
 * @brief Mock free callback forwarding to free.
 * @param ptr Pointer to memory to free.
 */
static void mock_free(void *ptr) { free(ptr); }

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
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);
  ASSERT_EQ(0, id);

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
 * @brief Memory driver test suite runner.
 * @param memory_driver_suite Suite runner function name.
 */
SUITE(memory_driver_suite) {
  static int recursed = 0;
  void *(*old_malloc)(size_t);
  void (*old_free)(void *);

  old_malloc = c_orm_malloc;
  old_free = c_orm_free;

  c_orm_set_allocators(mock_malloc, c_orm_realloc, mock_free);

  RUN_TEST(test_memory_edge_cases);

  c_orm_set_allocators(old_malloc, c_orm_realloc, old_free);

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
