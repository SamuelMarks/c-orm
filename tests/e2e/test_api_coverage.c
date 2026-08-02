/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_db.h"
#include "c_orm_log.h"
#include "Models.h"
#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

static void dummy_cb(c_orm_error_t err, void *ctx) {
  (void)err;
  (void)ctx;
}
static c_orm_error_t mock_decrypt(const void *in, size_t ins, void *ctx,
                                  void **out, size_t *outs) {
  (void)ctx;
  *out = c_orm_malloc(ins);
  if (!*out)
    return C_ORM_ERROR_MEMORY;
  memcpy(*out, in, ins);
  *outs = ins;
  return C_ORM_OK;
}
static c_orm_error_t mock_encrypt(const void *in, size_t ins, void *ctx,
                                  void **out, size_t *outs) {
  (void)ctx;
  *out = c_orm_malloc(ins);
  if (!*out)
    return C_ORM_ERROR_MEMORY;
  memcpy(*out, in, ins);
  *outs = ins;
  return C_ORM_OK;
}

static int g_malloc_fail = 0;
static int g_malloc_count = 0;
static int g_malloc_target = -1;
static void test_free_meta_data(const c_orm_table_meta_t *meta, void *obj) {
  size_t i;
  for (i = 0; i < meta->num_columns; i++) {
    const c_orm_column_meta_t *col = &meta->columns[i];
    void *field_ptr = (char *)obj + col->offset;
    if (col->type == C_ORM_TYPE_STRING || col->type == C_ORM_TYPE_DATE ||
        col->type == C_ORM_TYPE_TIMESTAMP || col->type == C_ORM_TYPE_ENUM ||
        col->type == C_ORM_TYPE_SET || col->type == C_ORM_TYPE_JSON) {
      if (*(char **)field_ptr) {
        free(*(char **)field_ptr);
        *(char **)field_ptr = NULL;
      }
    } else if (col->type == C_ORM_TYPE_BLOB) {
      c_orm_blob_t *b = (c_orm_blob_t *)field_ptr;
      if (b->data)
        free(b->data);
      b->data = NULL;
      b->size = 0;
    } else if (col->type == C_ORM_TYPE_POLYGON) {
      c_orm_polygon_t *p = (c_orm_polygon_t *)field_ptr;
      if (p->points)
        free(p->points);
      p->points = NULL;
      p->num_points = 0;
    } else if (col->is_nullable) {
      if (*(void **)field_ptr) {
        free(*(void **)field_ptr);
        *(void **)field_ptr = NULL;
      }
    }
  }
}

static void *tracked_allocs[20000];
static int tracked_count = 0;

static void *mock_malloc_fail(size_t size) {
  void *ptr;
  if (g_malloc_fail) {
    if (g_malloc_target == g_malloc_count++)
      return NULL;
  }
  ptr = malloc(size);
  if (ptr)
    tracked_allocs[tracked_count++] = ptr;
  return ptr;
}

static void *mock_realloc_fail(void *ptr, size_t size) {
  void *new_ptr;
  int i;
  if (g_malloc_fail) {
    if (g_malloc_target == g_malloc_count++)
      return NULL;
  }
  new_ptr = realloc(ptr, size);
  if (ptr) {
    for (i = 0; i < tracked_count; i++) {
      if (tracked_allocs[i] == ptr) {
        tracked_allocs[i] = new_ptr;
        return new_ptr;
      }
    }
  }
  if (new_ptr)
    tracked_allocs[tracked_count++] = new_ptr;
  return new_ptr;
}

static void mock_free(void *ptr) {
  int i;
  if (!ptr)
    return;
  for (i = 0; i < tracked_count; i++) {
    if (tracked_allocs[i] == ptr) {
      tracked_allocs[i] = NULL;
      break;
    }
  }
  free(ptr);
}

static void free_tracked(void) {
  int i;
  for (i = 0; i < tracked_count; i++) {
    if (tracked_allocs[i]) {
      free(tracked_allocs[i]);
      tracked_allocs[i] = NULL;
    }
  }
  tracked_count = 0;
}

static int g_db_fail = 0;
static int g_db_count = 0;
static int g_db_target = -1;
static c_orm_error_t check_db_fail(void) {
  if (g_db_fail) {
    if (g_db_target == g_db_count++)
      return C_ORM_ERROR_UNKNOWN;
  }
  return C_ORM_OK;
}

static int g_step_count = 0;

