#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_api_exhaust.h
 * @brief Unit tests covering remaining error branches in c_orm_api.c.
 */

#ifndef TEST_API_EXHAUST_H
#define TEST_API_EXHAUST_H

/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_string_builder.h"
#include "greatest.h"
/* clang-format on */

static int g_exhaust_fail_malloc = 0;
static int g_exhaust_fail_malloc_struct = 0;
static int g_exhaust_fail_realloc = 0;
static int g_exhaust_fail_realloc_size = 0;
static int g_exhaust_malloc_countdown = -1;
static int g_exhaust_realloc_countdown = -1;

static void *exhaust_mock_malloc(size_t sz) {
  if (g_exhaust_fail_malloc)
    return NULL;
  if (g_exhaust_fail_malloc_struct && sz == sizeof(struct NestedChild))
    return NULL;
  if (g_exhaust_malloc_countdown >= 0) {
    if (g_exhaust_malloc_countdown == 0) {
      g_exhaust_malloc_countdown = -1;
      return NULL;
    }
    g_exhaust_malloc_countdown--;
  }
  return malloc(sz);
}

static void *exhaust_mock_realloc(void *ptr, size_t sz) {
  if (g_exhaust_fail_realloc_size && (sz == 4 * sizeof(struct NestedChild) ||
                                      sz == 16 * sizeof(struct NestedChild)))
    return NULL;
  if (g_exhaust_fail_realloc == 1 && !ptr)
    return NULL;
  if (g_exhaust_realloc_countdown >= 0) {
    if (g_exhaust_realloc_countdown == 0) {
      g_exhaust_realloc_countdown = -1;
      return NULL;
    }
    g_exhaust_realloc_countdown--;
  }
  return realloc(ptr, sz);
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
  struct {
    char *id;
    char *name;
  } s_item;
  c_orm_column_meta_t s_cols[2];
  c_orm_table_meta_t s_meta;

  /* Int64 PK schema */
  struct {
    int64_t id;
    char *name;
  } i64_item;
  c_orm_column_meta_t i64_cols[2];
  c_orm_table_meta_t i64_meta;

  void *(*orig_m)(size_t) = c_orm_malloc;
  void *(*orig_r)(void *, size_t) = c_orm_realloc;
  void (*orig_f)(void *) = c_orm_free;

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
  if (s_item.id)
    C_ORM_STRCPY(s_item.id, 8, "s1");
  s_item.name = (char *)malloc(8);
  if (s_item.name)
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
    (void)c_orm_find_by_composite_key(&g_db, &p_meta, 1, key_vals, &p);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_delete_by_composite_key(&g_db, &p_meta, 1, key_vals);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_find_by_id_int32(&g_db, &p_meta, 1, &p);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "child_o2o", &p);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "children_o2m", &p);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "tags_m2m", &p);

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
    (void)c_orm_find_all_with_relation(&g_db, &p_meta, "child_o2o", &out_arr);

    free(out_arr.data);
    out_arr.data = calloc(2, sizeof(struct FullParentObj));
    out_arr.length = 2;
    out_arr.capacity = 2;
    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_find_all_with_relation(&g_db, &p_meta, "children_o2m",
                                       &out_arr);

    free(out_arr.data);
    out_arr.data = calloc(2, sizeof(struct FullParentObj));
    out_arr.length = 2;
    out_arr.capacity = 2;
    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_find_all_with_relation(&g_db, &p_meta, "tags_m2m", &out_arr);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_insert(&g_db, &p_meta, &p);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_update(&g_db, &p_meta, &p);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_update(&g_db, &s_meta, &s_item);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_update(&g_db, &i64_meta, &i64_item);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_delete(&g_db, &p_meta, &p);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_delete(&g_db, &s_meta, &s_item);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_delete(&g_db, &i64_meta, &i64_item);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_delete_by_id_int32(&g_db, &p_meta, 1);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_execute_raw(&g_db, "SELECT 1");

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_find_by_id_string(&g_db, &p_meta, "1", &p);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_find_for_update_by_id_int32(&g_db, &p_meta, 1, &p);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_find_for_update_by_id_string(&g_db, &s_meta, "1", &s_item);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_delete_by_id_string(&g_db, &p_meta, "1");

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_find_one_by_string(&g_db, &p_meta, "name", "alice", &p);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_update_partial(&g_db, &p_meta, &p, fields, 2);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_update_partial(&g_db, &s_meta, &s_item, fields, 1);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_exists_int32(&g_db, &p_meta, 1, &exists_flag);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_exists_string(&g_db, &p_meta, "1", &exists_flag);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_attach(&g_db, &p_meta, &p, "tags_m2m", &c_items[0]);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_detach(&g_db, &p_meta, &p, "tags_m2m", &c_items[0]);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_sync(&g_db, &p_meta, &p, "children_o2m", c_items, 2);

    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_sync(&g_db, &p_meta, &p, "tags_m2m", c_items, 2);
  }
  g_db_fail = 0;

  /* 3. Finalize cached fail when !has_row */
  g_step_count = 1;
  (void)c_orm_find_by_composite_key(&g_db, &p_meta, 1, key_vals, &p);
  g_step_count = 1;
  (void)c_orm_find_by_id_int32(&g_db, &p_meta, 1, &p);
  g_step_count = 1;
  (void)c_orm_find_by_id_string(&g_db, &p_meta, "1", &p);
  g_step_count = 1;
  (void)c_orm_find_for_update_by_id_int32(&g_db, &p_meta, 1, &p);
  g_step_count = 1;
  (void)c_orm_find_for_update_by_id_string(&g_db, &s_meta, "1", &s_item);
  g_step_count = 1;
  (void)c_orm_find_one_by_string(&g_db, &p_meta, "name", "alice", &p);
  g_step_count = 1;
  (void)c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "child_o2o", &p);
  g_step_count = 1;
  (void)c_orm_update_partial(&g_db, &p_meta, &p, fields, 2);
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
    (void)c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "child_o2o", &p);

    free(p.children_o2m.data);
    free(p.tags_m2m.data);
    free(p.child_o2o);
    memset(&p, 0, sizeof(p));
    p.id = 1;
    g_step_count = 0;
    g_exhaust_malloc_countdown = t;
    (void)c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "children_o2m", &p);

    free(p.children_o2m.data);
    free(p.tags_m2m.data);
    free(p.child_o2o);
    memset(&p, 0, sizeof(p));
    p.id = 1;
    g_step_count = 0;
    g_exhaust_malloc_countdown = t;
    (void)c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "tags_m2m", &p);
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
  (void)c_orm_update_partial(&g_db, &p_meta, &p, fields, 2);
  g_db_fail = 0;
  c_orm_mock_finalize_cached_fail = 0;

  /* 6. Child hydrate fail with finalize cached failure (lines 1301, 1331, 1734,
   * 1774) */
  if (out_arr.data) {
    free(out_arr.data);
    out_arr.data = NULL;
  }
  out_arr.length = 0;
  out_arr.capacity = 0;
  c_orm_mock_finalize_cached_fail = 1;
  g_mock_fail_col_index = 3;
  g_step_count = 0;
  (void)c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "children_o2m", &p);
  g_step_count = 0;
  (void)c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "tags_m2m", &p);
  g_step_count = 0;
  (void)c_orm_find_all_with_relation(&g_db, &p_meta, "child_o2o", &out_arr);
  g_step_count = 0;
  (void)c_orm_find_all_with_relation(&g_db, &p_meta, "children_o2m", &out_arr);
  g_step_count = 0;
  (void)c_orm_find_all_with_relation(&g_db, &p_meta, "tags_m2m", &out_arr);
  g_mock_fail_col_index = -1;
  c_orm_mock_finalize_cached_fail = 0;

  /* Cover exhaust_mock_malloc fail path (line 24) */
  g_exhaust_fail_malloc = 1;
  {
    void *m_fail = exhaust_mock_malloc(10);
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
  if (out_arr.data)
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
  if (out_arr.data)
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
  if (out_arr.data)
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
  if (out_arr.data)
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
  if (out_arr.data)
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
  if (out_arr.data)
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
  if (out_arr.data)
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
  if (out_arr.data)
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
    (void)c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "children_o2m", &p);
  }
  g_db_fail = 0;
  c_orm_mock_finalize_cached_countdown = -1;
  g_step_max = 1;

  c_orm_mock_finalize_cached_fail = 0;
  if (s_item.id)
    free(s_item.id);
  if (s_item.name)
    free(s_item.name);
  if (out_arr.data)
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
    (void)c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "child_o2o", &p);
    g_step_count = 0;
    c_orm_mock_string_builder_append_countdown = cd;
    (void)c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "children_o2m", &p);
    g_step_count = 0;
    c_orm_mock_string_builder_append_countdown = cd;
    (void)c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "tags_m2m", &p);
  }
  c_orm_mock_string_builder_append_countdown = -1;

  g_step_count = 0;
  c_orm_mock_string_builder_get_fail = 1;
  rc = c_orm_find_with_relation_int32(&g_db, &p_meta, 1, "child_o2o", &p);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  c_orm_mock_string_builder_get_fail = 0;

  /* 2. Sweep c_orm_find_all_with_relation for M2M and O2M (length=2 for comma)
   */
  if (out_arr.data)
    free(out_arr.data);
  out_arr.data = malloc(sizeof(struct ExtendedParent) * 2);
  for (cd = 0; cd < 40; cd++) {
    out_arr.length = 2;
    g_step_count = 0;
    c_orm_mock_string_builder_append_countdown = cd;
    (void)c_orm_find_all_with_relation(&g_db, &p_meta, "tags_m2m", &out_arr);
  }
  c_orm_mock_string_builder_append_countdown = -1;

  g_step_max = 2;
  for (cd = 0; cd < 30; cd++) {
    if (out_arr.data)
      free(out_arr.data);
    out_arr.data = NULL;
    out_arr.length = 0;
    out_arr.capacity = 0;
    g_step_count = 0;
    c_orm_mock_string_builder_append_countdown = cd;
    (void)c_orm_find_all_with_relation(&g_db, &p_meta, "children_o2m",
                                       &out_arr);
  }
  c_orm_mock_string_builder_append_countdown = -1;
  g_step_max = 1;

  /* Targeted comma append failure (lines 1586-1587) */
  if (out_arr.data)
    free(out_arr.data);
  out_arr.data = malloc(sizeof(struct ExtendedParent) * 2);
  out_arr.length = 2;
  g_step_count = 0;
  c_orm_mock_string_builder_append_countdown = 6;
  (void)c_orm_find_all_with_relation(&g_db, &p_meta, "children_o2m", &out_arr);
  c_orm_mock_string_builder_append_countdown = -1;

  if (out_arr.data)
    free(out_arr.data);
  out_arr.data = malloc(sizeof(struct ExtendedParent) * 2);
  out_arr.length = 2;
  g_step_count = 0;
  c_orm_mock_string_builder_get_fail = 1;
  (void)c_orm_find_all_with_relation(&g_db, &p_meta, "tags_m2m", &out_arr);
  c_orm_mock_string_builder_get_fail = 0;

  /* 3. Sweep c_orm_insert_batch_ext */
  for (cd = 0; cd < 40; cd++) {
    struct ExtendedParent batch[2];
    memset(batch, 0, sizeof(batch));
    batch[0].id = 1;
    batch[1].id = 2;
    c_orm_mock_string_builder_append_countdown = cd;
    (void)c_orm_insert_batch_ext(&g_db, &p_meta, batch, 2, 2,
                                 C_ORM_ON_CONFLICT_FAIL, NULL, NULL);
    c_orm_mock_string_builder_append_countdown = cd;
    (void)c_orm_insert_batch_ext(&g_db, &p_meta, batch, 2, 2,
                                 C_ORM_ON_CONFLICT_DO_NOTHING, NULL, NULL);
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
    (void)c_orm_delete_batch(&g_db, &p_meta, batch, 2, 2);
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
    (void)c_orm_update_batch(&g_db, &p_meta, batch, 2, 2);
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
    (void)c_orm_update_partial(&g_db, &p_meta, &p, fields, 2);
  }
  c_orm_mock_string_builder_append_countdown = -1;

  c_orm_mock_string_builder_get_fail = 1;
  rc = c_orm_update_partial(&g_db, &p_meta, &p, fields, 2);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  c_orm_mock_string_builder_get_fail = 0;

  /* 7. Sweep generic getters and insert */
  for (cd = 0; cd < 15; cd++) {
    c_orm_mock_string_builder_append_countdown = cd;
    (void)c_orm_insert_generic(&g_db, &p_meta, &p);
    c_orm_mock_string_builder_append_countdown = cd;
    (void)c_orm_get_generic(&g_db, &p_meta, 1, &p);
    c_orm_mock_string_builder_append_countdown = cd;
    (void)c_orm_find_all_generic(&g_db, &p_meta, &gen_data, &gen_count);
    c_orm_mock_string_builder_append_countdown = cd;
    (void)c_orm_get_generic_string(&g_db, &p_meta, "1", &p);
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
    (void)c_orm_find_one_by_string(&g_db, &p_meta, "name", "alice", &p);
  }
  c_orm_mock_string_builder_append_countdown = -1;

  c_orm_mock_string_builder_get_fail = 1;
  rc = c_orm_find_one_by_string(&g_db, &p_meta, "name", "alice", &p);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  c_orm_mock_string_builder_get_fail = 0;

  if (out_arr.data)
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

  void *(*orig_m)(size_t) = c_orm_malloc;
  void *(*orig_r)(void *, size_t) = c_orm_realloc;
  void (*orig_f)(void *) = c_orm_free;

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

  /* 1. Direct batch statement finalize failure via countdown = 1 (after BEGIN
   * succeeds) */
  g_mock_finalize_countdown = 1;
  rc = c_orm_insert_batch_ext(&g_db, &p_meta, batch, 2, 2,
                              C_ORM_ON_CONFLICT_FAIL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_finalize_countdown = -1;

  g_mock_finalize_countdown = 1;
  rc = c_orm_delete_batch(&g_db, &p_meta, batch, 2, 2);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_finalize_countdown = -1;

  g_mock_finalize_countdown = 1;
  rc = c_orm_update_batch(&g_db, &p_meta, batch, 2, 2);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_finalize_countdown = -1;

  /* Line 2764: BEFORE_SAVE hook fail & finalize fail / ok */
  p_meta.hooks[C_ORM_HOOK_BEFORE_SAVE] = mock_hook_fail;
  g_mock_finalize_countdown = 1;
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
  g_mock_finalize_countdown = 1;
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
  g_mock_finalize_countdown = 1;
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
  g_mock_finalize_countdown = 1;
  rc = c_orm_delete_batch(&g_db, &p_meta, batch, 2, 2);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_finalize_countdown = -1;
  rc = c_orm_delete_batch(&g_db, &p_meta, batch, 2, 2);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  p_meta.hooks[C_ORM_HOOK_BEFORE_DELETE] = NULL;

  /* Line 3634: delete bind fail & finalize fail / ok */
  g_mock_fail_bind = 1;
  g_mock_finalize_countdown = 1;
  rc = c_orm_delete_batch(&g_db, &p_meta, batch, 2, 2);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_finalize_countdown = -1;
  rc = c_orm_delete_batch(&g_db, &p_meta, batch, 2, 2);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_mock_fail_bind = 0;

  /* Line 3993: update bind fail (update_err) & finalize fail / ok */
  g_mock_fail_bind = 1;
  g_mock_finalize_countdown = 1;
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
    (void)c_orm_insert_generic(&g_db, &p_meta, &batch[0]);
    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_get_generic(&g_db, &p_meta, 1, &batch[0]);
    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_find_all_generic(&g_db, &p_meta, &gen_data, &gen_count);
    g_db_count = 0;
    g_step_count = 0;
    (void)c_orm_get_generic_string(&g_db, &p_meta, "1", &batch[0]);
  }
  g_db_fail = 0;

  g_step_count = 1; /* !has_row */
  (void)c_orm_get_generic(&g_db, &p_meta, 1, &batch[0]);
  g_step_count = 1;
  (void)c_orm_get_generic_string(&g_db, &p_meta, "1", &batch[0]);
  g_step_count = 0;

  /* 4. Find all generic OOM with finalize failure (line 7711) */
  g_mock_finalize_fail = 1;
  c_orm_set_allocators(exhaust_mock_malloc, orig_r, orig_f);
  for (t = 0; t <= 6; t++) {
    g_step_count = 0;
    g_exhaust_malloc_countdown = t;
    (void)c_orm_find_all_generic(&g_db, &p_meta, &gen_data, &gen_count);
  }
  c_orm_set_allocators(orig_m, orig_r, orig_f);
  g_exhaust_malloc_countdown = -1;

  /* 5. Find all generic realloc OOM with finalize failure (line 7745) */
  g_mock_finalize_fail = 1;
  g_exhaust_realloc_countdown = 1;
  g_step_count = 0;
  g_step_max = 20;
  c_orm_set_allocators(orig_m, exhaust_mock_realloc, orig_f);
  (void)c_orm_find_all_generic(&g_db, &p_meta, &gen_data, &gen_count);
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

  if (out_arr.data) {
    free(out_arr.data);
    out_arr.data = NULL;
  }

  PASS();
}

#endif /* TEST_API_EXHAUST_H */
