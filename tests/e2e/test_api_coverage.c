#if defined(__clang__) || defined(__GNUC__)
#endif
/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_db.h"
#include "c_orm_sql.h"
#include "c_orm_string_builder.h"
#include "c_orm_log.h"
#include "Models.h"
#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

static void dummy_cb(c_orm_error_t err, void *ctx) {
  if (err != C_ORM_OK) {
    /* err handled */
  }
  (void)ctx;
}
static void dummy_batch_progress(size_t p, size_t t, void *ctx) {
  (void)p;
  (void)t;
  (void)ctx;
}
static c_orm_error_t dummy_lifecycle_hook(void *st, void *user_data) {
  (void)st;
  (void)user_data;
  return C_ORM_OK;
}
static c_orm_error_t dummy_failing_hook(void *st, void *user_data) {
  (void)st;
  (void)user_data;
  return C_ORM_ERROR_UNKNOWN;
}
struct Generic_Array {
  void *data;
  size_t length;
  size_t capacity;
};
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
static int g_mock_fail_col_index = -1;
static int g_mock_is_null_fail_countdown = -1;
static int g_mock_fail_bind = 0;
static int g_mock_step_fail_countdown = -1;

static c_orm_error_t mock_is_null(c_orm_query_t *q, int i, int *out) {
  c_orm_error_t rc;
  if (g_mock_is_null_fail_countdown >= 0) {
    if (g_mock_is_null_fail_countdown == 0) {
      g_mock_is_null_fail_countdown = -1;
      return C_ORM_ERROR_UNKNOWN;
    }
    g_mock_is_null_fail_countdown--;
  }
  rc = check_db_fail();
  if (rc != C_ORM_OK)
    return rc;
  if (g_mock_fail_col_index == i)
    return C_ORM_ERROR_UNKNOWN;
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
static int g_step_max = 1;
static int g_step_pattern[16];
static int g_step_pattern_len = 0;
static int g_step_pattern_idx = 0;

static c_orm_error_t mock_step(c_orm_query_t *q, int *out) {
  c_orm_error_t rc;
  if (g_mock_step_fail_countdown >= 0) {
    if (g_mock_step_fail_countdown == 0) {
      g_mock_step_fail_countdown = -1;
      return C_ORM_ERROR_UNKNOWN;
    }
    g_mock_step_fail_countdown--;
  }
  rc = check_db_fail();
  if (rc != C_ORM_OK)
    return rc;
  if (out) {
    if (g_step_pattern_len > 0) {
      *out = (g_step_pattern_idx < g_step_pattern_len)
                 ? g_step_pattern[g_step_pattern_idx++]
                 : 0;
    } else {
      *out = (g_step_count < g_step_max) ? 1 : 0;
    }
    g_step_count++;
  }
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
  if (g_mock_fail_bind)
    return C_ORM_ERROR_UNKNOWN;
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
  if (o)
    *o = 0;
  return check_db_fail();
}
static c_orm_error_t mock_get_int64(c_orm_query_t *q, int i, int64_t *o) {
  (void)q;
  (void)i;
  if (o)
    *o = 0;
  return check_db_fail();
}
static c_orm_error_t mock_get_double(c_orm_query_t *q, int i, double *o) {
  (void)q;
  (void)i;
  if (o)
    *o = 0.0;
  return check_db_fail();
}
static int g_mock_blob_null = 0;
static const void *g_custom_blob_data = NULL;
static size_t g_custom_blob_size = 0;

static c_orm_error_t mock_get_blob(c_orm_query_t *q, int i, const void **o,
                                   size_t *s) {
  c_orm_error_t rc = check_db_fail();
  if (rc != C_ORM_OK)
    return rc;
  if (g_mock_blob_null) {
    if (o)
      *o = NULL;
    if (s)
      *s = 0;
  } else if (g_custom_blob_data) {
    if (o)
      *o = g_custom_blob_data;
    if (s)
      *s = g_custom_blob_size;
  } else {
    if (o)
      *o = "blobdata";
    if (s)
      *s = 8;
  }
  (void)q;
  (void)i;
  return C_ORM_OK;
}
static int g_mock_finalize_fail = 0;
static int g_mock_finalize_countdown = -1;
static c_orm_error_t mock_finalize(c_orm_query_t *q) {
  (void)q;
  if (g_mock_finalize_fail)
    return C_ORM_ERROR_UNKNOWN;
  if (g_mock_finalize_countdown >= 0) {
    if (g_mock_finalize_countdown == 0) {
      g_mock_finalize_countdown = -1;
      return C_ORM_ERROR_UNKNOWN;
    }
    g_mock_finalize_countdown--;
  }
  return C_ORM_OK;
}
static c_orm_error_t mock_reset(c_orm_query_t *q) {
  (void)q;
  return C_ORM_OK;
}
static int64_t g_mock_last_id = 123;
static c_orm_error_t mock_get_last_insert_rowid(c_orm_db_t *db,
                                                int64_t *out_id) {
  (void)db;
  if (out_id)
    *out_id = g_mock_last_id;
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
  g_vt.get_last_insert_rowid = mock_get_last_insert_rowid;
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
  mega_meta.query_select_by_pk_for_update =
      "SELECT * FROM users WHERE id = ? FOR UPDATE";
}

#define TEST_OOM(test_func, max_allocs)                                        \
  do {                                                                         \
    int i;                                                                     \
    for (i = 0; i < max_allocs; i++) {                                         \
      g_malloc_target = i;                                                     \
      g_malloc_count = 0;                                                      \
      g_step_count = 0;                                                        \
      g_malloc_fail = 1;                                                       \
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
  (void)buf;
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
  (void)buf;
  c_orm_find_by_id_int32(&g_db, &mega_meta, 1, buf);
}
static void test_c_orm_find_by_composite_key(void) {
  char buf[1024];
  struct CddCVariant keys[2];
  memset(buf, 0, sizeof(buf));
  memset(keys, 0, sizeof(keys));
  keys[0].type = CDD_C_VARIANT_TYPE_INT;
  keys[0].value.i_val = 1;
  keys[1].type = CDD_C_VARIANT_TYPE_STRING;
  keys[1].value.s_val = "test";
  c_orm_find_by_composite_key(&g_db, &mega_meta, 2, keys, buf);
}
static void test_c_orm_update_by_composite_key(void) {
  char buf[1024];
  struct CddCVariant keys[2];
  memset(buf, 0, sizeof(buf));
  memset(keys, 0, sizeof(keys));
  keys[0].type = CDD_C_VARIANT_TYPE_INT;
  keys[0].value.i_val = 1;
  keys[1].type = CDD_C_VARIANT_TYPE_STRING;
  keys[1].value.s_val = "test";
  c_orm_update_by_composite_key(&g_db, &mega_meta, 2, keys, buf);
}
static void test_c_orm_delete_by_composite_key(void) {
  struct CddCVariant keys[2];
  memset(keys, 0, sizeof(keys));
  keys[0].type = CDD_C_VARIANT_TYPE_INT;
  keys[0].value.i_val = 1;
  keys[1].type = CDD_C_VARIANT_TYPE_STRING;
  keys[1].value.s_val = "test";
  c_orm_delete_by_composite_key(&g_db, &mega_meta, 2, keys);
}
static void test_c_orm_find_all(void) {
  struct Generic_Array arr;
  memset(&arr, 0, sizeof(arr));
  c_orm_find_all(&g_db, &mega_meta, &arr);
  if (arr.data) {
    c_orm_free(arr.data);
    arr.data = NULL;
  }
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
  (void)buf;
  {
    struct Users *u = (struct Users *)(void *)buf;
    u->username = (char *)"Alice";
    u->created_at = (char *)"2024-05-15 14:30:00";
  }
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
  str_arr[0] = "username";
  c_orm_update_partial(&g_db, &mega_meta, buf, str_arr, 1);
  str_arr[0] = "age";
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
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
  (void)buf;
  c_orm_find_all_generic(&g_db, &mega_meta, &void_ptr, &sz_ptr);
  if (void_ptr) {
    c_orm_free(void_ptr);
    void_ptr = NULL;
  }
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
  (void)buf;
  c_orm_get_generic_string(&g_db, &mega_meta, "test", buf);
}

TEST run_all_api(void) {
  setup_vt();
  TEST_OOM(test_c_orm_validate, 8);
  TEST_DB(test_c_orm_validate, 8);
  test_mock_vt();
  TEST_OOM(test_c_orm_find_by_id_int32, 8);
  TEST_DB(test_c_orm_find_by_id_int32, 8);
  TEST_OOM(test_c_orm_find_by_composite_key, 8);
  TEST_DB(test_c_orm_find_by_composite_key, 8);
  TEST_OOM(test_c_orm_update_by_composite_key, 8);
  TEST_DB(test_c_orm_update_by_composite_key, 8);
  TEST_OOM(test_c_orm_delete_by_composite_key, 8);
  TEST_DB(test_c_orm_delete_by_composite_key, 8);
  TEST_OOM(test_c_orm_find_all, 8);
  TEST_DB(test_c_orm_find_all, 8);
  TEST_OOM(test_c_orm_insert, 8);
  TEST_DB(test_c_orm_insert, 8);
  TEST_OOM(test_c_orm_save, 8);
  TEST_DB(test_c_orm_save, 8);
  TEST_OOM(test_c_orm_update, 8);
  TEST_DB(test_c_orm_update, 8);
  TEST_OOM(test_c_orm_delete, 8);
  TEST_DB(test_c_orm_delete, 8);
  TEST_OOM(test_c_orm_delete_by_id_int32, 8);
  TEST_DB(test_c_orm_delete_by_id_int32, 8);
  TEST_OOM(test_c_orm_delete_by_id_string, 8);
  TEST_DB(test_c_orm_delete_by_id_string, 8);
  TEST_OOM(test_c_orm_update_partial, 8);
  TEST_DB(test_c_orm_update_partial, 8);
  TEST_OOM(test_c_orm_exists_int32, 8);
  TEST_DB(test_c_orm_exists_int32, 8);
  TEST_OOM(test_c_orm_exists_string, 8);
  TEST_DB(test_c_orm_exists_string, 8);
  TEST_OOM(test_c_orm_find_all_paginated, 8);
  TEST_DB(test_c_orm_find_all_paginated, 8);
  TEST_OOM(test_c_orm_delete_all, 8);
  TEST_DB(test_c_orm_delete_all, 8);
  TEST_OOM(test_c_orm_find_by_id_string, 8);
  TEST_DB(test_c_orm_find_by_id_string, 8);
  TEST_OOM(test_c_orm_find_for_update_by_id_string, 8);
  TEST_DB(test_c_orm_find_for_update_by_id_string, 8);
  TEST_OOM(test_c_orm_find_for_update_by_id_int32, 8);
  TEST_DB(test_c_orm_find_for_update_by_id_int32, 8);
  TEST_OOM(test_c_orm_find_one_by_string, 8);
  TEST_DB(test_c_orm_find_one_by_string, 8);
  TEST_OOM(test_c_orm_hydrate_all, 8);
  TEST_DB(test_c_orm_hydrate_all, 8);
  TEST_OOM(test_c_orm_hydrate_row_from, 8);
  TEST_DB(test_c_orm_hydrate_row_from, 8);
  TEST_OOM(test_c_orm_hydrate_row, 8);
  TEST_DB(test_c_orm_hydrate_row, 8);
  TEST_OOM(test_c_orm_hydrate_cache_row, 8);
  TEST_DB(test_c_orm_hydrate_cache_row, 8);
  TEST_OOM(test_c_orm_execute_raw, 8);
  TEST_DB(test_c_orm_execute_raw, 8);
  TEST_OOM(test_c_orm_transaction_begin, 8);
  TEST_DB(test_c_orm_transaction_begin, 8);
  TEST_OOM(test_c_orm_transaction_commit, 8);
  TEST_DB(test_c_orm_transaction_commit, 8);
  TEST_OOM(test_c_orm_transaction_rollback, 8);
  TEST_DB(test_c_orm_transaction_rollback, 8);
  TEST_OOM(test_c_orm_savepoint_create, 8);
  TEST_DB(test_c_orm_savepoint_create, 8);
  TEST_OOM(test_c_orm_savepoint_rollback, 8);
  TEST_DB(test_c_orm_savepoint_rollback, 8);
  TEST_OOM(test_c_orm_savepoint_release, 8);
  TEST_DB(test_c_orm_savepoint_release, 8);
  TEST_OOM(test_c_orm_get_field_value, 8);
  TEST_DB(test_c_orm_get_field_value, 8);
  TEST_OOM(test_c_orm_set_field_value, 8);
  TEST_DB(test_c_orm_set_field_value, 8);
  TEST_OOM(test_c_orm_hydrate_abstract_all, 8);
  TEST_DB(test_c_orm_hydrate_abstract_all, 8);
  TEST_OOM(test_c_orm_find_all_abstract, 8);
  TEST_DB(test_c_orm_find_all_abstract, 8);
  TEST_OOM(test_c_orm_abstract_free, 8);
  TEST_DB(test_c_orm_abstract_free, 8);
  TEST_OOM(test_c_orm_to_json, 8);
  TEST_DB(test_c_orm_to_json, 8);
  TEST_OOM(test_c_orm_from_json, 8);
  TEST_DB(test_c_orm_from_json, 8);
  TEST_OOM(test_c_orm_to_dict, 8);
  TEST_DB(test_c_orm_to_dict, 8);
  TEST_OOM(test_c_orm_from_dict, 8);
  TEST_DB(test_c_orm_from_dict, 8);
  TEST_OOM(test_c_orm_abstract_to_json, 8);
  TEST_DB(test_c_orm_abstract_to_json, 8);
  TEST_OOM(test_c_orm_abstract_from_json, 8);
  TEST_DB(test_c_orm_abstract_from_json, 8);
  TEST_OOM(test_c_orm_deep_free, 8);
  TEST_DB(test_c_orm_deep_free, 8);
  TEST_OOM(test_c_orm_deep_copy, 8);
  TEST_DB(test_c_orm_deep_copy, 8);
  TEST_OOM(test_c_orm_insert_async, 8);
  TEST_DB(test_c_orm_insert_async, 8);
  TEST_OOM(test_c_orm_find_all_async, 8);
  TEST_DB(test_c_orm_find_all_async, 8);
  TEST_OOM(test_c_orm_hydrate_routed, 8);
  TEST_DB(test_c_orm_hydrate_routed, 8);
  TEST_OOM(test_c_orm_config_sqlite_pragma, 8);
  TEST_DB(test_c_orm_config_sqlite_pragma, 8);
  TEST_OOM(test_c_orm_config_postgres_set, 8);
  TEST_DB(test_c_orm_config_postgres_set, 8);
  TEST_OOM(test_c_orm_config_mysql_session, 8);
  TEST_DB(test_c_orm_config_mysql_session, 8);
  TEST_OOM(test_c_orm_shard_manager_init, 8);
  TEST_DB(test_c_orm_shard_manager_init, 8);
  TEST_OOM(test_c_orm_escape_string, 8);
  TEST_DB(test_c_orm_escape_string, 8);
  TEST_OOM(test_c_orm_enable_statement_caching, 8);
  TEST_DB(test_c_orm_enable_statement_caching, 8);
  TEST_OOM(test_c_orm_disable_statement_caching, 8);
  TEST_DB(test_c_orm_disable_statement_caching, 8);
  TEST_OOM(test_c_orm_lazy_load, 8);
  TEST_DB(test_c_orm_lazy_load, 8);
  TEST_OOM(test_c_orm_lazy_load_paginated, 8);
  TEST_DB(test_c_orm_lazy_load_paginated, 8);
  TEST_OOM(test_c_orm_prepare_cached, 8);
  TEST_DB(test_c_orm_prepare_cached, 8);
  TEST_OOM(test_c_orm_finalize_cached, 8);
  TEST_DB(test_c_orm_finalize_cached, 8);
  TEST_OOM(test_c_orm_insert_generic, 8);
  TEST_DB(test_c_orm_insert_generic, 8);
  TEST_OOM(test_c_orm_get_generic, 8);
  TEST_DB(test_c_orm_get_generic, 8);
  TEST_OOM(test_c_orm_find_all_generic, 8);
  TEST_DB(test_c_orm_find_all_generic, 8);
  TEST_OOM(test_c_orm_get_generic_string, 8);
  TEST_DB(test_c_orm_get_generic_string, 8);

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
  C_ORM_STRCPY(u.username, 10, "old");
  c_orm_hydrate_row_from(&db_mem, q, &Users_meta, &u, 0);

  vt.is_null = mock_is_null_false;
  vt.get_string = mock_get_string_null;
  c_orm_hydrate_row_from(&db_mem, q, &Users_meta, &u, 0);
  test_free_meta_data(&Users_meta, &u);

  {
    /* Cover BLOB and POLYGON free in mega_meta */
    union {
      char buf[256];
      void *ptr;
      double d;
    } mega_buf_u;
    char *mega_buf = mega_buf_u.buf;
    c_orm_blob_t *blob_ptr;
    c_orm_polygon_t *poly_ptr;
    memset(mega_buf, 0, 256);

    blob_ptr = (c_orm_blob_t *)(void *)(mega_buf + 64);
    blob_ptr->data = malloc(8);
    blob_ptr->size = 8;

    poly_ptr = (c_orm_polygon_t *)(void *)(mega_buf + 80);
    poly_ptr->points = malloc(16);
    poly_ptr->num_points = 2;

    /* Make mega_meta columns nullable so they hit the NULL freeing branch */
    my_cols[mega_meta.num_columns - 3].is_nullable = 1;
    my_cols[mega_meta.num_columns - 2].is_nullable = 1;
    my_cols[mega_meta.num_columns - 1].is_nullable = 1;

    vt.is_null = mock_is_null_true;
    test_free_meta_data(&mega_meta, mega_buf);
    c_orm_hydrate_row_from(&db_mem, q, &mega_meta, mega_buf, 0);

    free(blob_ptr->data);
    blob_ptr->data = NULL;
    free(poly_ptr->points);
    poly_ptr->points = NULL;

    /* Dummy data removed to fix leaks */

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

  /* Lookup when not present */
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_identity_map_get_or_set_int(&map, &table, 999, NULL, &out));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_identity_map_get_or_set_str(&map, &table, "missing_key", NULL,
                                              &out));

  /* Hash collision testing: key 1 and key 65 (1 + 64) */
  c_orm_identity_map_get_or_set_int(&map, &table, 65, &val, &out);
  ASSERT_EQ(C_ORM_OK,
            c_orm_identity_map_get_or_set_int(&map, &table, 1, NULL, &out));
  ASSERT_EQ(C_ORM_OK,
            c_orm_identity_map_get_or_set_int(&map, &table, 65, NULL, &out));

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

  /* Test c_orm_hydrate_cache_row with identity map */
  {
    c_orm_table_meta_t h_meta;
    c_orm_column_meta_t h_col;
    struct {
      int32_t id;
    } h_obj;
    struct {
      char *id;
    } h_str_obj;
    void *cached_out = NULL;

    g_db.identity_map = &map;

    /* 1. INT32 PK */
    memset(&h_col, 0, sizeof(h_col));
    h_col.name = "id";
    h_col.type = C_ORM_TYPE_INT32;
    h_col.is_pk = 1;
    h_col.offset = 0;
    memset(&h_meta, 0, sizeof(h_meta));
    h_meta.columns = &h_col;
    h_meta.num_columns = 1;
    h_obj.id = 100;
    ASSERT_EQ(C_ORM_OK,
              c_orm_hydrate_cache_row(&g_db, &h_meta, &h_obj, &cached_out));
    ASSERT_EQ(&h_obj, cached_out);

    /* 2. STRING PK (non-null) */
    h_col.type = C_ORM_TYPE_STRING;
    h_str_obj.id = "str_pk_val";
    ASSERT_EQ(C_ORM_OK,
              c_orm_hydrate_cache_row(&g_db, &h_meta, &h_str_obj, &cached_out));
    ASSERT_EQ(&h_str_obj, cached_out);

    /* 3. STRING PK (NULL -> type mismatch) */
    h_str_obj.id = NULL;
    ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH,
              c_orm_hydrate_cache_row(&g_db, &h_meta, &h_str_obj, &cached_out));

    /* 4. Unsupported PK type (FLOAT) */
    h_col.type = C_ORM_TYPE_FLOAT;
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
              c_orm_hydrate_cache_row(&g_db, &h_meta, &h_obj, &cached_out));

    /* 5. No PK column */
    h_col.is_pk = 0;
    ASSERT_EQ(C_ORM_OK,
              c_orm_hydrate_cache_row(&g_db, &h_meta, &h_obj, &cached_out));

    g_db.identity_map = NULL;
  }

  c_orm_identity_map_free(&map);

  PASS();
}