static c_orm_error_t mock_is_null(c_orm_query_t *q, int i, int *out) {
  c_orm_error_t rc = check_db_fail();
  if (rc != C_ORM_OK)
    return rc;
  if (out)
    *out = 0;
  (void)q;
  (void)i;
  return C_ORM_OK;
}
static c_orm_error_t mock_prepare(c_orm_db_t *db, const char *sql,
                                  c_orm_query_t **out) {
  c_orm_error_t rc = check_db_fail();
  if (rc != C_ORM_OK)
    return rc;
  if (out)
    *out = (c_orm_query_t *)1;
  (void)db;
  (void)sql;
  return C_ORM_OK;
}
static c_orm_error_t mock_step(c_orm_query_t *q, int *out) {
  c_orm_error_t rc = check_db_fail();
  if (rc != C_ORM_OK)
    return rc;
  if (out)
    *out = (g_step_count++ == 0) ? 1 : 0;
  (void)q;
  return C_ORM_OK;
}
static c_orm_error_t mock_get_string(c_orm_query_t *q, int i,
                                     const char **out) {
  c_orm_error_t rc = check_db_fail();
  if (rc != C_ORM_OK)
    return rc;
  if (out)
    *out = "2024-01-01 12:00:00";
  (void)q;
  (void)i;
  return C_ORM_OK;
}
static c_orm_error_t mock_bind_int32(c_orm_query_t *q, int i, int32_t v) {
  (void)q;
  (void)i;
  (void)v;
  return check_db_fail();
}
static c_orm_error_t mock_bind_int64(c_orm_query_t *q, int i, int64_t v) {
  (void)q;
  (void)i;
  (void)v;
  return check_db_fail();
}
static c_orm_error_t mock_bind_double(c_orm_query_t *q, int i, double v) {
  (void)q;
  (void)i;
  (void)v;
  return check_db_fail();
}
static c_orm_error_t mock_bind_string(c_orm_query_t *q, int i, const char *v) {
  (void)q;
  (void)i;
  (void)v;
  return check_db_fail();
}
static c_orm_error_t mock_bind_blob(c_orm_query_t *q, int i, const void *v,
                                    size_t s) {
  (void)q;
  (void)i;
  (void)v;
  (void)s;
  return check_db_fail();
}
static c_orm_error_t mock_bind_null(c_orm_query_t *q, int i) {
  (void)q;
  (void)i;
  return check_db_fail();
}
static c_orm_error_t mock_get_int32(c_orm_query_t *q, int i, int32_t *o) {
  (void)q;
  (void)i;
  (void)o;
  return check_db_fail();
}
static c_orm_error_t mock_get_int64(c_orm_query_t *q, int i, int64_t *o) {
  (void)q;
  (void)i;
  (void)o;
  return check_db_fail();
}
static c_orm_error_t mock_get_double(c_orm_query_t *q, int i, double *o) {
  (void)q;
  (void)i;
  (void)o;
  return check_db_fail();
}
static c_orm_error_t mock_get_blob(c_orm_query_t *q, int i, const void **o,
                                   size_t *s) {
  c_orm_error_t rc = check_db_fail();
  if (rc != C_ORM_OK)
    return rc;
  if (o)
    *o = "blobdata";
  if (s)
    *s = 8;
  (void)q;
  (void)i;
  return C_ORM_OK;
}
static c_orm_error_t mock_finalize(c_orm_query_t *q) {
  (void)q;
  return C_ORM_OK;
}
static c_orm_error_t mock_reset(c_orm_query_t *q) {
  (void)q;
  return C_ORM_OK;
}

static c_orm_driver_vtable_t g_vt = {0};
static c_orm_db_t g_db = {0};

static c_orm_column_meta_t my_cols[20];
static c_orm_table_meta_t mega_meta;

