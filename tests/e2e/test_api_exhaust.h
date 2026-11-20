/**
 * @file test_api_exhaust.h
 * @brief Unit tests covering remaining error branches in c_orm_api.c.
 */

#ifndef TEST_API_EXHAUST_H
#define TEST_API_EXHAUST_H

/* clang-format off */
#include "abstract_struct.h"
#include "c_orm_api.h"
#include "c_orm_decimal.h"
#include "c_orm_safe_crt.h"
#include "c_orm_string_builder.h"
#include <greatest.h>
/* clang-format on */

static int g_exhaust_fail_malloc = 0;
static int g_exhaust_fail_malloc_struct = 0;
static int g_exhaust_fail_realloc = 0;
static int g_exhaust_fail_realloc_size = 0;
static int g_exhaust_malloc_countdown = -1;
static int g_exhaust_realloc_countdown = -1;

/**
 * @brief Mock malloc callback simulating memory exhaustion conditions.
 * @param sz Allocation size in bytes.
 * @param out_ptr Output pointer receiving allocated memory or NULL.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on simulated exhaustion.
 */
static c_orm_error_t exhaust_mock_malloc(size_t sz, void **out_ptr) {
  if (g_exhaust_fail_malloc) {
    if (out_ptr) {
      *out_ptr = NULL;
    }
    return C_ORM_ERROR_MEMORY;
  }
  if (g_exhaust_fail_malloc_struct && sz == sizeof(struct NestedChild)) {
    if (out_ptr) {
      *out_ptr = NULL;
    }
    return C_ORM_ERROR_MEMORY;
  }
  if (g_exhaust_malloc_countdown >= 0) {
    if (g_exhaust_malloc_countdown == 0) {
      g_exhaust_malloc_countdown = -1;
      if (out_ptr) {
        *out_ptr = NULL;
      }
      return C_ORM_ERROR_MEMORY;
    }
    g_exhaust_malloc_countdown--;
  }
  *out_ptr = malloc(sz == 0 ? 1 : sz);
  return *out_ptr ? C_ORM_OK : C_ORM_ERROR_MEMORY;
}

/**
 * @brief Mock realloc callback simulating memory exhaustion conditions.
 * @param ptr Existing memory pointer.
 * @param sz New allocation size in bytes.
 * @param out_ptr Output pointer receiving reallocated memory or NULL.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on simulated exhaustion.
 */
static c_orm_error_t exhaust_mock_realloc(void *ptr, size_t sz,
                                          void **out_ptr) {
  if (g_exhaust_fail_realloc_size) {
    if (sz == 4 * sizeof(struct NestedChild)) {
      if (out_ptr) {
        *out_ptr = NULL;
      }
      return C_ORM_ERROR_MEMORY;
    }
  }
  if (g_exhaust_fail_realloc == 1 && !ptr) {
    if (out_ptr) {
      *out_ptr = NULL;
    }
    return C_ORM_ERROR_MEMORY;
  }
  if (g_exhaust_realloc_countdown >= 0) {
    if (g_exhaust_realloc_countdown == 0) {
      g_exhaust_realloc_countdown = -1;
      if (out_ptr) {
        *out_ptr = NULL;
      }
      return C_ORM_ERROR_MEMORY;
    }
    g_exhaust_realloc_countdown--;
  }
  *out_ptr = realloc(ptr, sz == 0 ? 1 : sz);
  return *out_ptr ? C_ORM_OK : C_ORM_ERROR_MEMORY;
}

/**
 * @brief Test error paths when c_orm_finalize_cached returns failure.
 * @return GREATEST test result.
 */
TEST test_api_finalize_cached_errors(void) {
  struct FullParentObj p;
  struct NestedChild c_items[2];
  c_orm_column_meta_t p_cols[3];
  c_orm_relation_meta_t rels[3];
  c_orm_table_meta_t p_meta;
  c_orm_table_meta_t c_meta;
  c_orm_column_meta_t c_cols[2];
  struct Generic_Array out_arr;
  struct CddCVariant key_vals[1];
  const char *fields[2];
  int exists_flag;
  int t;
  c_orm_error_t rc;

  /* String PK schema */
  /** @brief Test item with string primary key. */
  struct {
    /** @brief String primary key identifier. */
    char *id;
    /** @brief Name string value. */
    char *name;
  } s_item;
  c_orm_column_meta_t s_cols[2];
  c_orm_table_meta_t s_meta;

  /* Int64 PK schema */
  /** @brief Test item with 64-bit integer primary key. */
  struct {
    /** @brief 64-bit integer primary key identifier. */
    int64_t id;
    /** @brief Name string value. */
    char *name;
  } i64_item;
  c_orm_column_meta_t i64_cols[2];
  c_orm_table_meta_t i64_meta;

  c_orm_malloc_fn_t orig_m = c_orm_malloc;
  c_orm_realloc_fn_t orig_r = c_orm_realloc;
  c_orm_free_fn_t orig_f = c_orm_free;

  memset(&p, 0, sizeof(p));
  memset(c_items, 0, sizeof(c_items));
  memset(p_cols, 0, sizeof(p_cols));
  memset(rels, 0, sizeof(rels));
  memset(&p_meta, 0, sizeof(p_meta));
  memset(&c_meta, 0, sizeof(c_meta));
  memset(c_cols, 0, sizeof(c_cols));
  memset(&out_arr, 0, sizeof(out_arr));
  memset(key_vals, 0, sizeof(key_vals));
  fields[0] = "name";
  fields[1] = "age";
  exists_flag = 0;

  p.id = 1;
  p_cols[0].name = "id";
  p_cols[0].type = C_ORM_TYPE_INT32;
  p_cols[0].is_pk = 1;
  p_cols[0].offset = offsetof(struct FullParentObj, id);

  p_cols[1].name = "name";
  p_cols[1].type = C_ORM_TYPE_INT32;
  p_cols[1].offset = offsetof(struct FullParentObj, belongs_to_id);

  p_cols[2].name = "age";
  p_cols[2].type = C_ORM_TYPE_INT32;
  p_cols[2].offset = offsetof(struct FullParentObj, id);

  c_cols[0].name = "id";
  c_cols[0].type = C_ORM_TYPE_INT32;
  c_cols[0].is_pk = 1;
  c_cols[0].offset = offsetof(struct NestedChild, id);

  c_cols[1].name = "parent_id";
  c_cols[1].type = C_ORM_TYPE_INT32;
  c_cols[1].offset = offsetof(struct NestedChild, parent_id);

  c_meta.name = "c";
  c_meta.columns = c_cols;
  c_meta.num_columns = 2;
  c_meta.struct_size = sizeof(struct NestedChild);

  rels[0].field_name = "child_o2o";
  rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
  rels[0].local_key = "id";
  rels[0].foreign_key = "parent_id";
  rels[0].target_meta = &c_meta;
  rels[0].struct_offset = offsetof(struct FullParentObj, o2o_ctx);
  rels[0].data_offset = offsetof(struct FullParentObj, child_o2o);
  rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2o_ctx);

  rels[1].field_name = "children_o2m";
  rels[1].type = C_ORM_RELATION_ONE_TO_MANY;
  rels[1].local_key = "id";
  rels[1].foreign_key = "parent_id";
  rels[1].target_meta = &c_meta;
  rels[1].struct_offset = offsetof(struct FullParentObj, o2m_ctx);
  rels[1].data_offset = offsetof(struct FullParentObj, children_o2m);
  rels[1].lazy_ctx_offset = offsetof(struct FullParentObj, o2m_ctx);
  rels[1].custom_filter = "status = 1";
  rels[1].order_by = "id ASC";

  rels[2].field_name = "tags_m2m";
  rels[2].type = C_ORM_RELATION_MANY_TO_MANY;
  rels[2].local_key = "id";
  rels[2].foreign_key = "id";
  rels[2].join_table = "join_t";
  rels[2].join_local_key = "p_id";
  rels[2].join_foreign_key = "c_id";
  rels[2].target_meta = &c_meta;
  rels[2].struct_offset = offsetof(struct FullParentObj, m2m_ctx);
  rels[2].data_offset = offsetof(struct FullParentObj, tags_m2m);
  rels[2].lazy_ctx_offset = offsetof(struct FullParentObj, m2m_ctx);

  p_meta.name = "p";
  p_meta.columns = p_cols;
  p_meta.num_columns = 3;
  p_meta.relations = rels;
  p_meta.num_relations = 3;
  p_meta.struct_size = sizeof(struct FullParentObj);
  p_meta.query_select_by_pk = "SELECT id, name, age FROM p WHERE id=?";
  p_meta.query_select_all = "SELECT id, name, age FROM p";
  p_meta.query_insert = "INSERT INTO p (id, name, age) VALUES (?, ?, ?)";
  p_meta.query_update = "UPDATE p SET name=?, age=? WHERE id=?";
  p_meta.query_delete_by_pk = "DELETE FROM p WHERE id=?";
  p_meta.query_select_by_pk_for_update =
      "SELECT id, name, age FROM p WHERE id=? FOR UPDATE";

  out_arr.data = malloc(sizeof(struct FullParentObj) * 2);
  memcpy(out_arr.data, &p, sizeof(struct FullParentObj));
  memcpy((char *)out_arr.data + sizeof(struct FullParentObj), &p,
         sizeof(struct FullParentObj));
  out_arr.length = 2;
  out_arr.capacity = 2;

  key_vals[0].type = CDD_C_VARIANT_TYPE_INT;
  key_vals[0].value.i_val = 1;

  /* String PK setup */
  memset(&s_item, 0, sizeof(s_item));
  memset(s_cols, 0, sizeof(s_cols));
  memset(&s_meta, 0, sizeof(s_meta));
  s_item.id = (char *)malloc(8);
  C_ORM_STRCPY(s_item.id, 8, "s1");
  s_item.name = (char *)malloc(8);
  C_ORM_STRCPY(s_item.name, 8, "sname");
  s_cols[0].name = "id";
  s_cols[0].type = C_ORM_TYPE_STRING;
  s_cols[0].is_pk = 1;
  s_cols[0].offset = 0;
  s_cols[1].name = "name";
  s_cols[1].type = C_ORM_TYPE_STRING;
  s_cols[1].offset = sizeof(char *);
  s_meta.name = "sp";
  s_meta.columns = s_cols;
  s_meta.num_columns = 2;
  s_meta.struct_size = sizeof(s_item);
  s_meta.query_update = "UPDATE sp SET name=? WHERE id=?";
  s_meta.query_delete_by_pk = "DELETE FROM sp WHERE id=?";
  s_meta.query_select_by_pk = "SELECT id, name FROM sp WHERE id=?";
  s_meta.query_select_by_pk_for_update =
      "SELECT id, name FROM sp WHERE id=? FOR UPDATE";

  /* Int64 PK setup */
  memset(&i64_item, 0, sizeof(i64_item));
  memset(i64_cols, 0, sizeof(i64_cols));
  memset(&i64_meta, 0, sizeof(i64_meta));
  i64_item.id = 100;
  i64_item.name = "i64name";
  i64_cols[0].name = "id";
  i64_cols[0].type = C_ORM_TYPE_INT64;
  i64_cols[0].is_pk = 1;
  i64_cols[0].offset = 0;
  i64_cols[1].name = "name";
  i64_cols[1].type = C_ORM_TYPE_STRING;
  i64_cols[1].offset = sizeof(int64_t);
  i64_meta.name = "i64p";
  i64_meta.columns = i64_cols;
  i64_meta.num_columns = 2;
  i64_meta.struct_size = sizeof(i64_item);
  i64_meta.query_update = "UPDATE i64p SET name=? WHERE id=?";
  i64_meta.query_delete_by_pk = "DELETE FROM i64p WHERE id=?";
  i64_meta.query_select_by_pk = "SELECT id, name FROM i64p WHERE id=?";

  /* 1. Finalize cached fail on normal execution */
  c_orm_mock_finalize_cached_fail = 1;

  g_step_count = 0;
  rc = c_orm_find_by_composite_key(&g_db, &p_meta, 1, key_vals, &p);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_delete_by_composite_key(&g_db, &p_meta, 1, key_vals);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_find_by_id_int32(&g_db, &p_meta, 1, &p);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "child_o2o", &p);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_find_all_with_relation(&g_db, &p_meta, "child_o2o", &out_arr);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_insert(&g_db, &p_meta, &p);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_update(&g_db, &p_meta, &p);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_delete(&g_db, &p_meta, &p);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_delete_by_id_int32(&g_db, &p_meta, 1);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_execute_raw(&g_db, "SELECT 1");
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_find_by_id_string(&g_db, &p_meta, "1", &p);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_find_for_update_by_id_int32(&g_db, &p_meta, 1, &p);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_find_for_update_by_id_string(&g_db, &p_meta, "1", &p);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_delete_by_id_string(&g_db, &p_meta, "1");
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_find_one_by_string(&g_db, &p_meta, "name", "alice", &p);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_update_partial(&g_db, &p_meta, &p, fields, 2);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  /* Non-int32, non-string PK in update_partial (line 6725) */
  g_step_count = 0;
  rc = c_orm_update_partial(&g_db, &i64_meta, &i64_item, fields, 1);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_exists_int32(&g_db, &p_meta, 1, &exists_flag);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_exists_string(&g_db, &p_meta, "1", &exists_flag);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_attach(&g_db, &p_meta, &p, "tags_m2m", &c_items[0]);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_detach(&g_db, &p_meta, &p, "tags_m2m", &c_items[0]);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_sync(&g_db, &p_meta, &p, "tags_m2m", c_items, 2);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  /* 2. Finalize cached fail during error cleanup (bind fail, step fail, hydrate
   * fail) */
  for (t = 0; t <= 10; t++) {
    g_db_fail = 1;
    g_db_count = 0;
    g_db_target = t;
    g_step_count = 0;
    rc = c_orm_find_by_composite_key(&g_db, &p_meta, 1, key_vals, &p);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_delete_by_composite_key(&g_db, &p_meta, 1, key_vals);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_find_by_id_int32(&g_db, &p_meta, 1, &p);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "child_o2o", &p);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "children_o2m", &p);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "tags_m2m", &p);
    ASSERT(rc != C_ORM_OK);

    free(p.children_o2m.data);
    free(p.tags_m2m.data);
    free(p.child_o2o);
    memset(&p, 0, sizeof(p));
    p.id = 1;

    free(out_arr.data);
    out_arr.data = calloc(2, sizeof(struct FullParentObj));
    out_arr.length = 2;
    out_arr.capacity = 2;
    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_find_all_with_relation(&g_db, &p_meta, "child_o2o", &out_arr);
    ASSERT(rc != C_ORM_OK);

    free(out_arr.data);
    out_arr.data = calloc(2, sizeof(struct FullParentObj));
    out_arr.length = 2;
    out_arr.capacity = 2;
    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_find_all_with_relation(&g_db, &p_meta, "children_o2m", &out_arr);
    ASSERT(rc != C_ORM_OK);

    free(out_arr.data);
    out_arr.data = calloc(2, sizeof(struct FullParentObj));
    out_arr.length = 2;
    out_arr.capacity = 2;
    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_find_all_with_relation(&g_db, &p_meta, "tags_m2m", &out_arr);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_insert(&g_db, &p_meta, &p);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_update(&g_db, &p_meta, &p);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_update(&g_db, &s_meta, &s_item);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_update(&g_db, &i64_meta, &i64_item);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_delete(&g_db, &p_meta, &p);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_delete(&g_db, &s_meta, &s_item);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_delete(&g_db, &i64_meta, &i64_item);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_delete_by_id_int32(&g_db, &p_meta, 1);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_execute_raw(&g_db, "SELECT 1");
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_find_by_id_string(&g_db, &p_meta, "1", &p);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_find_for_update_by_id_int32(&g_db, &p_meta, 1, &p);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_find_for_update_by_id_string(&g_db, &s_meta, "1", &s_item);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_delete_by_id_string(&g_db, &p_meta, "1");
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_find_one_by_string(&g_db, &p_meta, "name", "alice", &p);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_update_partial(&g_db, &p_meta, &p, fields, 2);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_update_partial(&g_db, &s_meta, &s_item, fields, 1);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_exists_int32(&g_db, &p_meta, 1, &exists_flag);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_exists_string(&g_db, &p_meta, "1", &exists_flag);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_attach(&g_db, &p_meta, &p, "tags_m2m", &c_items[0]);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_detach(&g_db, &p_meta, &p, "tags_m2m", &c_items[0]);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_sync(&g_db, &p_meta, &p, "children_o2m", c_items, 2);
    ASSERT(rc != C_ORM_OK);

    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_sync(&g_db, &p_meta, &p, "tags_m2m", c_items, 2);
    ASSERT(rc != C_ORM_OK);
  }
  g_db_fail = 0;

  /* 3. Finalize cached fail when !has_row */
  g_step_count = 1;
  rc = c_orm_find_by_composite_key(&g_db, &p_meta, 1, key_vals, &p);
  ASSERT(rc != C_ORM_OK);
  g_step_count = 1;
  rc = c_orm_find_by_id_int32(&g_db, &p_meta, 1, &p);
  ASSERT(rc != C_ORM_OK);
  g_step_count = 1;
  rc = c_orm_find_by_id_string(&g_db, &p_meta, "1", &p);
  ASSERT(rc != C_ORM_OK);
  g_step_count = 1;
  rc = c_orm_find_for_update_by_id_int32(&g_db, &p_meta, 1, &p);
  ASSERT(rc != C_ORM_OK);
  g_step_count = 1;
  rc = c_orm_find_for_update_by_id_string(&g_db, &s_meta, "1", &s_item);
  ASSERT(rc != C_ORM_OK);
  g_step_count = 1;
  rc = c_orm_find_one_by_string(&g_db, &p_meta, "name", "alice", &p);
  ASSERT(rc != C_ORM_OK);
  g_step_count = 1;
  rc = c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "child_o2o", &p);
  ASSERT(rc != C_ORM_OK);
  g_step_count = 1;
  rc = c_orm_update_partial(&g_db, &p_meta, &p, fields, 2);
  ASSERT(rc != C_ORM_OK);
  g_step_count = 0;

  /* 4. Malloc failures with finalize cached failure (lines 1253, 1289, 1321) */
  c_orm_set_allocators(exhaust_mock_malloc, orig_r, orig_f);
  c_orm_mock_finalize_cached_fail = 1;
  for (t = 0; t <= 10; t++) {
    free(p.children_o2m.data);
    free(p.tags_m2m.data);
    free(p.child_o2o);
    memset(&p, 0, sizeof(p));
    p.id = 1;
    g_step_count = 0;
    g_exhaust_malloc_countdown = t;
    rc = c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "child_o2o", &p);
    ASSERT(rc != C_ORM_OK);

    free(p.children_o2m.data);
    free(p.tags_m2m.data);
    free(p.child_o2o);
    memset(&p, 0, sizeof(p));
    p.id = 1;
    g_step_count = 0;
    g_exhaust_malloc_countdown = t;
    rc = c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "children_o2m", &p);
    ASSERT(rc != C_ORM_OK);

    free(p.children_o2m.data);
    free(p.tags_m2m.data);
    free(p.child_o2o);
    memset(&p, 0, sizeof(p));
    p.id = 1;
    g_step_count = 0;
    g_exhaust_malloc_countdown = t;
    rc = c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "tags_m2m", &p);
    ASSERT(rc != C_ORM_OK);
  }
  free(p.children_o2m.data);
  free(p.tags_m2m.data);
  free(p.child_o2o);
  memset(&p, 0, sizeof(p));
  p.id = 1;
  c_orm_mock_finalize_cached_fail = 0;
  g_exhaust_malloc_countdown = -1;
  c_orm_set_allocators(orig_m, orig_r, orig_f);

  /* 5. Update partial step fail with finalize failure (line 6733) */
  g_db_fail = 1;
  g_db_target = 4;
  c_orm_mock_finalize_cached_fail = 1;
  g_step_count = 0;
  rc = c_orm_update_partial(&g_db, &p_meta, &p, fields, 2);
  ASSERT(rc != C_ORM_OK);
  g_db_fail = 0;
  c_orm_mock_finalize_cached_fail = 0;

  /* 6. Child hydrate fail with finalize cached failure (lines 1301, 1331, 1734,
   * 1774) */
  free(out_arr.data);
  out_arr.data = NULL;
  out_arr.length = 0;
  out_arr.capacity = 0;
  c_orm_mock_finalize_cached_fail = 1;
  g_mock_fail_col_index = 3;
  g_step_count = 0;
  rc = c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "children_o2m", &p);
  ASSERT(rc != C_ORM_OK);
  g_step_count = 0;
  rc = c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "tags_m2m", &p);
  ASSERT(rc != C_ORM_OK);
  g_step_count = 0;
  rc = c_orm_find_all_with_relation(&g_db, &p_meta, "child_o2o", &out_arr);
  ASSERT(rc != C_ORM_OK);
  g_step_count = 0;
  rc = c_orm_find_all_with_relation(&g_db, &p_meta, "children_o2m", &out_arr);
  ASSERT(rc != C_ORM_OK);
  g_step_count = 0;
  rc = c_orm_find_all_with_relation(&g_db, &p_meta, "tags_m2m", &out_arr);
  ASSERT(rc != C_ORM_OK);
  g_mock_fail_col_index = -1;
  c_orm_mock_finalize_cached_fail = 0;

  /* Cover exhaust_mock_malloc fail path (line 24) */
  g_exhaust_fail_malloc = 1;
  {
    void *m_fail = NULL;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, exhaust_mock_malloc(10, &m_fail));
    ASSERT(m_fail == NULL);
  }
  g_exhaust_fail_malloc = 0;

  /* Line 1302: find_with_relation_int32 realloc fail & finalize fail */
  memset(&p, 0, sizeof(p));
  p.id = 1;
  g_step_max = 10;
  g_exhaust_fail_realloc = 1;
  c_orm_mock_finalize_cached_countdown = 0;
  c_orm_set_allocators(orig_m, exhaust_mock_realloc, orig_f);
  g_step_count = 0;
  rc = c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "children_o2m", &p);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  c_orm_set_allocators(orig_m, orig_r, orig_f);
  g_exhaust_fail_realloc = 0;
  c_orm_mock_finalize_cached_countdown = -1;

  /* Line 1332: find_with_relation_int32 hydrate fail & finalize fail */
  memset(&p, 0, sizeof(p));
  p.id = 1;
  g_step_max = 10;
  g_mock_is_null_fail_countdown = 4;
  c_orm_mock_finalize_cached_countdown = 0;
  g_step_count = 0;
  rc = c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "children_o2m", &p);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_is_null_fail_countdown = -1;
  c_orm_mock_finalize_cached_countdown = -1;

  /* Line 1332: find_with_relation_int32 loop step fail & finalize fail */
  memset(&p, 0, sizeof(p));
  p.id = 1;
  g_step_max = 10;
  g_mock_step_fail_countdown = 1;
  c_orm_mock_finalize_cached_countdown = 0;
  g_step_count = 0;
  rc = c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "children_o2m", &p);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_step_fail_countdown = -1;
  c_orm_mock_finalize_cached_countdown = -1;

  /* Line 1652: bind_int32 fail & finalize fail */
  free(out_arr.data);
  out_arr.data = NULL;
  out_arr.length = 0;
  out_arr.capacity = 0;
  g_step_max = 2;
  g_mock_fail_bind = 1;
  c_orm_mock_finalize_cached_countdown = 1;
  g_step_count = 0;
  rc = c_orm_find_all_with_relation(&g_db, &p_meta, "child_o2o", &out_arr);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_fail_bind = 0;
  c_orm_mock_finalize_cached_countdown = -1;

  /* Line 1663: first child step fail & finalize fail */
  free(out_arr.data);
  out_arr.data = NULL;
  out_arr.length = 0;
  out_arr.capacity = 0;
  g_step_pattern[0] = 1;
  g_step_pattern[1] = 0;
  g_step_pattern_len = 2;
  g_step_pattern_idx = 0;
  g_mock_step_fail_countdown = 2;
  c_orm_mock_finalize_cached_countdown = 1;
  g_step_count = 0;
  rc = c_orm_find_all_with_relation(&g_db, &p_meta, "child_o2o", &out_arr);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_step_fail_countdown = -1;
  g_step_pattern_len = 0;
  c_orm_mock_finalize_cached_countdown = -1;

  /* Line 1721: O2O struct malloc fail & finalize fail */
  free(out_arr.data);
  out_arr.data = NULL;
  out_arr.length = 0;
  out_arr.capacity = 0;
  g_step_pattern[0] = 1;
  g_step_pattern[1] = 0;
  g_step_pattern[2] = 1;
  g_step_pattern[3] = 0;
  g_step_pattern_len = 4;
  g_step_pattern_idx = 0;
  g_exhaust_fail_malloc_struct = 1;
  c_orm_mock_finalize_cached_countdown = 1;
  c_orm_set_allocators(exhaust_mock_malloc, orig_r, orig_f);
  g_step_count = 0;
  rc = c_orm_find_all_with_relation(&g_db, &p_meta, "child_o2o", &out_arr);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  c_orm_set_allocators(orig_m, orig_r, orig_f);
  g_exhaust_fail_malloc_struct = 0;
  g_step_pattern_len = 0;
  c_orm_mock_finalize_cached_countdown = -1;

  /* Line 1734: O2O child hydrate fail & finalize fail */
  free(out_arr.data);
  out_arr.data = NULL;
  out_arr.length = 0;
  out_arr.capacity = 0;
  g_step_pattern[0] = 1;
  g_step_pattern[1] = 0;
  g_step_pattern[2] = 1;
  g_step_pattern[3] = 0;
  g_step_pattern_len = 4;
  g_step_pattern_idx = 0;
  g_mock_is_null_fail_countdown = 3;
  c_orm_mock_finalize_cached_countdown = 1;
  g_step_count = 0;
  rc = c_orm_find_all_with_relation(&g_db, &p_meta, "child_o2o", &out_arr);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_is_null_fail_countdown = -1;
  g_step_pattern_len = 0;
  c_orm_mock_finalize_cached_countdown = -1;

  /* Line 1756: O2M child realloc fail & finalize fail */
  free(out_arr.data);
  out_arr.data = NULL;
  out_arr.length = 0;
  out_arr.capacity = 0;
  g_step_pattern[0] = 1;
  g_step_pattern[1] = 0;
  g_step_pattern[2] = 1;
  g_step_pattern[3] = 0;
  g_step_pattern_len = 4;
  g_step_pattern_idx = 0;
  g_exhaust_fail_realloc_size = 1;
  c_orm_mock_finalize_cached_countdown = 1;
  c_orm_set_allocators(orig_m, exhaust_mock_realloc, orig_f);
  g_step_count = 0;
  rc = c_orm_find_all_with_relation(&g_db, &p_meta, "children_o2m", &out_arr);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  c_orm_set_allocators(orig_m, orig_r, orig_f);
  g_exhaust_fail_realloc_size = 0;
  g_step_pattern_len = 0;
  c_orm_mock_finalize_cached_countdown = -1;

  /* Line 1774: O2M child hydrate fail & finalize fail */
  free(out_arr.data);
  out_arr.data = NULL;
  out_arr.length = 0;
  out_arr.capacity = 0;
  g_step_pattern[0] = 1;
  g_step_pattern[1] = 0;
  g_step_pattern[2] = 1;
  g_step_pattern[3] = 0;
  g_step_pattern_len = 4;
  g_step_pattern_idx = 0;
  g_mock_is_null_fail_countdown = 3;
  c_orm_mock_finalize_cached_countdown = 1;
  g_step_count = 0;
  rc = c_orm_find_all_with_relation(&g_db, &p_meta, "children_o2m", &out_arr);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_is_null_fail_countdown = -1;
  g_step_pattern_len = 0;
  c_orm_mock_finalize_cached_countdown = -1;

  /* Line 1791: child 2nd step fail & finalize fail */
  free(out_arr.data);
  out_arr.data = NULL;
  out_arr.length = 0;
  out_arr.capacity = 0;
  g_step_pattern[0] = 1;
  g_step_pattern[1] = 0;
  g_step_pattern[2] = 1;
  g_step_pattern_len = 3;
  g_step_pattern_idx = 0;
  g_mock_step_fail_countdown = 3;
  c_orm_mock_finalize_cached_countdown = 1;
  g_step_count = 0;
  rc = c_orm_find_all_with_relation(&g_db, &p_meta, "children_o2m", &out_arr);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_step_fail_countdown = -1;
  g_step_pattern_len = 0;
  c_orm_mock_finalize_cached_countdown = -1;

  /* Line 1798-1799: child query normal finish & finalize fail */
  free(out_arr.data);
  out_arr.data = NULL;
  out_arr.length = 0;
  out_arr.capacity = 0;
  g_step_max = 2;
  c_orm_mock_finalize_cached_countdown = 1;
  g_step_count = 0;
  rc = c_orm_find_all_with_relation(&g_db, &p_meta, "child_o2o", &out_arr);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  c_orm_mock_finalize_cached_countdown = -1;

  for (t = 1; t <= 8; t++) {
    g_db_fail = 1;
    g_db_count = 0;
    g_db_target = t;
    g_step_count = 0;
    c_orm_mock_finalize_cached_countdown = 1;
    rc = c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "children_o2m", &p);
    ASSERT(rc != C_ORM_OK);
  }
  g_db_fail = 0;
  c_orm_mock_finalize_cached_countdown = -1;
  g_step_max = 1;

  c_orm_mock_finalize_cached_fail = 0;
  free(s_item.id);
  free(s_item.name);
  free(out_arr.data);

  PASS();
}