TEST test_meta_free_helper(void) {
  c_orm_column_meta_t cols[5];
  c_orm_table_meta_t meta;
  struct {
    char *s;
    c_orm_blob_t b;
    c_orm_polygon_t p;
    void *null_ptr;
    int i;
  } test_obj;
  const char *out_s = NULL;
  const void *out_b = NULL;
  size_t out_sz = 0;
  void *tmp_p = NULL;

  memset(&cols, 0, sizeof(cols));
  memset(&meta, 0, sizeof(meta));
  memset(&test_obj, 0, sizeof(test_obj));

  cols[0].type = C_ORM_TYPE_STRING;
  cols[0].offset = (size_t)((char *)&test_obj.s - (char *)&test_obj);

  cols[1].type = C_ORM_TYPE_BLOB;
  cols[1].offset = (size_t)((char *)&test_obj.b - (char *)&test_obj);

  cols[2].type = C_ORM_TYPE_POLYGON;
  cols[2].offset = (size_t)((char *)&test_obj.p - (char *)&test_obj);

  cols[3].is_nullable = 1;
  cols[3].offset = (size_t)((char *)&test_obj.null_ptr - (char *)&test_obj);

  cols[4].type = C_ORM_TYPE_INT32;
  cols[4].offset = (size_t)((char *)&test_obj.i - (char *)&test_obj);

  meta.columns = cols;
  meta.num_columns = 5;

  test_obj.s = (char *)malloc(16);
  test_obj.b.data = malloc(16);
  test_obj.b.size = 16;
  test_obj.p.points = (c_orm_point_t *)malloc(sizeof(c_orm_point_t));
  test_obj.p.num_points = 1;
  test_obj.null_ptr = malloc(16);

  test_free_meta_data(&meta, &test_obj);
  ASSERT_EQ(NULL, test_obj.s);
  ASSERT_EQ(NULL, test_obj.b.data);
  ASSERT_EQ(NULL, test_obj.p.points);
  ASSERT_EQ(NULL, test_obj.null_ptr);

  /* mock_get_string failure */
  g_db_fail = 1;
  g_db_target = g_db_count;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, mock_get_string(NULL, 0, &out_s));
  g_db_fail = 0;

  /* mock_get_blob failure */
  g_db_fail = 1;
  g_db_target = g_db_count;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, mock_get_blob(NULL, 0, &out_b, &out_sz));
  g_db_fail = 0;

  /* mock_realloc_fail with NULL result */
  g_malloc_fail = 1;
  g_malloc_target = 0;
  g_malloc_count = 0;
  tmp_p = mock_realloc_fail(NULL, 10);
  ASSERT_EQ(NULL, tmp_p);
  g_malloc_fail = 0;

  /* mock_realloc_fail with untracked ptr */
  {
    void *untracked = malloc(10);
    void *reallocated = mock_realloc_fail(untracked, 20);
    (void)reallocated;
  }

  PASS();
}

TEST test_batch_coverage(void) {
  struct {
    int32_t id;
    char name[32];
  } items[10];
  c_orm_column_meta_t cols[2];
  c_orm_table_meta_t meta;
  c_orm_table_meta_t view_meta;
  c_orm_table_meta_t no_pk_meta;
  struct c_orm_iterator *iter = NULL;
  size_t num_fetched = 0;
  size_t i;

  memset(items, 0, sizeof(items));
  for (i = 0; i < 10; i++) {
    items[i].id = (int32_t)(i + 1);
  }

  memset(cols, 0, sizeof(cols));
  cols[0].name = "id";
  cols[0].type = C_ORM_TYPE_INT32;
  cols[0].offset = 0;
  cols[0].is_pk = 1;

  cols[1].name = "name";
  cols[1].type = C_ORM_TYPE_STRING;
  cols[1].offset = sizeof(int32_t);

  memset(&meta, 0, sizeof(meta));
  meta.name = "items";
  meta.columns = cols;
  meta.num_columns = 2;
  meta.struct_size = sizeof(items[0]);
  meta.query_select_all = "SELECT id, name FROM items";

  view_meta = meta;
  view_meta.is_view = 1;

  no_pk_meta = meta;
  no_pk_meta.columns = cols;

  /* insert_batch_ext error & edge branches */
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_insert_batch_ext(NULL, &meta, items, 5, 2,
                                   C_ORM_ON_CONFLICT_FAIL, NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_READ_ONLY,
            c_orm_insert_batch_ext(&g_db, &view_meta, items, 5, 2,
                                   C_ORM_ON_CONFLICT_FAIL, NULL, NULL));
  ASSERT_EQ(C_ORM_OK,
            c_orm_insert_batch_ext(&g_db, &meta, items, 0, 2,
                                   C_ORM_ON_CONFLICT_FAIL, NULL, NULL));

  /* insert_batch normal execution */
  ASSERT_EQ(C_ORM_OK,
            c_orm_insert_batch_ext(&g_db, &meta, items, 5, 2,
                                   C_ORM_ON_CONFLICT_DO_NOTHING, NULL, NULL));
  ASSERT_EQ(C_ORM_OK,
            c_orm_insert_batch_ext(&g_db, &meta, items, 5, 2,
                                   C_ORM_ON_CONFLICT_DO_UPDATE, NULL, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_insert_batch(&g_db, &meta, items, 5, 0));

  /* insert_batch with progress_cb and lifecycle hooks */
  meta.hooks[C_ORM_HOOK_BEFORE_SAVE] = dummy_lifecycle_hook;
  meta.hooks[C_ORM_HOOK_BEFORE_INSERT] = dummy_lifecycle_hook;
  meta.hooks[C_ORM_HOOK_AFTER_INSERT] = dummy_lifecycle_hook;
  meta.hooks[C_ORM_HOOK_AFTER_SAVE] = dummy_lifecycle_hook;
  ASSERT_EQ(C_ORM_OK, c_orm_insert_batch_ext(&g_db, &meta, items, 5, 2,
                                             C_ORM_ON_CONFLICT_DO_NOTHING,
                                             dummy_batch_progress, NULL));
  memset(meta.hooks, 0, sizeof(meta.hooks));

  /* Failing hooks in insert_batch */
  meta.hooks[C_ORM_HOOK_BEFORE_SAVE] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_insert_batch_ext(&g_db, &meta, items, 5, 2,
                                   C_ORM_ON_CONFLICT_DO_NOTHING, NULL, NULL));
  memset(meta.hooks, 0, sizeof(meta.hooks));

  meta.hooks[C_ORM_HOOK_BEFORE_INSERT] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_insert_batch_ext(&g_db, &meta, items, 5, 2,
                                   C_ORM_ON_CONFLICT_DO_NOTHING, NULL, NULL));
  memset(meta.hooks, 0, sizeof(meta.hooks));

  meta.hooks[C_ORM_HOOK_AFTER_INSERT] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_insert_batch_ext(&g_db, &meta, items, 5, 2,
                                   C_ORM_ON_CONFLICT_DO_NOTHING, NULL, NULL));
  memset(meta.hooks, 0, sizeof(meta.hooks));

  meta.hooks[C_ORM_HOOK_AFTER_SAVE] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_insert_batch_ext(&g_db, &meta, items, 5, 2,
                                   C_ORM_ON_CONFLICT_DO_NOTHING, NULL, NULL));
  memset(meta.hooks, 0, sizeof(meta.hooks));

  /* delete_batch error & edge branches */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_delete_batch(NULL, &meta, items, 5, 2));
  ASSERT_EQ(C_ORM_ERROR_READ_ONLY,
            c_orm_delete_batch(&g_db, &view_meta, items, 5, 2));
  ASSERT_EQ(C_ORM_OK, c_orm_delete_batch(&g_db, &meta, items, 0, 2));

  cols[0].is_pk = 0;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_delete_batch(&g_db, &no_pk_meta, items, 5, 2));
  cols[0].is_pk = 1;

  meta.hooks[C_ORM_HOOK_BEFORE_DELETE] = dummy_lifecycle_hook;
  meta.hooks[C_ORM_HOOK_AFTER_DELETE] = dummy_lifecycle_hook;
  ASSERT_EQ(C_ORM_OK, c_orm_delete_batch(&g_db, &meta, items, 5, 2));
  memset(meta.hooks, 0, sizeof(meta.hooks));

  meta.hooks[C_ORM_HOOK_BEFORE_DELETE] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_delete_batch(&g_db, &meta, items, 5, 2));
  memset(meta.hooks, 0, sizeof(meta.hooks));

  meta.hooks[C_ORM_HOOK_AFTER_DELETE] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_delete_batch(&g_db, &meta, items, 5, 2));
  memset(meta.hooks, 0, sizeof(meta.hooks));

  ASSERT_EQ(C_ORM_OK, c_orm_delete_batch(&g_db, &meta, items, 5, 0));

  /* update_batch error & edge branches */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_update_batch(NULL, &meta, items, 5, 2));
  ASSERT_EQ(C_ORM_ERROR_READ_ONLY,
            c_orm_update_batch(&g_db, &view_meta, items, 5, 2));
  ASSERT_EQ(C_ORM_OK, c_orm_update_batch(&g_db, &meta, items, 0, 2));

  cols[0].is_pk = 0;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_update_batch(&g_db, &no_pk_meta, items, 5, 2));
  cols[0].is_pk = 1;

  meta.hooks[C_ORM_HOOK_AFTER_SAVE] = dummy_lifecycle_hook;
  ASSERT_EQ(C_ORM_OK, c_orm_update_batch(&g_db, &meta, items, 5, 2));
  memset(meta.hooks, 0, sizeof(meta.hooks));

  meta.hooks[C_ORM_HOOK_AFTER_UPDATE] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_update_batch(&g_db, &meta, items, 5, 2));
  memset(meta.hooks, 0, sizeof(meta.hooks));

  meta.hooks[C_ORM_HOOK_AFTER_SAVE] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_update_batch(&g_db, &meta, items, 5, 2));
  memset(meta.hooks, 0, sizeof(meta.hooks));

  ASSERT_EQ(C_ORM_OK, c_orm_update_batch(&g_db, &meta, items, 5, 0));

  /* Conflict policy DO_UPDATE */
  ASSERT_EQ(C_ORM_OK,
            c_orm_insert_batch_ext(&g_db, &meta, items, 5, 2,
                                   C_ORM_ON_CONFLICT_DO_UPDATE, NULL, NULL));

  /* STRING, INT64, and NOT_IMPLEMENTED PK in batch operations */
  {
    c_orm_table_meta_t str_bmeta;
    c_orm_column_meta_t str_bcol;
    struct {
      char *id;
      int32_t val;
    } str_bitems[2];
    str_bmeta = meta;
    str_bcol = cols[0];
    str_bitems[0].id = "id1";
    str_bitems[0].val = 1;
    str_bitems[1].id = "id2";
    str_bitems[1].val = 2;
    str_bcol.type = C_ORM_TYPE_STRING;
    str_bmeta.columns = &str_bcol;
    str_bmeta.num_columns = 1;
    str_bmeta.struct_size = sizeof(str_bitems[0]);

    ASSERT_EQ(C_ORM_OK,
              c_orm_update_batch(&g_db, &str_bmeta, str_bitems, 2, 1));
    ASSERT_EQ(C_ORM_OK,
              c_orm_delete_batch(&g_db, &str_bmeta, str_bitems, 2, 1));

    str_bcol.type = C_ORM_TYPE_INT64;
    ASSERT_EQ(C_ORM_OK,
              c_orm_update_batch(&g_db, &str_bmeta, str_bitems, 2, 1));
    ASSERT_EQ(C_ORM_OK,
              c_orm_delete_batch(&g_db, &str_bmeta, str_bitems, 2, 1));

    str_bcol.type = C_ORM_TYPE_FLOAT;
    ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED,
              c_orm_update_batch(&g_db, &str_bmeta, str_bitems, 2, 1));
    ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED,
              c_orm_delete_batch(&g_db, &str_bmeta, str_bitems, 2, 1));
  }

  /* find_batch_init & iterator */
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_batch_init(NULL, &meta, NULL, 5, &iter));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_batch_init(&g_db, &meta, NULL, 0, &iter));
  ASSERT_EQ(C_ORM_OK, c_orm_find_batch_init(&g_db, &meta, NULL, 2, &iter));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_iterator_next(NULL, items, &num_fetched));
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_iterator_next(iter, items, &num_fetched));
  ASSERT_EQ(1, num_fetched);
  ASSERT_EQ(C_ORM_OK, c_orm_iterator_next(iter, items, &num_fetched));
  ASSERT_EQ(0, num_fetched);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_iterator_close(NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_iterator_close(iter));

  PASS();
}