static void setup_vt(void) {
  g_vt.is_null = mock_is_null;
  g_vt.prepare = mock_prepare;
  g_vt.step = mock_step;
  g_vt.get_string = mock_get_string;
  g_vt.bind_int32 = mock_bind_int32;
  g_vt.bind_int64 = mock_bind_int64;
  g_vt.bind_double = mock_bind_double;
  g_vt.bind_string = mock_bind_string;
  g_vt.bind_blob = mock_bind_blob;
  g_vt.bind_null = mock_bind_null;
  g_vt.get_int32 = mock_get_int32;
  g_vt.get_int64 = mock_get_int64;
  g_vt.get_double = mock_get_double;
  g_vt.get_blob = mock_get_blob;
  g_vt.finalize = mock_finalize;
  g_vt.reset = mock_reset;
  g_db.vtable = &g_vt;
  g_db.timezone.offset_minutes = 60;
  g_db.decrypt_hook = mock_decrypt;
  g_db.encrypt_hook = mock_encrypt;
  (void)dummy_cb;

  memcpy(&mega_meta, &Users_meta, sizeof(c_orm_table_meta_t));
  memcpy(my_cols, Users_meta.columns,
         sizeof(c_orm_column_meta_t) * Users_meta.num_columns);
  mega_meta.columns = my_cols;

  my_cols[mega_meta.num_columns].name = "test_blob";
  my_cols[mega_meta.num_columns].type = C_ORM_TYPE_BLOB;
  my_cols[mega_meta.num_columns].is_secure = 1;
  my_cols[mega_meta.num_columns].offset = 64;
  mega_meta.num_columns++;

  my_cols[mega_meta.num_columns].name = "test_ts";
  my_cols[mega_meta.num_columns].type = C_ORM_TYPE_TIMESTAMP;
  my_cols[mega_meta.num_columns].is_secure = 0;
  my_cols[mega_meta.num_columns].offset = 72;
  mega_meta.num_columns++;

  my_cols[mega_meta.num_columns].name = "test_polygon";
  my_cols[mega_meta.num_columns].type = C_ORM_TYPE_POLYGON;
  my_cols[mega_meta.num_columns].is_secure = 0;
  my_cols[mega_meta.num_columns].offset = 80;
  mega_meta.num_columns++;

  /* Reset relations so they don't crash us */
  mega_meta.relations = NULL;
  mega_meta.num_relations = 0;
}

#define TEST_OOM(test_func, max_allocs)                                        \
  do {                                                                         \
    int i;                                                                     \
    for (i = 0; i < max_allocs; i++) {                                         \
      g_malloc_target = i;                                                     \
      g_malloc_count = 0;                                                      \
      g_step_count = 0;                                                        \
      g_malloc_fail = 1;                                                       \
      g_step_count = 0;                                                        \
      test_func();                                                             \
      free_tracked();                                                          \
      g_malloc_fail = 0;                                                       \
      if (g_malloc_count < i)                                                  \
        break;                                                                 \
    }                                                                          \
    g_malloc_fail = 0;                                                         \
    g_step_count = 0;                                                          \
    test_func();                                                               \
    free_tracked();                                                            \
  } while (0)

#define TEST_DB(test_func, max_calls)                                          \
  do {                                                                         \
    int i;                                                                     \
    for (i = 0; i < max_calls; i++) {                                          \
      g_db_target = i;                                                         \
      g_db_count = 0;                                                          \
      g_step_count = 0;                                                        \
      g_db_fail = 1;                                                           \
      g_step_count = 0;                                                        \
      test_func();                                                             \
      free_tracked();                                                          \
      g_db_fail = 0;                                                           \
      if (g_db_count < i)                                                      \
        break;                                                                 \
    }                                                                          \
    g_db_fail = 0;                                                             \
    g_step_count = 0;                                                          \
    test_func();                                                               \
    free_tracked();                                                            \
  } while (0)

static void test_mock_vt(void) {
  int i = 0;
  int64_t i64 = 0;
  double d = 0;
  const void *blob = NULL;
  size_t sz = 0;
  void *out = NULL;
  size_t outs = 0;
  mock_bind_int64(NULL, 0, 0);
  mock_bind_double(NULL, 0, 0.0);
  mock_bind_string(NULL, 0, NULL);
  mock_bind_blob(NULL, 0, NULL, 0);
  mock_bind_null(NULL, 0);
  mock_get_int32(NULL, 0, &i);
  mock_get_int64(NULL, 0, &i64);
  mock_get_double(NULL, 0, &d);
  mock_get_blob(NULL, 0, &blob, &sz);
  mock_finalize(NULL);
  mock_reset(NULL);
  g_malloc_fail = 1;
  g_malloc_target = 0;
  g_malloc_count = 0;
  mock_decrypt("a", 1, NULL, &out, &outs);
  g_malloc_fail = 1;
  g_malloc_target = 0;
  g_malloc_count = 0;
  mock_encrypt("a", 1, NULL, &out, &outs);
  g_malloc_fail = 0;
  mock_decrypt("a", 1, NULL, &out, &outs);
  c_orm_free(out);
  mock_encrypt("a", 1, NULL, &out, &outs);
  c_orm_free(out);
}

