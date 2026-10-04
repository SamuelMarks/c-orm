#if defined(__clang__) || defined(__GNUC__)
#endif
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
  err = vt->finalize(q);
  ASSERT_EQ(C_ORM_OK, err);

  err = vt->prepare(db, "SELECT * FROM t WHERE id = 1", &q);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->finalize(q);
  ASSERT_EQ(C_ORM_OK, err);

  err = vt->prepare(db, "INSERT INTO t (id) VALUES (1)", &q);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->finalize(q);
  ASSERT_EQ(C_ORM_OK, err);

  err = vt->prepare(db, "INSERT INTO t(id) VALUES (1)", &q);
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

  err = vt->bind_int32(q, 1, 1);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->bind_int64(q, 1, 1);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->bind_double(q, 1, 1.0);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->bind_string(q, 1, "test");
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->bind_blob(q, 1, "test", 4);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->bind_null(q, 1);
  ASSERT_EQ(C_ORM_OK, err);

  err = vt->step(q, &has_row);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(0, has_row);

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

  err = vt->reset(q);
  ASSERT_EQ(C_ORM_OK, err);

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

  /* Test parsing whitespace before table name */
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

  /* Coverage for pointers passed in */
  vt->is_null(NULL, 0, &out_null);
  vt->get_column_name(NULL, 0, &out_name);

  /* Test disconnect with allocated tables */
  {
    /** @brief Mock memory row structure for testing memory cleanup. */
    typedef struct mem_row {
      /** @brief Pointer array of column values. */
      void **columns;
      /** @brief Number of columns stored in row. */
      size_t num_cols;
      /** @brief Pointer to next row in list. */
      struct mem_row *next;
    } mem_row_t;

    /** @brief Mock memory table structure for testing table teardown. */
    typedef struct mem_table {
      /** @brief Table name string. */
      char *name;
      /** @brief Pointer to head row of table. */
      mem_row_t *head;
      /** @brief Pointer to next table in list. */
      struct mem_table *next;
    } mem_table_t;

    /** @brief Mock memory database driver context. */
    typedef struct {
      /** @brief Linked list head of memory tables. */
      mem_table_t *tables;
      /** @brief Last recorded error message buffer. */
      char last_error[256];
    } c_orm_memory_db_t;

    c_orm_memory_db_t *ctx;
    mem_table_t *t;
    mem_row_t *r;

    ctx = (c_orm_memory_db_t *)db->driver_data;
    t = (mem_table_t *)C_ORM_MALLOC(sizeof(mem_table_t));
    r = (mem_row_t *)C_ORM_MALLOC(sizeof(mem_row_t));

    C_ORM_STRDUP("mock_table", &t->name);
    t->next = NULL;
    t->head = r;

    r->columns = (void **)C_ORM_MALLOC(sizeof(void *));
    r->num_cols = 1;
    r->next = NULL;

    ctx->tables = t;
  }

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

#if defined(__clang__) || defined(__GNUC__)
#endif