/**
 * @brief Test string builder append and get error branches in API functions.
 * @return GREATEST test result.
 */
TEST test_api_string_builder_error_branches(void) {
  struct ExtendedParent p;
  struct NestedChild c_items[2];
  c_orm_column_meta_t p_cols[3];
  c_orm_relation_meta_t rels[3];
  c_orm_table_meta_t p_meta;
  c_orm_table_meta_t c_meta;
  c_orm_column_meta_t c_cols[2];
  struct Generic_Array out_arr;
  const char *fields[2];
  void *gen_data;
  size_t gen_count;
  int cd;
  c_orm_error_t rc;

  memset(&p, 0, sizeof(p));
  memset(c_items, 0, sizeof(c_items));
  memset(p_cols, 0, sizeof(p_cols));
  memset(rels, 0, sizeof(rels));
  memset(&p_meta, 0, sizeof(p_meta));
  memset(&c_meta, 0, sizeof(c_meta));
  memset(c_cols, 0, sizeof(c_cols));
  memset(&out_arr, 0, sizeof(out_arr));
  fields[0] = "name";
  fields[1] = "age";
  gen_data = NULL;
  gen_count = 0;

  p.id = 1;
  p_cols[0].name = "id";
  p_cols[0].type = C_ORM_TYPE_INT32;
  p_cols[0].is_pk = 1;
  p_cols[0].offset = offsetof(struct ExtendedParent, id);

  p_cols[1].name = "name";
  p_cols[1].type = C_ORM_TYPE_INT32;
  p_cols[1].offset = offsetof(struct ExtendedParent, belongs_to_id);

  p_cols[2].name = "age";
  p_cols[2].type = C_ORM_TYPE_INT32;
  p_cols[2].offset = offsetof(struct ExtendedParent, id);

  c_cols[0].name = "id";
  c_cols[0].type = C_ORM_TYPE_INT32;
  c_cols[0].is_pk = 1;
  c_cols[0].offset = offsetof(struct NestedChild, id);

  c_cols[1].name = "parent_id";
  c_cols[1].type = C_ORM_TYPE_INT32;
  c_cols[1].offset = offsetof(struct NestedChild, parent_id);

  c_meta.name = "c";
  c_meta.columns = c_cols;
  c_meta.num_columns = 2;
  c_meta.struct_size = sizeof(struct NestedChild);

  rels[0].field_name = "child_o2o";
  rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
  rels[0].local_key = "id";
  rels[0].foreign_key = "parent_id";
  rels[0].target_meta = &c_meta;
  rels[0].struct_offset = offsetof(struct ExtendedParent, child_o2o);
  rels[0].data_offset = offsetof(struct ExtendedParent, child_o2o);

  rels[1].field_name = "children_o2m";
  rels[1].type = C_ORM_RELATION_ONE_TO_MANY;
  rels[1].local_key = "id";
  rels[1].foreign_key = "parent_id";
  rels[1].target_meta = &c_meta;
  rels[1].struct_offset = offsetof(struct ExtendedParent, children_o2m);
  rels[1].data_offset = offsetof(struct ExtendedParent, children_o2m);
  rels[1].custom_filter = "status = 1";
  rels[1].order_by = "id ASC";

  rels[2].field_name = "tags_m2m";
  rels[2].type = C_ORM_RELATION_MANY_TO_MANY;
  rels[2].local_key = "id";
  rels[2].foreign_key = "id";
  rels[2].join_table = "join_t";
  rels[2].join_local_key = "p_id";
  rels[2].join_foreign_key = "c_id";
  rels[2].target_meta = &c_meta;
  rels[2].struct_offset = offsetof(struct ExtendedParent, tags_m2m);
  rels[2].data_offset = offsetof(struct ExtendedParent, tags_m2m);

  p_meta.name = "p";
  p_meta.columns = p_cols;
  p_meta.num_columns = 3;
  p_meta.relations = rels;
  p_meta.num_relations = 3;
  p_meta.struct_size = sizeof(struct ExtendedParent);
  p_meta.query_select_all = "SELECT 1 FROM p";
  p_meta.query_insert = "INSERT INTO p (id, name, age) VALUES (?, ?, ?)";

  out_arr.data = malloc(sizeof(struct ExtendedParent) * 2);
  memcpy(out_arr.data, &p, sizeof(struct ExtendedParent));
  memcpy((char *)out_arr.data + sizeof(struct ExtendedParent), &p,
         sizeof(struct ExtendedParent));
  out_arr.length = 2;
  out_arr.capacity = 2;

  /* 1. Sweep c_orm_find_with_relation_int32 across relations */
  for (cd = 0; cd < 35; cd++) {
    g_step_count = 0;
    c_orm_mock_string_builder_append_countdown = cd;
    rc = c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "child_o2o", &p);
    if (rc == C_ORM_OK) {
      free(p.child_o2o);
      p.child_o2o = NULL;
    }
    g_step_count = 0;
    c_orm_mock_string_builder_append_countdown = cd;
    rc = c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "children_o2m", &p);
    if (rc == C_ORM_OK) {
      free(p.children_o2m.data);
      p.children_o2m.data = NULL;
    }
    g_step_count = 0;
    c_orm_mock_string_builder_append_countdown = cd;
    rc = c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "tags_m2m", &p);
    if (rc == C_ORM_OK) {
      free(p.tags_m2m.data);
      p.tags_m2m.data = NULL;
    }
  }
  c_orm_mock_string_builder_append_countdown = -1;

  g_step_count = 0;
  c_orm_mock_string_builder_get_fail = 1;
  rc = c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "child_o2o", &p);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  c_orm_mock_string_builder_get_fail = 0;

  /* 2. Sweep c_orm_find_all_with_relation for M2M and O2M (length=2 for comma)
   */
  free(out_arr.data);
  out_arr.data = NULL;
  for (cd = 0; cd < 40; cd++) {
    memset(&out_arr, 0, sizeof(out_arr));
    g_step_count = 0;
    c_orm_mock_string_builder_append_countdown = cd;
    rc = c_orm_find_all_with_relation(&g_db, &p_meta, "tags_m2m", &out_arr);
    if (rc == C_ORM_OK) {
      struct ExtendedParent *ep = (struct ExtendedParent *)out_arr.data;
      if (ep) {
        free(ep[0].tags_m2m.data);
      }
      free(out_arr.data);
      out_arr.data = NULL;
      break;
    }
    free(out_arr.data);
    out_arr.data = NULL;
  }
  c_orm_mock_string_builder_append_countdown = -1;

  g_step_max = 2;
  for (cd = 0; cd < 30; cd++) {
    memset(&out_arr, 0, sizeof(out_arr));
    g_step_count = 0;
    c_orm_mock_string_builder_append_countdown = cd;
    rc = c_orm_find_all_with_relation(&g_db, &p_meta, "children_o2m", &out_arr);
    if (rc == C_ORM_OK) {
      struct ExtendedParent *ep = (struct ExtendedParent *)out_arr.data;
      if (ep) {
        free(ep[0].children_o2m.data);
        if (out_arr.length > 1) {
          free(ep[1].children_o2m.data);
        }
      }
      free(out_arr.data);
      out_arr.data = NULL;
      break;
    }
    free(out_arr.data);
    out_arr.data = NULL;
  }
  c_orm_mock_string_builder_append_countdown = -1;
  g_step_max = 1;

  /* Targeted comma append failure (lines 1586-1587) */
  out_arr.data = malloc(sizeof(struct ExtendedParent) * 2);
  out_arr.length = 2;
  g_step_count = 0;
  c_orm_mock_string_builder_append_countdown = 6;
  rc = c_orm_find_all_with_relation(&g_db, &p_meta, "children_o2m", &out_arr);
  ASSERT(rc != C_ORM_OK);
  c_orm_mock_string_builder_append_countdown = -1;

  free(out_arr.data);
  out_arr.data = malloc(sizeof(struct ExtendedParent) * 2);
  out_arr.length = 2;
  g_step_count = 0;
  c_orm_mock_string_builder_get_fail = 1;
  rc = c_orm_find_all_with_relation(&g_db, &p_meta, "tags_m2m", &out_arr);
  ASSERT(rc != C_ORM_OK);
  c_orm_mock_string_builder_get_fail = 0;

  /* 3. Sweep c_orm_insert_batch_ext */
  for (cd = 0; cd < 40; cd++) {
    struct ExtendedParent batch[2];
    memset(batch, 0, sizeof(batch));
    batch[0].id = 1;
    batch[1].id = 2;
    c_orm_mock_string_builder_append_countdown = cd;
    rc = c_orm_insert_batch_ext(&g_db, &p_meta, batch, 2, 2,
                                C_ORM_ON_CONFLICT_FAIL, NULL, NULL);
    c_orm_mock_string_builder_append_countdown = cd;
    rc = c_orm_insert_batch_ext(&g_db, &p_meta, batch, 2, 2,
                                C_ORM_ON_CONFLICT_DO_NOTHING, NULL, NULL);
    if (rc == C_ORM_OK) {
      break;
    }
  }
  c_orm_mock_string_builder_append_countdown = -1;

  {
    struct ExtendedParent batch[2];
    memset(batch, 0, sizeof(batch));
    batch[0].id = 1;
    batch[1].id = 2;
    c_orm_mock_string_builder_get_fail = 1;
    rc = c_orm_insert_batch_ext(&g_db, &p_meta, batch, 2, 2,
                                C_ORM_ON_CONFLICT_FAIL, NULL, NULL);
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
    c_orm_mock_string_builder_get_fail = 0;
  }

  /* 4. Sweep c_orm_delete_batch */
  for (cd = 0; cd < 30; cd++) {
    struct ExtendedParent batch[2];
    memset(batch, 0, sizeof(batch));
    batch[0].id = 1;
    batch[1].id = 2;
    c_orm_mock_string_builder_append_countdown = cd;
    rc = c_orm_delete_batch(&g_db, &p_meta, batch, 2, 2);
    if (rc == C_ORM_OK) {
      break;
    }
  }
  c_orm_mock_string_builder_append_countdown = -1;

  {
    struct ExtendedParent batch[2];
    memset(batch, 0, sizeof(batch));
    batch[0].id = 1;
    batch[1].id = 2;
    c_orm_mock_string_builder_get_fail = 1;
    rc = c_orm_delete_batch(&g_db, &p_meta, batch, 2, 2);
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
    c_orm_mock_string_builder_get_fail = 0;
  }

  /* 5. Sweep c_orm_update_batch (3 columns for comma) */
  for (cd = 0; cd < 35; cd++) {
    struct ExtendedParent batch[2];
    memset(batch, 0, sizeof(batch));
    batch[0].id = 1;
    batch[1].id = 2;
    c_orm_mock_string_builder_append_countdown = cd;
    rc = c_orm_update_batch(&g_db, &p_meta, batch, 2, 2);
    if (rc == C_ORM_OK) {
      break;
    }
  }
  c_orm_mock_string_builder_append_countdown = -1;

  {
    struct ExtendedParent batch[2];
    memset(batch, 0, sizeof(batch));
    batch[0].id = 1;
    batch[1].id = 2;
    c_orm_mock_string_builder_get_fail = 1;
    rc = c_orm_update_batch(&g_db, &p_meta, batch, 2, 2);
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
    c_orm_mock_string_builder_get_fail = 0;
  }

  /* 6. Sweep c_orm_update_partial (2 fields for comma) */
  for (cd = 0; cd < 20; cd++) {
    c_orm_mock_string_builder_append_countdown = cd;
    rc = c_orm_update_partial(&g_db, &p_meta, &p, fields, 2);
    if (rc == C_ORM_OK) {
      break;
    }
  }
  c_orm_mock_string_builder_append_countdown = -1;

  c_orm_mock_string_builder_get_fail = 1;
  rc = c_orm_update_partial(&g_db, &p_meta, &p, fields, 2);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  c_orm_mock_string_builder_get_fail = 0;

  /* 7. Sweep generic getters and insert */
  for (cd = 0; cd < 15; cd++) {
    c_orm_mock_string_builder_append_countdown = cd;
    rc = c_orm_insert_generic(&g_db, &p_meta, &p);
    c_orm_mock_string_builder_append_countdown = cd;
    rc = c_orm_get_generic(&g_db, &p_meta, 1, &p);
    c_orm_mock_string_builder_append_countdown = cd;
    gen_data = NULL;
    gen_count = 0;
    rc = c_orm_find_all_generic(&g_db, &p_meta, &gen_data, &gen_count);
    if (rc == C_ORM_OK && gen_data) {
      free(gen_data);
      gen_data = NULL;
    }
    c_orm_mock_string_builder_append_countdown = cd;
    rc = c_orm_get_generic_string(&g_db, &p_meta, "1", &p);
  }
  c_orm_mock_string_builder_append_countdown = -1;

  c_orm_mock_string_builder_get_fail = 1;
  rc = c_orm_insert_generic(&g_db, &p_meta, &p);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = c_orm_get_generic(&g_db, &p_meta, 1, &p);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = c_orm_find_all_generic(&g_db, &p_meta, &gen_data, &gen_count);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = c_orm_get_generic_string(&g_db, &p_meta, "1", &p);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  c_orm_mock_string_builder_get_fail = 0;

  /* 8. c_orm_find_one_by_string builder fail */
  for (cd = 0; cd < 15; cd++) {
    c_orm_mock_string_builder_append_countdown = cd;
    rc = c_orm_find_one_by_string(&g_db, &p_meta, "name", "alice", &p);
    ASSERT(rc != C_ORM_OK);
  }
  c_orm_mock_string_builder_append_countdown = -1;

  c_orm_mock_string_builder_get_fail = 1;
  rc = c_orm_find_one_by_string(&g_db, &p_meta, "name", "alice", &p);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  c_orm_mock_string_builder_get_fail = 0;

  free(out_arr.data);

  PASS();
}