static void test_c_orm_validate(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_validate(&mega_meta, buf);
}
static void test_c_orm_find_by_id_int32(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_find_by_id_int32(&g_db, &mega_meta, 1, buf);
}
static void test_c_orm_find_by_composite_key(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_find_by_composite_key(&g_db, &mega_meta, 1, NULL, buf);
}
static void test_c_orm_update_by_composite_key(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_update_by_composite_key(&g_db, &mega_meta, 1, NULL, buf);
}
static void test_c_orm_delete_by_composite_key(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_delete_by_composite_key(&g_db, &mega_meta, 1, NULL);
}
static void test_c_orm_find_all(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_find_all(&g_db, &mega_meta, buf);
}
static void test_c_orm_insert(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_insert(&g_db, &mega_meta, buf);
}
static void test_c_orm_save(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_save(&g_db, &mega_meta, buf);
}
static void test_c_orm_update(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_update(&g_db, &mega_meta, buf);
}
static void test_c_orm_delete(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_delete(&g_db, &mega_meta, buf);
}
static void test_c_orm_delete_by_id_int32(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_delete_by_id_int32(&g_db, &mega_meta, 1);
}
static void test_c_orm_delete_by_id_string(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_delete_by_id_string(&g_db, &mega_meta, "test");
}
static void test_c_orm_update_partial(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_update_partial(&g_db, &mega_meta, buf, str_arr, 1);
}
static void test_c_orm_exists_int32(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_exists_int32(&g_db, &mega_meta, 1, &int_out);
}
static void test_c_orm_exists_string(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_exists_string(&g_db, &mega_meta, "test", &int_out);
}
static void test_c_orm_find_all_paginated(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_find_all_paginated(&g_db, &mega_meta, buf, 1, 1);
}
static void test_c_orm_delete_all(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_delete_all(&g_db, &mega_meta);
}
static void test_c_orm_find_by_id_string(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_find_by_id_string(&g_db, &mega_meta, "test", buf);
}
static void test_c_orm_find_for_update_by_id_string(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_find_for_update_by_id_string(&g_db, &mega_meta, "test", buf);
}
static void test_c_orm_find_for_update_by_id_int32(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_find_for_update_by_id_int32(&g_db, &mega_meta, 1, buf);
}
static void test_c_orm_find_one_by_string(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_find_one_by_string(&g_db, &mega_meta, "test", "test", buf);
}
static void test_c_orm_hydrate_all(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_hydrate_all(&g_db, (c_orm_query_t *)1, &mega_meta, buf);
}
static void test_c_orm_hydrate_row_from(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_hydrate_row_from(&g_db, (c_orm_query_t *)1, &mega_meta, buf, 1);
}
static void test_c_orm_hydrate_row(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_hydrate_row(&g_db, (c_orm_query_t *)1, &mega_meta, buf);
}
static void test_c_orm_hydrate_cache_row(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_hydrate_cache_row(&g_db, &mega_meta, buf, &void_ptr);
}
static void test_c_orm_execute_raw(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_execute_raw(&g_db, "test");
}
static void test_c_orm_transaction_begin(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_transaction_begin(&g_db);
}
static void test_c_orm_transaction_commit(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_transaction_commit(&g_db);
}
static void test_c_orm_transaction_rollback(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_transaction_rollback(&g_db);
}
static void test_c_orm_savepoint_create(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_savepoint_create(&g_db, "test");
}
static void test_c_orm_savepoint_rollback(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_savepoint_rollback(&g_db, "test");
}
static void test_c_orm_savepoint_release(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_savepoint_release(&g_db, "test");
}
static void test_c_orm_get_field_value(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_get_field_value(&mega_meta, buf, "test", NULL);
}
static void test_c_orm_set_field_value(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_set_field_value(&mega_meta, buf, "test", NULL);
}
static void test_c_orm_hydrate_abstract_all(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_hydrate_abstract_all(&g_db, (c_orm_query_t *)1, NULL);
}
static void test_c_orm_find_all_abstract(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_find_all_abstract(&g_db, "test", NULL);
}
static void test_c_orm_abstract_free(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_abstract_free(NULL);
}
static void test_c_orm_to_json(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_to_json(&mega_meta, buf, &str_ptr);
}
static void test_c_orm_from_json(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_from_json(&mega_meta, "test", buf);
}
static void test_c_orm_to_dict(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_to_dict(&mega_meta, buf, NULL);
}
static void test_c_orm_from_dict(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_from_dict(&mega_meta, NULL, buf);
}
static void test_c_orm_abstract_to_json(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_abstract_to_json(NULL, &str_ptr);
}
static void test_c_orm_abstract_from_json(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_abstract_from_json("test", NULL);
}
static void test_c_orm_deep_free(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_deep_free(NULL, buf);
  c_orm_deep_free((const struct cdd_c_meta *)&mega_meta, NULL);
  c_orm_deep_free((const struct cdd_c_meta *)&mega_meta, buf);
}
static void test_c_orm_deep_copy(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_deep_copy(NULL, buf, buf);
}
static void test_c_orm_insert_async(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_insert_async(&g_db, &mega_meta, buf, dummy_cb, NULL);
}
static void test_c_orm_find_all_async(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_find_all_async(&g_db, &mega_meta, buf, dummy_cb, NULL);
}
static void test_c_orm_hydrate_routed(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_hydrate_routed(&g_db, (c_orm_query_t *)1, 1, buf);
}
static void test_c_orm_config_sqlite_pragma(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_config_sqlite_pragma(&g_db, "test");
}
static void test_c_orm_config_postgres_set(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_config_postgres_set(&g_db, "test");
}
static void test_c_orm_config_mysql_session(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_config_mysql_session(&g_db, "test");
}
static void test_c_orm_shard_manager_init(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_shard_manager_init(1, &sm_ptr);
}
static void test_c_orm_escape_string(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_escape_string(&g_db, "test", buf, 1);
}
static void test_c_orm_enable_statement_caching(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_enable_statement_caching(&g_db, 1);
  g_db.stmt_cache = NULL;
}
static void test_c_orm_disable_statement_caching(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_enable_statement_caching(&g_db, 10);
  c_orm_disable_statement_caching(&g_db);
  g_db.stmt_cache = NULL;
}
static void test_c_orm_lazy_load(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_lazy_load(&g_db, &mega_meta, buf, "test");
}
static void test_c_orm_lazy_load_paginated(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_lazy_load_paginated(&g_db, &mega_meta, buf, "test", 1, 1);
}
static void test_c_orm_prepare_cached(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_prepare_cached(&g_db, "test", &q_ptr);
}
static void test_c_orm_finalize_cached(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_finalize_cached(&g_db, (c_orm_query_t *)1);
}
static void test_c_orm_insert_generic(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_insert_generic(&g_db, &mega_meta, buf);
}
static void test_c_orm_get_generic(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_get_generic(&g_db, &mega_meta, 1, buf);
}
static void test_c_orm_find_all_generic(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_find_all_generic(&g_db, &mega_meta, &void_ptr, &sz_ptr);
}
static void test_c_orm_get_generic_string(void) {
  char buf[1024] = {0};
  void *void_ptr = NULL;
  char *str_ptr = NULL;
  const char *str_arr[2] = {"a", "b"};
  c_orm_query_t *q_ptr = NULL;
  c_orm_db_t *db_ptr = NULL;
  c_orm_pool_t *pool_ptr = NULL;
  c_orm_shard_manager_t *sm_ptr = NULL;
  c_orm_relation_meta_t *rel_ptr = NULL;
  size_t sz_ptr = 0;
  const c_orm_table_meta_t *m_arr[1] = {&mega_meta};
  int int_out = 0;
  (void)m_arr;
  (void)str_ptr;
  (void)void_ptr;
  (void)q_ptr;
  (void)db_ptr;
  (void)pool_ptr;
  (void)sm_ptr;
  (void)rel_ptr;
  (void)sz_ptr;
  (void)int_out;
  (void)str_arr;
  c_orm_get_generic_string(&g_db, &mega_meta, "test", buf);
}
TEST run_all_api(void) {
  setup_vt();
  TEST_OOM(test_c_orm_validate, 2);
  TEST_DB(test_c_orm_validate, 2);
  test_mock_vt();
  TEST_OOM(test_c_orm_find_by_id_int32, 2);
  TEST_DB(test_c_orm_find_by_id_int32, 2);
  TEST_OOM(test_c_orm_find_by_composite_key, 2);
  TEST_DB(test_c_orm_find_by_composite_key, 2);
  TEST_OOM(test_c_orm_update_by_composite_key, 2);
  TEST_DB(test_c_orm_update_by_composite_key, 2);
  TEST_OOM(test_c_orm_delete_by_composite_key, 2);
  TEST_DB(test_c_orm_delete_by_composite_key, 2);
  TEST_OOM(test_c_orm_find_all, 2);
  TEST_DB(test_c_orm_find_all, 2);
  TEST_OOM(test_c_orm_insert, 2);
  TEST_DB(test_c_orm_insert, 2);
  TEST_OOM(test_c_orm_save, 2);
  TEST_DB(test_c_orm_save, 2);
  TEST_OOM(test_c_orm_update, 2);
  TEST_DB(test_c_orm_update, 2);
  TEST_OOM(test_c_orm_delete, 2);
  TEST_DB(test_c_orm_delete, 2);
  TEST_OOM(test_c_orm_delete_by_id_int32, 2);
  TEST_DB(test_c_orm_delete_by_id_int32, 2);
  TEST_OOM(test_c_orm_delete_by_id_string, 2);
  TEST_DB(test_c_orm_delete_by_id_string, 2);
  TEST_OOM(test_c_orm_update_partial, 2);
  TEST_DB(test_c_orm_update_partial, 2);
  TEST_OOM(test_c_orm_exists_int32, 2);
  TEST_DB(test_c_orm_exists_int32, 2);
  TEST_OOM(test_c_orm_exists_string, 2);
  TEST_DB(test_c_orm_exists_string, 2);
  TEST_OOM(test_c_orm_find_all_paginated, 2);
  TEST_DB(test_c_orm_find_all_paginated, 2);
  TEST_OOM(test_c_orm_delete_all, 2);
  TEST_DB(test_c_orm_delete_all, 2);
  TEST_OOM(test_c_orm_find_by_id_string, 2);
  TEST_DB(test_c_orm_find_by_id_string, 2);
  TEST_OOM(test_c_orm_find_for_update_by_id_string, 2);
  TEST_DB(test_c_orm_find_for_update_by_id_string, 2);
  TEST_OOM(test_c_orm_find_for_update_by_id_int32, 2);
  TEST_DB(test_c_orm_find_for_update_by_id_int32, 2);
  TEST_OOM(test_c_orm_find_one_by_string, 2);
  TEST_DB(test_c_orm_find_one_by_string, 2);
  TEST_OOM(test_c_orm_hydrate_all, 2);
  TEST_DB(test_c_orm_hydrate_all, 2);
  TEST_OOM(test_c_orm_hydrate_row_from, 2);
  TEST_DB(test_c_orm_hydrate_row_from, 2);
  TEST_OOM(test_c_orm_hydrate_row, 2);
  TEST_DB(test_c_orm_hydrate_row, 2);
  TEST_OOM(test_c_orm_hydrate_cache_row, 2);
  TEST_DB(test_c_orm_hydrate_cache_row, 2);
  TEST_OOM(test_c_orm_execute_raw, 2);
  TEST_DB(test_c_orm_execute_raw, 2);
  TEST_OOM(test_c_orm_transaction_begin, 2);
  TEST_DB(test_c_orm_transaction_begin, 2);
  TEST_OOM(test_c_orm_transaction_commit, 2);
  TEST_DB(test_c_orm_transaction_commit, 2);
  TEST_OOM(test_c_orm_transaction_rollback, 2);
  TEST_DB(test_c_orm_transaction_rollback, 2);
  TEST_OOM(test_c_orm_savepoint_create, 2);
  TEST_DB(test_c_orm_savepoint_create, 2);
  TEST_OOM(test_c_orm_savepoint_rollback, 2);
  TEST_DB(test_c_orm_savepoint_rollback, 2);
  TEST_OOM(test_c_orm_savepoint_release, 2);
  TEST_DB(test_c_orm_savepoint_release, 2);
  TEST_OOM(test_c_orm_get_field_value, 2);
  TEST_DB(test_c_orm_get_field_value, 2);
  TEST_OOM(test_c_orm_set_field_value, 2);
  TEST_DB(test_c_orm_set_field_value, 2);
  TEST_OOM(test_c_orm_hydrate_abstract_all, 2);
  TEST_DB(test_c_orm_hydrate_abstract_all, 2);
  TEST_OOM(test_c_orm_find_all_abstract, 2);
  TEST_DB(test_c_orm_find_all_abstract, 2);
  TEST_OOM(test_c_orm_abstract_free, 2);
  TEST_DB(test_c_orm_abstract_free, 2);
  TEST_OOM(test_c_orm_to_json, 2);
  TEST_DB(test_c_orm_to_json, 2);
  TEST_OOM(test_c_orm_from_json, 2);
  TEST_DB(test_c_orm_from_json, 2);
  TEST_OOM(test_c_orm_to_dict, 2);
  TEST_DB(test_c_orm_to_dict, 2);
  TEST_OOM(test_c_orm_from_dict, 2);
  TEST_DB(test_c_orm_from_dict, 2);
  TEST_OOM(test_c_orm_abstract_to_json, 2);
  TEST_DB(test_c_orm_abstract_to_json, 2);
  TEST_OOM(test_c_orm_abstract_from_json, 2);
  TEST_DB(test_c_orm_abstract_from_json, 2);
  TEST_OOM(test_c_orm_deep_free, 2);
  TEST_DB(test_c_orm_deep_free, 2);
  TEST_OOM(test_c_orm_deep_copy, 2);
  TEST_DB(test_c_orm_deep_copy, 2);
  TEST_OOM(test_c_orm_insert_async, 2);
  TEST_DB(test_c_orm_insert_async, 2);
  TEST_OOM(test_c_orm_find_all_async, 2);
  TEST_DB(test_c_orm_find_all_async, 2);
  TEST_OOM(test_c_orm_hydrate_routed, 2);
  TEST_DB(test_c_orm_hydrate_routed, 2);
  TEST_OOM(test_c_orm_config_sqlite_pragma, 2);
  TEST_DB(test_c_orm_config_sqlite_pragma, 2);
  TEST_OOM(test_c_orm_config_postgres_set, 2);
  TEST_DB(test_c_orm_config_postgres_set, 2);
  TEST_OOM(test_c_orm_config_mysql_session, 2);
  TEST_DB(test_c_orm_config_mysql_session, 2);
  TEST_OOM(test_c_orm_shard_manager_init, 2);
  TEST_DB(test_c_orm_shard_manager_init, 2);
  TEST_OOM(test_c_orm_escape_string, 2);
  TEST_DB(test_c_orm_escape_string, 2);
  TEST_OOM(test_c_orm_enable_statement_caching, 2);
  TEST_DB(test_c_orm_enable_statement_caching, 2);
  TEST_OOM(test_c_orm_disable_statement_caching, 2);
  TEST_DB(test_c_orm_disable_statement_caching, 2);
  TEST_OOM(test_c_orm_lazy_load, 2);
  TEST_DB(test_c_orm_lazy_load, 2);
  TEST_OOM(test_c_orm_lazy_load_paginated, 2);
  TEST_DB(test_c_orm_lazy_load_paginated, 2);
  TEST_OOM(test_c_orm_prepare_cached, 2);
  TEST_DB(test_c_orm_prepare_cached, 2);
  TEST_OOM(test_c_orm_finalize_cached, 2);
  TEST_DB(test_c_orm_finalize_cached, 2);
  TEST_OOM(test_c_orm_insert_generic, 2);
  TEST_DB(test_c_orm_insert_generic, 2);
  TEST_OOM(test_c_orm_get_generic, 2);
  TEST_DB(test_c_orm_get_generic, 2);
  TEST_OOM(test_c_orm_find_all_generic, 2);
  TEST_DB(test_c_orm_find_all_generic, 2);
  TEST_OOM(test_c_orm_get_generic_string, 2);
  TEST_DB(test_c_orm_get_generic_string, 2);

  /* Cover NULL branches in mocks */
  mock_is_null(NULL, 0, NULL);
  mock_prepare(NULL, NULL, NULL);
  mock_step(NULL, NULL);
  mock_get_string(NULL, 0, NULL);
  mock_get_blob(NULL, 0, NULL, NULL);

  {
    const void *tmp_o;
    size_t tmp_s;
    mock_get_blob(NULL, 0, &tmp_o, NULL);
    mock_get_blob(NULL, 0, NULL, &tmp_s);
  }

  PASS();
}

