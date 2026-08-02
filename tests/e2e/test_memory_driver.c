/* clang-format off */
#include "c_orm_memory.h"
#include "greatest.h"
#include <stdio.h>
#include <string.h>
/* clang-format on */

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

TEST test_memory_edge_cases(void) {

  c_orm_db_t *db = NULL;
  const c_orm_driver_vtable_t *vt = NULL;
  c_orm_query_t *q = NULL;
  c_orm_error_t err;
  int64_t id;
  int count;

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

  {
    int has_row = 0;

    err = vt->step(q, &has_row);
    ASSERT_EQ(C_ORM_OK, err);
    ASSERT_EQ(0, has_row);
  }

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

  {
    const char *msg = NULL;
    vt->get_last_error(db, &msg);
    ASSERT_STR_EQ("", msg);
  }

  err = vt->finalize(q);
  ASSERT_EQ(C_ORM_OK, err);

  vt->disconnect(db);

  /* Test OOM in mem_connect */
  oom_active = 1;
  oom_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_memory_connect("mem://", &db));
  oom_countdown = 1;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_memory_connect("mem://", &db));
  oom_active = 0;

  /* Test parsing whitespace before table name */
  err = c_orm_memory_connect("mem://", &db);
  err = vt->prepare(db, "SELECT * FROM    my_table", &q);
  ASSERT_EQ(C_ORM_OK, err);
  err = vt->finalize(q);

  /* OOM in prepare */
  oom_active = 1;
  oom_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->prepare(db, "SELECT * FROM t", &q));
  oom_countdown = 1;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->prepare(db, "SELECT * FROM t", &q));
  oom_active = 0;

  /* Coverage for pointers passed in */
  {
    int out_null;
    int64_t out_id;
    int out_count;
    const char *out_name;
    const char *msg;

    vt->is_null(NULL, 0, &out_null);
    vt->get_column_name(NULL, 0, &out_name);

    /* Test get_last_error with null db */
  }

  /* Test disconnect with allocated tables (mocking internal structs is hard so
   * we just rely on standard path) */
  /* Actually, c_orm_memory_db_t has a tables pointer. To cover mem_disconnect
   * we need a table. But mem_prepare doesn't create tables. Let's force a table
   * by casting db->driver_data */
  {
    typedef struct mem_row {
      void **columns;
      size_t num_cols;
      struct mem_row *next;
    } mem_row_t;

    typedef struct mem_table {
      char *name;
      mem_row_t *head;
      struct mem_table *next;
    } mem_table_t;

    typedef struct {
      mem_table_t *tables;
      char last_error[256];
    } c_orm_memory_db_t;

    c_orm_memory_db_t *ctx = (c_orm_memory_db_t *)db->driver_data;
    mem_table_t *t = (mem_table_t *)C_ORM_MALLOC(sizeof(mem_table_t));
    mem_row_t *r = (mem_row_t *)C_ORM_MALLOC(sizeof(mem_row_t));

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

SUITE(memory_driver_suite) {
  void *(*old_malloc)(size_t) = c_orm_malloc;
  void (*old_free)(void *) = c_orm_free;

  c_orm_set_allocators(mock_malloc, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, mock_free);

  RUN_TEST(test_memory_edge_cases);

  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, old_free);
}