/**
 * @brief Test direct finalize failures in batch operations and generic
 * functions.
 * @return GREATEST test result.
 */
TEST test_api_batch_finalize_error_branches(void) {
  struct ExtendedParent batch[2];
  c_orm_column_meta_t p_cols[2];
  c_orm_table_meta_t p_meta;
  void *gen_data;
  size_t gen_count;
  int t;
  c_orm_error_t rc;

  c_orm_malloc_fn_t orig_m = c_orm_malloc;
  c_orm_realloc_fn_t orig_r = c_orm_realloc;
  c_orm_free_fn_t orig_f = c_orm_free;

  memset(batch, 0, sizeof(batch));
  memset(p_cols, 0, sizeof(p_cols));
  memset(&p_meta, 0, sizeof(p_meta));
  gen_data = NULL;
  gen_count = 0;

  batch[0].id = 1;
  batch[1].id = 2;

  p_cols[0].name = "id";
  p_cols[0].type = C_ORM_TYPE_INT32;
  p_cols[0].is_pk = 1;
  p_cols[0].offset = offsetof(struct ExtendedParent, id);

  p_cols[1].name = "name";
  p_cols[1].type = C_ORM_TYPE_STRING;
  p_cols[1].offset = offsetof(struct ExtendedParent, name);

  p_meta.name = "p";
  p_meta.columns = p_cols;
  p_meta.num_columns = 2;
  p_meta.struct_size = sizeof(struct ExtendedParent);
  p_meta.query_insert = "INSERT INTO p (id, name) VALUES (?, ?)";
  p_meta.query_update = "UPDATE p SET name=? WHERE id=?";
  p_meta.query_delete_by_pk = "DELETE FROM p WHERE id=?";

  /* 1. Direct batch statement finalize failure via countdown = 0 */
  g_mock_finalize_countdown = 0;
  rc = c_orm_insert_batch_ext(&g_db, &p_meta, batch, 2, 2,
                              C_ORM_ON_CONFLICT_FAIL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_finalize_countdown = -1;

  g_mock_finalize_countdown = 0;
  rc = c_orm_delete_batch(&g_db, &p_meta, batch, 2, 2);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_finalize_countdown = -1;

  g_mock_finalize_countdown = 0;
  rc = c_orm_update_batch(&g_db, &p_meta, batch, 2, 2);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_finalize_countdown = -1;

  /* Line 2764: BEFORE_SAVE hook fail & finalize fail / ok */
  p_meta.hooks[C_ORM_HOOK_BEFORE_SAVE] = mock_hook_fail;
  g_mock_finalize_countdown = 0;
  rc = c_orm_insert_batch_ext(&g_db, &p_meta, batch, 2, 2,
                              C_ORM_ON_CONFLICT_FAIL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_finalize_countdown = -1;
  rc = c_orm_insert_batch_ext(&g_db, &p_meta, batch, 2, 2,
                              C_ORM_ON_CONFLICT_FAIL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  p_meta.hooks[C_ORM_HOOK_BEFORE_SAVE] = NULL;

  /* Line 2773: BEFORE_INSERT hook fail & finalize fail / ok */
  p_meta.hooks[C_ORM_HOOK_BEFORE_INSERT] = mock_hook_fail;
  g_mock_finalize_countdown = 0;
  rc = c_orm_insert_batch_ext(&g_db, &p_meta, batch, 2, 2,
                              C_ORM_ON_CONFLICT_FAIL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_finalize_countdown = -1;
  rc = c_orm_insert_batch_ext(&g_db, &p_meta, batch, 2, 2,
                              C_ORM_ON_CONFLICT_FAIL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  p_meta.hooks[C_ORM_HOOK_BEFORE_INSERT] = NULL;

  /* Line 2782: bind_row fail & finalize fail / ok */
  g_mock_fail_bind = 1;
  g_mock_finalize_countdown = 0;
  rc = c_orm_insert_batch_ext(&g_db, &p_meta, batch, 2, 2,
                              C_ORM_ON_CONFLICT_FAIL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_finalize_countdown = -1;
  rc = c_orm_insert_batch_ext(&g_db, &p_meta, batch, 2, 2,
                              C_ORM_ON_CONFLICT_FAIL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_fail_bind = 0;

  /* Line 3613: BEFORE_DELETE hook fail & finalize fail / ok */
  p_meta.hooks[C_ORM_HOOK_BEFORE_DELETE] = mock_hook_fail;
  g_mock_finalize_countdown = 0;
  rc = c_orm_delete_batch(&g_db, &p_meta, batch, 2, 2);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_finalize_countdown = -1;
  rc = c_orm_delete_batch(&g_db, &p_meta, batch, 2, 2);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  p_meta.hooks[C_ORM_HOOK_BEFORE_DELETE] = NULL;

  /* Line 3634: delete bind fail & finalize fail / ok */
  g_mock_fail_bind = 1;
  g_mock_finalize_countdown = 0;
  rc = c_orm_delete_batch(&g_db, &p_meta, batch, 2, 2);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_finalize_countdown = -1;
  rc = c_orm_delete_batch(&g_db, &p_meta, batch, 2, 2);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_fail_bind = 0;

  /* Line 3993: update bind fail (update_err) & finalize fail / ok */
  g_mock_fail_bind = 1;
  g_mock_finalize_countdown = 0;
  rc = c_orm_update_batch(&g_db, &p_meta, batch, 2, 2);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_finalize_countdown = -1;
  rc = c_orm_update_batch(&g_db, &p_meta, batch, 2, 2);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_fail_bind = 0;

  /* 2. Direct finalize failure on generic functions */
  g_mock_finalize_fail = 1;
  g_step_count = 0;
  rc = c_orm_insert_generic(&g_db, &p_meta, &batch[0]);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_get_generic(&g_db, &p_meta, 1, &batch[0]);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_find_all_generic(&g_db, &p_meta, &gen_data, &gen_count);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  g_step_count = 0;
  rc = c_orm_get_generic_string(&g_db, &p_meta, "1", &batch[0]);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  /* 3. Error cleanup paths with direct finalize failure */
  for (t = 0; t <= 4; t++) {
    g_db_fail = 1;
    g_db_count = 0;
    g_db_target = t;
    g_step_count = 0;
    rc = c_orm_insert_generic(&g_db, &p_meta, &batch[0]);
    ASSERT(rc != C_ORM_OK);
    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_get_generic(&g_db, &p_meta, 1, &batch[0]);
    ASSERT(rc != C_ORM_OK);
    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_find_all_generic(&g_db, &p_meta, &gen_data, &gen_count);
    ASSERT(rc != C_ORM_OK);
    g_db_count = 0;
    g_step_count = 0;
    rc = c_orm_get_generic_string(&g_db, &p_meta, "1", &batch[0]);
    ASSERT(rc != C_ORM_OK);
  }
  g_db_fail = 0;

  g_step_count = 1; /* !has_row */
  rc = c_orm_get_generic(&g_db, &p_meta, 1, &batch[0]);
  ASSERT(rc != C_ORM_OK);
  g_step_count = 1;
  rc = c_orm_get_generic_string(&g_db, &p_meta, "1", &batch[0]);
  ASSERT(rc != C_ORM_OK);
  g_step_count = 0;

  /* 4. Find all generic OOM with finalize failure (line 7711) */
  g_mock_finalize_fail = 1;
  c_orm_set_allocators(exhaust_mock_malloc, orig_r, orig_f);
  for (t = 0; t <= 6; t++) {
    g_step_count = 0;
    g_exhaust_malloc_countdown = t;
    rc = c_orm_find_all_generic(&g_db, &p_meta, &gen_data, &gen_count);
    ASSERT(rc != C_ORM_OK);
  }
  c_orm_set_allocators(orig_m, orig_r, orig_f);
  g_exhaust_malloc_countdown = -1;

  /* 5. Find all generic realloc OOM with finalize failure (line 7745) */
  g_mock_finalize_fail = 1;
  g_exhaust_realloc_countdown = 1;
  g_step_count = 0;
  g_step_max = 20;
  c_orm_set_allocators(orig_m, exhaust_mock_realloc, orig_f);
  rc = c_orm_find_all_generic(&g_db, &p_meta, &gen_data, &gen_count);
  ASSERT(rc != C_ORM_OK);
  c_orm_set_allocators(orig_m, orig_r, orig_f);
  g_exhaust_realloc_countdown = -1;
  g_step_count = 0;
  g_step_max = 1;
  g_mock_finalize_fail = 0;

  g_mock_finalize_fail = 0;

  PASS();
}

/**
 * @brief Test deep nested relation dot path error branches.
 * @return GREATEST test result.
 */
TEST test_api_relation_dot_missing_branches(void) {
  struct ExtendedParent p;
  struct NestedChild c;
  c_orm_column_meta_t p_cols[2];
  c_orm_relation_meta_t rels[2];
  c_orm_table_meta_t p_meta;
  c_orm_table_meta_t c_meta;
  c_orm_column_meta_t c_cols[2];
  struct Generic_Array out_arr;
  const char *good_nested[1];
  const char *bad_paths[1];
  c_orm_error_t rc;

  memset(&p, 0, sizeof(p));
  memset(&c, 0, sizeof(c));
  memset(p_cols, 0, sizeof(p_cols));
  memset(rels, 0, sizeof(rels));
  memset(&p_meta, 0, sizeof(p_meta));
  memset(&c_meta, 0, sizeof(c_meta));
  memset(c_cols, 0, sizeof(c_cols));
  memset(&out_arr, 0, sizeof(out_arr));

  p.id = 1;
  p.child_o2o = &c;
  p_cols[0].name = "id";
  p_cols[0].type = C_ORM_TYPE_INT32;
  p_cols[0].is_pk = 1;
  p_cols[0].offset = offsetof(struct ExtendedParent, id);

  p_cols[1].name = "name";
  p_cols[1].type = C_ORM_TYPE_INT32;
  p_cols[1].offset = offsetof(struct ExtendedParent, belongs_to_id);

  c.id = 10;
  c.parent_id = 1;
  c_cols[0].name = "id";
  c_cols[0].type = C_ORM_TYPE_INT32;
  c_cols[0].is_pk = 1;
  c_cols[0].offset = offsetof(struct NestedChild, id);

  c_cols[1].name = "parent_id";
  c_cols[1].type = C_ORM_TYPE_INT32;
  c_cols[1].offset = offsetof(struct NestedChild, parent_id);

  c_meta.name = "c";
  c_meta.columns = c_cols;
  c_meta.num_columns = 2;
  c_meta.struct_size = sizeof(struct NestedChild);

  rels[0].field_name = "child_o2o";
  rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
  rels[0].local_key = "id";
  rels[0].foreign_key = "parent_id";
  rels[0].target_meta = &c_meta;
  rels[0].struct_offset = offsetof(struct ExtendedParent, child_o2o);
  rels[0].data_offset = offsetof(struct ExtendedParent, child_o2o);
  rels[0].lazy_ctx_offset = offsetof(struct ExtendedParent, child_o2o);

  rels[1].field_name = "children_o2m";
  rels[1].type = C_ORM_RELATION_ONE_TO_MANY;
  rels[1].local_key = "id";
  rels[1].foreign_key = "parent_id";
  rels[1].target_meta = &c_meta;
  rels[1].struct_offset = offsetof(struct ExtendedParent, children_o2m);
  rels[1].data_offset = offsetof(struct ExtendedParent, children_o2m);
  rels[1].lazy_ctx_offset = offsetof(struct ExtendedParent, children_o2m);

  p_meta.name = "p";
  p_meta.columns = p_cols;
  p_meta.num_columns = 2;
  p_meta.relations = rels;
  p_meta.num_relations = 2;
  p_meta.struct_size = sizeof(struct ExtendedParent);
  p_meta.query_select_by_pk = "SELECT id FROM p WHERE id=?";
  p_meta.query_select_all = "SELECT id FROM p";

  out_arr.data = malloc(sizeof(struct ExtendedParent));
  memcpy(out_arr.data, &p, sizeof(struct ExtendedParent));
  out_arr.length = 1;
  out_arr.capacity = 1;

  /* 1. Invalid first relation component */
  bad_paths[0] = "nonexistent_relation.child";
  g_step_count = 0;
  rc = c_orm_find_with_relations_int32(&g_db, &p_meta, 1, bad_paths, 1, &p);
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, rc);

  g_step_count = 0;
  rc = c_orm_find_all_with_relations(&g_db, &p_meta, bad_paths, 1, &out_arr);
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, rc);

  /* 2. Failure on 2nd find_relation_meta via countdown (lines 1987, 2092) */
  good_nested[0] = "child_o2o.sub";
  g_step_count = 0;
  c_orm_mock_find_relation_meta_countdown = 1;
  rc = c_orm_find_with_relations_int32(&g_db, &p_meta, 1, good_nested, 1, &p);
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, rc);
  c_orm_mock_find_relation_meta_countdown = -1;

  g_step_count = 0;
  c_orm_mock_find_relation_meta_countdown = 1;
  rc = c_orm_find_all_with_relations(&g_db, &p_meta, good_nested, 1, &out_arr);
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, rc);
  c_orm_mock_find_relation_meta_countdown = -1;

  /* 3. Nested relation failure on children_o2m (lines 2005, 2134) */
  {
    const char *o2m_nested[1];
    o2m_nested[0] = "children_o2m.sub";

    g_step_pattern[0] = 1;
    g_step_pattern[1] = 0;
    g_step_pattern[2] = 1;
    g_step_pattern[3] = 0;
    g_step_pattern_len = 4;
    g_step_pattern_idx = 0;
    g_step_count = 0;
    rc = c_orm_find_all_with_relations(&g_db, &p_meta, o2m_nested, 1, &out_arr);
    ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, rc);
    g_step_pattern_len = 0;

    g_step_pattern[0] = 1;
    g_step_pattern[1] = 1;
    g_step_pattern[2] = 0;
    g_step_pattern_len = 3;
    g_step_pattern_idx = 0;
    g_step_count = 0;
    rc = c_orm_find_with_relations_int32(&g_db, &p_meta, 1, o2m_nested, 1, &p);
    ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, rc);
    g_step_pattern_len = 0;

    p.children_o2m.data = NULL;
    p.children_o2m.length = 0;
    p.children_o2m.capacity = 0;
  }

  free(out_arr.data);
  out_arr.data = NULL;

  PASS();
}

/**
 * @brief Extended column test struct exercising DECIMAL, DECFLOAT, CHAR_FIXED,
 * BINARY_FIXED, VARBINARY, GRAPHIC, VARGRAPHIC, and TIME columns.
 */
struct ExhaustExtRow {
  /** @brief Primary key ID. */
  int32_t id;
  /** @brief Non-nullable DECIMAL(10, 2). */
  c_orm_decimal_t dec_val;
  /** @brief Nullable DECIMAL(8, 2) pointer. */
  c_orm_decimal_t *dec_null;
  /** @brief Non-nullable DECFLOAT. */
  c_orm_decimal_t df_val;
  /** @brief Inline CHAR(8) fixed string. */
  char char_in[9];
  /** @brief Heap CHAR_FIXED string (`fixed_length == 0`). */
  char *char_heap;
  /** @brief Inline CHAR(4) FOR BIT DATA binary array. */
  unsigned char bin_in[4];
  /** @brief Heap BINARY_FIXED (`fixed_length == 0`). */
  c_orm_blob_t bin_heap;
  /** @brief Variable binary column. */
  c_orm_blob_t varbin;
  /** @brief GRAPHIC string column. */
  char *graphic_s;
  /** @brief VARGRAPHIC string column. */
  char *vgraphic_s;
  /** @brief TIME string column. */
  char *time_s;
};

/** @brief Control state for extended-type & abstract hydration mock callbacks.
 */
static int g_ex_ext_mode = 0;
/** @brief Row counter for abstract hydration mock. */
static int g_ex_abs_row = 0;
/** @brief Max rows for abstract hydration mock. */
static int g_ex_abs_max = 2;

/**
 * @brief Mock get_decimal callback for extended hydration tests.
 * @param q Query handle.
 * @param idx Column index.
 * @param out Output decimal value.
 * @return C_ORM_OK or error code.
 */
static c_orm_error_t ex_mock_get_decimal(c_orm_query_t *q, int idx,
                                         c_orm_decimal_t *out) {
  (void)q;
  (void)idx;
  if (g_ex_ext_mode == 1) {
    return C_ORM_ERROR_SQL;
  }
  if (g_ex_ext_mode == 2) {
    return c_orm_decimal_from_str("999999999999.99", out);
  }
  return c_orm_decimal_from_str("42.50", out);
}

/**
 * @brief Mock bind_decimal callback for extended bind tests.
 * @param q Query handle.
 * @param idx 1-based parameter index.
 * @param v Decimal value pointer.
 * @return C_ORM_OK or error code.
 */
static c_orm_error_t ex_mock_bind_decimal(c_orm_query_t *q, int idx,
                                          const c_orm_decimal_t *v) {
  (void)q;
  (void)idx;
  (void)v;
  return g_ex_ext_mode == 3 ? C_ORM_ERROR_BIND : C_ORM_OK;
}

/**
 * @brief Mock get_string callback for extended & abstract hydration tests.
 * @param q Query handle.
 * @param idx Column index.
 * @param out Output string pointer.
 * @return C_ORM_OK or error code.
 */
static c_orm_error_t ex_mock_get_str(c_orm_query_t *q, int idx,
                                     const char **out) {
  (void)q;
  (void)idx;
  if (g_ex_ext_mode == 4) {
    return C_ORM_ERROR_SQL;
  }
  if (g_ex_ext_mode == 5) {
    *out = NULL;
    return C_ORM_OK;
  }
  if (g_ex_ext_mode == 6) {
    *out = "NOT_A_DECIMAL";
    return C_ORM_OK;
  }
  if (g_ex_ext_mode == 7) {
    *out = "LONG_FIXED_STRING_VALUE";
    return C_ORM_OK;
  }
  if (g_ex_ext_mode == 14) {
    *out = "2026-05-01 10:20:30";
    return C_ORM_OK;
  }
  *out = "12.34";
  return C_ORM_OK;
}

/**
 * @brief Mock get_blob callback for extended hydration tests.
 * @param q Query handle.
 * @param idx Column index.
 * @param out Output blob pointer.
 * @param sz Output blob byte length.
 * @return C_ORM_OK or error code.
 */
static c_orm_error_t ex_mock_get_blob(c_orm_query_t *q, int idx,
                                      const void **out, size_t *sz) {
  static const unsigned char raw8[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  (void)q;
  (void)idx;
  if (g_ex_ext_mode == 8) {
    return C_ORM_ERROR_SQL;
  }
  if (g_ex_ext_mode == 9) {
    *out = NULL;
    *sz = 0;
    return C_ORM_OK;
  }
  if (g_ex_ext_mode == 13) {
    *out = raw8;
    *sz = 0;
    return C_ORM_OK;
  }
  *out = raw8;
  *sz = (g_ex_ext_mode == 10) ? 8 : 2;
  return C_ORM_OK;
}

/**
 * @brief Mock TTL expiration callback returning C_ORM_OK.
 * @param db Database connection.
 * @param meta Table metadata.
 * @param obj Expired row struct.
 * @param ud User data pointer.
 * @return C_ORM_OK.
 */
static c_orm_error_t ex_ttl_expire_ok(c_orm_db_t *db,
                                      const c_orm_table_meta_t *meta, void *obj,
                                      void *ud) {
  (void)db;
  (void)meta;
  (void)obj;
  (void)ud;
  return C_ORM_OK;
}

/**
 * @brief Mock free callback returning C_ORM_ERROR_MEMORY for shard free test.
 * @param ptr Pointer to free.
 * @return C_ORM_ERROR_MEMORY.
 */
static c_orm_error_t ex_fail_free(void *ptr) {
  free(ptr);
  return C_ORM_ERROR_MEMORY;
}

/**
 * @brief Mock is_null callback for extended hydration tests.
 * @param q Query handle.
 * @param idx Column index.
 * @param out_null Output null indicator.
 * @return C_ORM_OK or error code.
 */
static c_orm_error_t ex_mock_is_null(c_orm_query_t *q, int idx, int *out_null) {
  (void)q;
  if (g_ex_ext_mode == 11) {
    return C_ORM_ERROR_SQL;
  }
  *out_null = (g_ex_ext_mode == 12 && idx >= 2 && idx != 3) ? 1 : 0;
  return C_ORM_OK;
}

/**
 * @brief Mock step callback for abstract hydration tests.
 * @param q Query handle.
 * @param out_has Output row availability flag.
 * @return C_ORM_OK or error code.
 */
static c_orm_error_t ex_abs_step(c_orm_query_t *q, int *out_has) {
  (void)q;
  if (g_ex_ext_mode == 20) {
    return C_ORM_ERROR_STEP;
  }
  g_ex_abs_row++;
  *out_has = (g_ex_abs_row <= g_ex_abs_max) ? 1 : 0;
  return C_ORM_OK;
}

/**
 * @brief Mock get_column_count callback for abstract hydration tests.
 * @param q Query handle.
 * @param out_cnt Output column count.
 * @return C_ORM_OK or error code.
 */
static c_orm_error_t ex_abs_col_cnt(c_orm_query_t *q, int *out_cnt) {
  (void)q;
  if (g_ex_ext_mode == 21) {
    return C_ORM_ERROR_SQL;
  }
  *out_cnt = 4;
  return C_ORM_OK;
}

/**
 * @brief Mock get_column_name callback for abstract hydration tests.
 * @param q Query handle.
 * @param idx 0-based column index.
 * @param out_name Output column name.
 * @return C_ORM_OK or error code.
 */
static c_orm_error_t ex_abs_col_name(c_orm_query_t *q, int idx,
                                     const char **out_name) {
  static const char *names[4] = {"c_null", "c_int", "c_dbl", "c_str"};
  (void)q;
  if (g_ex_ext_mode == 22) {
    return C_ORM_ERROR_SQL;
  }
  *out_name = (idx == 0 && g_ex_ext_mode == 23) ? NULL : names[idx % 4];
  return C_ORM_OK;
}

/**
 * @brief Mock is_null callback for abstract hydration tests.
 * @param q Query handle.
 * @param idx 0-based column index.
 * @param out_n Output null flag.
 * @return C_ORM_OK or error code.
 */
static c_orm_error_t ex_abs_is_null(c_orm_query_t *q, int idx, int *out_n) {
  (void)q;
  if (g_ex_ext_mode == 24) {
    return C_ORM_ERROR_SQL;
  }
  *out_n = (idx == 0) ? 1 : 0;
  return C_ORM_OK;
}

/**
 * @brief Mock get_int64 callback for abstract hydration tests.
 * @param q Query handle.
 * @param idx 0-based column index.
 * @param out_v Output int64 value.
 * @return C_ORM_OK for column 1, error otherwise.
 */
static c_orm_error_t ex_abs_get_i64(c_orm_query_t *q, int idx, int64_t *out_v) {
  (void)q;
  if (idx == 1 && g_ex_ext_mode != 25) {
    *out_v = 77;
    return C_ORM_OK;
  }
  return C_ORM_ERROR_TYPE_MISMATCH;
}

/**
 * @brief Mock get_double callback for abstract hydration tests.
 * @param q Query handle.
 * @param idx 0-based column index.
 * @param out_v Output double value.
 * @return C_ORM_OK for column 2, error otherwise.
 */
static c_orm_error_t ex_abs_get_dbl(c_orm_query_t *q, int idx, double *out_v) {
  (void)q;
  if (idx == 2 && g_ex_ext_mode != 25) {
    *out_v = 3.5;
    return C_ORM_OK;
  }
  return C_ORM_ERROR_TYPE_MISMATCH;
}

/**
 * @brief Mock get_string callback for abstract hydration tests.
 * @param q Query handle.
 * @param idx 0-based column index.
 * @param out_v Output string pointer.
 * @return C_ORM_OK for column 3, error otherwise.
 */
static c_orm_error_t ex_abs_get_str(c_orm_query_t *q, int idx,
                                    const char **out_v) {
  (void)q;
  if (idx == 3 && g_ex_ext_mode != 25) {
    *out_v = (g_ex_ext_mode == 26) ? NULL : "ok";
    return C_ORM_OK;
  }
  return C_ORM_ERROR_TYPE_MISMATCH;
}

/**
 * @brief Exhaustive coverage test for extended types, prefix DECIMAL hydration,
 * MySQL/SQLite/Postgres batch conflict SB countdowns, abstract query APIs,
 * Safe CRT countdowns, and `c_orm_load_relation_ext` OOM sweeps.
 * @return GREATEST test result.
 */
TEST test_api_db2_decimal_safe_crt_and_abstract_exhaustive(void) {
  struct ExhaustExtRow ext_row;
  c_orm_column_meta_t ext_cols[12];
  c_orm_table_meta_t ext_meta;
  c_orm_driver_vtable_t evt;
  c_orm_db_t edb;
  c_orm_malloc_fn_t orig_m = c_orm_malloc;
  c_orm_realloc_fn_t orig_r = c_orm_realloc;
  c_orm_free_fn_t orig_f = c_orm_free;
  int step;

  setup_vt();
  evt = g_vt;
  evt.is_null = ex_mock_is_null;
  evt.get_decimal = ex_mock_get_decimal;
  evt.bind_decimal = ex_mock_bind_decimal;
  evt.get_string = ex_mock_get_str;
  evt.get_blob = ex_mock_get_blob;
  memset(&edb, 0, sizeof(edb));
  edb.vtable = &evt;
  edb.driver_name = "sqlite";

  memset(ext_cols, 0, sizeof(ext_cols));
  ext_cols[0].name = "id";
  ext_cols[0].type = C_ORM_TYPE_INT32;
  ext_cols[0].offset = offsetof(struct ExhaustExtRow, id);
  ext_cols[0].is_pk = true;

  ext_cols[1].name = "dec_val";
  ext_cols[1].type = C_ORM_TYPE_DECIMAL;
  ext_cols[1].offset = offsetof(struct ExhaustExtRow, dec_val);
  ext_cols[1].precision = 10;
  ext_cols[1].scale = 2;

  ext_cols[2].name = "dec_null";
  ext_cols[2].type = C_ORM_TYPE_DECIMAL;
  ext_cols[2].offset = offsetof(struct ExhaustExtRow, dec_null);
  ext_cols[2].is_nullable = true;
  ext_cols[2].precision = 8;
  ext_cols[2].scale = 2;

  ext_cols[3].name = "df_val";
  ext_cols[3].type = C_ORM_TYPE_DECFLOAT;
  ext_cols[3].offset = offsetof(struct ExhaustExtRow, df_val);

  ext_cols[4].name = "char_in";
  ext_cols[4].type = C_ORM_TYPE_CHAR_FIXED;
  ext_cols[4].offset = offsetof(struct ExhaustExtRow, char_in);
  ext_cols[4].is_nullable = true;
  ext_cols[4].fixed_length = 8;

  ext_cols[5].name = "char_heap";
  ext_cols[5].type = C_ORM_TYPE_CHAR_FIXED;
  ext_cols[5].offset = offsetof(struct ExhaustExtRow, char_heap);
  ext_cols[5].is_nullable = true;
  ext_cols[5].precision = 6;

  ext_cols[6].name = "bin_in";
  ext_cols[6].type = C_ORM_TYPE_BINARY_FIXED;
  ext_cols[6].offset = offsetof(struct ExhaustExtRow, bin_in);
  ext_cols[6].is_nullable = true;
  ext_cols[6].fixed_length = 4;

  ext_cols[7].name = "bin_heap";
  ext_cols[7].type = C_ORM_TYPE_BINARY_FIXED;
  ext_cols[7].offset = offsetof(struct ExhaustExtRow, bin_heap);
  ext_cols[7].is_nullable = true;

  ext_cols[8].name = "varbin";
  ext_cols[8].type = C_ORM_TYPE_VARBINARY;
  ext_cols[8].offset = offsetof(struct ExhaustExtRow, varbin);
  ext_cols[8].is_nullable = true;

  ext_cols[9].name = "graphic_s";
  ext_cols[9].type = C_ORM_TYPE_GRAPHIC;
  ext_cols[9].offset = offsetof(struct ExhaustExtRow, graphic_s);
  ext_cols[9].is_nullable = true;

  ext_cols[10].name = "vgraphic_s";
  ext_cols[10].type = C_ORM_TYPE_VARGRAPHIC;
  ext_cols[10].offset = offsetof(struct ExhaustExtRow, vgraphic_s);
  ext_cols[10].is_nullable = true;

  ext_cols[11].name = "time_s";
  ext_cols[11].type = C_ORM_TYPE_TIME;
  ext_cols[11].offset = offsetof(struct ExhaustExtRow, time_s);
  ext_cols[11].is_nullable = true;

  memset(&ext_meta, 0, sizeof(ext_meta));
  ext_meta.name = "ext_tbl";
  ext_meta.columns = ext_cols;
  ext_meta.num_columns = 12;
  ext_meta.struct_size = sizeof(struct ExhaustExtRow);
  ext_meta.query_insert =
      "INSERT INTO ext_tbl VALUES (?,?,?,?,?,?,?,?,?,?,?,?)";
  ext_meta.query_select_by_pk = "SELECT * FROM ext_tbl WHERE id=?";

  /* 1. Hydrate non-null, null (with pre-populated heap pointers), long strings,
   * large/empty blobs, get_decimal==NULL fallback, rescale error, & OOMs */
  memset(&ext_row, 0, sizeof(ext_row));
  g_ex_ext_mode = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_hydrate_row_from(&edb, (c_orm_query_t *)1,
                                             &ext_meta, &ext_row, 0));
  /* Hydrate NULLs over populated heap pointers (lines 138..156) */
  g_ex_ext_mode = 12;
  ASSERT_EQ(C_ORM_OK, c_orm_hydrate_row_from(&edb, (c_orm_query_t *)1,
                                             &ext_meta, &ext_row, 0));
  /* Hydrate NULLs again when heap pointers are already NULL */
  ASSERT_EQ(C_ORM_OK, c_orm_hydrate_row_from(&edb, (c_orm_query_t *)1,
                                             &ext_meta, &ext_row, 0));

  g_ex_ext_mode = 7; /* strlen(fval) > fixed_length */
  ASSERT_EQ(C_ORM_OK, c_orm_hydrate_row_from(&edb, (c_orm_query_t *)1,
                                             &ext_meta, &ext_row, 0));
  /* Validate CHAR_FIXED truncation (`strlen > precision`) & valid */
  ASSERT_EQ(C_ORM_ERROR_TRUNCATION, c_orm_validate(&ext_meta, &ext_row));
  ASSERT_EQ(C_ORM_OK, c_orm_free_columns(&ext_meta, &ext_row));
  ASSERT_EQ(C_ORM_OK, c_orm_validate(&ext_meta, &ext_row));

  /* Validate DECIMAL invalid precision & rescale overflow */
  ext_row.dec_val.precision = 99;
  ASSERT_NEQ(C_ORM_OK, c_orm_validate(&ext_meta, &ext_row));
  (void)c_orm_decimal_from_str("999999999.99", &ext_row.dec_val);
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW, c_orm_validate(&ext_meta, &ext_row));
  (void)c_orm_decimal_from_str("12.34", &ext_row.dec_val);
  ASSERT_EQ(C_ORM_OK, c_orm_validate(&ext_meta, &ext_row));

  /* Blob sizes: empty (mode 9), > fixed_length (mode 10), error (mode 8) */
  g_ex_ext_mode = 9;
  ASSERT_EQ(C_ORM_OK, c_orm_hydrate_row_from(&edb, (c_orm_query_t *)1,
                                             &ext_meta, &ext_row, 0));
  ASSERT_EQ(C_ORM_OK, c_orm_free_columns(&ext_meta, &ext_row));
  g_ex_ext_mode = 10;
  ASSERT_EQ(C_ORM_OK, c_orm_hydrate_row_from(&edb, (c_orm_query_t *)1,
                                             &ext_meta, &ext_row, 0));
  /* Bind all extended types (with non-null and null nullable fields) */
  g_ex_ext_mode = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_insert(&edb, &ext_meta, &ext_row));
  /* Bind with bind_decimal == NULL (valid & invalid precision) */
  evt.bind_decimal = NULL;
  ASSERT_EQ(C_ORM_OK, c_orm_insert(&edb, &ext_meta, &ext_row));
  ASSERT_EQ(C_ORM_OK, c_orm_free_columns(&ext_meta, &ext_row));
  ASSERT_EQ(C_ORM_OK, c_orm_insert(&edb, &ext_meta, &ext_row));
  evt.bind_decimal = ex_mock_bind_decimal;
  g_ex_ext_mode = 3; /* bind_decimal error */
  ASSERT_NEQ(C_ORM_OK, c_orm_insert(&edb, &ext_meta, &ext_row));

  /* Hydrate error modes: get_decimal err (1), rescale err (2), get_str err (4),
   * get_blob err (8), get_decimal==NULL with valid/NULL/invalid/err string */
  g_ex_ext_mode = 1;
  ASSERT_NEQ(C_ORM_OK, c_orm_hydrate_row_from(&edb, (c_orm_query_t *)1,
                                              &ext_meta, &ext_row, 0));
  g_ex_ext_mode = 2;
  ASSERT_NEQ(C_ORM_OK, c_orm_hydrate_row_from(&edb, (c_orm_query_t *)1,
                                              &ext_meta, &ext_row, 0));
  g_ex_ext_mode = 4;
  ASSERT_NEQ(C_ORM_OK, c_orm_hydrate_row_from(&edb, (c_orm_query_t *)1,
                                              &ext_meta, &ext_row, 0));
  g_ex_ext_mode = 8;
  ASSERT_NEQ(C_ORM_OK, c_orm_hydrate_row_from(&edb, (c_orm_query_t *)1,
                                              &ext_meta, &ext_row, 0));

  evt.get_decimal = NULL;
  g_ex_ext_mode = 4; /* get_decimal == NULL and get_string fails (L303) */
  ASSERT_NEQ(C_ORM_OK, c_orm_hydrate_row_from(&edb, (c_orm_query_t *)1,
                                              &ext_meta, &ext_row, 0));
  g_ex_ext_mode = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_hydrate_row_from(&edb, (c_orm_query_t *)1,
                                             &ext_meta, &ext_row, 0));
  ASSERT_EQ(C_ORM_OK, c_orm_free_columns(&ext_meta, &ext_row));
  g_ex_ext_mode = 5; /* sval == NULL -> decimal_zero & CHAR_FIXED NULL */
  ASSERT_EQ(C_ORM_OK, c_orm_hydrate_row_from(&edb, (c_orm_query_t *)1,
                                             &ext_meta, &ext_row, 0));
  ASSERT_EQ(C_ORM_OK, c_orm_free_columns(&ext_meta, &ext_row));
  g_ex_ext_mode = 6; /* invalid decimal string */
  ASSERT_NEQ(C_ORM_OK, c_orm_hydrate_row_from(&edb, (c_orm_query_t *)1,
                                              &ext_meta, &ext_row, 0));
  evt.get_decimal = ex_mock_get_decimal;
  g_ex_ext_mode = 13; /* bval != NULL && bsize == 0 (L534) */
  ASSERT_EQ(C_ORM_OK, c_orm_hydrate_row_from(&edb, (c_orm_query_t *)1,
                                             &ext_meta, &ext_row, 0));
  ASSERT_EQ(C_ORM_OK, c_orm_free_columns(&ext_meta, &ext_row));
  g_ex_ext_mode = 0;

  /* bind_decimal == NULL with invalid precision = 99 & CHAR_FIXED/BINARY_FIXED
   * bind failures */
  evt.bind_decimal = NULL;
  ext_row.dec_val.precision = 99;
  ext_cols[1].precision = 0; /* bypass validate rescale so bind_row runs */
  ASSERT_NEQ(C_ORM_OK, c_orm_insert(&edb, &ext_meta, &ext_row));
  ext_cols[1].precision = 10;
  (void)c_orm_decimal_from_str("12.34", &ext_row.dec_val);
  evt.bind_decimal = ex_mock_bind_decimal;

  /* Test CHAR_FIXED inline bind failure & BINARY_FIXED inline bind failure */
  {
    c_orm_column_meta_t f_col;
    c_orm_table_meta_t f_meta;
    memset(&f_meta, 0, sizeof(f_meta));
    f_meta.name = "ft";
    f_meta.num_columns = 1;
    f_meta.struct_size = sizeof(struct ExhaustExtRow);
    f_meta.query_insert = "INSERT INTO ft VALUES (?)";
    f_col = ext_cols[4]; /* CHAR_FIXED inline */
    f_meta.columns = &f_col;
    g_db_fail = 1;
    g_db_count = 0;
    g_db_target = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_insert(&edb, &f_meta, &ext_row));
    f_col = ext_cols[6]; /* BINARY_FIXED inline */
    g_db_count = 0;
    g_db_target = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_insert(&edb, &f_meta, &ext_row));
    g_db_fail = 0;
    g_db_target = -1;
  }

  /* OOM sweep on c_orm_hydrate_row_from (hits nullable decimal malloc OOM) */
  c_orm_set_allocators(exhaust_mock_malloc, orig_r, orig_f);
  for (step = 0; step < 10; step++) {
    g_exhaust_malloc_countdown = step;
    if (c_orm_hydrate_row_from(&edb, (c_orm_query_t *)1, &ext_meta, &ext_row,
                               0) == C_ORM_OK) {
      g_exhaust_malloc_countdown = -1;
      (void)c_orm_free_columns(&ext_meta, &ext_row);
      break;
    }
    g_exhaust_malloc_countdown = -1;
  }
  c_orm_set_allocators(orig_m, orig_r, orig_f);

  /* 2. MySQL & SQLite/Postgres insert_batch_ext conflict SB countdown sweeps */
  {
    struct ExtendedParent b[2];
    c_orm_column_meta_t bcols[4];
    c_orm_table_meta_t bmeta;
    memset(b, 0, sizeof(b));
    b[0].id = 1;
    b[1].id = 2;
    memset(bcols, 0, sizeof(bcols));
    bcols[0].name = "id";
    bcols[0].type = C_ORM_TYPE_INT32;
    bcols[0].is_pk = true;
    bcols[0].offset = offsetof(struct ExtendedParent, id);
    bcols[1].name = "pk2";
    bcols[1].type = C_ORM_TYPE_INT32;
    bcols[1].is_pk = true;
    bcols[1].offset = offsetof(struct ExtendedParent, belongs_to_id);
    bcols[2].name = "c1";
    bcols[2].type = C_ORM_TYPE_INT32;
    bcols[2].offset = offsetof(struct ExtendedParent, id);
    bcols[3].name = "c2";
    bcols[3].type = C_ORM_TYPE_INT32;
    bcols[3].offset = offsetof(struct ExtendedParent, belongs_to_id);

    memset(&bmeta, 0, sizeof(bmeta));
    bmeta.name = "b_tbl";
    bmeta.columns = bcols;
    bmeta.num_columns = 4;
    bmeta.struct_size = sizeof(struct ExtendedParent);
    bmeta.query_insert = "INSERT INTO b_tbl VALUES (?,?,?,?)";

    /* MySQL DO_NOTHING, DO_UPDATE (2 non-PK & 0 non-PK) + SB countdown */
    edb.driver_name = "mysql";
    ASSERT_EQ(C_ORM_OK,
              c_orm_insert_batch_ext(&edb, &bmeta, b, 2, 2,
                                     C_ORM_ON_CONFLICT_DO_NOTHING, NULL, NULL));
    ASSERT_EQ(C_ORM_OK,
              c_orm_insert_batch_ext(&edb, &bmeta, b, 2, 2,
                                     C_ORM_ON_CONFLICT_DO_UPDATE, NULL, NULL));
    for (step = 0; step < 100; step++) {
      c_orm_mock_string_builder_append_countdown = step;
      (void)c_orm_insert_batch_ext(&edb, &bmeta, b, 2, 2,
                                   C_ORM_ON_CONFLICT_DO_NOTHING, NULL, NULL);
      c_orm_mock_string_builder_append_countdown = step;
      if (c_orm_insert_batch_ext(&edb, &bmeta, b, 2, 2,
                                 C_ORM_ON_CONFLICT_DO_UPDATE, NULL,
                                 NULL) == C_ORM_OK) {
        c_orm_mock_string_builder_append_countdown = -1;
        break;
      }
      c_orm_mock_string_builder_append_countdown = -1;
    }
    bmeta.num_columns = 2; /* 0 non-PK cols on MySQL DO_UPDATE */
    for (step = 0; step < 60; step++) {
      c_orm_mock_string_builder_append_countdown = step;
      if (c_orm_insert_batch_ext(&edb, &bmeta, b, 1, 1,
                                 C_ORM_ON_CONFLICT_DO_UPDATE, NULL,
                                 NULL) == C_ORM_OK) {
        c_orm_mock_string_builder_append_countdown = -1;
        break;
      }
      c_orm_mock_string_builder_append_countdown = -1;
    }

    /* SQLite/Postgres/NULL driver_name composite PK DO_NOTHING & DO_UPDATE */
    edb.driver_name = NULL;
    ASSERT_EQ(C_ORM_OK,
              c_orm_insert_batch_ext(&edb, &bmeta, b, 1, 1,
                                     C_ORM_ON_CONFLICT_DO_UPDATE, NULL, NULL));
    edb.driver_name = "postgres";
    for (step = 0; step < 60; step++) {
      c_orm_mock_string_builder_append_countdown = step;
      if (c_orm_insert_batch_ext(&edb, &bmeta, b, 1, 1,
                                 C_ORM_ON_CONFLICT_DO_UPDATE, NULL,
                                 NULL) == C_ORM_OK) {
        c_orm_mock_string_builder_append_countdown = -1;
        break;
      }
      c_orm_mock_string_builder_append_countdown = -1;
    }
    bmeta.num_columns = 4;
    for (step = 0; step < 100; step++) {
      c_orm_mock_string_builder_append_countdown = step;
      (void)c_orm_insert_batch_ext(&edb, &bmeta, b, 2, 2,
                                   C_ORM_ON_CONFLICT_DO_NOTHING, NULL, NULL);
      c_orm_mock_string_builder_append_countdown = step;
      if (c_orm_insert_batch_ext(&edb, &bmeta, b, 2, 2,
                                 C_ORM_ON_CONFLICT_DO_UPDATE, NULL,
                                 NULL) == C_ORM_OK) {
        c_orm_mock_string_builder_append_countdown = -1;
        break;
      }
      c_orm_mock_string_builder_append_countdown = -1;
    }
  }

  /* 3. Abstract hydration (`c_orm_hydrate_abstract_all`, `c_orm_select_raw`,
   * `c_orm_find_all_abstract`, `c_orm_abstract_free`) */
  {
    cdd_c_abstract_struct_array_t aarr;
    c_orm_driver_vtable_t avt = g_vt;
    c_orm_db_t adb;

    avt.step = ex_abs_step;
    avt.get_column_count = ex_abs_col_cnt;
    avt.get_column_name = ex_abs_col_name;
    avt.is_null = ex_abs_is_null;
    avt.get_int64 = ex_abs_get_i64;
    avt.get_double = ex_abs_get_dbl;
    avt.get_string = ex_abs_get_str;
    memset(&adb, 0, sizeof(adb));
    adb.vtable = &avt;
    adb.driver_name = "sqlite";

    ASSERT_NEQ(C_ORM_OK, c_orm_hydrate_abstract_all(NULL, NULL, NULL));
    ASSERT_EQ(C_ORM_OK, c_orm_abstract_free(NULL));

    memset(&aarr, 0, sizeof(aarr));
    g_ex_ext_mode = 23; /* col_name == NULL -> "col" fallback */
    g_ex_abs_row = 0;
    g_ex_abs_max = 18; /* > 16 triggers array realloc */
    ASSERT_EQ(C_ORM_OK, c_orm_find_all_abstract(&adb, "SELECT 1", &aarr));
    ASSERT_EQ(18U, aarr.count);
    ASSERT_EQ(C_ORM_OK, c_orm_abstract_free(&aarr));

    memset(&aarr, 0, sizeof(aarr));
    g_ex_ext_mode = 25; /* all getters fail -> NULL variant */
    g_ex_abs_row = 0;
    g_ex_abs_max = 1;
    ASSERT_EQ(C_ORM_OK, c_orm_find_all_abstract(&adb, "SELECT 1", &aarr));
    ASSERT_EQ(C_ORM_OK, c_orm_abstract_free(&aarr));

    memset(&aarr, 0, sizeof(aarr));
    g_ex_ext_mode = 26; /* get_string returns sv == NULL */
    g_ex_abs_row = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_find_all_abstract(&adb, "SELECT 1", &aarr));
    ASSERT_EQ(C_ORM_OK, c_orm_abstract_free(&aarr));

    /* Error modes 20 (step), 21 (col_cnt), 22 (col_name), 24 (is_null) */
    for (step = 20; step <= 24; step++) {
      if (step == 23)
        continue;
      memset(&aarr, 0, sizeof(aarr));
      g_ex_ext_mode = step;
      g_ex_abs_row = 0;
      ASSERT_NEQ(C_ORM_OK, c_orm_find_all_abstract(&adb, "SELECT 1", &aarr));
      (void)c_orm_abstract_free(&aarr);
    }

    /* OOM sweep on c_orm_find_all_abstract with 18 rows */
    g_ex_ext_mode = 0;
    g_ex_abs_max = 18;
    c_orm_set_allocators(exhaust_mock_malloc, exhaust_mock_realloc, orig_f);
    for (step = 0; step < 40; step++) {
      memset(&aarr, 0, sizeof(aarr));
      g_ex_abs_row = 0;
      g_exhaust_malloc_countdown = step;
      if (c_orm_find_all_abstract(&adb, "SELECT 1", &aarr) == C_ORM_OK) {
        g_exhaust_malloc_countdown = -1;
        (void)c_orm_abstract_free(&aarr);
        break;
      }
      g_exhaust_malloc_countdown = -1;
      (void)c_orm_abstract_free(&aarr);
    }
    memset(&aarr, 0, sizeof(aarr));
    g_ex_abs_row = 0;
    g_exhaust_realloc_countdown = 0;
    ASSERT_NEQ(C_ORM_OK, c_orm_find_all_abstract(&adb, "SELECT 1", &aarr));
    g_exhaust_realloc_countdown = -1;
    (void)c_orm_abstract_free(&aarr);
    c_orm_set_allocators(orig_m, orig_r, orig_f);
    g_ex_abs_max = 2;
  }

  /* 4. Safe CRT countdowns & c_orm_load_relation_ext OOM sweeps */
  {
    /** @brief Local parent struct for relation Safe CRT & OOM tests. */
    struct ExRelParent {
      /** @brief Primary key ID. */
      int32_t id;
      /** @brief Belongs-to foreign key ID. */
      int32_t belongs_to_id;
      /** @brief Parent name string. */
      char *name;
      /** @brief Shared lazy-load context. */
      c_orm_lazy_load_context_t lazy_ctx;
      /** @brief One-to-one child pointer. */
      struct NestedChild *o2o_child;
      /** @brief One-to-many children array. */
      struct Generic_Array o2m_children;
      /** @brief Many-to-many children array. */
      struct Generic_Array m2m;
    } p;
    struct NestedChild c;
    c_orm_column_meta_t p_cols[3];
    c_orm_column_meta_t c_cols[3];
    c_orm_relation_meta_t rels[3];
    c_orm_table_meta_t p_meta;
    c_orm_table_meta_t c_meta;
    struct Generic_Array pag_arr;

    memset(&p, 0, sizeof(p));
    memset(&c, 0, sizeof(c));
    memset(p_cols, 0, sizeof(p_cols));
    memset(c_cols, 0, sizeof(c_cols));
    memset(rels, 0, sizeof(rels));
    memset(&p_meta, 0, sizeof(p_meta));
    memset(&c_meta, 0, sizeof(c_meta));
    memset(&pag_arr, 0, sizeof(pag_arr));

    p.id = 1;
    p.belongs_to_id = 2;
    p.name = "parent";
    c.id = 2;
    c.parent_id = 1;

    p_cols[0].name = "id";
    p_cols[0].type = C_ORM_TYPE_INT32;
    p_cols[0].is_pk = true;
    p_cols[0].offset = 0;
    p_cols[1].name = "belongs_to_id";
    p_cols[1].type = C_ORM_TYPE_INT32;
    p_cols[1].offset = (size_t)((char *)&p.belongs_to_id - (char *)&p);
    p_cols[2].name = "name";
    p_cols[2].type = C_ORM_TYPE_STRING;
    p_cols[2].offset = (size_t)((char *)&p.name - (char *)&p);

    c_cols[0].name = "id";
    c_cols[0].type = C_ORM_TYPE_INT32;
    c_cols[0].is_pk = true;
    c_cols[0].offset = offsetof(struct NestedChild, id);
    c_cols[1].name = "parent_id";
    c_cols[1].type = C_ORM_TYPE_INT32;
    c_cols[1].offset = offsetof(struct NestedChild, parent_id);
    c_cols[2].name = "deleted_at";
    c_cols[2].type = C_ORM_TYPE_INT32;
    c_cols[2].offset = offsetof(struct NestedChild, parent_id);

    c_meta.name = "children";
    c_meta.columns = c_cols;
    c_meta.num_columns = 3;
    c_meta.struct_size = sizeof(struct NestedChild);
    c_meta.query_select_all = "SELECT id, parent_id, deleted_at FROM children";
    c_meta.query_insert = "INSERT INTO children VALUES (?, ?, ?)";
    c_meta.query_update = "UPDATE children SET parent_id=? WHERE id=?";

    rels[0].field_name = "m2m";
    rels[0].type = C_ORM_RELATION_MANY_TO_MANY;
    rels[0].local_key = "id";
    rels[0].foreign_key = "id";
    rels[0].join_table = "jt";
    rels[0].join_local_key = "pid";
    rels[0].join_foreign_key = "cid";
    rels[0].target_meta = &c_meta;
    rels[0].data_offset = (size_t)((char *)&p.m2m - (char *)&p);
    rels[0].lazy_ctx_offset = (size_t)((char *)&p.lazy_ctx - (char *)&p);
    rels[0].custom_filter = "1=1";
    rels[0].order_by = "id DESC";
    rels[0].soft_delete_aware = 1;

    rels[1].field_name = "o2m";
    rels[1].type = C_ORM_RELATION_ONE_TO_MANY;
    rels[1].local_key = "name"; /* string FK! */
    rels[1].foreign_key = "parent_id";
    rels[1].target_meta = &c_meta;
    rels[1].data_offset = (size_t)((char *)&p.o2m_children - (char *)&p);
    rels[1].lazy_ctx_offset = (size_t)((char *)&p.lazy_ctx - (char *)&p);
    rels[1].custom_filter = "1=1";
    rels[1].order_by = "id DESC";
    rels[1].soft_delete_aware = 1;

    rels[2].field_name = "o2o";
    rels[2].type = C_ORM_RELATION_ONE_TO_ONE;
    rels[2].local_key = "id";
    rels[2].foreign_key = "parent_id";
    rels[2].target_meta = &c_meta;
    rels[2].data_offset = (size_t)((char *)&p.o2o_child - (char *)&p);
    rels[2].lazy_ctx_offset = (size_t)((char *)&p.lazy_ctx - (char *)&p);
    rels[2].custom_filter = "1=1";
    rels[2].soft_delete_aware = 1;

    p_meta.name = "parents";
    p_meta.columns = p_cols;
    p_meta.num_columns = 3;
    p_meta.relations = rels;
    p_meta.num_relations = 3;
    p_meta.struct_size = sizeof(p);
    p_meta.query_select_all = "SELECT id, belongs_to_id, name FROM parents";
    p_meta.query_delete_by_pk = "DELETE FROM parents WHERE id=?";

    setup_vt();
    /* Safe CRT countdown sweeps */
    for (step = 0; step < 6; step++) {
      c_orm_mock_safe_crt_countdown = step;
      (void)c_orm_savepoint_create(&g_db, "sp");
      c_orm_mock_safe_crt_countdown = step;
      (void)c_orm_savepoint_rollback(&g_db, "sp");
      c_orm_mock_safe_crt_countdown = step;
      (void)c_orm_savepoint_release(&g_db, "sp");
      c_orm_mock_safe_crt_countdown = step;
      (void)c_orm_delete(&g_db, &p_meta, &p);
      c_orm_mock_safe_crt_countdown = step;
      (void)c_orm_find_all_paginated(&g_db, &p_meta, &pag_arr, 10, 0);
      free(pag_arr.data);
      pag_arr.data = NULL;
      pag_arr.length = 0;
      pag_arr.capacity = 0;
      c_orm_mock_safe_crt_countdown = step;
      (void)c_orm_attach(&g_db, &p_meta, &p, "m2m", &c);
      c_orm_mock_safe_crt_countdown = step;
      (void)c_orm_detach(&g_db, &p_meta, &p, "m2m", &c);
      c_orm_mock_safe_crt_countdown = step;
      (void)c_orm_sync(&g_db, &p_meta, &p, "m2m", &c, 1);
      c_orm_mock_safe_crt_countdown = step;
      (void)c_orm_sync(&g_db, &p_meta, &p, "o2m", &c, 1);
      c_orm_mock_safe_crt_countdown = step;
      (void)c_orm_sync(&g_db, &p_meta, &p, "o2o", &c, 1);
      c_orm_mock_safe_crt_countdown = step;
      (void)c_orm_load_relation_ext(&g_db, &p, &p_meta, 0, 5, 1);
      free(p.m2m.data);
      p.m2m.data = NULL;
      c_orm_mock_safe_crt_countdown = step;
      (void)c_orm_load_relation_ext(&g_db, &p, &p_meta, 1, 5, 1);
      free(p.o2m_children.data);
      p.o2m_children.data = NULL;
      c_orm_mock_safe_crt_countdown = -1;
    }

    /* Arena/malloc OOM sweep on c_orm_load_relation_ext (m2m, o2m, o2o) */
    c_orm_set_allocators(exhaust_mock_malloc, exhaust_mock_realloc, orig_f);
    for (step = 0; step < 30; step++) {
      g_step_count = 1;
      p.m2m.data = NULL;
      p.m2m.length = 0;
      g_exhaust_malloc_countdown = step;
      (void)c_orm_load_relation_ext(&g_db, &p, &p_meta, 0, 5, 1);
      g_exhaust_malloc_countdown = -1;
      free(p.m2m.data);
      p.m2m.data = NULL;

      p.o2m_children.data = NULL;
      p.o2m_children.length = 0;
      g_exhaust_malloc_countdown = step;
      (void)c_orm_load_relation_ext(&g_db, &p, &p_meta, 1, 5, 1);
      g_exhaust_malloc_countdown = -1;
      free(p.o2m_children.data);
      p.o2m_children.data = NULL;

      p.o2o_child = NULL;
      g_exhaust_malloc_countdown = step;
      (void)c_orm_load_relation_ext(&g_db, &p, &p_meta, 2, 5, 1);
      g_exhaust_malloc_countdown = -1;
      free(p.o2o_child);
      p.o2o_child = NULL;
    }
    c_orm_set_allocators(orig_m, orig_r, orig_f);
  }

  /* 5. Remaining branches: find_by_id_decimal, upsert hooks/errors/OOMs,
   * transaction NULLs, TTL expire_cb OK, TIMESTAMP/STRING Safe CRT, prefix
   * DECIMAL hydration, shard_manager_free error, & finalize_cached errors */
  {
    c_orm_decimal_t dpk;
    struct ExhaustExtRow out_er;
    struct CddCVariant kvars[1];
    c_orm_shard_manager_t *sm = NULL;

    memset(&out_er, 0, sizeof(out_er));
    (void)c_orm_decimal_from_str("9.99", &dpk);
    kvars[0].type = CDD_C_VARIANT_TYPE_DECIMAL;
    kvars[0].value.d_val = dpk;

    /* Transaction NULL & driver_name == NULL */
    ASSERT_NEQ(C_ORM_OK, c_orm_transaction_begin(NULL));
    ASSERT_NEQ(C_ORM_OK, c_orm_transaction_commit(NULL));
    ASSERT_NEQ(C_ORM_OK, c_orm_transaction_rollback(NULL));
    edb.driver_name = NULL;
    (void)c_orm_transaction_begin(&edb);
    (void)c_orm_transaction_commit(&edb);
    (void)c_orm_transaction_rollback(&edb);
    edb.driver_name = "sqlite";

    /* find_by_id_decimal exhaustive error & success paths */
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_find_by_id_decimal(&edb, NULL, &dpk, &out_er));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_find_by_id_decimal(&edb, &ext_meta, NULL, &out_er));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_find_by_id_decimal(&edb, &ext_meta, &dpk, NULL));
    ext_meta.query_select_by_pk = NULL;
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
              c_orm_find_by_id_decimal(&edb, &ext_meta, &dpk, &out_er));
    ext_meta.query_select_by_pk = "SELECT * FROM ext_tbl WHERE id=?";
    ext_meta.query_delete_by_pk = "DELETE FROM ext_tbl WHERE id=?";

    /* find/delete_by_composite_key with DECIMAL & bind_decimal == NULL */
    evt.bind_decimal = NULL;
    ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED,
              c_orm_find_by_composite_key(&edb, &ext_meta, 1, kvars, &out_er));
    ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED,
              c_orm_delete_by_composite_key(&edb, &ext_meta, 1, kvars));
    dpk.precision = 99;
    ASSERT_NEQ(C_ORM_OK,
               c_orm_find_by_id_decimal(&edb, &ext_meta, &dpk, &out_er));
    (void)c_orm_decimal_from_str("9.99", &dpk);
    evt.bind_decimal = ex_mock_bind_decimal;

    g_db_fail = 1;
    g_db_count = 0;
    g_db_target = 0;
    ASSERT_NEQ(C_ORM_OK,
               c_orm_find_by_id_decimal(&edb, &ext_meta, &dpk, &out_er));
    g_db_fail = 0;
    g_db_target = -1;

    g_ex_ext_mode = 3; /* bind_decimal error (+ finalize fail) */
    ASSERT_NEQ(C_ORM_OK,
               c_orm_find_by_id_decimal(&edb, &ext_meta, &dpk, &out_er));
    c_orm_mock_finalize_cached_fail = 1;
    ASSERT_NEQ(C_ORM_OK,
               c_orm_find_by_id_decimal(&edb, &ext_meta, &dpk, &out_er));
    c_orm_mock_finalize_cached_fail = 0;
    g_ex_ext_mode = 0;

    g_mock_step_fail_countdown = 0; /* step error (+ finalize fail) */
    ASSERT_NEQ(C_ORM_OK,
               c_orm_find_by_id_decimal(&edb, &ext_meta, &dpk, &out_er));
    g_mock_step_fail_countdown = 0;
    c_orm_mock_finalize_cached_fail = 1;
    ASSERT_NEQ(C_ORM_OK,
               c_orm_find_by_id_decimal(&edb, &ext_meta, &dpk, &out_er));
    c_orm_mock_finalize_cached_fail = 0;
    g_mock_step_fail_countdown = -1;

    g_step_count = 5;
    g_step_max = 1; /* !has_row + finalize fail */
    c_orm_mock_finalize_cached_fail = 1;
    ASSERT_NEQ(C_ORM_OK,
               c_orm_find_by_id_decimal(&edb, &ext_meta, &dpk, &out_er));
    c_orm_mock_finalize_cached_fail = 0;

    g_step_count = 0; /* has_row == 1: hydrate err (+fin fail) & hydrate OK */
    g_ex_ext_mode = 1;
    ASSERT_NEQ(C_ORM_OK,
               c_orm_find_by_id_decimal(&edb, &ext_meta, &dpk, &out_er));
    g_step_count = 0;
    c_orm_mock_finalize_cached_fail = 1;
    ASSERT_NEQ(C_ORM_OK,
               c_orm_find_by_id_decimal(&edb, &ext_meta, &dpk, &out_er));
    c_orm_mock_finalize_cached_fail = 0;
    g_ex_ext_mode = 0;
    g_step_count = 0;
    memset(&out_er, 0, sizeof(out_er));
    ASSERT_EQ(C_ORM_OK,
              c_orm_find_by_id_decimal(&edb, &ext_meta, &dpk, &out_er));
    ASSERT_EQ(C_ORM_OK, c_orm_free_columns(&ext_meta, &out_er));

    /* update_by_composite_key with DECFLOAT & bind_decimal==NULL bad prec */
    ext_meta.query_update = "UPDATE ext_tbl SET dec_val=? WHERE id=?";
    ext_meta.num_columns = 2;
    evt.bind_decimal = NULL;
    ext_row.dec_val.precision = 99;
    kvars[0].type = CDD_C_VARIANT_TYPE_INT;
    kvars[0].value.i_val = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_update_by_composite_key(&edb, &ext_meta, 1,
                                                       kvars, &ext_row));
    (void)c_orm_decimal_from_str("12.34", &ext_row.dec_val);
    ext_cols[1].type = C_ORM_TYPE_DECFLOAT;
    ASSERT_EQ(C_ORM_OK, c_orm_update_by_composite_key(&edb, &ext_meta, 1, kvars,
                                                      &ext_row));
    ext_cols[1].type = C_ORM_TYPE_DECIMAL;
    ext_meta.num_columns = 12;
    evt.bind_decimal = ex_mock_bind_decimal;

    /* c_orm_upsert & c_orm_upsert_batch edge cases (hooks, bind/step/fin errs,
     * OOM, memory batch fail & 0 items) */
    ASSERT_NEQ(C_ORM_OK, c_orm_upsert_batch(&edb, &ext_meta, NULL, 0, 1));
    edb.driver_name = "memory";
    g_ex_ext_mode = 3;
    ASSERT_NEQ(C_ORM_OK, c_orm_upsert_batch(&edb, &ext_meta, &ext_row, 1, 1));
    g_ex_ext_mode = 0;
    edb.driver_name = "sqlite";

    ext_meta.hooks[C_ORM_HOOK_BEFORE_SAVE] = mock_hook_fail;
    ASSERT_NEQ(C_ORM_OK, c_orm_upsert(&edb, &ext_meta, &ext_row));
    ext_meta.hooks[C_ORM_HOOK_BEFORE_SAVE] = mock_hook_success;
    ext_meta.hooks[C_ORM_HOOK_AFTER_SAVE] = mock_hook_fail;
    ASSERT_NEQ(C_ORM_OK, c_orm_upsert(&edb, &ext_meta, &ext_row));
    ext_meta.hooks[C_ORM_HOOK_AFTER_SAVE] = mock_hook_success;
    ASSERT_EQ(C_ORM_OK, c_orm_upsert(&edb, &ext_meta, &ext_row));
    ext_meta.hooks[C_ORM_HOOK_BEFORE_SAVE] = NULL;
    ext_meta.hooks[C_ORM_HOOK_AFTER_SAVE] = NULL;

    g_ex_ext_mode = 3; /* bind error in upsert (+fin fail) */
    ASSERT_NEQ(C_ORM_OK, c_orm_upsert(&edb, &ext_meta, &ext_row));
    c_orm_mock_finalize_cached_fail = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_upsert(&edb, &ext_meta, &ext_row));
    c_orm_mock_finalize_cached_fail = 0;
    g_ex_ext_mode = 0;

    g_mock_step_fail_countdown = 0; /* step error in upsert (+fin fail) */
    ASSERT_NEQ(C_ORM_OK, c_orm_upsert(&edb, &ext_meta, &ext_row));
    g_mock_step_fail_countdown = 0;
    c_orm_mock_finalize_cached_fail = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_upsert(&edb, &ext_meta, &ext_row));
    g_mock_step_fail_countdown = -1;
    /* step OK + finalize error in upsert */
    ASSERT_NEQ(C_ORM_OK, c_orm_upsert(&edb, &ext_meta, &ext_row));
    c_orm_mock_finalize_cached_fail = 0;

    c_orm_set_allocators(exhaust_mock_malloc, exhaust_mock_realloc, orig_f);
    for (step = 0; step < 30; step++) {
      g_exhaust_malloc_countdown = step;
      if (c_orm_upsert(&edb, &ext_meta, &ext_row) == C_ORM_OK) {
        g_exhaust_malloc_countdown = -1;
        break;
      }
      g_exhaust_malloc_countdown = -1;
    }
    c_orm_set_allocators(orig_m, orig_r, orig_f);

    /* TTL expire_cb returning C_ORM_OK -> C_ORM_ERROR_EXPIRED (L622) */
    {
      /** @brief Local TTL row fixture. */
      struct ExTtlRow1 {
        /** @brief Creation timestamp epoch seconds. */
        int64_t created_at;
        /** @brief Expiration TTL seconds. */
        int32_t expires_in;
      } ttl_row;
      c_orm_column_meta_t ttl_cols[2];
      c_orm_table_meta_t ttl_meta;
      memset(&ttl_row, 0, sizeof(ttl_row));
      memset(ttl_cols, 0, sizeof(ttl_cols));
      memset(&ttl_meta, 0, sizeof(ttl_meta));
      ttl_cols[0].name = "created_at";
      ttl_cols[0].type = C_ORM_TYPE_INT64;
      ttl_cols[0].offset = 0;
      ttl_cols[1].name = "expires_in";
      ttl_cols[1].type = C_ORM_TYPE_INT32;
      ttl_cols[1].offset = sizeof(int64_t);
      ttl_meta.name = "ttl_t";
      ttl_meta.columns = ttl_cols;
      ttl_meta.num_columns = 2;
      ttl_meta.struct_size = sizeof(ttl_row);
      ttl_meta.has_ttl = true;
      ttl_meta.created_at_offset = 0;
      ttl_meta.expires_in_offset = sizeof(int64_t);
      edb.expire_cb = ex_ttl_expire_ok;
      ASSERT_EQ(C_ORM_ERROR_EXPIRED,
                c_orm_hydrate_row_from(&edb, (c_orm_query_t *)1, &ttl_meta,
                                       &ttl_row, 0));
      edb.expire_cb = NULL;
    }

    /* TIMESTAMP timezone & STRING Safe CRT countdowns in hydrate & bind_row */
    {
      /** @brief Local timestamp and string row fixture. */
      struct ExTsRow {
        /** @brief Timestamp string pointer. */
        char *ts;
        /** @brief String column pointer. */
        char *s;
      } ts_row;
      c_orm_column_meta_t ts_cols[2];
      c_orm_table_meta_t ts_meta;
      memset(&ts_row, 0, sizeof(ts_row));
      memset(ts_cols, 0, sizeof(ts_cols));
      memset(&ts_meta, 0, sizeof(ts_meta));
      ts_cols[0].name = "ts";
      ts_cols[0].type = C_ORM_TYPE_TIMESTAMP;
      ts_cols[0].offset = 0;
      ts_cols[1].name = "s";
      ts_cols[1].type = C_ORM_TYPE_STRING;
      ts_cols[1].offset = sizeof(char *);
      ts_meta.name = "ts_t";
      ts_meta.columns = ts_cols;
      ts_meta.num_columns = 2;
      ts_meta.struct_size = sizeof(ts_row);
      ts_meta.query_insert = "INSERT INTO ts_t VALUES (?, ?)";
      edb.timezone.offset_minutes = 60;
      g_ex_ext_mode = 14;
      for (step = 0; step < 5; step++) {
        memset(&ts_row, 0, sizeof(ts_row));
        c_orm_mock_safe_crt_countdown = step;
        if (c_orm_hydrate_row_from(&edb, (c_orm_query_t *)1, &ts_meta, &ts_row,
                                   0) == C_ORM_OK) {
          c_orm_mock_safe_crt_countdown = -1;
          (void)c_orm_free_columns(&ts_meta, &ts_row);
          break;
        }
        c_orm_mock_safe_crt_countdown = -1;
      }
      ts_row.ts = "2026-05-01 10:20:30";
      ts_row.s = "hi";
      for (step = 0; step < 4; step++) {
        c_orm_mock_safe_crt_countdown = step;
        (void)c_orm_insert(&edb, &ts_meta, &ts_row);
        c_orm_mock_safe_crt_countdown = -1;
      }
      g_ex_ext_mode = 0;
      edb.timezone.offset_minutes = 0;
    }

    /* shard_manager_free with failing free */
    ASSERT_EQ(C_ORM_OK, c_orm_shard_manager_init(2, &sm));
    c_orm_set_allocators(orig_m, orig_r, ex_fail_free);
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_shard_manager_free(sm));
    c_orm_set_allocators(orig_m, orig_r, orig_f);
  }

  PASS();
}