TEST test_shard_and_misc_coverage(void) {
  c_orm_shard_manager_t *sm = NULL;
  c_orm_db_t *node = NULL;
  void *scatter_arr = NULL;
  size_t scatter_cnt = 0;
  c_orm_table_meta_t meta;
  c_orm_column_meta_t cols[1];
  void *tmp_ptr = NULL;
  char dummy_buf[64];

  memset(cols, 0, sizeof(cols));
  cols[0].name = "id";
  cols[0].type = C_ORM_TYPE_INT32;
  cols[0].offset = 0;
  cols[0].is_pk = 1;

  memset(&meta, 0, sizeof(meta));
  meta.name = "items";
  meta.columns = cols;
  meta.num_columns = 1;
  meta.struct_size = sizeof(int32_t);

  /* Shard manager init, add, route, scatter-gather, free */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_shard_manager_init(0, &sm));
  ASSERT_EQ(C_ORM_OK, c_orm_shard_manager_init(2, &sm));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_shard_manager_add_node(NULL, 0, &g_db));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_shard_manager_add_node(sm, 5, &g_db));
  ASSERT_EQ(C_ORM_OK, c_orm_shard_manager_add_node(sm, 0, &g_db));
  ASSERT_EQ(C_ORM_OK, c_orm_shard_manager_add_node(sm, 1, &g_db));

  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_shard_route_hash(NULL, "key", &node));
  ASSERT_EQ(C_ORM_OK, c_orm_shard_route_hash(sm, "key123", &node));

  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_scatter_gather_generic(
                                    NULL, &meta, &scatter_arr, &scatter_cnt));
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_scatter_gather_generic(sm, &meta, &scatter_arr,
                                                   &scatter_cnt));
  if (scatter_arr) {
    c_orm_free(scatter_arr);
    scatter_arr = NULL;
  }

  c_orm_shard_manager_free(NULL);
  c_orm_shard_manager_free(sm);

  /* Misc APIs */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_raw(NULL, NULL, NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_register_timestamp_hooks(NULL));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_register_timestamp_hooks(&meta));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_register_soft_delete_hook(NULL));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_register_soft_delete_hook(&meta));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_resolve_n_plus_one(NULL, NULL, NULL, 0));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_resolve_n_plus_one(&g_db, dummy_buf, &meta, 5));

  /* System allocators */
  c_orm_system_free(NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_system_malloc(10, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_system_malloc(10, &tmp_ptr));
  c_orm_system_free(tmp_ptr);
  tmp_ptr = NULL;

  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_system_calloc(1, 10, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_system_calloc(1, 10, &tmp_ptr));
  c_orm_system_free(tmp_ptr);
  tmp_ptr = NULL;

  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_system_realloc(NULL, 10, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_system_realloc(NULL, 10, &tmp_ptr));
  c_orm_system_free(tmp_ptr);
  tmp_ptr = NULL;

  PASS();
}

static c_orm_error_t mock_on_attach(void *parent, void *child, void *db) {
  (void)parent;
  (void)child;
  (void)db;
  return C_ORM_OK;
}

static c_orm_error_t mock_is_null_child_only(c_orm_query_t *q, int index,
                                             int *is_null) {
  (void)q;
  if (index < 2)
    *is_null = 0;
  else
    *is_null = 1;
  return C_ORM_OK;
}

TEST test_relations_extended_coverage(void) {
  struct Parent {
    int32_t id;
    char name[32];
    c_orm_lazy_load_context_t ctx;
    struct Generic_Array children_arr;
    void *child_ptr;
  } parent;
  struct Child {
    int64_t id;
    int64_t parent_id;
    char val[32];
  } child, children[3];
  struct Tag {
    int64_t tag_id;
    char tag_name[32];
  } tag;

  c_orm_column_meta_t parent_cols[2];
  c_orm_column_meta_t child_cols[3];
  c_orm_column_meta_t tag_cols[2];
  c_orm_relation_meta_t rels[2];
  c_orm_table_meta_t parent_meta;
  c_orm_table_meta_t child_meta;
  c_orm_table_meta_t tag_meta;
  const char *rel_paths[1];
  struct Generic_Array out_arr;

  rel_paths[0] = "children";

  memset(&parent, 0, sizeof(parent));
  parent.id = 1;
  memset(&child, 0, sizeof(child));
  child.id = 10;
  memset(children, 0, sizeof(children));
  children[0].id = 10;
  children[1].id = 11;
  memset(&tag, 0, sizeof(tag));
  tag.tag_id = 100;
  memset(&out_arr, 0, sizeof(out_arr));

  /* Child cols */
  memset(child_cols, 0, sizeof(child_cols));
  child_cols[0].name = "id";
  child_cols[0].type = C_ORM_TYPE_INT64;
  child_cols[0].offset = 0;
  child_cols[0].is_pk = 1;

  child_cols[1].name = "parent_id";
  child_cols[1].type = C_ORM_TYPE_INT64;
  child_cols[1].offset = (size_t)((char *)&child.parent_id - (char *)&child);
  child_cols[1].is_nullable = 1;

  child_cols[2].name = "val";
  child_cols[2].type = C_ORM_TYPE_STRING;
  child_cols[2].offset = (size_t)((char *)&child.val - (char *)&child);

  memset(&child_meta, 0, sizeof(child_meta));
  child_meta.name = "children";
  child_meta.columns = child_cols;
  child_meta.num_columns = 3;
  child_meta.struct_size = sizeof(struct Child);
  child_meta.query_insert =
      "INSERT INTO children (id, parent_id, val) VALUES (?, ?, ?)";
  child_meta.query_update =
      "UPDATE children SET id = ?, parent_id = ?, val = ? WHERE id = ?";

  /* Tag cols */
  memset(tag_cols, 0, sizeof(tag_cols));
  tag_cols[0].name = "tag_id";
  tag_cols[0].type = C_ORM_TYPE_INT64;
  tag_cols[0].offset = 0;
  tag_cols[0].is_pk = 1;

  tag_cols[1].name = "tag_name";
  tag_cols[1].type = C_ORM_TYPE_STRING;
  tag_cols[1].offset = (size_t)((char *)&tag.tag_name - (char *)&tag);

  memset(&tag_meta, 0, sizeof(tag_meta));
  tag_meta.name = "tags";
  tag_meta.columns = tag_cols;
  tag_meta.num_columns = 2;
  tag_meta.struct_size = sizeof(struct Tag);

  /* Parent cols & relations */
  memset(parent_cols, 0, sizeof(parent_cols));
  parent_cols[0].name = "id";
  parent_cols[0].type = C_ORM_TYPE_INT32;
  parent_cols[0].offset = 0;
  parent_cols[0].is_pk = 1;

  parent_cols[1].name = "name";
  parent_cols[1].type = C_ORM_TYPE_STRING;
  parent_cols[1].offset = (size_t)((char *)&parent.name - (char *)&parent);

  memset(rels, 0, sizeof(rels));
  rels[0].field_name = "children";
  rels[0].type = C_ORM_RELATION_ONE_TO_MANY;
  rels[0].local_key = "id";
  rels[0].foreign_key = "parent_id";
  rels[0].target_meta = &child_meta;
  rels[0].on_attach = mock_on_attach;
  rels[0].on_detach = mock_on_attach;
  rels[0].struct_offset = (size_t)((char *)&parent.ctx - (char *)&parent);
  rels[0].lazy_ctx_offset = (size_t)((char *)&parent.ctx - (char *)&parent);
  rels[0].data_offset =
      (size_t)((char *)&parent.children_arr - (char *)&parent);

  rels[1].field_name = "tags";
  rels[1].type = C_ORM_RELATION_MANY_TO_MANY;
  rels[1].local_key = "id";
  rels[1].foreign_key = "tag_id";
  rels[1].join_table = "parent_tags";
  rels[1].join_local_key = "parent_id";
  rels[1].join_foreign_key = "tag_id";
  rels[1].target_meta = &tag_meta;
  rels[1].struct_offset = (size_t)((char *)&parent.ctx - (char *)&parent);
  rels[1].lazy_ctx_offset = (size_t)((char *)&parent.ctx - (char *)&parent);
  rels[1].data_offset =
      (size_t)((char *)&parent.children_arr - (char *)&parent);

  memset(&parent_meta, 0, sizeof(parent_meta));
  parent_meta.name = "parents";
  parent_meta.columns = parent_cols;
  parent_meta.num_columns = 2;
  parent_meta.relations = rels;
  parent_meta.num_relations = 2;
  parent_meta.struct_size = sizeof(struct Parent);
  parent_meta.query_select_all = "SELECT id, name FROM parents";
  parent_meta.query_select_by_pk = "SELECT id, name FROM parents WHERE id = ?";

  /* resolve_n_plus_one with valid relation */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_resolve_n_plus_one(&g_db, &parent, &parent_meta, 0));

  /* attach error & success branches */
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_attach(NULL, &parent_meta, &parent, "children", &child));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_attach(&g_db, &parent_meta, &parent, "nonexistent", &child));
  ASSERT_EQ(C_ORM_OK,
            c_orm_attach(&g_db, &parent_meta, &parent, "children", &child));
  ASSERT_EQ(C_ORM_OK, c_orm_attach(&g_db, &parent_meta, &parent, "tags", &tag));

  /* detach error & success branches (exercises set_null_field!) */
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_detach(NULL, &parent_meta, &parent, "children", &child));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_detach(&g_db, &parent_meta, &parent, "nonexistent", &child));
  ASSERT_EQ(C_ORM_OK,
            c_orm_detach(&g_db, &parent_meta, &parent, "children", &child));
  ASSERT_EQ(C_ORM_OK, c_orm_detach(&g_db, &parent_meta, &parent, "tags", &tag));

  /* sync error & success branches */
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_sync(NULL, &parent_meta, &parent, "children", children, 2));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, c_orm_sync(&g_db, &parent_meta, &parent,
                                              "nonexistent", children, 2));
  ASSERT_EQ(C_ORM_OK,
            c_orm_sync(&g_db, &parent_meta, &parent, "children", children, 2));
  ASSERT_EQ(C_ORM_OK,
            c_orm_sync(&g_db, &parent_meta, &parent, "tags", &tag, 1));

  /* find_all_with_relation error branches */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_find_all_with_relation(
                                    NULL, &parent_meta, "children", &out_arr));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_find_all_with_relation(&g_db, &parent_meta, "nonexistent",
                                         &out_arr));
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_find_all_with_relation(&g_db, &parent_meta,
                                                   "children", &out_arr));
  c_orm_free(out_arr.data);
  memset(&out_arr, 0, sizeof(out_arr));
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_find_all_with_relation(&g_db, &parent_meta, "tags",
                                                   &out_arr));
  c_orm_free(out_arr.data);
  memset(&out_arr, 0, sizeof(out_arr));
  rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
  rels[0].data_offset = (size_t)((char *)&parent.child_ptr - (char *)&parent);
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_find_all_with_relation(&g_db, &parent_meta,
                                                   "children", &out_arr));
  c_orm_free(out_arr.data);
  memset(&out_arr, 0, sizeof(out_arr));
  rels[0].type = C_ORM_RELATION_BELONGS_TO;
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_find_all_with_relation(&g_db, &parent_meta,
                                                   "children", &out_arr));
  c_orm_free(out_arr.data);
  memset(&out_arr, 0, sizeof(out_arr));
  rels[0].type = C_ORM_RELATION_ONE_TO_MANY;
  rels[0].data_offset =
      (size_t)((char *)&parent.children_arr - (char *)&parent);

  /* find_with_relation_int32 branches */
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_with_relation_int32(NULL, &parent_meta, 1, "children",
                                           &parent));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_find_with_relation_int32(&g_db, &parent_meta, 1,
                                           "nonexistent", &parent));
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_find_with_relation_int32(&g_db, &parent_meta, 1,
                                                     "children", &parent));
  if (parent.children_arr.data) {
    c_orm_free(parent.children_arr.data);
    parent.children_arr.data = NULL;
  }
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_find_with_relation_int32(&g_db, &parent_meta, 1,
                                                     "tags", &parent));
  c_orm_free(parent.children_arr.data);
  parent.children_arr.data = NULL;
  rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
  rels[0].data_offset = (size_t)((char *)&parent.child_ptr - (char *)&parent);
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_find_with_relation_int32(&g_db, &parent_meta, 1,
                                                     "children", &parent));
  c_orm_free(parent.child_ptr);
  parent.child_ptr = NULL;
  rels[0].type = C_ORM_RELATION_BELONGS_TO;
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_find_with_relation_int32(&g_db, &parent_meta, 1,
                                                     "children", &parent));
  c_orm_free(parent.child_ptr);
  parent.child_ptr = NULL;
  rels[0].type = C_ORM_RELATION_ONE_TO_MANY;
  rels[0].data_offset =
      (size_t)((char *)&parent.children_arr - (char *)&parent);

  /* find_with_relation_int32 not_found and edge branches */
  g_step_count = 1;
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_find_with_relation_int32(&g_db, &parent_meta, 1, "children",
                                           &parent));
  g_step_count = 0;

  /* ONE_TO_ONE with null child */
  rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
  rels[0].struct_offset = (size_t)((char *)&parent.child_ptr - (char *)&parent);
  rels[0].data_offset = (size_t)((char *)&parent.child_ptr - (char *)&parent);
  parent.child_ptr = (void *)1;
  {
    c_orm_driver_vtable_t is_null_vt;
    c_orm_db_t is_null_db;
    is_null_vt = g_vt;
    is_null_db = g_db;
    is_null_vt.is_null = mock_is_null_child_only;
    is_null_db.vtable = &is_null_vt;
    g_step_count = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_find_with_relation_int32(
                            &is_null_db, &parent_meta, 1, "children", &parent));
    ASSERT_EQ(NULL, parent.child_ptr);
  }

  /* Invalid relation type */
  rels[0].type = (c_orm_relation_type_t)99;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_find_with_relation_int32(&g_db, &parent_meta, 1, "children",
                                           &parent));
  ASSERT_EQ(
      C_ORM_ERROR_UNKNOWN,
      c_orm_find_all_with_relation(&g_db, &parent_meta, "children", &out_arr));

  /* Missing target_meta */
  rels[0].type = C_ORM_RELATION_ONE_TO_MANY;
  rels[0].target_meta = NULL;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_find_with_relation_int32(&g_db, &parent_meta, 1, "children",
                                           &parent));
  ASSERT_EQ(
      C_ORM_ERROR_UNKNOWN,
      c_orm_find_all_with_relation(&g_db, &parent_meta, "children", &out_arr));
  rels[0].target_meta = &child_meta;

  /* Missing / wrong PK type */
  parent_cols[0].type = C_ORM_TYPE_STRING;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_find_with_relation_int32(&g_db, &parent_meta, 1, "children",
                                           &parent));
  ASSERT_EQ(
      C_ORM_ERROR_UNKNOWN,
      c_orm_find_all_with_relation(&g_db, &parent_meta, "children", &out_arr));
  parent_cols[0].type = C_ORM_TYPE_INT32;
  rels[0].data_offset =
      (size_t)((char *)&parent.children_arr - (char *)&parent);

  /* find_with_relations_int32 paths */
  {
    const char *bad_paths[1];
    bad_paths[0] = "nonexistent";
    g_step_count = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_find_with_relations_int32(&g_db, &parent_meta, 1,
                                                        rel_paths, 0, &parent));
    ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
              c_orm_find_with_relations_int32(&g_db, &parent_meta, 1, bad_paths,
                                              1, &parent));
  }

  /* find_all_with_relations error branches */
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_all_with_relations(NULL, &parent_meta, rel_paths, 1,
                                          &out_arr));
  ASSERT_EQ(C_ORM_OK, c_orm_find_all_with_relations(&g_db, &parent_meta,
                                                    rel_paths, 0, &out_arr));
  c_orm_free(out_arr.data);
  memset(&out_arr, 0, sizeof(out_arr));
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_find_all_with_relations(&g_db, &parent_meta,
                                                    rel_paths, 1, &out_arr));
  c_orm_free(out_arr.data);
  memset(&out_arr, 0, sizeof(out_arr));
  /* find_with_relations_int32 error branches */
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_with_relations_int32(NULL, &parent_meta, 1, rel_paths, 1,
                                            &parent));
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_find_with_relations_int32(&g_db, &parent_meta, 1,
                                                      rel_paths, 0, &parent));
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_find_with_relations_int32(&g_db, &parent_meta, 1,
                                                      rel_paths, 1, &parent));
  c_orm_free(parent.children_arr.data);
  parent.children_arr.data = NULL;
  {
    const char *nested_paths[1];
    nested_paths[0] = "children.val";
    g_step_count = 0;
    c_orm_find_with_relations_int32(&g_db, &parent_meta, 1, nested_paths, 1,
                                    &parent);
    c_orm_free(parent.children_arr.data);
    parent.children_arr.data = NULL;
    g_step_count = 0;
    c_orm_find_all_with_relations(&g_db, &parent_meta, nested_paths, 1,
                                  &out_arr);
    c_orm_free(out_arr.data);
    memset(&out_arr, 0, sizeof(out_arr));
  }

  PASS();
}