static c_orm_error_t mock_get_int32_zero(c_orm_query_t *q, int index,
                                         int32_t *val) {
  (void)q;
  (void)index;
  *val = 0;
  return C_ORM_OK;
}

static c_orm_error_t mock_get_double_zero(c_orm_query_t *q, int index,
                                          double *val) {
  (void)q;
  (void)index;
  *val = 0.0;
  return C_ORM_OK;
}

static c_orm_error_t mock_get_string_null(c_orm_query_t *q, int index,
                                          const char **val) {
  (void)q;
  (void)index;
  *val = NULL;
  return C_ORM_OK;
}

static c_orm_error_t mock_is_null_false(c_orm_query_t *q, int index,
                                        int *is_null) {
  (void)q;
  (void)index;
  *is_null = 0;
  return C_ORM_OK;
}

static c_orm_error_t mock_is_null_true(c_orm_query_t *q, int index,
                                       int *is_null) {
  (void)q;
  (void)index;
  *is_null = 1;
  return C_ORM_OK;
}

TEST test_hydrate_set_null_field(void) {
  c_orm_db_t db_mem;
  c_orm_driver_vtable_t vt;
  void *q = NULL;
  struct Users u;

  memset(&db_mem, 0, sizeof(db_mem));
  memset(&vt, 0, sizeof(vt));
  memset(&u, 0, sizeof(u));
  db_mem.vtable = &vt;

  vt.is_null = mock_is_null_true;
  vt.get_string = mock_get_string_null;
  vt.get_int32 = mock_get_int32_zero;

  vt.get_double = mock_get_double_zero;

  u.username = (char *)malloc(10);
  strcpy(u.username, "old");
  c_orm_hydrate_row_from(&db_mem, q, &Users_meta, &u, 0);

  vt.is_null = mock_is_null_false;
  vt.get_string = mock_get_string_null;
  c_orm_hydrate_row_from(&db_mem, q, &Users_meta, &u, 0);
  test_free_meta_data(&Users_meta, &u);

  {
    /* Cover BLOB and POLYGON free in mega_meta */
    char mega_buf[256];
    c_orm_blob_t *blob_ptr;
    c_orm_polygon_t *poly_ptr;
    memset(mega_buf, 0, sizeof(mega_buf));

    blob_ptr = (c_orm_blob_t *)(mega_buf + 64);
    blob_ptr->data = malloc(8);
    blob_ptr->size = 8;

    poly_ptr = (c_orm_polygon_t *)(mega_buf + 80);
    poly_ptr->points = malloc(16);
    poly_ptr->num_points = 2;

    /* Make mega_meta columns nullable so they hit the NULL freeing branch */
    my_cols[mega_meta.num_columns - 3].is_nullable = 1;
    my_cols[mega_meta.num_columns - 2].is_nullable = 1;
    my_cols[mega_meta.num_columns - 1].is_nullable = 1;

    vt.is_null = mock_is_null_true;
    c_orm_hydrate_row_from(&db_mem, q, &mega_meta, mega_buf, 0);

    /* Allocate dummy data so test_free_meta_data hits the free branches */
    *(char **)(mega_buf + 0) = malloc(1); /* string col */
    blob_ptr->data = malloc(1);
    blob_ptr->size = 1;
    poly_ptr->points = malloc(1);
    poly_ptr->num_points = 1;

    test_free_meta_data(&mega_meta, mega_buf);
  }

  PASS();
}