/** @brief Mode selector for prefix & memory-upsert mock callbacks. */
static int g_fz_mode = 0;

/**
 * @brief Mock TTL expiration callback returning C_ORM_ERROR_SQL.
 * @param db Database connection.
 * @param meta Table metadata.
 * @param obj Expired row struct.
 * @param ud User data pointer.
 * @return C_ORM_ERROR_SQL.
 */
static c_orm_error_t fz_ttl_expire_err(c_orm_db_t *db,
                                       const c_orm_table_meta_t *meta,
                                       void *obj, void *ud) {
  (void)db;
  (void)meta;
  (void)obj;
  (void)ud;
  return C_ORM_ERROR_SQL;
}

/**
 * @brief Mock query interceptor callback returning C_ORM_ERROR_SQL.
 * @param db Database connection.
 * @param sql SQL text.
 * @param ctx Interceptor context.
 * @return C_ORM_ERROR_SQL.
 */
static c_orm_error_t fz_intercept_fail(c_orm_db_t *db, const char *sql,
                                       void *ctx) {
  (void)db;
  (void)sql;
  (void)ctx;
  return C_ORM_ERROR_SQL;
}

/**
 * @brief Mock column count returning 10 columns for prefix hydration tests.
 * @param q Query handle.
 * @param out Output column count.
 * @return C_ORM_OK.
 */