TEST test_escape_string_coverage(void) {
  char buf[64];
  char tiny[2];

  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_escape_string(NULL, "abc", buf, sizeof(buf)));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_escape_string(&g_db, NULL, buf, sizeof(buf)));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_escape_string(&g_db, "abc", NULL, sizeof(buf)));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_escape_string(&g_db, "abc", buf, 0));

  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_escape_string(&g_db, "abc", tiny, sizeof(tiny)));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_escape_string(&g_db, "'", tiny, sizeof(tiny)));

  ASSERT_EQ(C_ORM_OK,
            c_orm_escape_string(&g_db, "hello world", buf, sizeof(buf)));
  ASSERT_STR_EQ("hello world", buf);

  ASSERT_EQ(C_ORM_OK,
            c_orm_escape_string(&g_db, "O'Reilly's", buf, sizeof(buf)));
  ASSERT_STR_EQ("O''Reilly''s", buf);

  PASS();
}

TEST test_validation_coverage(void) {
  struct ValParent {
    int64_t id;
    int64_t fk_id;
    void *child_ptr;
    c_orm_lazy_load_context_t ctx;
  } val_parent;

  c_orm_column_meta_t cols[2];
  c_orm_relation_meta_t rels[1];
  c_orm_table_meta_t val_meta;
  int dummy_child;

  dummy_child = 42;
  memset(&val_parent, 0, sizeof(val_parent));
  memset(&val_meta, 0, sizeof(val_meta));

  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_validate(NULL, &val_parent));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_validate(&val_meta, NULL));

  memset(&cols, 0, sizeof(cols));
  cols[0].name = "id";
  cols[0].type = C_ORM_TYPE_INT64;
  cols[0].offset = offsetof(struct ValParent, id);
  cols[0].is_nullable = 0;

  cols[1].name = "fk_id";
  cols[1].type = C_ORM_TYPE_INT64;
  cols[1].offset = offsetof(struct ValParent, fk_id);
  cols[1].is_nullable = 0;

  memset(&rels, 0, sizeof(rels));
  rels[0].field_name = "child";
  rels[0].type = C_ORM_RELATION_BELONGS_TO;
  rels[0].foreign_key = "fk_id";
  rels[0].struct_offset = offsetof(struct ValParent, child_ptr);
  rels[0].data_offset = offsetof(struct ValParent, child_ptr);
  rels[0].lazy_ctx_offset = offsetof(struct ValParent, ctx);

  memset(&val_meta, 0, sizeof(val_meta));
  val_meta.columns = cols;
  val_meta.num_columns = 2;
  val_meta.relations = rels;
  val_meta.num_relations = 1;
  val_meta.struct_size = sizeof(struct ValParent);

  memset(&val_parent, 0, sizeof(val_parent));
  val_parent.id = 1;
  val_parent.fk_id = 0;
  val_parent.child_ptr = NULL;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_validate(&val_meta, &val_parent));

  val_parent.fk_id = 99;
  ASSERT_EQ(C_ORM_OK, c_orm_validate(&val_meta, &val_parent));

  val_parent.fk_id = 0;
  val_parent.child_ptr = &dummy_child;
  ASSERT_EQ(C_ORM_OK, c_orm_validate(&val_meta, &val_parent));

  cols[1].is_nullable = 1;
  val_parent.child_ptr = NULL;
  ASSERT_EQ(C_ORM_OK, c_orm_validate(&val_meta, &val_parent));

  PASS();
}

TEST test_find_for_update_coverage(void) {
  struct S {
    int32_t id;
    char name[32];
  } out_s;
  c_orm_table_meta_t meta;
  c_orm_column_meta_t cols[2];

  memset(&cols, 0, sizeof(cols));
  cols[0].name = "id";
  cols[0].type = C_ORM_TYPE_INT32;
  cols[0].offset = offsetof(struct S, id);
  cols[1].name = "name";
  cols[1].type = C_ORM_TYPE_STRING;
  cols[1].offset = offsetof(struct S, name);

  memset(&meta, 0, sizeof(meta));
  meta.name = "test_s";
  meta.columns = cols;
  meta.num_columns = 2;
  meta.struct_size = sizeof(struct S);

  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_for_update_by_id_int32(NULL, &meta, 1, &out_s));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_for_update_by_id_int32(&g_db, NULL, 1, &out_s));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_for_update_by_id_int32(&g_db, &meta, 1, NULL));

  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_for_update_by_id_string(NULL, &meta, "abc", &out_s));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_for_update_by_id_string(&g_db, NULL, "abc", &out_s));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_for_update_by_id_string(&g_db, &meta, "abc", NULL));

  meta.query_select_by_pk = "SELECT id, name FROM test_s WHERE id = ?";
  meta.query_select_by_pk_for_update = NULL;
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_find_for_update_by_id_int32(&g_db, &meta, 1, &out_s));
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_find_for_update_by_id_string(&g_db, &meta, "1", &out_s));

  meta.query_select_by_pk_for_update =
      "SELECT id, name FROM test_s WHERE id = ? FOR UPDATE";
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_find_for_update_by_id_int32(&g_db, &meta, 1, &out_s));
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_find_for_update_by_id_string(&g_db, &meta, "1", &out_s));

  PASS();
}

TEST test_free_relations_coverage(void) {
  struct TargetChild {
    char *name;
  };
  struct FreeParent {
    struct TargetChild *single_child;
    c_orm_lazy_load_context_t single_ctx;
    struct Generic_Array children_arr;
    c_orm_lazy_load_context_t multi_ctx;
  } parent;

  c_orm_column_meta_t child_cols[1];
  c_orm_relation_meta_t parent_rels[2];
  c_orm_table_meta_t child_meta;
  c_orm_table_meta_t parent_meta;
  struct TargetChild *elem;

  memset(&parent, 0, sizeof(parent));
  memset(&parent_meta, 0, sizeof(parent_meta));

  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_free_relations(NULL, &parent));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_free_relations(&parent_meta, NULL));

  memset(&child_cols, 0, sizeof(child_cols));
  child_cols[0].name = "name";
  child_cols[0].type = C_ORM_TYPE_STRING;
  child_cols[0].offset = offsetof(struct TargetChild, name);

  memset(&child_meta, 0, sizeof(child_meta));
  child_meta.columns = child_cols;
  child_meta.num_columns = 1;
  child_meta.struct_size = sizeof(struct TargetChild);

  memset(&parent_rels, 0, sizeof(parent_rels));
  parent_rels[0].field_name = "single";
  parent_rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
  parent_rels[0].struct_offset = offsetof(struct FreeParent, single_child);
  parent_rels[0].data_offset = offsetof(struct FreeParent, single_child);
  parent_rels[0].lazy_ctx_offset = offsetof(struct FreeParent, single_ctx);
  parent_rels[0].target_meta = &child_meta;

  parent_rels[1].field_name = "multi";
  parent_rels[1].type = C_ORM_RELATION_ONE_TO_MANY;
  parent_rels[1].struct_offset = offsetof(struct FreeParent, children_arr);
  parent_rels[1].data_offset = offsetof(struct FreeParent, children_arr);
  parent_rels[1].lazy_ctx_offset = offsetof(struct FreeParent, multi_ctx);
  parent_rels[1].target_meta = &child_meta;

  memset(&parent_meta, 0, sizeof(parent_meta));
  parent_meta.relations = parent_rels;
  parent_meta.num_relations = 2;
  parent_meta.struct_size = sizeof(struct FreeParent);

  memset(&parent, 0, sizeof(parent));

  parent.single_ctx.is_loaded = 1;
  parent.single_child =
      (struct TargetChild *)c_orm_malloc(sizeof(struct TargetChild));
  parent.single_child->name = (char *)c_orm_malloc(16);
  C_ORM_STRCPY(parent.single_child->name, 16, "single_val");

  parent.multi_ctx.is_loaded = 1;
  parent.children_arr.length = 1;
  parent.children_arr.capacity = 1;
  parent.children_arr.data = c_orm_malloc(sizeof(struct TargetChild));
  elem = (struct TargetChild *)parent.children_arr.data;
  elem->name = (char *)c_orm_malloc(16);
  C_ORM_STRCPY(elem->name, 16, "multi_val");

  ASSERT_EQ(C_ORM_OK, c_orm_free_relations(&parent_meta, &parent));

  ASSERT_EQ(0, parent.single_ctx.is_loaded);
  ASSERT(parent.single_child == NULL);
  ASSERT_EQ(0, parent.multi_ctx.is_loaded);
  ASSERT(parent.children_arr.data == NULL);

  PASS();
}

TEST test_relation_meta_builder_coverage(void) {
  struct sql_table_t tbl;
  struct sql_column_t cols[1];
  struct sql_constraint_t col_cons[1];
  struct sql_constraint_t tbl_cons[1];
  c_orm_relation_meta_t *built_rels = NULL;
  size_t n_rels = 0;

  memset(&tbl, 0, sizeof(tbl));
  memset(cols, 0, sizeof(cols));
  memset(col_cons, 0, sizeof(col_cons));
  memset(tbl_cons, 0, sizeof(tbl_cons));

  cols[0].name = "team_id";
  cols[0].constraints = col_cons;
  cols[0].n_constraints = 1;
  col_cons[0].type = SQL_CONSTRAINT_FOREIGN_KEY;
  col_cons[0].reference_table = "teams";
  col_cons[0].reference_column = "id";

  tbl_cons[0].type = SQL_CONSTRAINT_FOREIGN_KEY;
  tbl_cons[0].reference_table = "roles";
  tbl_cons[0].reference_column = "role_id";

  tbl.name = "users";
  tbl.columns = cols;
  tbl.n_columns = 1;
  tbl.table_constraints = tbl_cons;
  tbl.n_table_constraints = 1;

  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_build_relation_meta(NULL, &built_rels, &n_rels));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_build_relation_meta(&tbl, NULL, &n_rels));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_build_relation_meta(&tbl, &built_rels, NULL));

  /* 0 FKs */
  col_cons[0].type = SQL_CONSTRAINT_NOT_NULL;
  tbl_cons[0].type = SQL_CONSTRAINT_PRIMARY_KEY;
  ASSERT_EQ(C_ORM_OK, c_orm_build_relation_meta(&tbl, &built_rels, &n_rels));
  ASSERT_EQ(0, n_rels);
  ASSERT_EQ(NULL, built_rels);

  /* Column and table FKs */
  col_cons[0].type = SQL_CONSTRAINT_FOREIGN_KEY;
  tbl_cons[0].type = SQL_CONSTRAINT_FOREIGN_KEY;
  ASSERT_EQ(C_ORM_OK, c_orm_build_relation_meta(&tbl, &built_rels, &n_rels));
  ASSERT_EQ(2, n_rels);
  ASSERT(built_rels != NULL);
  c_orm_free(built_rels);

  PASS();
}

struct SubCompany {
  int32_t id;
  char *name;
  double score;
  int32_t is_active;
  int64_t salary;
  c_orm_blob_t logo;
};

struct UserSubCompany {
  int32_t id;
  c_orm_lazy_load_context_t ctx;
  struct SubCompany *company;
};

static c_orm_error_t mock_prefix_get_column_count(c_orm_query_t *q,
                                                  int *count) {
  (void)q;
  if (!count)
    return C_ORM_ERROR_MEMORY;
  *count = 9;
  return C_ORM_OK;
}

static c_orm_error_t mock_prefix_get_column_name(c_orm_query_t *q, int index,
                                                 const char **name) {
  (void)q;
  if (!name)
    return C_ORM_ERROR_MEMORY;
  switch (index) {
  case 0:
    *name = "id";
    break;
  case 1:
    *name = "company_id";
    break;
  case 2:
    *name = "company_name";
    break;
  case 3:
    *name = "company_score";
    break;
  case 4:
    *name = "company_is_active";
    break;
  case 5:
    *name = "company_salary";
    break;
  case 6:
    *name = "company_logo";
    break;
  case 7:
    *name = "company_extra";
    break;
  case 8:
    *name = "unknown_prefix_col";
    break;
  default:
    *name = "";
    break;
  }
  return C_ORM_OK;
}