TEST test_identity_map_coverage(void) {
  c_orm_identity_map_t map;
  c_orm_table_meta_t table;
  int32_t val;
  void *out;

  memset(&map, 0, sizeof(map));
  memset(&table, 0, sizeof(table));

  /* Test normal flow */
  c_orm_identity_map_init(&map);

  c_orm_identity_map_get_or_set_int(&map, &table, 1, &val, &out);
  c_orm_identity_map_get_or_set_int(&map, &table, 1, &val,
                                    &out); /* hit cache */

  c_orm_identity_map_get_or_set_str(&map, &table, "key", &val, &out);
  c_orm_identity_map_get_or_set_str(&map, &table, "key", &val,
                                    &out); /* hit cache */

  /* Hit malloc fail */
  g_malloc_fail = 1;
  g_malloc_count = 0;
  g_malloc_target = 0;
  c_orm_identity_map_get_or_set_int(&map, &table, 2, &val, &out);
  g_malloc_fail = 0;

  g_malloc_fail = 1;
  g_malloc_count = 0;
  g_malloc_target = 0;
  c_orm_identity_map_get_or_set_str(&map, &table, "key2", &val, &out);
  g_malloc_fail = 0;

  c_orm_identity_map_free(&map);

  PASS();
}

SUITE(api_coverage_suite) {
  void *(*old_malloc)(size_t);
  void *(*old_realloc)(void *, size_t);
  void (*old_free)(void *);
  old_malloc = c_orm_malloc;
  old_realloc = c_orm_realloc;
  old_free = c_orm_free;

  RUN_TEST(test_hydrate_set_null_field);
  RUN_TEST(test_identity_map_coverage);

  c_orm_set_allocators(mock_malloc_fail, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, mock_realloc_fail, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, mock_free);
  mock_free(NULL);

  RUN_TEST(run_all_api);

  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, old_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, old_free);
}