static c_orm_error_t fz_pfx_col_cnt(c_orm_query_t *q, int *out) {
  (void)q;
  *out = 10;
  return C_ORM_OK;
}

/**
 * @brief Mock column name returning parent id + 9 `child_*` columns.
 * @param q Query handle.
 * @param idx Column index.
 * @param out Output column name.
 * @return C_ORM_OK.
 */
static c_orm_error_t fz_pfx_col_name(c_orm_query_t *q, int idx,
                                     const char **out) {
  static const char *names[10] = {
      "id",        "child_id", "child_i64", "child_f32",  "child_f64",
      "child_dec", "child_df", "child_str", "child_blob", "child_bool"};
  (void)q;
  *out = names[idx % 10];
  return C_ORM_OK;
}

/**
 * @brief Mock is_null callback for prefix hydration tests.
 * @param q Query handle.
 * @param idx Column index.
 * @param out_n Output null indicator.
 * @return C_ORM_OK or error code.
 */
static c_orm_error_t fz_pfx_is_null(c_orm_query_t *q, int idx, int *out_n) {
  (void)q;
  if (g_fz_mode == 1 && idx == 1) {
    return C_ORM_ERROR_SQL;
  }
  *out_n = 0;
  return C_ORM_OK;
}

/**
 * @brief Mock get_int32 callback for prefix hydration tests.
 * @param q Query handle.
 * @param idx Column index.
 * @param out_v Output int32 value.
 * @return C_ORM_OK or error code.
 */