TEST test_prefix_column_hydration_coverage(void) {
  struct UserSubCompany user_obj;
  c_orm_column_meta_t pcols[1];
  c_orm_column_meta_t ccols[7];
  c_orm_relation_meta_t rels[1];
  c_orm_table_meta_t parent_meta;
  c_orm_table_meta_t company_meta;
  c_orm_db_t local_db;
  c_orm_driver_vtable_t local_vt;
  c_orm_query_t *q = (c_orm_query_t *)1;

  memset(&user_obj, 0, sizeof(user_obj));
  memset(&local_db, 0, sizeof(local_db));
  memset(&local_vt, 0, sizeof(local_vt));

  local_vt = g_vt;
  local_vt.get_column_count = mock_prefix_get_column_count;
  local_vt.get_column_name = mock_prefix_get_column_name;
  local_db.vtable = &local_vt;

  memset(pcols, 0, sizeof(pcols));
  pcols[0].name = "id";
  pcols[0].type = C_ORM_TYPE_INT32;
  pcols[0].offset = offsetof(struct UserSubCompany, id);
  pcols[0].is_pk = 1;

  memset(ccols, 0, sizeof(ccols));
  ccols[0].name = "id";
  ccols[0].type = C_ORM_TYPE_INT32;
  ccols[0].offset = offsetof(struct SubCompany, id);

  ccols[1].name = "name";
  ccols[1].type = C_ORM_TYPE_STRING;
  ccols[1].offset = offsetof(struct SubCompany, name);

  ccols[2].name = "score";
  ccols[2].type = C_ORM_TYPE_DOUBLE;
  ccols[2].offset = offsetof(struct SubCompany, score);

  ccols[3].name = "is_active";
  ccols[3].type = C_ORM_TYPE_BOOL;
  ccols[3].offset = offsetof(struct SubCompany, is_active);

  ccols[4].name = "salary";
  ccols[4].type = C_ORM_TYPE_INT64;
  ccols[4].offset = offsetof(struct SubCompany, salary);

  ccols[5].name = "logo";
  ccols[5].type = C_ORM_TYPE_BLOB;
  ccols[5].offset = offsetof(struct SubCompany, logo);

  ccols[6].name = "extra";
  ccols[6].type = (c_orm_type_t)999;
  ccols[6].offset = offsetof(struct SubCompany, id);

  memset(&company_meta, 0, sizeof(company_meta));
  company_meta.name = "company";
  company_meta.columns = ccols;
  company_meta.num_columns = 7;
  company_meta.struct_size = sizeof(struct SubCompany);

  memset(rels, 0, sizeof(rels));
  rels[0].field_name = "company";
  rels[0].type = C_ORM_RELATION_BELONGS_TO;
  rels[0].struct_offset = offsetof(struct UserSubCompany, company);
  rels[0].data_offset = offsetof(struct UserSubCompany, company);
  rels[0].lazy_ctx_offset = offsetof(struct UserSubCompany, ctx);
  rels[0].target_meta = &company_meta;

  memset(&parent_meta, 0, sizeof(parent_meta));
  parent_meta.name = "users";
  parent_meta.columns = pcols;
  parent_meta.num_columns = 1;
  parent_meta.relations = rels;
  parent_meta.num_relations = 1;
  parent_meta.struct_size = sizeof(struct UserSubCompany);

  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_hydrate_row(&local_db, q, &parent_meta, &user_obj));
  ASSERT(user_obj.company != NULL);
  if (user_obj.company) {
    if (user_obj.company->name) {
      c_orm_free(user_obj.company->name);
    }
    c_orm_free(user_obj.company);
    user_obj.company = NULL;
  }

  PASS();
}

struct GeoSecureStruct {
  int32_t id;
  c_orm_point_t pt;
  c_orm_polygon_t poly;
  char *sec_str;
};

TEST test_point_polygon_secure_coverage(void) {
  struct GeoSecureStruct geo_obj;
  c_orm_column_meta_t cols[4];
  c_orm_table_meta_t meta;
  c_orm_db_t local_db;
  c_orm_query_t *q = (c_orm_query_t *)1;
  unsigned char pt_wkb[21];
  unsigned char poly_wkb[29];
  double x = 1.23, y = 4.56;
  uint32_t n_pts = 1;

  memset(&geo_obj, 0, sizeof(geo_obj));
  memset(&local_db, 0, sizeof(local_db));
  local_db = g_db;
  local_db.decrypt_hook = mock_decrypt;

  memset(pt_wkb, 0, sizeof(pt_wkb));
  memcpy(&pt_wkb[5], &x, 8);
  memcpy(&pt_wkb[13], &y, 8);

  memset(poly_wkb, 0, sizeof(poly_wkb));
  memcpy(&poly_wkb[9], &n_pts, 4);
  memcpy(&poly_wkb[13], &x, 8);
  memcpy(&poly_wkb[21], &y, 8);

  memset(cols, 0, sizeof(cols));
  cols[0].name = "id";
  cols[0].type = C_ORM_TYPE_INT32;
  cols[0].offset = offsetof(struct GeoSecureStruct, id);
  cols[0].is_pk = 1;

  cols[1].name = "pt";
  cols[1].type = C_ORM_TYPE_POINT;
  cols[1].offset = offsetof(struct GeoSecureStruct, pt);

  cols[2].name = "poly";
  cols[2].type = C_ORM_TYPE_POLYGON;
  cols[2].offset = offsetof(struct GeoSecureStruct, poly);

  cols[3].name = "sec_str";
  cols[3].type = C_ORM_TYPE_STRING;
  cols[3].offset = offsetof(struct GeoSecureStruct, sec_str);
  cols[3].is_secure = 1;

  memset(&meta, 0, sizeof(meta));
  meta.name = "geo_secure";
  meta.columns = cols;
  meta.num_columns = 4;
  meta.struct_size = sizeof(struct GeoSecureStruct);

  g_custom_blob_data = pt_wkb;
  g_custom_blob_size = sizeof(pt_wkb);
  g_step_count = 0;

  ASSERT_EQ(C_ORM_OK, c_orm_hydrate_row(&local_db, q, &meta, &geo_obj));
  ASSERT_EQ(1.23, geo_obj.pt.x);
  ASSERT_EQ(4.56, geo_obj.pt.y);

  c_orm_free(geo_obj.poly.points);
  geo_obj.poly.points = NULL;
  c_orm_free(geo_obj.sec_str);
  geo_obj.sec_str = NULL;

  g_custom_blob_data = poly_wkb;
  g_custom_blob_size = sizeof(poly_wkb);
  g_step_count = 0;

  ASSERT_EQ(C_ORM_OK, c_orm_hydrate_row(&local_db, q, &meta, &geo_obj));
  ASSERT_EQ(1, geo_obj.poly.num_points);
  ASSERT(geo_obj.poly.points != NULL);

  c_orm_free(geo_obj.poly.points);
  geo_obj.poly.points = NULL;
  c_orm_free(geo_obj.sec_str);
  geo_obj.sec_str = NULL;

  g_custom_blob_data = NULL;
  g_custom_blob_size = 0;

  PASS();
}

static void mock_expire_cb(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                           void *obj, void *user_data) {
  (void)db;
  (void)meta;
  (void)obj;
  if (user_data) {
    *(int *)user_data = 1;
  }
}

struct NullablePrimStruct {
  int32_t *i32_val;
  bool *b_val;
  int64_t *i64_val;
  float *f_val;
  double *d_val;
};

struct EdgeCaseStruct {
  int64_t created_at;
  int32_t expires_in;
  c_orm_blob_t sec_blob;
  c_orm_blob_t empty_blob;
  char *tz_ts;
  int32_t bad_type_val;
};

TEST test_hydrate_and_free_columns_coverage(void) {
  struct NullablePrimStruct np;
  c_orm_column_meta_t np_cols[5];
  c_orm_table_meta_t np_meta;
  c_orm_query_t *q = (c_orm_query_t *)1;
  struct EdgeCaseStruct ec;
  c_orm_column_meta_t ec_cols[3];
  c_orm_table_meta_t ec_meta;
  c_orm_db_t local_db;
  int expired_called = 0;

  memset(&np_meta, 0, sizeof(np_meta));
  /* 1. Test c_orm_free_columns error & type paths */
  c_orm_free_columns(NULL, NULL);
  c_orm_free_columns(&np_meta, NULL);

  memset(&np, 0, sizeof(np));
  memset(np_cols, 0, sizeof(np_cols));

  np_cols[0].name = "i32";
  np_cols[0].type = C_ORM_TYPE_INT32;
  np_cols[0].offset = offsetof(struct NullablePrimStruct, i32_val);
  np_cols[0].is_nullable = 1;

  np_cols[1].name = "b";
  np_cols[1].type = C_ORM_TYPE_BOOL;
  np_cols[1].offset = offsetof(struct NullablePrimStruct, b_val);
  np_cols[1].is_nullable = 1;

  np_cols[2].name = "i64";
  np_cols[2].type = C_ORM_TYPE_INT64;
  np_cols[2].offset = offsetof(struct NullablePrimStruct, i64_val);
  np_cols[2].is_nullable = 1;

  np_cols[3].name = "f";
  np_cols[3].type = C_ORM_TYPE_FLOAT;
  np_cols[3].offset = offsetof(struct NullablePrimStruct, f_val);
  np_cols[3].is_nullable = 1;

  np_cols[4].name = "d";
  np_cols[4].type = C_ORM_TYPE_DOUBLE;
  np_cols[4].offset = offsetof(struct NullablePrimStruct, d_val);
  np_cols[4].is_nullable = 1;

  memset(&np_meta, 0, sizeof(np_meta));
  np_meta.name = "nullable_prims";
  np_meta.columns = np_cols;
  np_meta.num_columns = 5;
  np_meta.struct_size = sizeof(struct NullablePrimStruct);

  /* Hydrate with non-null values */
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_hydrate_row(&g_db, q, &np_meta, &np));
  ASSERT(np.i32_val != NULL);
  ASSERT(np.b_val != NULL);
  ASSERT(np.i64_val != NULL);
  ASSERT(np.f_val != NULL);
  ASSERT(np.d_val != NULL);

  /* Free via c_orm_free_columns */
  c_orm_free_columns(&np_meta, &np);
  ASSERT(np.i32_val == NULL);
  ASSERT(np.b_val == NULL);
  ASSERT(np.i64_val == NULL);
  ASSERT(np.f_val == NULL);
  ASSERT(np.d_val == NULL);

  /* 2. Edge cases in hydrate_row: Timezone timestamp, secure blob, empty blob,
   * bad type, and TTL */
  memset(&ec, 0, sizeof(ec));
  memset(&local_db, 0, sizeof(local_db));
  local_db = g_db;
  local_db.timezone.offset_minutes = 60;
  local_db.decrypt_hook = mock_decrypt;
  local_db.expire_cb = mock_expire_cb;
  local_db.expire_user_data = &expired_called;

  memset(ec_cols, 0, sizeof(ec_cols));
  ec_cols[0].name = "tz_ts";
  ec_cols[0].type = C_ORM_TYPE_TIMESTAMP;
  ec_cols[0].offset = offsetof(struct EdgeCaseStruct, tz_ts);

  ec_cols[1].name = "sec_blob";
  ec_cols[1].type = C_ORM_TYPE_BLOB;
  ec_cols[1].offset = offsetof(struct EdgeCaseStruct, sec_blob);
  ec_cols[1].is_secure = 1;

  ec_cols[2].name = "empty_blob";
  ec_cols[2].type = C_ORM_TYPE_BLOB;
  ec_cols[2].offset = offsetof(struct EdgeCaseStruct, empty_blob);

  memset(&ec_meta, 0, sizeof(ec_meta));
  ec_meta.name = "edge_cases";
  ec_meta.columns = ec_cols;
  ec_meta.num_columns = 3;
  ec_meta.struct_size = sizeof(struct EdgeCaseStruct);
  ec_meta.has_ttl = 1;
  ec_meta.created_at_offset = offsetof(struct EdgeCaseStruct, created_at);
  ec_meta.expires_in_offset = offsetof(struct EdgeCaseStruct, expires_in);

  /* Test with valid blob and timestamp, and TTL expiration */
  g_custom_blob_data = "securedata";
  g_custom_blob_size = 10;
  g_step_count = 0;
  ASSERT_EQ(C_ORM_ERROR_EXPIRED,
            c_orm_hydrate_row(&local_db, q, &ec_meta, &ec));
  ASSERT_EQ(1, expired_called);
  c_orm_free_columns(&ec_meta, &ec);

  /* Test unsupported type returns C_ORM_ERROR_TYPE_MISMATCH */
  ec_meta.has_ttl = 0;
  ec_cols[2].type = C_ORM_TYPE_UNKNOWN;
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH,
            c_orm_hydrate_row(&local_db, q, &ec_meta, &ec));

  g_custom_blob_data = NULL;
  g_custom_blob_size = 0;

  PASS();
}

TEST test_api_null_and_error_params(void) {
  char buf[256];
  int exists;
  void *void_out;
  size_t sz_out;
  const char *str_arr[2];
  c_orm_shard_manager_t *sm;
  c_orm_db_t *db_out;
  c_orm_query_t *q;
  struct c_orm_iterator *it;
  struct CddCVariant var[1];
  const c_orm_table_meta_t *meta_arr[1];
  c_orm_identity_map_t imap;

  exists = 0;
  void_out = NULL;
  sz_out = 0;
  sm = NULL;
  db_out = NULL;
  q = (c_orm_query_t *)1;
  it = NULL;
  str_arr[0] = "col";
  str_arr[1] = NULL;
  memset(var, 0, sizeof(var));
  meta_arr[0] = &mega_meta;
  memset(&imap, 0, sizeof(imap));
  memset(buf, 0, sizeof(buf));

  /* Savepoints */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_savepoint_create(NULL, "sp"));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_savepoint_create(&g_db, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_savepoint_rollback(NULL, "sp"));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_savepoint_rollback(&g_db, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_savepoint_release(NULL, "sp"));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_savepoint_release(&g_db, NULL));

  /* Config & Pragmas */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_config_sqlite_pragma(NULL, "p"));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_config_sqlite_pragma(&g_db, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_config_postgres_set(NULL, "s"));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_config_postgres_set(&g_db, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_config_mysql_session(NULL, "s"));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_config_mysql_session(&g_db, NULL));

  /* Raw & Delete All */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_execute_raw(NULL, "sql"));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_execute_raw(&g_db, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_delete_all(NULL, &mega_meta));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_delete_all(&g_db, NULL));

  /* Find & Hydrate */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_find_all(NULL, &mega_meta, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_find_all(&g_db, NULL, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_find_all(&g_db, &mega_meta, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_by_id_int32(NULL, &mega_meta, 1, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_find_by_id_int32(&g_db, NULL, 1, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_by_id_int32(&g_db, &mega_meta, 1, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_by_id_string(NULL, &mega_meta, "1", buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_find_by_id_string(&g_db, NULL, "1", buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_by_id_string(&g_db, &mega_meta, NULL, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_by_id_string(&g_db, &mega_meta, "1", NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_for_update_by_id_int32(NULL, &mega_meta, 1, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_for_update_by_id_int32(&g_db, NULL, 1, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_for_update_by_id_int32(&g_db, &mega_meta, 1, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_for_update_by_id_string(NULL, &mega_meta, "1", buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_for_update_by_id_string(&g_db, NULL, "1", buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_for_update_by_id_string(&g_db, &mega_meta, NULL, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_for_update_by_id_string(&g_db, &mega_meta, "1", NULL));

  /* Delete by id */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_delete_by_id_int32(NULL, &mega_meta, 1));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_delete_by_id_int32(&g_db, NULL, 1));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_delete_by_id_string(NULL, &mega_meta, "1"));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_delete_by_id_string(&g_db, NULL, "1"));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_delete_by_id_string(&g_db, &mega_meta, NULL));

  /* Find One By String */
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_one_by_string(NULL, &mega_meta, "id", "1", buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_one_by_string(&g_db, NULL, "id", "1", buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_one_by_string(&g_db, &mega_meta, NULL, "1", buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_one_by_string(&g_db, &mega_meta, "id", NULL, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_one_by_string(&g_db, &mega_meta, "id", "1", NULL));

  /* Exists */
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_exists_int32(NULL, &mega_meta, 1, &exists));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_exists_int32(&g_db, NULL, 1, &exists));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_exists_int32(&g_db, &mega_meta, 1, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_exists_string(NULL, &mega_meta, "1", &exists));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_exists_string(&g_db, NULL, "1", &exists));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_exists_string(&g_db, &mega_meta, NULL, &exists));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_exists_string(&g_db, &mega_meta, "1", NULL));

  /* Paginated */
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_all_paginated(NULL, &mega_meta, buf, 10, 0));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_all_paginated(&g_db, NULL, buf, 10, 0));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_all_paginated(&g_db, &mega_meta, NULL, 10, 0));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_lazy_load_paginated(NULL, &mega_meta, buf, "rel", 10, 0));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_lazy_load_paginated(&g_db, NULL, buf, "rel", 10, 0));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_lazy_load_paginated(&g_db, &mega_meta, NULL, "rel", 10, 0));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_lazy_load_paginated(&g_db, &mega_meta, buf, NULL, 10, 0));

  /* Attach, Detach, Sync */
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_attach(NULL, &mega_meta, buf, "rel", buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_attach(&g_db, NULL, buf, "rel", buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_attach(&g_db, &mega_meta, NULL, "rel", buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_attach(&g_db, &mega_meta, buf, NULL, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_attach(&g_db, &mega_meta, buf, "rel", NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_detach(NULL, &mega_meta, buf, "rel", buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_detach(&g_db, NULL, buf, "rel", buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_detach(&g_db, &mega_meta, NULL, "rel", buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_detach(&g_db, &mega_meta, buf, NULL, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_detach(&g_db, &mega_meta, buf, "rel", NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_sync(NULL, &mega_meta, buf, "rel", buf, 1));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_sync(&g_db, NULL, buf, "rel", buf, 1));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_sync(&g_db, &mega_meta, NULL, "rel", buf, 1));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_sync(&g_db, &mega_meta, buf, NULL, buf, 1));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_sync(&g_db, &mega_meta, buf, "rel", NULL, 1));

  /* Generic CRUD */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_insert_generic(NULL, &mega_meta, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_insert_generic(&g_db, NULL, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_insert_generic(&g_db, &mega_meta, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_get_generic(NULL, &mega_meta, 1, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_get_generic(&g_db, NULL, 1, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_get_generic(&g_db, &mega_meta, 1, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_get_generic_string(NULL, &mega_meta, "1", buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_get_generic_string(&g_db, NULL, "1", buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_get_generic_string(&g_db, &mega_meta, NULL, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_get_generic_string(&g_db, &mega_meta, "1", NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_all_generic(NULL, &mega_meta, &void_out, &sz_out));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_all_generic(&g_db, NULL, &void_out, &sz_out));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_all_generic(&g_db, &mega_meta, NULL, &sz_out));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_all_generic(&g_db, &mega_meta, &void_out, NULL));

  /* Update Partial */
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_update_partial(NULL, &mega_meta, buf, str_arr, 1));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_update_partial(&g_db, NULL, buf, str_arr, 1));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_update_partial(&g_db, &mega_meta, NULL, str_arr, 1));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_update_partial(&g_db, &mega_meta, buf, NULL, 1));

  /* Deep copy & Shard & Relations */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_deep_copy(NULL, buf, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_deep_copy((const struct cdd_c_meta *)&mega_meta, NULL, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_deep_copy((const struct cdd_c_meta *)&mega_meta, buf, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_identity_map_get_or_set_int(
                                    NULL, &mega_meta, 1, buf, &void_out));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_identity_map_get_or_set_int(&imap, NULL, 1, buf, &void_out));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_identity_map_get_or_set_int(&imap, &mega_meta, 1, buf, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_identity_map_get_or_set_str(
                                    NULL, &mega_meta, "1", buf, &void_out));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_identity_map_get_or_set_str(
                                    &imap, NULL, "1", buf, &void_out));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_identity_map_get_or_set_str(
                                    &imap, &mega_meta, NULL, buf, &void_out));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_identity_map_get_or_set_str(
                                    &imap, &mega_meta, "1", buf, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_shard_route_hash(NULL, "key", &db_out));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_shard_route_hash(sm, NULL, &db_out));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_shard_route_hash(sm, "key", NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_scatter_gather_generic(NULL, &mega_meta, &void_out, &sz_out));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_scatter_gather_generic(sm, NULL, &void_out, &sz_out));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_scatter_gather_generic(sm, &mega_meta, NULL, &sz_out));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_scatter_gather_generic(sm, &mega_meta, &void_out, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_validate_relations(NULL, 1));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_validate_relations(meta_arr, 0));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_build_relation_meta(NULL, (c_orm_relation_meta_t **)&void_out,
                                      &sz_out));

  /* Iterator & Batch */
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_batch_init(NULL, &mega_meta, "s", 1, &it));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_batch_init(&g_db, NULL, "s", 1, &it));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_batch_init(&g_db, &mega_meta, "s", 0, &it));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_batch_init(&g_db, &mega_meta, "s", 1, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_iterator_next(NULL, buf, &sz_out));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_iterator_next(it, NULL, &sz_out));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_iterator_next(it, buf, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_iterator_close(NULL));

  /* Composite keys */
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_by_composite_key(NULL, &mega_meta, 1, var, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_by_composite_key(&g_db, NULL, 1, var, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_by_composite_key(&g_db, &mega_meta, 1, NULL, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_by_composite_key(&g_db, &mega_meta, 1, var, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_delete_by_composite_key(NULL, &mega_meta, 1, var));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_delete_by_composite_key(&g_db, NULL, 1, var));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_delete_by_composite_key(&g_db, &mega_meta, 1, NULL));

  /* Hydrate All / Row */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_hydrate_all(NULL, q, &mega_meta, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_hydrate_all(&g_db, NULL, &mega_meta, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_hydrate_all(&g_db, q, NULL, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_hydrate_all(&g_db, q, &mega_meta, NULL));

  /* Relations */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_find_with_relations_int32(
                                    NULL, &mega_meta, 1, str_arr, 1, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_with_relations_int32(&g_db, NULL, 1, str_arr, 1, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_find_with_relations_int32(
                                    &g_db, &mega_meta, 1, NULL, 1, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_find_with_relations_int32(
                                    &g_db, &mega_meta, 1, str_arr, 1, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_all_with_relations(NULL, &mega_meta, str_arr, 1, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_all_with_relations(&g_db, NULL, str_arr, 1, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_all_with_relations(&g_db, &mega_meta, NULL, 1, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_all_with_relations(&g_db, &mega_meta, str_arr, 1, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_with_relation_int32(NULL, &mega_meta, 1, "rel", buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_with_relation_int32(&g_db, NULL, 1, "rel", buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_with_relation_int32(&g_db, &mega_meta, 1, NULL, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_with_relation_int32(&g_db, &mega_meta, 1, "rel", NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_all_with_relation(NULL, &mega_meta, "rel", buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_all_with_relation(&g_db, NULL, "rel", buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_all_with_relation(&g_db, &mega_meta, NULL, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_all_with_relation(&g_db, &mega_meta, "rel", NULL));

  /* Insert, Update, Delete, Save */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_insert(NULL, &mega_meta, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_insert(&g_db, NULL, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_insert(&g_db, &mega_meta, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_update(NULL, &mega_meta, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_update(&g_db, NULL, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_update(&g_db, &mega_meta, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_delete(NULL, &mega_meta, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_delete(&g_db, NULL, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_delete(&g_db, &mega_meta, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_save(NULL, &mega_meta, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_save(&g_db, NULL, buf));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_save(&g_db, &mega_meta, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_insert_batch(NULL, &mega_meta, buf, 1, 1));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_insert_batch(&g_db, NULL, buf, 1, 1));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_insert_batch(&g_db, &mega_meta, NULL, 1, 1));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_insert_batch_ext(NULL, &mega_meta, buf, 1, 1, 0, NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_insert_batch_ext(&g_db, NULL, buf, 1, 1, 0, NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_insert_batch_ext(&g_db, &mega_meta, NULL,
                                                       1, 1, 0, NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_update_batch(NULL, &mega_meta, buf, 1, 1));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_update_batch(&g_db, NULL, buf, 1, 1));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_update_batch(&g_db, &mega_meta, NULL, 1, 1));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_delete_batch(NULL, &mega_meta, buf, 1, 1));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_delete_batch(&g_db, NULL, buf, 1, 1));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_delete_batch(&g_db, &mega_meta, NULL, 1, 1));

  PASS();
}

static void dummy_query_interceptor(c_orm_db_t *db, const char *sql,
                                    void *ctx) {
  int *called;
  (void)db;
  (void)sql;
  called = (int *)ctx;
  if (called)
    *called = 1;
}

struct TestFreePolyStruct {
  c_orm_polygon_t poly;
};

struct TestHydrateNullPrealloc {
  char *str_val;
  char *json_val;
  c_orm_blob_t blob_val;
  int32_t *i32_val;
};

TEST test_api_deep_branches(void) {
  char buf[256];
  int exists;
  struct TestFreePolyStruct ps;
  c_orm_column_meta_t p_col;
  c_orm_table_meta_t p_meta;
  struct TestHydrateNullPrealloc thnp;
  c_orm_column_meta_t np_cols[4];
  c_orm_table_meta_t np_meta;
  c_orm_db_t local_db;
  c_orm_driver_vtable_t local_vt;
  c_orm_query_t *dummy_q;
  int interceptor_called;
  c_orm_table_meta_t no_pk_meta;
  c_orm_table_meta_t t1_meta;
  c_orm_table_meta_t t2_meta;
  c_orm_relation_meta_t r1;
  c_orm_relation_meta_t r2;
  const c_orm_table_meta_t *val_tables[2];
  c_orm_shard_manager_t *sm2;
  c_orm_db_t *routed;
  c_orm_table_meta_t view_meta;
  struct CddCVariant unk_var[1];
  struct CddCVariant var[1];
  c_orm_table_meta_t no_pk_col_meta;
  c_orm_column_meta_t dummy_col;

  exists = 0;
  interceptor_called = 0;
  dummy_q = (c_orm_query_t *)1;
  sm2 = NULL;
  routed = NULL;
  memset(var, 0, sizeof(var));
  memset(buf, 0, sizeof(buf));

  /* 1. Polygon free with allocated points */
  memset(&p_col, 0, sizeof(p_col));
  memset(&p_meta, 0, sizeof(p_meta));
  p_col.name = "poly";
  p_col.type = C_ORM_TYPE_POLYGON;
  p_col.offset = offsetof(struct TestFreePolyStruct, poly);
  p_meta.columns = &p_col;
  p_meta.num_columns = 1;
  ps.poly.num_points = 2;
  ps.poly.points = (c_orm_point_t *)malloc(sizeof(c_orm_point_t) * 2);
  c_orm_free_columns(&p_meta, &ps);
  ASSERT_EQ(NULL, ps.poly.points);
  ASSERT_EQ(0, ps.poly.num_points);

  /* 2. Hydrate NULL into pre-allocated fields to hit free branches */
  memset(np_cols, 0, sizeof(np_cols));
  memset(&np_meta, 0, sizeof(np_meta));
  np_cols[0].name = "str";
  np_cols[0].type = C_ORM_TYPE_STRING;
  np_cols[0].offset = offsetof(struct TestHydrateNullPrealloc, str_val);
  np_cols[0].is_nullable = 1;

  np_cols[1].name = "json";
  np_cols[1].type = C_ORM_TYPE_JSON;
  np_cols[1].offset = offsetof(struct TestHydrateNullPrealloc, json_val);
  np_cols[1].is_nullable = 1;

  np_cols[2].name = "blob";
  np_cols[2].type = C_ORM_TYPE_BLOB;
  np_cols[2].offset = offsetof(struct TestHydrateNullPrealloc, blob_val);
  np_cols[2].is_nullable = 1;

  np_cols[3].name = "i32";
  np_cols[3].type = C_ORM_TYPE_INT32;
  np_cols[3].offset = offsetof(struct TestHydrateNullPrealloc, i32_val);
  np_cols[3].is_nullable = 1;

  np_meta.name = "test_prealloc";
  np_meta.columns = np_cols;
  np_meta.num_columns = 4;
  np_meta.struct_size = sizeof(thnp);

  thnp.str_val = (char *)malloc(10);
  thnp.json_val = (char *)malloc(10);
  thnp.blob_val.data = malloc(10);
  thnp.blob_val.size = 10;
  thnp.i32_val = (int32_t *)malloc(sizeof(int32_t));

  local_vt = g_vt;
  local_vt.is_null = mock_is_null_true;
  local_db = g_db;
  local_db.vtable = &local_vt;

  ASSERT_EQ(C_ORM_OK,
            c_orm_hydrate_row_from(&local_db, dummy_q, &np_meta, &thnp, 0));
  ASSERT_EQ(NULL, thnp.str_val);
  ASSERT_EQ(NULL, thnp.json_val);
  ASSERT_EQ(NULL, thnp.blob_val.data);
  ASSERT_EQ(NULL, thnp.i32_val);

  /* 3. Query interceptor in c_orm_execute_raw */
  g_db.query_interceptor = dummy_query_interceptor;
  g_db.query_interceptor_ctx = &interceptor_called;
  ASSERT_EQ(C_ORM_OK, c_orm_execute_raw(&g_db, "SELECT 1"));
  ASSERT_EQ(1, interceptor_called);
  g_db.query_interceptor = NULL;
  g_db.query_interceptor_ctx = NULL;

  /* 4. Missing query_delete_by_pk / query_select_by_pk */
  no_pk_meta = mega_meta;
  no_pk_meta.query_delete_by_pk = NULL;
  no_pk_meta.query_select_by_pk = NULL;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_delete_by_id_string(&g_db, &no_pk_meta, "1"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_delete_by_id_int32(&g_db, &no_pk_meta, 1));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_exists_int32(&g_db, &no_pk_meta, 1, &exists));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_exists_string(&g_db, &no_pk_meta, "1", &exists));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_find_by_composite_key(&g_db, &no_pk_meta, 1, var, buf));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_delete_by_composite_key(&g_db, &no_pk_meta, 1, var));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_find_by_id_int32(&g_db, &no_pk_meta, 1, buf));

  /* View read-only checks */
  view_meta = mega_meta;
  view_meta.is_view = 1;
  ASSERT_EQ(C_ORM_ERROR_READ_ONLY,
            c_orm_delete_by_id_int32(&g_db, &view_meta, 1));
  ASSERT_EQ(C_ORM_ERROR_READ_ONLY,
            c_orm_delete_by_composite_key(&g_db, &view_meta, 1, var));

  /* Composite key unknown variant */
  unk_var[0].type = 999;
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED,
            c_orm_find_by_composite_key(&g_db, &mega_meta, 1, unk_var, buf));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED,
            c_orm_delete_by_composite_key(&g_db, &mega_meta, 1, unk_var));

  /* Exists string without PK column */
  memset(&dummy_col, 0, sizeof(dummy_col));
  dummy_col.name = "dummy";
  dummy_col.is_pk = 0;
  no_pk_col_meta = mega_meta;
  no_pk_col_meta.columns = &dummy_col;
  no_pk_col_meta.num_columns = 1;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_exists_string(&g_db, &no_pk_col_meta, "1", &exists));

  /* 5. Circular relation validation */
  memset(&t1_meta, 0, sizeof(t1_meta));
  memset(&t2_meta, 0, sizeof(t2_meta));
  memset(&r1, 0, sizeof(r1));
  memset(&r2, 0, sizeof(r2));
  t1_meta.name = "t1";
  t2_meta.name = "t2";
  r1.target_table = "t2";
  r2.target_table = "t1";
  t1_meta.relations = &r1;
  t1_meta.num_relations = 1;
  t2_meta.relations = &r2;
  t2_meta.num_relations = 1;
  val_tables[0] = &t1_meta;
  val_tables[1] = &t2_meta;
  ASSERT_EQ(C_ORM_ERROR_RECURSION, c_orm_validate_relations(val_tables, 2));

  /* 6. Identity map NULL init & free */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_identity_map_init(NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_identity_map_free(NULL));

  /* 7. Shard route hash with missing node */
  ASSERT_EQ(C_ORM_OK, c_orm_shard_manager_init(2, &sm2));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_shard_route_hash(sm2, "test_key", &routed));
  c_orm_shard_manager_free(sm2);

  /* 8. Lazy load paginated relation not found */
  ASSERT_EQ(
      C_ORM_ERROR_NOT_FOUND,
      c_orm_lazy_load_paginated(&g_db, &mega_meta, buf, "nonexistent", 10, 0));

  /* 9. find_for_update not found */
  g_step_count = 1;
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_find_for_update_by_id_int32(&g_db, &mega_meta, 1, buf));
  g_step_count = 1;
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_find_for_update_by_id_string(&g_db, &mega_meta, "1", buf));

  /* 10. Save with string PK and fallback */
  {
    c_orm_table_meta_t str_pk_meta;
    c_orm_column_meta_t str_pk_col;
    struct {
      char *id;
    } str_obj;
    str_pk_meta = mega_meta;
    str_pk_col = my_cols[1];
    str_pk_col.is_pk = 1;
    str_pk_col.type = C_ORM_TYPE_STRING;
    str_pk_col.offset = 0;
    str_pk_meta.columns = &str_pk_col;
    str_pk_meta.num_columns = 1;
    str_pk_meta.query_insert = "INSERT INTO t (id) VALUES (?)";
    str_pk_meta.query_update = "UPDATE t SET id = ? WHERE id = ?";

    str_obj.id = "my_id";
    ASSERT_EQ(C_ORM_OK, c_orm_save(&g_db, &str_pk_meta, &str_obj));

    str_obj.id = "";
    ASSERT_EQ(C_ORM_OK, c_orm_save(&g_db, &str_pk_meta, &str_obj));

    str_pk_col.type = C_ORM_TYPE_INT64;
    ASSERT_EQ(C_ORM_OK, c_orm_save(&g_db, &str_pk_meta, &str_obj));
  }

  /* 11. update_partial with string PK, int32 field, and errors */
  {
    c_orm_table_meta_t up_meta;
    c_orm_column_meta_t up_cols[3];
    const char *up_fields[1];
    struct {
      char *id;
      int32_t count;
      float score;
    } up_obj;

    up_obj.id = "pk1";
    up_obj.count = 42;
    up_obj.score = 3.14f;

    memset(up_cols, 0, sizeof(up_cols));
    up_cols[0].name = "id";
    up_cols[0].type = C_ORM_TYPE_STRING;
    up_cols[0].offset = 0;
    up_cols[0].is_pk = 1;

    up_cols[1].name = "count";
    up_cols[1].type = C_ORM_TYPE_INT32;
    up_cols[1].offset = sizeof(char *);

    up_cols[2].name = "score";
    up_cols[2].type = C_ORM_TYPE_FLOAT;
    up_cols[2].offset = sizeof(char *) + sizeof(int32_t);

    memset(&up_meta, 0, sizeof(up_meta));
    up_meta.name = "up_tbl";
    up_meta.columns = up_cols;
    up_meta.num_columns = 3;
    up_meta.struct_size = sizeof(up_obj);

    /* Update int32 field with string PK */
    up_fields[0] = "count";
    g_step_count = 0;
    ASSERT_EQ(C_ORM_OK,
              c_orm_update_partial(&g_db, &up_meta, &up_obj, up_fields, 1));

    /* Update float field -> not implemented */
    up_fields[0] = "score";
    ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED,
              c_orm_update_partial(&g_db, &up_meta, &up_obj, up_fields, 1));

    /* Missing PK column */
    up_cols[0].is_pk = 0;
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
              c_orm_update_partial(&g_db, &up_meta, &up_obj, up_fields, 1));
    up_cols[0].is_pk = 1;

    /* PK type neither int32 nor string */
    up_cols[0].type = C_ORM_TYPE_FLOAT;
    up_fields[0] = "count";
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
              c_orm_update_partial(&g_db, &up_meta, &up_obj, up_fields, 1));
  }

  PASS();
}

static c_orm_error_t mock_attach_fail(void *parent, void *child, void *db) {
  (void)parent;
  (void)child;
  (void)db;
  return C_ORM_ERROR_VALIDATION;
}

static c_orm_error_t mock_attach_ok(void *parent, void *child, void *db) {
  (void)parent;
  (void)child;
  (void)db;
  return C_ORM_OK;
}

static c_orm_error_t mock_detach_fail(void *parent, void *child, void *db) {
  (void)parent;
  (void)child;
  (void)db;
  return C_ORM_ERROR_VALIDATION;
}

struct ParentObj {
  int32_t id;
};

struct ChildObj {
  int32_t id;
  int32_t parent_id;
  int64_t *fk64;
  float f_val;
  double d_val;
};

TEST test_attach_detach_sync_coverage(void) {
  struct ParentObj parent;
  struct ChildObj child;
  struct ChildObj children[2];
  c_orm_column_meta_t p_cols[1];
  c_orm_column_meta_t c_cols[5];
  c_orm_relation_meta_t rels[2];
  c_orm_table_meta_t p_meta;
  c_orm_table_meta_t c_meta;
  int64_t val64;

  val64 = 42;
  parent.id = 1;
  memset(&child, 0, sizeof(child));
  child.id = 10;
  child.parent_id = 0;
  child.fk64 = &val64;
  child.f_val = 1.0f;
  child.d_val = 2.0;

  memset(children, 0, sizeof(children));
  children[0].id = 10;
  children[1].id = 11;

  memset(p_cols, 0, sizeof(p_cols));
  p_cols[0].name = "id";
  p_cols[0].type = C_ORM_TYPE_INT32;
  p_cols[0].offset = offsetof(struct ParentObj, id);
  p_cols[0].is_pk = 1;

  memset(c_cols, 0, sizeof(c_cols));
  c_cols[0].name = "id";
  c_cols[0].type = C_ORM_TYPE_INT32;
  c_cols[0].offset = offsetof(struct ChildObj, id);
  c_cols[0].is_pk = 1;

  c_cols[1].name = "parent_id";
  c_cols[1].type = C_ORM_TYPE_INT32;
  c_cols[1].offset = offsetof(struct ChildObj, parent_id);

  c_cols[2].name = "fk64";
  c_cols[2].type = C_ORM_TYPE_INT64;
  c_cols[2].offset = offsetof(struct ChildObj, fk64);
  c_cols[2].is_nullable = 1;

  c_cols[3].name = "f_val";
  c_cols[3].type = C_ORM_TYPE_FLOAT;
  c_cols[3].offset = offsetof(struct ChildObj, f_val);

  c_cols[4].name = "d_val";
  c_cols[4].type = C_ORM_TYPE_DOUBLE;
  c_cols[4].offset = offsetof(struct ChildObj, d_val);

  memset(&c_meta, 0, sizeof(c_meta));
  c_meta.name = "children";
  c_meta.columns = c_cols;
  c_meta.num_columns = 5;
  c_meta.struct_size = sizeof(struct ChildObj);
  c_meta.query_insert = "INSERT INTO children (id, parent_id) VALUES (?, ?)";
  c_meta.query_update = "UPDATE children SET parent_id = ? WHERE id = ?";

  memset(rels, 0, sizeof(rels));
  /* 0: ONE_TO_MANY */
  rels[0].field_name = "children";
  rels[0].type = C_ORM_RELATION_ONE_TO_MANY;
  rels[0].local_key = "id";
  rels[0].foreign_key = "parent_id";
  rels[0].target_meta = &c_meta;

  /* 1: MANY_TO_MANY */
  rels[1].field_name = "m2m_children";
  rels[1].type = C_ORM_RELATION_MANY_TO_MANY;
  rels[1].local_key = "id";
  rels[1].foreign_key = "id";
  rels[1].join_table = "parent_children";
  rels[1].join_local_key = "parent_id";
  rels[1].join_foreign_key = "child_id";
  rels[1].target_meta = &c_meta;

  memset(&p_meta, 0, sizeof(p_meta));
  p_meta.name = "parents";
  p_meta.columns = p_cols;
  p_meta.num_columns = 1;
  p_meta.relations = rels;
  p_meta.num_relations = 2;
  p_meta.struct_size = sizeof(struct ParentObj);

  /* 1. Test c_orm_attach ONE_TO_MANY */
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_attach(&g_db, &p_meta, &parent, "children", &child));
  ASSERT_EQ(1, child.parent_id);

  /* 2. Test c_orm_attach with on_attach returning error and OK */
  rels[0].on_attach = mock_attach_fail;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_attach(&g_db, &p_meta, &parent, "children", &child));
  rels[0].on_attach = mock_attach_ok;
  ASSERT_EQ(C_ORM_OK,
            c_orm_attach(&g_db, &p_meta, &parent, "children", &child));
  rels[0].on_attach = NULL;

  /* 3. Test c_orm_attach MANY_TO_MANY */
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_attach(&g_db, &p_meta, &parent, "m2m_children", &child));

  /* 4. Test c_orm_detach ONE_TO_MANY (sets parent_id to 0) */
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_detach(&g_db, &p_meta, &parent, "children", &child));
  ASSERT_EQ(0, child.parent_id);

  /* 5. Test c_orm_detach with on_detach */
  rels[0].on_detach = mock_detach_fail;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_detach(&g_db, &p_meta, &parent, "children", &child));
  rels[0].on_detach = NULL;

  /* 6. Test c_orm_detach MANY_TO_MANY */
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_detach(&g_db, &p_meta, &parent, "m2m_children", &child));

  /* 7. Test c_orm_sync ONE_TO_MANY */
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_sync(&g_db, &p_meta, &parent, "children", children, 2));

  /* 8. Test c_orm_sync MANY_TO_MANY */
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_sync(&g_db, &p_meta, &parent, "m2m_children", children, 2));

  /* 9. Test set_null_field with all types: fk64 (nullable), f_val, d_val */
  rels[0].foreign_key = "fk64";
  child.fk64 = (int64_t *)malloc(sizeof(int64_t));
  *child.fk64 = 99;
  ASSERT_EQ(C_ORM_OK,
            c_orm_detach(&g_db, &p_meta, &parent, "children", &child));
  ASSERT_EQ(NULL, child.fk64);

  rels[0].foreign_key = "f_val";
  child.f_val = 5.0f;
  ASSERT_EQ(C_ORM_OK,
            c_orm_detach(&g_db, &p_meta, &parent, "children", &child));
  ASSERT_EQ(0.0f, child.f_val);

  rels[0].foreign_key = "d_val";
  child.d_val = 10.0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_detach(&g_db, &p_meta, &parent, "children", &child));
  ASSERT_EQ(0.0, child.d_val);

  /* 10. Test set_int_field with fk64 (INT64 nullable allocating and non-null)
   */
  rels[0].foreign_key = "fk64";
  child.fk64 = NULL;
  ASSERT_EQ(C_ORM_OK,
            c_orm_attach(&g_db, &p_meta, &parent, "children", &child));
  ASSERT(child.fk64 != NULL);
  ASSERT_EQ(1, *child.fk64);
  /* attach again when already allocated */
  ASSERT_EQ(C_ORM_OK,
            c_orm_attach(&g_db, &p_meta, &parent, "children", &child));
  ASSERT_EQ(1, *child.fk64);
  free(child.fk64);
  child.fk64 = NULL;

  /* 11. Relation not found in attach/detach/sync */
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_attach(&g_db, &p_meta, &parent, "unknown_rel", &child));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_detach(&g_db, &p_meta, &parent, "unknown_rel", &child));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_sync(&g_db, &p_meta, &parent, "unknown_rel", children, 2));

  PASS();
}