static c_orm_error_t fz_pfx_get_i32(c_orm_query_t *q, int idx, int32_t *out_v) {
  (void)q;
  if ((g_fz_mode == 2 && idx == 1) || (g_fz_mode == 9 && idx == 9)) {
    return C_ORM_ERROR_SQL;
  }
  *out_v = 1;
  return C_ORM_OK;
}

/**
 * @brief Mock get_int64 callback for prefix & FINAL TABLE tests.
 * @param q Query handle.
 * @param idx Column index.
 * @param out_v Output int64 value.
 * @return C_ORM_OK or error code.
 */
static c_orm_error_t fz_pfx_get_i64(c_orm_query_t *q, int idx, int64_t *out_v) {
  (void)q;
  if (g_fz_mode == 3 && idx == 2) {
    return C_ORM_ERROR_SQL;
  }
  if (g_fz_mode == 50 || g_fz_mode == 55) {
    return C_ORM_ERROR_SQL;
  }
  *out_v = (g_fz_mode == 56) ? 0 : 10;
  return C_ORM_OK;
}

/**
 * @brief Mock get_double callback for prefix hydration tests.
 * @param q Query handle.
 * @param idx Column index.
 * @param out_v Output double value.
 * @return C_ORM_OK or error code.
 */
static c_orm_error_t fz_pfx_get_dbl(c_orm_query_t *q, int idx, double *out_v) {
  (void)q;
  if ((g_fz_mode == 4 && idx == 3) || (g_fz_mode == 5 && idx == 4)) {
    return C_ORM_ERROR_SQL;
  }
  *out_v = 2.5;
  return C_ORM_OK;
}

/**
 * @brief Mock get_decimal callback for prefix hydration tests.
 * @param q Query handle.
 * @param idx Column index.
 * @param out_v Output decimal value.
 * @return C_ORM_OK or error code.
 */
static c_orm_error_t fz_pfx_get_dec(c_orm_query_t *q, int idx,
                                    c_orm_decimal_t *out_v) {
  (void)q;
  (void)idx;
  if (g_fz_mode == 6) {
    return C_ORM_ERROR_SQL;
  }
  return c_orm_decimal_from_str("7.25", out_v);
}

/**
 * @brief Mock get_string callback for prefix hydration tests.
 * @param q Query handle.
 * @param idx Column index.
 * @param out_v Output string pointer.
 * @return C_ORM_OK or error code.
 */
static c_orm_error_t fz_pfx_get_str(c_orm_query_t *q, int idx,
                                    const char **out_v) {
  (void)q;
  if (g_fz_mode == 7 && idx == 7) {
    return C_ORM_ERROR_SQL;
  }
  *out_v = "pfx_val";
  return C_ORM_OK;
}

/**
 * @brief Mock get_blob callback for prefix hydration tests.
 * @param q Query handle.
 * @param idx Column index.
 * @param out_v Output blob pointer.
 * @param sz Output blob byte length.
 * @return C_ORM_OK or error code.
 */
static c_orm_error_t fz_pfx_get_blob(c_orm_query_t *q, int idx,
                                     const void **out_v, size_t *sz) {
  static const unsigned char b2[2] = {0xAA, 0xBB};
  (void)q;
  if (g_fz_mode == 8 && idx == 8) {
    return C_ORM_ERROR_SQL;
  }
  *out_v = b2;
  *sz = 2;
  return C_ORM_OK;
}

/**
 * @brief Mock step callback returning configurable error codes for memory
 * upsert & FINAL TABLE tests.
 * @param q Query handle.
 * @param out_has Output row availability flag.
 * @return Configured error code or C_ORM_OK.
 */
static c_orm_error_t fz_custom_step(c_orm_query_t *q, int *out_has) {
  (void)q;
  *out_has = 0;
  if (g_fz_mode == 30) {
    g_fz_mode = 0;
    return C_ORM_ERROR_CONSTRAINT;
  }
  if (g_fz_mode == 31) {
    g_fz_mode = 0;
    return C_ORM_ERROR_SQL;
  }
  if (g_fz_mode == 32) {
    g_fz_mode = 0;
    return C_ORM_ERROR_UNKNOWN;
  }
  if (g_fz_mode == 51) {
    return C_ORM_ERROR_STEP;
  }
  if (g_fz_mode == 52 || g_fz_mode == 55 || g_fz_mode == 56) {
    *out_has = 1;
    return C_ORM_OK;
  }
  return C_ORM_OK;
}

/**
 * @brief Mock get_last_insert_rowid callback for FINAL TABLE fallback tests.
 * @param db Database connection.
 * @param out_id Output generated ID.
 * @return C_ORM_OK or error code.
 */
static c_orm_error_t fz_last_rowid(c_orm_db_t *db, int64_t *out_id) {
  (void)db;
  if (g_fz_mode == 53) {
    return C_ORM_ERROR_SQL;
  }
  *out_id = (g_fz_mode == 54) ? 0 : 99;
  return C_ORM_OK;
}

/**
 * @brief Closes the final remaining line and branch gaps in `src/c_orm_api.c`.
 * @return GREATEST test result.
 */
TEST test_api_final_zero_gaps(void) {
  c_orm_malloc_fn_t orig_m = c_orm_malloc;
  c_orm_realloc_fn_t orig_r = c_orm_realloc;
  c_orm_free_fn_t orig_f = c_orm_free;
  int step;

  /* 1. TTL expire_cb returning non-OK (L622) */
  {
    /** @brief Local TTL row fixture for error callback test. */
    struct ExTtlRow2 {
      /** @brief Creation timestamp epoch seconds. */
      int64_t created_at;
      /** @brief Expiration TTL seconds. */
      int32_t expires_in;
    } ttl_row;
    c_orm_column_meta_t ttl_cols[2];
    c_orm_table_meta_t ttl_meta;
    memset(&ttl_row, 0, sizeof(ttl_row));
    memset(ttl_cols, 0, sizeof(ttl_cols));
    memset(&ttl_meta, 0, sizeof(ttl_meta));
    ttl_cols[0].name = "created_at";
    ttl_cols[0].type = C_ORM_TYPE_INT64;
    ttl_cols[1].name = "expires_in";
    ttl_cols[1].type = C_ORM_TYPE_INT32;
    ttl_cols[1].offset = sizeof(int64_t);
    ttl_meta.name = "ttl_t";
    ttl_meta.columns = ttl_cols;
    ttl_meta.num_columns = 2;
    ttl_meta.struct_size = sizeof(ttl_row);
    ttl_meta.has_ttl = true;
    ttl_meta.created_at_offset = 0;
    ttl_meta.expires_in_offset = sizeof(int64_t);
    setup_vt();
    g_db.expire_cb = fz_ttl_expire_err;
    ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_hydrate_row_from(&g_db, (c_orm_query_t *)1,
                                                      &ttl_meta, &ttl_row, 0));
    g_db.expire_cb = NULL;
  }

  /* 2. Prefix nested hydration: DECIMAL, DECFLOAT, FLOAT, all getter errors,
   * Safe CRT error, and identity-map cache error (L709..808) */
  {
    /** @brief Nested child struct for prefix hydration coverage. */
    struct FzChild {
      /** @brief Primary key ID. */
      int32_t id;
      /** @brief 64-bit integer field. */
      int64_t i64;
      /** @brief 32-bit float field. */
      float f32;
      /** @brief 64-bit double field. */
      double f64;
      /** @brief Exact decimal field. */
      c_orm_decimal_t dec;
      /** @brief Decimal float field. */
      c_orm_decimal_t df;
      /** @brief Heap string field. */
      char *str;
      /** @brief Binary blob field. */
      c_orm_blob_t blob;
      /** @brief Boolean field. */
      bool b;
    };
    /** @brief Parent struct containing nested FzChild pointer. */
    struct FzParent {
      /** @brief Primary key ID. */
      int32_t id;
      /** @brief Lazy load context. */
      c_orm_lazy_load_context_t ctx;
      /** @brief Hydrated child pointer. */
      struct FzChild *child;
    } pobj;
    c_orm_column_meta_t pcols[1];
    c_orm_column_meta_t ccols[9];
    c_orm_relation_meta_t prel;
    c_orm_table_meta_t pmeta;
    c_orm_table_meta_t cmeta;
    c_orm_driver_vtable_t pvt;
    c_orm_db_t pdb;
    c_orm_identity_map_t pmap;

    setup_vt();
    pvt = g_vt;
    pvt.get_column_count = fz_pfx_col_cnt;
    pvt.get_column_name = fz_pfx_col_name;
    pvt.is_null = fz_pfx_is_null;
    pvt.get_int32 = fz_pfx_get_i32;
    pvt.get_int64 = fz_pfx_get_i64;
    pvt.get_double = fz_pfx_get_dbl;
    pvt.get_decimal = fz_pfx_get_dec;
    pvt.get_string = fz_pfx_get_str;
    pvt.get_blob = fz_pfx_get_blob;
    memset(&pdb, 0, sizeof(pdb));
    pdb.vtable = &pvt;
    pdb.driver_name = "sqlite";

    memset(pcols, 0, sizeof(pcols));
    pcols[0].name = "id";
    pcols[0].type = C_ORM_TYPE_INT32;
    pcols[0].is_pk = true;
    pcols[0].offset = offsetof(struct FzParent, id);

    memset(ccols, 0, sizeof(ccols));
    ccols[0].name = "id";
    ccols[0].type = C_ORM_TYPE_INT32;
    ccols[0].is_pk = true;
    ccols[0].offset = offsetof(struct FzChild, id);
    ccols[1].name = "i64";
    ccols[1].type = C_ORM_TYPE_INT64;
    ccols[1].offset = offsetof(struct FzChild, i64);
    ccols[2].name = "f32";
    ccols[2].type = C_ORM_TYPE_FLOAT;
    ccols[2].offset = offsetof(struct FzChild, f32);
    ccols[3].name = "f64";
    ccols[3].type = C_ORM_TYPE_DOUBLE;
    ccols[3].offset = offsetof(struct FzChild, f64);
    ccols[4].name = "dec";
    ccols[4].type = C_ORM_TYPE_DECIMAL;
    ccols[4].offset = offsetof(struct FzChild, dec);
    ccols[5].name = "df";
    ccols[5].type = C_ORM_TYPE_DECFLOAT;
    ccols[5].offset = offsetof(struct FzChild, df);
    ccols[6].name = "str";
    ccols[6].type = C_ORM_TYPE_STRING;
    ccols[6].offset = offsetof(struct FzChild, str);
    ccols[7].name = "blob";
    ccols[7].type = C_ORM_TYPE_BLOB;
    ccols[7].offset = offsetof(struct FzChild, blob);
    ccols[8].name = "bool";
    ccols[8].type = C_ORM_TYPE_BOOL;
    ccols[8].offset = offsetof(struct FzChild, b);

    memset(&cmeta, 0, sizeof(cmeta));
    cmeta.name = "fz_child";
    cmeta.columns = ccols;
    cmeta.num_columns = 9;
    cmeta.struct_size = sizeof(struct FzChild);

    memset(&prel, 0, sizeof(prel));
    prel.field_name = "child";
    prel.type = C_ORM_RELATION_ONE_TO_ONE;
    prel.target_meta = &cmeta;
    prel.struct_offset = offsetof(struct FzParent, child);
    prel.data_offset = offsetof(struct FzParent, child);
    prel.lazy_ctx_offset = offsetof(struct FzParent, ctx);

    memset(&pmeta, 0, sizeof(pmeta));
    pmeta.name = "fz_parent";
    pmeta.columns = pcols;
    pmeta.num_columns = 1;
    pmeta.relations = &prel;
    pmeta.num_relations = 1;
    pmeta.struct_size = sizeof(struct FzParent);

    /* Success with get_decimal != NULL and get_decimal == NULL */
    memset(&pobj, 0, sizeof(pobj));
    g_fz_mode = 0;
    ASSERT_EQ(C_ORM_OK,
              c_orm_hydrate_row(&pdb, (c_orm_query_t *)1, &pmeta, &pobj));
    free(pobj.child->str);
    free(pobj.child->blob.data);
    free(pobj.child);

    pvt.get_decimal = NULL;
    memset(&pobj, 0, sizeof(pobj));
    ASSERT_EQ(C_ORM_OK,
              c_orm_hydrate_row(&pdb, (c_orm_query_t *)1, &pmeta, &pobj));
    free(pobj.child->str);
    free(pobj.child->blob.data);
    free(pobj.child);
    pvt.get_decimal = fz_pfx_get_dec;

    /* Error modes 1..9 (is_null, i32, i64, f32, f64, dec, str, blob, bool) */
    for (step = 1; step <= 9; step++) {
      memset(&pobj, 0, sizeof(pobj));
      g_fz_mode = step;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_hydrate_row(&pdb, (c_orm_query_t *)1, &pmeta, &pobj));
      if (pobj.child) {
        free(pobj.child->str);
        free(pobj.child->blob.data);
        free(pobj.child);
      }
    }
    g_fz_mode = 0;

    /* Safe CRT failure on prefix string copy (L770) */
    for (step = 0; step < 3; step++) {
      memset(&pobj, 0, sizeof(pobj));
      c_orm_mock_safe_crt_countdown = step;
      (void)c_orm_hydrate_row(&pdb, (c_orm_query_t *)1, &pmeta, &pobj);
      c_orm_mock_safe_crt_countdown = -1;
      if (pobj.child) {
        free(pobj.child->str);
        free(pobj.child->blob.data);
        free(pobj.child);
      }
    }

    /* Identity map cache error on prefix child (L815-816) */
    ASSERT_EQ(C_ORM_OK, c_orm_identity_map_init(&pmap));
    ASSERT_EQ(C_ORM_OK, c_orm_db_attach_identity_map(&pdb, &pmap));
    ccols[0].is_pk = false;
    ccols[1].is_pk = true;
    memset(&pobj, 0, sizeof(pobj));
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
              c_orm_hydrate_row(&pdb, (c_orm_query_t *)1, &pmeta, &pobj));
    if (pobj.child) {
      free(pobj.child->str);
      free(pobj.child->blob.data);
      free(pobj.child);
    }
    ccols[0].is_pk = true;
    ccols[1].is_pk = false;
    pdb.identity_map = NULL;
    ASSERT_EQ(C_ORM_OK, c_orm_identity_map_free(&pmap));
  }

  /* 3. Batch finalize failures, upsert/insert/update/delete/cascade/interceptor
   * edge cases, abstract hydration NULLs, & Safe CRT sweeps */
  {
    struct ExtendedParent b[2];
    c_orm_column_meta_t bcols[2];
    c_orm_table_meta_t bmeta;
    c_orm_driver_vtable_t fvt;
    c_orm_db_t fdb;
    int exists_out = 0;

    setup_vt();
    fvt = g_vt;
    memset(&fdb, 0, sizeof(fdb));
    fdb.vtable = &fvt;
    fdb.driver_name = "sqlite";

    memset(b, 0, sizeof(b));
    b[0].id = 1;
    b[1].id = 2;
    memset(bcols, 0, sizeof(bcols));
    bcols[0].name = "id";
    bcols[0].type = C_ORM_TYPE_INT32;
    bcols[0].is_pk = true;
    bcols[0].offset = offsetof(struct ExtendedParent, id);
    bcols[1].name = "val";
    bcols[1].type = C_ORM_TYPE_INT32;
    bcols[1].offset = offsetof(struct ExtendedParent, belongs_to_id);

    memset(&bmeta, 0, sizeof(bmeta));
    bmeta.name = "bt";
    bmeta.columns = bcols;
    bmeta.num_columns = 2;
    bmeta.struct_size = sizeof(struct ExtendedParent);
    bmeta.query_insert = "INSERT INTO bt VALUES (?, ?)";
    bmeta.query_update = "UPDATE bt SET val=? WHERE id=?";
    bmeta.query_delete_by_pk = "DELETE FROM bt WHERE id=?";
    bmeta.query_select_by_pk = "SELECT id, val FROM bt WHERE id=?";
    bmeta.query_select_all = "SELECT id, val FROM bt";

    /* insert_batch_ext: BEFORE_SAVE/BEFORE_INSERT/bind/step + finalize fail */
    bmeta.hooks[C_ORM_HOOK_BEFORE_SAVE] = mock_hook_fail;
    g_mock_finalize_fail = 0;
    g_mock_finalize_countdown = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_insert_batch(&fdb, &bmeta, b, 1, 1));
    bmeta.hooks[C_ORM_HOOK_BEFORE_SAVE] = NULL;

    bmeta.hooks[C_ORM_HOOK_BEFORE_INSERT] = mock_hook_fail;
    g_mock_finalize_countdown = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_insert_batch(&fdb, &bmeta, b, 1, 1));
    bmeta.hooks[C_ORM_HOOK_BEFORE_INSERT] = NULL;

    g_mock_fail_bind = 1;
    g_mock_finalize_countdown = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_insert_batch(&fdb, &bmeta, b, 1, 1));
    g_mock_fail_bind = 0;
    /* step OK + finalize fail on chunk (g_mock_finalize_countdown = 1 skips
     * BEGIN's finalize and fails the chunk's finalize!) */
    g_mock_finalize_countdown = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_insert_batch(&fdb, &bmeta, b, 1, 1));
    g_mock_finalize_countdown = -1;

    /* delete_batch: BEFORE_DELETE + fin ok/fail, bind + fin fail, step + fin */
    bmeta.hooks[C_ORM_HOOK_BEFORE_DELETE] = mock_hook_fail;
    g_mock_finalize_fail = 0;
    ASSERT_NEQ(C_ORM_OK, c_orm_delete_batch(&fdb, &bmeta, b, 1, 1));
    g_mock_finalize_countdown = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_delete_batch(&fdb, &bmeta, b, 1, 1));
    g_mock_finalize_countdown = -1;
    bmeta.hooks[C_ORM_HOOK_BEFORE_DELETE] = NULL;

    g_mock_fail_bind = 1;
    g_mock_finalize_countdown = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_delete_batch(&fdb, &bmeta, b, 1, 1));
    g_mock_fail_bind = 0;
    g_mock_finalize_countdown = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_delete_batch(&fdb, &bmeta, b, 1, 1));
    g_mock_finalize_countdown = -1;

    /* update_batch: step OK + fin fail, bind fail + fin fail */
    g_mock_finalize_countdown = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_update_batch(&fdb, &bmeta, b, 1, 1));
    g_mock_fail_bind = 1;
    g_mock_finalize_countdown = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_update_batch(&fdb, &bmeta, b, 1, 1));
    g_mock_fail_bind = 0;
    g_mock_finalize_countdown = -1;

    /* upsert & upsert_batch remaining branches */
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_upsert_batch(&fdb, NULL, b, 1, 1));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_upsert(&fdb, NULL, &b[0]));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_upsert(&fdb, &bmeta, NULL));
    fdb.driver_name = NULL;
    ASSERT_EQ(C_ORM_OK, c_orm_upsert_batch(&fdb, &bmeta, b, 1, 1));
    c_orm_mock_string_builder_append_countdown = 0;
    ASSERT_NEQ(C_ORM_OK, c_orm_upsert(&fdb, &bmeta, &b[0]));
    c_orm_mock_string_builder_append_countdown = -1;

    fvt.step = fz_custom_step;
    fdb.driver_name = "memory";
    for (step = 30; step <= 32; step++) {
      g_fz_mode = step;
      ASSERT_EQ(C_ORM_OK, c_orm_upsert(&fdb, &bmeta, &b[0]));
    }
    /* Non-constraint insert error on memory upsert returns directly */
    g_mock_fail_bind = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_upsert(&fdb, &bmeta, &b[0]));
    g_mock_fail_bind = 0;

    /* Db2 FINAL TABLE insert errors & INT64/nullable/string PK variants */
    fdb.driver_name = "db2";
    fvt.get_int64 = fz_pfx_get_i64;
    fvt.get_last_insert_rowid = fz_last_rowid;
    b[0].id = 0;
    c_orm_set_allocators(exhaust_mock_malloc, exhaust_mock_realloc, orig_f);
    g_exhaust_malloc_countdown = 0; /* c_orm_string_builder_init OOM */
    ASSERT_NEQ(C_ORM_OK, c_orm_insert(&fdb, &bmeta, &b[0]));
    g_exhaust_malloc_countdown = -1;
    c_orm_set_allocators(orig_m, orig_r, orig_f);

    g_db_fail = 1;
    g_db_count = 0;
    g_db_target = 0; /* prepare fail */
    ASSERT_NEQ(C_ORM_OK, c_orm_insert(&fdb, &bmeta, &b[0]));
    g_db_fail = 0;
    g_db_target = -1;

    g_mock_fail_bind = 1; /* bind fail (+ fin fail) */
    ASSERT_NEQ(C_ORM_OK, c_orm_insert(&fdb, &bmeta, &b[0]));
    c_orm_mock_finalize_cached_fail = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_insert(&fdb, &bmeta, &b[0]));
    c_orm_mock_finalize_cached_fail = 0;
    g_mock_fail_bind = 0;

    g_fz_mode = 51; /* step fail (+ fin fail) */
    ASSERT_NEQ(C_ORM_OK, c_orm_insert(&fdb, &bmeta, &b[0]));
    c_orm_mock_finalize_cached_fail = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_insert(&fdb, &bmeta, &b[0]));
    g_fz_mode = 52; /* step OK + fin fail */
    ASSERT_NEQ(C_ORM_OK, c_orm_insert(&fdb, &bmeta, &b[0]));
    c_orm_mock_finalize_cached_fail = 0;

    /* get_int64 err -> fallback rowid err (53), rowid == 0 (54), NULLs */
    g_fz_mode = 50;
    b[0].id = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_insert(&fdb, &bmeta, &b[0]));
    g_fz_mode = 53;
    b[0].id = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_insert(&fdb, &bmeta, &b[0]));
    g_fz_mode = 54;
    b[0].id = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_insert(&fdb, &bmeta, &b[0]));
    fvt.get_int64 = NULL;
    fvt.get_last_insert_rowid = NULL;
    g_fz_mode = 52;
    b[0].id = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_insert(&fdb, &bmeta, &b[0]));
    g_fz_mode = 0;

    /* Nullable PK & INT64 PK & STRING PK on Db2 insert */
    bcols[0].is_nullable = true;
    ASSERT_EQ(C_ORM_OK, c_orm_insert(&fdb, &bmeta, &b[0]));
    bcols[0].is_nullable = false;
    bcols[0].type = C_ORM_TYPE_INT64;
    {
      /** @brief Local 64-bit PK row fixture. */
      struct ExI64PkRow {
        /** @brief 64-bit primary key. */
        int64_t id;
        /** @brief Integer payload column. */
        int32_t val;
      } i64r;
      c_orm_column_meta_t i64c[2];
      c_orm_table_meta_t i64m = bmeta;
      memset(&i64r, 0, sizeof(i64r));
      i64c[0] = bcols[0];
      i64c[0].offset = 0;
      i64c[1] = bcols[1];
      i64c[1].offset = sizeof(int64_t);
      i64m.columns = i64c;
      i64m.struct_size = sizeof(i64r);
      ASSERT_EQ(C_ORM_OK, c_orm_insert(&fdb, &i64m, &i64r));
    }
    bcols[0].type = C_ORM_TYPE_INT32;

    /* c_orm_update BEFORE_SAVE & AFTER_SAVE failures + query_interceptor */
    fvt = g_vt;
    fdb.driver_name = "sqlite";
    b[0].id = 1;
    bmeta.hooks[C_ORM_HOOK_BEFORE_SAVE] = mock_hook_fail;
    ASSERT_NEQ(C_ORM_OK, c_orm_update(&fdb, &bmeta, &b[0]));
    bmeta.hooks[C_ORM_HOOK_BEFORE_SAVE] = NULL;
    bmeta.hooks[C_ORM_HOOK_AFTER_SAVE] = mock_hook_fail;
    ASSERT_NEQ(C_ORM_OK, c_orm_update(&fdb, &bmeta, &b[0]));
    bmeta.hooks[C_ORM_HOOK_AFTER_SAVE] = NULL;

    fdb.query_interceptor = fz_intercept_fail;
    ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_execute_raw(&fdb, "SELECT 1"));
    fdb.query_interceptor = NULL;

    /* Exists & delete_all Safe CRT countdowns */
    c_orm_mock_safe_crt_countdown = 0;
    ASSERT_NEQ(C_ORM_OK, c_orm_exists_int32(&fdb, &bmeta, 1, &exists_out));
    c_orm_mock_safe_crt_countdown = 0;
    ASSERT_NEQ(C_ORM_OK, c_orm_delete_all(&fdb, &bmeta));
    bcols[0].type = C_ORM_TYPE_STRING;
    c_orm_mock_safe_crt_countdown = 0;
    ASSERT_NEQ(C_ORM_OK, c_orm_exists_string(&fdb, &bmeta, "a", &exists_out));
    bcols[0].type = C_ORM_TYPE_INT32;
    c_orm_mock_safe_crt_countdown = -1;

    /* Abstract hydration NULLs, get_column_count/name == NULL, & select_raw
     * with non-NULL meta */
    {
      cdd_c_abstract_struct_array_t aarr;
      struct Generic_Array sarr;
      c_orm_db_t empty_db;
      memset(&empty_db, 0, sizeof(empty_db));
      memset(&aarr, 0, sizeof(aarr));
      memset(&sarr, 0, sizeof(sarr));

      ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_hydrate_abstract_all(
                                         &empty_db, (c_orm_query_t *)1, &aarr));
      ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
                c_orm_hydrate_abstract_all(&fdb, NULL, &aarr));
      ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
                c_orm_hydrate_abstract_all(&fdb, (c_orm_query_t *)1, NULL));

      ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
                c_orm_find_all_abstract(NULL, "SELECT 1", &aarr));
      ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
                c_orm_find_all_abstract(&empty_db, "SELECT 1", &aarr));
      ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
                c_orm_find_all_abstract(&fdb, NULL, &aarr));
      ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
                c_orm_find_all_abstract(&fdb, "SELECT 1", NULL));

      /* get_column_count == NULL & get_column_name == NULL */
      fvt.step = ex_abs_step;
      fvt.get_column_count = NULL;
      g_ex_abs_row = 0;
      g_ex_abs_max = 1;
      ASSERT_EQ(C_ORM_OK, c_orm_find_all_abstract(&fdb, "SELECT 1", &aarr));
      /* Also calls hydrate_abstract_all with pre-allocated aarr! */
      g_ex_abs_row = 0;
      ASSERT_EQ(C_ORM_OK,
                c_orm_hydrate_abstract_all(&fdb, (c_orm_query_t *)1, &aarr));
      ASSERT_EQ(C_ORM_OK, c_orm_abstract_free(&aarr));

      memset(&aarr, 0, sizeof(aarr));
      fvt.get_column_count = ex_abs_col_cnt;
      fvt.get_column_name = NULL;
      fvt.is_null = ex_abs_is_null;
      g_ex_abs_row = 0;
      ASSERT_EQ(C_ORM_OK, c_orm_find_all_abstract(&fdb, "SELECT 1", &aarr));
      ASSERT_EQ(C_ORM_OK, c_orm_abstract_free(&aarr));

      /* find_all_abstract prepare error & finalize error */
      memset(&aarr, 0, sizeof(aarr));
      g_db_fail = 1;
      g_db_count = 0;
      g_db_target = 0;
      ASSERT_NEQ(C_ORM_OK, c_orm_find_all_abstract(&fdb, "SELECT 1", &aarr));
      g_db_fail = 0;
      g_db_target = -1;
      g_ex_abs_row = 0;
      c_orm_mock_finalize_cached_fail = 1;
      ASSERT_NEQ(C_ORM_OK, c_orm_find_all_abstract(&fdb, "SELECT 1", &aarr));
      c_orm_mock_finalize_cached_fail = 0;
      (void)c_orm_abstract_free(&aarr);

      /* c_orm_select_raw with non-NULL meta (+ NULLs, prepare/hydrate/fin) */
      fvt = g_vt;
      ASSERT_NEQ(C_ORM_OK, c_orm_select_raw(NULL, "SELECT 1", &bmeta, &sarr));
      ASSERT_NEQ(C_ORM_OK, c_orm_select_raw(&fdb, NULL, &bmeta, &sarr));
      ASSERT_NEQ(C_ORM_OK, c_orm_select_raw(&fdb, "SELECT 1", &bmeta, NULL));
      g_step_count = 0;
      g_step_max = 1;
      ASSERT_EQ(C_ORM_OK, c_orm_select_raw(&fdb, "SELECT 1", &bmeta, &sarr));
      free(sarr.data);
      memset(&sarr, 0, sizeof(sarr));
      g_step_count = 0;
      c_orm_mock_finalize_cached_fail = 1;
      ASSERT_NEQ(C_ORM_OK, c_orm_select_raw(&fdb, "SELECT 1", &bmeta, &sarr));
      c_orm_mock_finalize_cached_fail = 0;
      free(sarr.data);
      memset(&sarr, 0, sizeof(sarr));
      g_mock_step_fail_countdown = 0;
      ASSERT_NEQ(C_ORM_OK, c_orm_select_raw(&fdb, "SELECT 1", &bmeta, &sarr));
      g_mock_step_fail_countdown = 0;
      c_orm_mock_finalize_cached_fail = 1;
      ASSERT_NEQ(C_ORM_OK, c_orm_select_raw(&fdb, "SELECT 1", &bmeta, &sarr));
      c_orm_mock_finalize_cached_fail = 0;
      g_mock_step_fail_countdown = -1;
    }

    /* CHAR_FIXED validate: precision == 0 and strlen <= precision */
    {
      char *sval = "abc";
      c_orm_column_meta_t cf_col;
      c_orm_table_meta_t cf_meta;
      memset(&cf_col, 0, sizeof(cf_col));
      memset(&cf_meta, 0, sizeof(cf_meta));
      cf_col.name = "cf";
      cf_col.type = C_ORM_TYPE_CHAR_FIXED;
      cf_meta.name = "cft";
      cf_meta.columns = &cf_col;
      cf_meta.num_columns = 1;
      cf_meta.struct_size = sizeof(char *);
      ASSERT_EQ(C_ORM_OK, c_orm_validate(&cf_meta, &sval));
      cf_col.precision = 6;
      ASSERT_EQ(C_ORM_OK, c_orm_validate(&cf_meta, &sval));
    }

    /* c_orm_deep_copy & identity_map_get_or_set_str Safe CRT failures */
    {
      /** @brief Local single-string struct for deep_copy Safe CRT test. */
      struct ExStrOnly {
        /** @brief Heap string member. */
        char *s;
      } src_o, dst_o;
      cdd_c_prop_meta_t dp;
      cdd_c_meta_t dm;
      c_orm_identity_map_t fmap;
      void *out_obj = NULL;
      src_o.s = "hello";
      memset(&dst_o, 0, sizeof(dst_o));
      memset(&dp, 0, sizeof(dp));
      memset(&dm, 0, sizeof(dm));
      dp.name = "s";
      dp.type = "char *";
      dp.offset = 0;
      dm.name = "o";
      dm.size = sizeof(src_o);
      dm.num_props = 1;
      dm.props = &dp;
      c_orm_mock_safe_crt_countdown = 0;
      ASSERT_NEQ(C_ORM_OK, c_orm_deep_copy(&dm, &dst_o, &src_o));
      c_orm_mock_safe_crt_countdown = -1;

      ASSERT_EQ(C_ORM_OK, c_orm_identity_map_init(&fmap));
      c_orm_mock_safe_crt_countdown = 0;
      ASSERT_NEQ(C_ORM_OK, c_orm_identity_map_get_or_set_str(
                               &fmap, &bmeta, "k1", &src_o, &out_obj));
      c_orm_mock_safe_crt_countdown = -1;
      ASSERT_EQ(C_ORM_OK, c_orm_identity_map_free(&fmap));
    }

    /* DECIMAL FK set_null_field via ONE_TO_MANY c_orm_detach (L2650) */
    {
      /** @brief Local child struct with a DECIMAL foreign key. */
      struct FzDecChild {
        /** @brief Child primary key ID. */
        int32_t id;
        /** @brief Decimal foreign key referencing parent. */
        c_orm_decimal_t parent_fk;
      } dec_child;
      c_orm_column_meta_t dcc[2];
      c_orm_relation_meta_t drel;
      c_orm_table_meta_t dcm;
      c_orm_table_meta_t dpm = bmeta;
      memset(&dec_child, 0, sizeof(dec_child));
      dec_child.id = 5;
      (void)c_orm_decimal_from_str("1.00", &dec_child.parent_fk);
      memset(dcc, 0, sizeof(dcc));
      dcc[0].name = "id";
      dcc[0].type = C_ORM_TYPE_INT32;
      dcc[0].is_pk = true;
      dcc[1].name = "parent_fk";
      dcc[1].type = C_ORM_TYPE_DECIMAL;
      dcc[1].scale = 2;
      dcc[1].offset = offsetof(struct FzDecChild, parent_fk);
      memset(&dcm, 0, sizeof(dcm));
      dcm.name = "dc";
      dcm.columns = dcc;
      dcm.num_columns = 2;
      dcm.struct_size = sizeof(dec_child);
      dcm.query_update = "UPDATE dc SET parent_fk=? WHERE id=?";
      memset(&drel, 0, sizeof(drel));
      drel.field_name = "dc_rel";
      drel.type = C_ORM_RELATION_ONE_TO_MANY;
      drel.local_key = "id";
      drel.foreign_key = "parent_fk";
      drel.target_meta = &dcm;
      dpm.relations = &drel;
      dpm.num_relations = 1;
      fvt.bind_decimal = ex_mock_bind_decimal;
      ASSERT_EQ(C_ORM_OK,
                c_orm_detach(&fdb, &dpm, &b[0], "dc_rel", &dec_child));
    }

    /* Final remaining branches across find_with_relations, upsert, insert,
     * cascade delete, abstract/raw select, order_by Safe CRT,
     * attach/detach/sync */
    {
      const char *p_dot[1] = {"rel.sub"};
      const char *p_nodot[1] = {"rel"};
      struct ExtendedParent row_out;
      struct Generic_Array arr_out;
      struct NestedChild ch_in;
      struct FullParentObj par_in;
      c_orm_column_meta_t ch_cols[1];
      c_orm_table_meta_t ch_meta;
      c_orm_relation_meta_t r_meta;
      c_orm_column_meta_t fp_cols[2];
      c_orm_table_meta_t p_meta;
      c_orm_column_meta_t str_pk_col;
      c_orm_table_meta_t str_pk_meta;
      char *str_pk_val = "pk1";
      cdd_c_abstract_struct_array_t aarr2;
      c_orm_db_t empty_db2;

      setup_vt();
      fvt = g_vt;
      fvt.get_int32 = fz_pfx_get_i32;
      fvt.get_int64 = fz_pfx_get_i64;
      memset(&fdb, 0, sizeof(fdb));
      fdb.vtable = &fvt;
      fdb.driver_name = "sqlite";

      /* L2432-2438 & L2549-2555: Safe CRT errors on dot & non-dot paths */
      g_fz_mode = 0;
      g_step_count = 0;
      g_step_max = 1;
      c_orm_mock_safe_crt_countdown = 0;
      memset(&row_out, 0, sizeof(row_out));
      ASSERT_NEQ(C_ORM_OK, c_orm_find_with_relations_int32(&fdb, &bmeta, 1,
                                                           p_dot, 1, &row_out));
      c_orm_mock_safe_crt_countdown = -1;

      g_step_count = 0;
      g_step_max = 1;
      c_orm_mock_safe_crt_countdown = 0;
      memset(&row_out, 0, sizeof(row_out));
      ASSERT_NEQ(C_ORM_OK, c_orm_find_with_relations_int32(
                               &fdb, &bmeta, 1, p_nodot, 1, &row_out));
      c_orm_mock_safe_crt_countdown = -1;

      g_step_count = 0;
      g_step_max = 1;
      c_orm_mock_safe_crt_countdown = 0;
      memset(&arr_out, 0, sizeof(arr_out));
      ASSERT_NEQ(C_ORM_OK, c_orm_find_all_with_relations(&fdb, &bmeta, p_dot, 1,
                                                         &arr_out));
      c_orm_mock_safe_crt_countdown = -1;
      free(arr_out.data);

      g_step_count = 0;
      g_step_max = 1;
      c_orm_mock_safe_crt_countdown = 0;
      memset(&arr_out, 0, sizeof(arr_out));
      ASSERT_NEQ(C_ORM_OK, c_orm_find_all_with_relations(&fdb, &bmeta, p_nodot,
                                                         1, &arr_out));
      c_orm_mock_safe_crt_countdown = -1;
      free(arr_out.data);

      /* L3771-3772: c_orm_upsert prepare_cached failure */
      g_db_fail = 1;
      g_db_count = 0;
      g_db_target = 0;
      ASSERT_NEQ(C_ORM_OK, c_orm_upsert(&fdb, &bmeta, &b[0]));
      g_db_fail = 0;

      /* Setup child & parent metadata for BELONGS_TO / ONE_TO_ONE / cascades */
      memset(&ch_in, 0, sizeof(ch_in));
      ch_in.id = 7;
      memset(ch_cols, 0, sizeof(ch_cols));
      ch_cols[0].name = "id";
      ch_cols[0].type = C_ORM_TYPE_INT32;
      ch_cols[0].is_pk = true;
      ch_cols[0].offset = offsetof(struct NestedChild, id);
      memset(&ch_meta, 0, sizeof(ch_meta));
      ch_meta.name = "ch";
      ch_meta.columns = ch_cols;
      ch_meta.num_columns = 1;
      ch_meta.struct_size = sizeof(ch_in);
      ch_meta.query_insert = "INSERT INTO ch VALUES (?)";

      memset(fp_cols, 0, sizeof(fp_cols));
      fp_cols[0].name = "id";
      fp_cols[0].type = C_ORM_TYPE_INT32;
      fp_cols[0].is_pk = true;
      fp_cols[0].offset = offsetof(struct FullParentObj, id);
      fp_cols[1].name = "val";
      fp_cols[1].type = C_ORM_TYPE_INT32;
      fp_cols[1].offset = offsetof(struct FullParentObj, belongs_to_id);
      memset(&p_meta, 0, sizeof(p_meta));
      p_meta.name = "bt";
      p_meta.columns = fp_cols;
      p_meta.num_columns = 2;
      p_meta.struct_size = sizeof(par_in);
      p_meta.query_insert = "INSERT INTO bt VALUES (?, ?)";
      p_meta.query_delete_by_pk = "DELETE FROM bt WHERE id=?";
      p_meta.relations = &r_meta;
      p_meta.num_relations = 1;

      /* L4023-4037: BELONGS_TO set_int_field failures */
      memset(&par_in, 0, sizeof(par_in));
      par_in.id = 1;
      par_in.child_o2o = &ch_in;
      memset(&r_meta, 0, sizeof(r_meta));
      r_meta.field_name = "bt_rel";
      r_meta.type = C_ORM_RELATION_BELONGS_TO;
      r_meta.target_meta = &ch_meta;
      r_meta.struct_offset = offsetof(struct FullParentObj, child_o2o);
      r_meta.data_offset = offsetof(struct FullParentObj, child_o2o);
      r_meta.lazy_ctx_offset = offsetof(struct FullParentObj, o2o_ctx);
      fvt.get_last_insert_rowid = mock_get_last_insert_rowid;
      g_mock_last_id = 99;

      r_meta.local_key = "missing_local";
      r_meta.foreign_key = "id";
      ASSERT_NEQ(C_ORM_OK, c_orm_insert(&fdb, &p_meta, &par_in));

      r_meta.local_key = "val";
      r_meta.foreign_key = "missing_fk";
      ASSERT_NEQ(C_ORM_OK, c_orm_insert(&fdb, &p_meta, &par_in));

      fvt.get_last_insert_rowid = NULL;
      ch_in.id = 7;
      r_meta.local_key = "missing_local";
      r_meta.foreign_key = "id";
      ASSERT_NEQ(C_ORM_OK, c_orm_insert(&fdb, &p_meta, &par_in));
      fvt.get_last_insert_rowid = mock_get_last_insert_rowid;

      /* L4294-4295: ONE_TO_ONE set_int_field failure */
      memset(&par_in, 0, sizeof(par_in));
      par_in.id = 5;
      par_in.child_o2o = &ch_in;
      r_meta.type = C_ORM_RELATION_ONE_TO_ONE;
      r_meta.struct_offset = offsetof(struct FullParentObj, child_o2o);
      r_meta.data_offset = offsetof(struct FullParentObj, child_o2o);
      r_meta.lazy_ctx_offset = offsetof(struct FullParentObj, o2o_ctx);
      r_meta.local_key = "id";
      r_meta.foreign_key = "missing_fk";
      ASSERT_NEQ(C_ORM_OK, c_orm_insert(&fdb, &p_meta, &par_in));

      /* L4117[5], L4123[0], L4221[1,2]: Db2 PK branches */
      fdb.driver_name = "db2";
      memset(&str_pk_col, 0, sizeof(str_pk_col));
      str_pk_col.name = "id";
      str_pk_col.type = C_ORM_TYPE_STRING;
      str_pk_col.is_pk = true;
      memset(&str_pk_meta, 0, sizeof(str_pk_meta));
      str_pk_meta.name = "spk";
      str_pk_meta.columns = &str_pk_col;
      str_pk_meta.num_columns = 1;
      str_pk_meta.struct_size = sizeof(char *);
      str_pk_meta.query_insert = "INSERT INTO spk VALUES (?)";
      ASSERT_EQ(C_ORM_OK, c_orm_insert(&fdb, &str_pk_meta, &str_pk_val));

      b[0].id = 42;
      ASSERT_EQ(C_ORM_OK, c_orm_insert(&fdb, &bmeta, &b[0]));

      fvt.step = fz_custom_step;
      fvt.get_last_insert_rowid = fz_last_rowid;
      b[0].id = 0;
      g_fz_mode = 55;
      ASSERT_EQ(C_ORM_OK, c_orm_insert(&fdb, &bmeta, &b[0]));
      b[0].id = 0;
      g_fz_mode = 56;
      ASSERT_EQ(C_ORM_OK, c_orm_insert(&fdb, &bmeta, &b[0]));
      g_fz_mode = 0;
      fvt.step = mock_step;
      fvt.get_last_insert_rowid = mock_get_last_insert_rowid;
      fdb.driver_name = "sqlite";

      /* L5220-5298: c_orm_delete cascade ONE_TO_MANY & MANY_TO_MANY errors */
      b[0].id = 1;
      r_meta.type = C_ORM_RELATION_ONE_TO_MANY;
      r_meta.on_delete = C_ORM_CASCADE_DELETE;
      r_meta.local_key = "id";
      r_meta.foreign_key = "id";
      c_orm_mock_safe_crt_countdown = 0;
      ASSERT_NEQ(C_ORM_OK, c_orm_delete(&fdb, &p_meta, &b[0]));
      c_orm_mock_safe_crt_countdown = -1;

      g_mock_fail_bind = 1;
      ASSERT_NEQ(C_ORM_OK, c_orm_delete(&fdb, &p_meta, &b[0]));
      g_mock_fail_bind = 0;

      r_meta.type = C_ORM_RELATION_MANY_TO_MANY;
      r_meta.on_delete = C_ORM_CASCADE_DELETE;
      r_meta.join_table = "jt";
      r_meta.join_local_key = "pid";
      r_meta.join_foreign_key = "cid";
      c_orm_mock_safe_crt_countdown = 0;
      ASSERT_NEQ(C_ORM_OK, c_orm_delete(&fdb, &p_meta, &b[0]));
      c_orm_mock_safe_crt_countdown = -1;

      g_mock_fail_bind = 1;
      ASSERT_NEQ(C_ORM_OK, c_orm_delete(&fdb, &p_meta, &b[0]));
      g_mock_fail_bind = 0;

      /* L6498, L6576-6578, L6603, L6609-6610: abstract & raw select */
      memset(&aarr2, 0, sizeof(aarr2));
      aarr2.capacity = 4;
      g_step_count = 0;
      g_step_max = 0;
      ASSERT_EQ(C_ORM_OK,
                c_orm_hydrate_abstract_all(&fdb, (c_orm_query_t *)1, &aarr2));

      memset(&aarr2, 0, sizeof(aarr2));
      aarr2.capacity = (size_t)-1;
      aarr2.count = (size_t)-1;
      g_step_count = 0;
      g_step_max = 1;
      ASSERT_EQ(C_ORM_ERROR_VALIDATION,
                c_orm_hydrate_abstract_all(&fdb, (c_orm_query_t *)1, &aarr2));

      memset(&empty_db2, 0, sizeof(empty_db2));
      memset(&arr_out, 0, sizeof(arr_out));
      ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
                c_orm_select_raw(&empty_db2, "SELECT 1", &bmeta, &arr_out));
      ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
                c_orm_select_raw(&fdb, "SELECT 1", NULL, &arr_out));
      g_db_fail = 1;
      g_db_count = 0;
      g_db_target = 0;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_select_raw(&fdb, "SELECT 1", &bmeta, &arr_out));
      g_db_fail = 0;

      /* L7664-7673: c_orm_load_relation_ext order_by Safe CRT errors */
      for (step = 0; step < 4; step++) {
        memset(&par_in, 0, sizeof(par_in));
        par_in.id = 1;
        r_meta.type = C_ORM_RELATION_ONE_TO_MANY;
        r_meta.struct_offset = offsetof(struct FullParentObj, children_o2m);
        r_meta.data_offset = offsetof(struct FullParentObj, children_o2m);
        r_meta.lazy_ctx_offset = offsetof(struct FullParentObj, o2m_ctx);
        r_meta.order_by = "id ASC";
        c_orm_mock_safe_crt_countdown = step;
        (void)c_orm_load_relation_ext(&fdb, &par_in, &p_meta, 0, 0, 0);
        c_orm_mock_safe_crt_countdown = -1;
        free(par_in.children_o2m.data);

        memset(&par_in, 0, sizeof(par_in));
        par_in.id = 1;
        r_meta.order_by = "id";
        c_orm_mock_safe_crt_countdown = step;
        (void)c_orm_load_relation_ext(&fdb, &par_in, &p_meta, 0, 0, 0);
        c_orm_mock_safe_crt_countdown = -1;
        free(par_in.children_o2m.data);
      }
      r_meta.order_by = NULL;

      /* L8878, L9007, L9088-9217: attach, detach, sync error & finalize paths
       */
      for (step = 0; step <= 1; step++) {
        r_meta.type = C_ORM_RELATION_MANY_TO_MANY;
        g_db_fail = 1;
        g_mock_finalize_fail = step;
        g_db_count = 0;
        g_db_target = 1;
        ASSERT_NEQ(C_ORM_OK,
                   c_orm_attach(&fdb, &p_meta, &par_in, "bt_rel", &ch_in));
        g_db_count = 0;
        g_db_target = 1;
        ASSERT_NEQ(C_ORM_OK,
                   c_orm_detach(&fdb, &p_meta, &par_in, "bt_rel", &ch_in));
        g_db_count = 0;
        g_db_target = 1;
        ASSERT_NEQ(C_ORM_OK,
                   c_orm_sync(&fdb, &p_meta, &par_in, "bt_rel", &ch_in, 1));
      }
      g_mock_finalize_fail = 0;

      /* sync MANY_TO_MANY second INSERT bind1 fail + fin ok, bind2 fail + fin
       * fail */
      g_db_count = 0;
      g_db_target = 4;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_sync(&fdb, &p_meta, &par_in, "bt_rel", &ch_in, 1));
      g_db_count = 0;
      g_db_target = 5;
      g_mock_finalize_countdown = 1;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_sync(&fdb, &p_meta, &par_in, "bt_rel", &ch_in, 1));
      g_mock_finalize_countdown = -1;
      g_db_fail = 0;

      /* sync ONE_TO_MANY sprintf fail & bind fail + fin ok/fail */
      r_meta.type = C_ORM_RELATION_ONE_TO_MANY;
      c_orm_mock_safe_crt_countdown = 0;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_sync(&fdb, &p_meta, &par_in, "bt_rel", &ch_in, 1));
      c_orm_mock_safe_crt_countdown = -1;

      for (step = 0; step <= 1; step++) {
        g_db_fail = 1;
        g_db_count = 0;
        g_db_target = 1;
        g_mock_finalize_fail = step;
        ASSERT_NEQ(C_ORM_OK,
                   c_orm_sync(&fdb, &p_meta, &par_in, "bt_rel", &ch_in, 1));
      }
      g_mock_finalize_fail = 0;
      g_db_fail = 0;
    }
  }

  PASS();
}

#endif /* TEST_API_EXHAUST_H */