static c_orm_error_t mock_encrypt_ok(const void *in_data, size_t in_size,
                                     void *context, void **out_data,
                                     size_t *out_size) {
  (void)context;
  *out_data = malloc(in_size);
  if (!*out_data)
    return C_ORM_ERROR_MEMORY;
  memcpy(*out_data, in_data, in_size);
  *out_size = in_size;
  return C_ORM_OK;
}

static c_orm_error_t mock_encrypt_fail(const void *in_data, size_t in_size,
                                       void *context, void **out_data,
                                       size_t *out_size) {
  (void)in_data;
  (void)in_size;
  (void)context;
  *out_data = NULL;
  *out_size = 0;
  return C_ORM_ERROR_UNKNOWN;
}

struct BindRowTestObj {
  char *id;
  char *ts;
  char *secret_str;
  c_orm_blob_t secret_blob;
  c_orm_blob_t null_blob;
  int unk_field;
};

TEST test_bind_row_extended_coverage(void) {
  struct BindRowTestObj obj;
  c_orm_column_meta_t cols[6];
  c_orm_table_meta_t meta;

  memset(&obj, 0, sizeof(obj));
  memset(cols, 0, sizeof(cols));
  memset(&meta, 0, sizeof(meta));

  /* 0: UUID PK */
  cols[0].name = "id";
  cols[0].type = C_ORM_TYPE_STRING;
  cols[0].offset = offsetof(struct BindRowTestObj, id);
  cols[0].is_pk = 1;

  /* 1: Timestamp */
  cols[1].name = "ts";
  cols[1].type = C_ORM_TYPE_TIMESTAMP;
  cols[1].offset = offsetof(struct BindRowTestObj, ts);

  /* 2: Secure string */
  cols[2].name = "secret_str";
  cols[2].type = C_ORM_TYPE_STRING;
  cols[2].offset = offsetof(struct BindRowTestObj, secret_str);
  cols[2].is_secure = 1;

  /* 3: Secure blob */
  cols[3].name = "secret_blob";
  cols[3].type = C_ORM_TYPE_BLOB;
  cols[3].offset = offsetof(struct BindRowTestObj, secret_blob);
  cols[3].is_secure = 1;

  /* 4: Null blob */
  cols[4].name = "null_blob";
  cols[4].type = C_ORM_TYPE_BLOB;
  cols[4].offset = offsetof(struct BindRowTestObj, null_blob);

  meta.name = "bind_test";
  meta.columns = cols;
  meta.num_columns = 5;
  meta.struct_size = sizeof(obj);
  meta.query_insert = "INSERT INTO bind_test VALUES (?, ?, ?, ?, ?)";
  meta.query_update = "UPDATE bind_test SET id=? WHERE id=?";

  obj.id = "";
  obj.ts = "2026-09-10 12:00:00";
  obj.secret_str = "sensitive";
  obj.secret_blob.data = "blob_data";
  obj.secret_blob.size = 9;
  obj.null_blob.data = NULL;
  obj.null_blob.size = 0;

  /* 1. Normal insert with UUID generation, timezone offset, and encrypt hook */
  g_db.timezone.offset_minutes = 60;
  g_db.encrypt_hook = mock_encrypt_ok;
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_insert(&g_db, &meta, &obj));
  ASSERT(obj.id != NULL);
  ASSERT(strlen(obj.id) > 0);
  free(obj.id);
  obj.id = "static_id";

  /* 2. Encrypt hook failure on string */
  g_db.encrypt_hook = mock_encrypt_fail;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&g_db, &meta, &obj));

  /* 3. Encrypt hook failure on blob */
  cols[2].is_secure = 0;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&g_db, &meta, &obj));

  /* 4. Type mismatch default */
  cols[2].is_secure = 1;
  g_db.encrypt_hook = NULL;
  g_db.timezone.offset_minutes = 0;
  cols[5].name = "unk";
  cols[5].type = (c_orm_type_t)99;
  cols[5].offset = offsetof(struct BindRowTestObj, unk_field);
  meta.num_columns = 6;
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, c_orm_insert(&g_db, &meta, &obj));

  PASS();
}

struct NestedChild {
  int32_t id;
  int32_t parent_id;
  char name[32];
};

struct NestedParent {
  int32_t id;
  int32_t belongs_to_id;
  struct NestedChild *belongs_to_child;
  struct NestedChild *o2o_child;
  struct Generic_Array o2m_children;
  char name[32];
};

TEST test_crud_relations_and_hooks_coverage(void) {
  struct NestedParent p;
  struct NestedChild bt_child;
  struct NestedChild o2o_child;
  struct NestedChild o2m_items[2];
  c_orm_column_meta_t p_cols[3];
  c_orm_column_meta_t c_cols[3];
  c_orm_relation_meta_t rels[4];
  c_orm_table_meta_t p_meta;
  c_orm_table_meta_t c_meta;

  memset(&p, 0, sizeof(p));
  memset(&bt_child, 0, sizeof(bt_child));
  memset(&o2o_child, 0, sizeof(o2o_child));
  memset(o2m_items, 0, sizeof(o2m_items));

  p.id = 1;
  p.belongs_to_id = 10;
  p.belongs_to_child = &bt_child;
  p.o2o_child = &o2o_child;
  p.o2m_children.data = o2m_items;
  p.o2m_children.length = 2;
  p.o2m_children.capacity = 2;

  bt_child.id = 10;
  o2o_child.id = 20;
  o2m_items[0].id = 30;
  o2m_items[1].id = 31;

  memset(c_cols, 0, sizeof(c_cols));
  c_cols[0].name = "id";
  c_cols[0].type = C_ORM_TYPE_INT32;
  c_cols[0].offset = offsetof(struct NestedChild, id);
  c_cols[0].is_pk = 1;

  c_cols[1].name = "parent_id";
  c_cols[1].type = C_ORM_TYPE_INT32;
  c_cols[1].offset = offsetof(struct NestedChild, parent_id);

  c_cols[2].name = "name";
  c_cols[2].type = C_ORM_TYPE_STRING;
  c_cols[2].offset = offsetof(struct NestedChild, name);

  memset(&c_meta, 0, sizeof(c_meta));
  c_meta.name = "nested_children";
  c_meta.columns = c_cols;
  c_meta.num_columns = 3;
  c_meta.struct_size = sizeof(struct NestedChild);
  c_meta.query_insert = "INSERT INTO nested_children VALUES (?, ?, ?)";
  c_meta.query_update = "UPDATE nested_children SET parent_id=? WHERE id=?";
  c_meta.query_delete_by_pk = "DELETE FROM nested_children WHERE id=?";
  c_meta.query_select_by_pk = "SELECT * FROM nested_children WHERE id=?";

  memset(p_cols, 0, sizeof(p_cols));
  p_cols[0].name = "id";
  p_cols[0].type = C_ORM_TYPE_INT32;
  p_cols[0].offset = offsetof(struct NestedParent, id);
  p_cols[0].is_pk = 1;

  p_cols[1].name = "belongs_to_id";
  p_cols[1].type = C_ORM_TYPE_INT32;
  p_cols[1].offset = offsetof(struct NestedParent, belongs_to_id);
  p_cols[1].is_nullable = 0;

  p_cols[2].name = "name";
  p_cols[2].type = C_ORM_TYPE_STRING;
  p_cols[2].offset = offsetof(struct NestedParent, name);

  memset(rels, 0, sizeof(rels));
  /* 0: BELONGS_TO */
  rels[0].field_name = "belongs_to_child";
  rels[0].type = C_ORM_RELATION_BELONGS_TO;
  rels[0].local_key = "belongs_to_id";
  rels[0].foreign_key = "id";
  rels[0].data_offset = offsetof(struct NestedParent, belongs_to_child);
  rels[0].struct_offset = offsetof(struct NestedParent, belongs_to_child);
  rels[0].target_meta = &c_meta;
  rels[0].on_update = C_ORM_CASCADE_UPDATE;
  rels[0].on_delete = C_ORM_CASCADE_DELETE;

  /* 1: ONE_TO_ONE */
  rels[1].field_name = "o2o_child";
  rels[1].type = C_ORM_RELATION_ONE_TO_ONE;
  rels[1].local_key = "id";
  rels[1].foreign_key = "parent_id";
  rels[1].data_offset = offsetof(struct NestedParent, o2o_child);
  rels[1].struct_offset = offsetof(struct NestedParent, o2o_child);
  rels[1].target_meta = &c_meta;
  rels[1].on_update = C_ORM_CASCADE_UPDATE;
  rels[1].on_delete = C_ORM_CASCADE_SET_NULL;

  /* 2: ONE_TO_MANY */
  rels[2].field_name = "o2m_children";
  rels[2].type = C_ORM_RELATION_ONE_TO_MANY;
  rels[2].local_key = "id";
  rels[2].foreign_key = "parent_id";
  rels[2].data_offset = offsetof(struct NestedParent, o2m_children);
  rels[2].struct_offset = offsetof(struct NestedParent, o2m_children);
  rels[2].target_meta = &c_meta;
  rels[2].on_update = C_ORM_CASCADE_UPDATE;
  rels[2].on_delete = C_ORM_CASCADE_DELETE;

  /* 3: MANY_TO_MANY */
  rels[3].field_name = "m2m";
  rels[3].type = C_ORM_RELATION_MANY_TO_MANY;
  rels[3].local_key = "id";
  rels[3].foreign_key = "id";
  rels[3].join_table = "join_t";
  rels[3].join_local_key = "p_id";
  rels[3].join_foreign_key = "c_id";
  rels[3].target_meta = &c_meta;
  rels[3].on_delete = C_ORM_CASCADE_DELETE;

  memset(&p_meta, 0, sizeof(p_meta));
  p_meta.name = "nested_parents";
  p_meta.columns = p_cols;
  p_meta.num_columns = 3;
  p_meta.relations = rels;
  p_meta.num_relations = 4;
  p_meta.struct_size = sizeof(struct NestedParent);
  p_meta.query_insert = "INSERT INTO nested_parents VALUES (?, ?, ?)";
  p_meta.query_update = "UPDATE nested_parents SET belongs_to_id=? WHERE id=?";
  p_meta.query_delete_by_pk = "DELETE FROM nested_parents WHERE id=?";
  p_meta.query_select_by_pk = "SELECT * FROM nested_parents WHERE id=?";

  /* 1. Successful insert with all nested relations */
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_insert(&g_db, &p_meta, &p));

  /* 2. Successful update with all nested cascading updates */
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_update(&g_db, &p_meta, &p));

  /* 3. Successful delete with all cascading rules */
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_delete(&g_db, &p_meta, &p));

  /* 4. Hooks failure testing in insert */
  p_meta.hooks[C_ORM_HOOK_BEFORE_SAVE] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&g_db, &p_meta, &p));
  p_meta.hooks[C_ORM_HOOK_BEFORE_SAVE] = NULL;

  p_meta.hooks[C_ORM_HOOK_BEFORE_INSERT] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&g_db, &p_meta, &p));
  p_meta.hooks[C_ORM_HOOK_BEFORE_INSERT] = NULL;

  p_meta.hooks[C_ORM_HOOK_AFTER_INSERT] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&g_db, &p_meta, &p));
  p_meta.hooks[C_ORM_HOOK_AFTER_INSERT] = NULL;

  p_meta.hooks[C_ORM_HOOK_AFTER_SAVE] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&g_db, &p_meta, &p));
  p_meta.hooks[C_ORM_HOOK_AFTER_SAVE] = NULL;

  /* 5. Hooks failure testing in delete */
  p_meta.hooks[C_ORM_HOOK_BEFORE_DELETE] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_delete(&g_db, &p_meta, &p));
  p_meta.hooks[C_ORM_HOOK_BEFORE_DELETE] = NULL;

  p_meta.hooks[C_ORM_HOOK_AFTER_DELETE] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_delete(&g_db, &p_meta, &p));
  p_meta.hooks[C_ORM_HOOK_AFTER_DELETE] = NULL;

  /* 7. Required BelongsTo validation */
  p.belongs_to_child = NULL;
  p.belongs_to_id = 0;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_insert(&g_db, &p_meta, &p));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_update(&g_db, &p_meta, &p));

  PASS();
}

#include "test_api_collections.h"
#include "test_api_crud.h"
#include "test_api_exhaust.h"
#include "test_api_helpers.h"
#include "test_api_hydration.h"
#include "test_api_relations.h"
#include "test_api_transactions.h"

TEST test_api_helpers_full_coverage(void) {
  int32_t val32;
  double vald;
  const char *str_val;
  void *out_data;
  size_t out_size;
  int has_row;
  int is_n;
  c_orm_error_t rc;

  val32 = 0;
  vald = 0.0;
  str_val = NULL;
  out_data = NULL;
  out_size = 0;
  has_row = 0;
  is_n = 0;

  /* 1. test_api_coverage.c mock helpers */
  rc = mock_get_int32_zero(NULL, 0, &val32);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = mock_get_double_zero(NULL, 0, &vald);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = mock_prefix_get_column_count(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = mock_prefix_get_column_name(NULL, 1, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  str_val = NULL;
  rc = mock_prefix_get_column_name(NULL, 0, &str_val);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(str_val != NULL && strcmp("id", str_val) == 0);
  str_val = NULL;
  rc = mock_prefix_get_column_name(NULL, 999, &str_val);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = mock_encrypt_ok(NULL, (size_t)-1, NULL, &out_data, &out_size);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  /* 2. test_api_helpers.h mock callbacks */
  rc = mock_step_sequence(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  rc = mock_stage_step(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  rc = mock_step_parent2_and_child2(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  rc = mock_step_parent_and_child(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  rc = mock_step_fail_third(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  rc = mock_step_fail_on_second(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  rc = mock_step_fail_inside_loop(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  rc = mock_step_begin_ok_then_fail(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_stage_bind_cnt = 2;
  rc = mock_bind_fail_on_second(NULL, 1, 0);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_stage_bind_cnt = 0;

  out_data = NULL;
  out_size = 0;
  rc = mock_test_encrypt_hook_ok(NULL, (size_t)-1, NULL, &out_data, &out_size);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = mock_test_encrypt_hook_ok(NULL, 0, NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = mock_is_null_child_true(NULL, 2, &is_n);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(1, is_n);
  rc = mock_is_null_child_true(NULL, 0, NULL);
  ASSERT_EQ(C_ORM_OK, rc);

  dummy_cov_expire_callback_100(NULL, NULL, NULL, NULL);
  ASSERT_EQ(NULL, cov_always_null_malloc(10));

  /* 3. test_api_hydration.h mock callbacks */
  rc = mock_prefix_col_name_ok(NULL, 1, NULL);
  ASSERT_EQ(C_ORM_OK, rc);
  str_val = NULL;
  rc = mock_prefix_col_name_ok(NULL, 0, &str_val);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_STR_EQ("id", str_val);
  str_val = NULL;
  rc = mock_prefix_col_name_ok(NULL, 4, &str_val);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_STR_EQ("child_is_flag", str_val);

  rc = mock_scatter_mixed_step(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  rc = mock_step_parent_only(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  rc = mock_step_rel_o2m(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  rc = mock_step_rel_o2o(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  /* 4. test_api_relations.h mock callbacks */
  rc = mock_is_null_child_check(NULL, 0, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_eager_fail_bind = 1;
  rc = mock_eager_bind_int32(NULL, 0, 0);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_eager_fail_bind = 0;
  rc = mock_eager_bind_int32(NULL, 0, 0);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = mock_eager_step(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  rc = mock_sync_step(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_sync_fail_step = 1;
  has_row = 0;
  rc = mock_sync_step(NULL, &has_row);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_sync_fail_step = 0;
  has_row = 0;
  rc = mock_sync_step(NULL, &has_row);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(1, has_row);

  /* 5. test_api_transactions.h mock callbacks */
  rc = mock_step_countdown_100(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  rc = mock_step_pattern_100(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_step_pattern_idx_100 = 6;
  has_row = 1;
  rc = mock_step_pattern_100(NULL, &has_row);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(0, has_row);

  PASS();
}

SUITE(api_coverage_suite) {
  void *(*old_malloc)(size_t);
  void *(*old_realloc)(void *, size_t);
  void (*old_free)(void *);
  old_malloc = c_orm_malloc;
  old_realloc = c_orm_realloc;
  old_free = c_orm_free;

  setup_vt();

  RUN_TEST(test_api_helpers_full_coverage);

  RUN_TEST(test_hydrate_set_null_field);
  RUN_TEST(test_identity_map_coverage);
  RUN_TEST(test_meta_free_helper);
  RUN_TEST(test_batch_coverage);
  RUN_TEST(test_shard_and_misc_coverage);
  RUN_TEST(test_relations_extended_coverage);
  RUN_TEST(test_escape_string_coverage);
  RUN_TEST(test_validation_coverage);
  RUN_TEST(test_find_for_update_coverage);
  RUN_TEST(test_free_relations_coverage);
  RUN_TEST(test_relation_meta_builder_coverage);
  RUN_TEST(test_prefix_column_hydration_coverage);
  RUN_TEST(test_point_polygon_secure_coverage);
  RUN_TEST(test_hydrate_and_free_columns_coverage);
  RUN_TEST(test_api_null_and_error_params);
  RUN_TEST(test_api_deep_branches);
  RUN_TEST(test_attach_detach_sync_coverage);
  RUN_TEST(test_bind_row_extended_coverage);
  RUN_TEST(test_crud_relations_and_hooks_coverage);
  RUN_TEST(test_api_find_all_and_find_with_relations_deep);
  RUN_TEST(test_api_crud_string_and_int64_pks);
  RUN_TEST(test_api_field_helpers_and_misc);
  RUN_TEST(test_api_batch_and_iterator_branches);
  RUN_TEST(test_api_validation_and_relations_misc);
  RUN_TEST(test_api_belongs_to_validation_branches);
  RUN_TEST(test_api_find_all_generic_realloc);
  RUN_TEST(test_api_scatter_gather_realloc_and_err);
  RUN_TEST(test_api_load_relation_branches);
  RUN_TEST(test_api_attach_detach_sync_error_branches);
  RUN_TEST(test_api_hydrate_and_bind_deep);
  RUN_TEST(test_api_field_helpers_and_introspection);
  RUN_TEST(test_api_batch_crud_deep_errors);
  RUN_TEST(test_api_crud_relations_deep_errors);
  RUN_TEST(test_api_relation_loading_and_sync_deep);
  RUN_TEST(test_api_identity_map_and_generic_deep);
  RUN_TEST(test_api_find_with_relation_int32_deep);
  RUN_TEST(test_api_load_relation_deep_errors);
  RUN_TEST(test_api_find_all_with_relation_eager_errors);
  RUN_TEST(test_api_sync_and_attach_detach_errors);
  RUN_TEST(test_api_generic_and_scatter_gather_deep);
  RUN_TEST(test_api_crud_missing_error_branches);
  RUN_TEST(test_api_find_and_eager_missing_branches);
  RUN_TEST(test_api_sync_attach_detach_all_errors);
  RUN_TEST(test_api_transactions_savepoints_and_softdelete);
  RUN_TEST(test_api_driver_edge_cases);
  RUN_TEST(test_api_transactions_and_error_injection);
  RUN_TEST(test_api_hydration_types_and_boundaries);
  RUN_TEST(test_api_finalize_cached_errors);
  RUN_TEST(test_api_string_builder_error_branches);
  RUN_TEST(test_api_batch_finalize_error_branches);
  RUN_TEST(test_api_relation_dot_missing_branches);

  c_orm_set_allocators(mock_malloc_fail, mock_realloc_fail, mock_free);
  mock_free(NULL);

  RUN_TEST(run_all_api);

  c_orm_set_allocators(old_malloc, old_realloc, old_free);
}

#if defined(__clang__) || defined(__GNUC__)
#endif
