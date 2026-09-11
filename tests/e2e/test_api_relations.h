/**
 * @file test_api_relations.h
 * @brief Relationship, association, and eager loading tests for C ORM API.
 */

#ifndef TEST_API_RELATIONS_H
#define TEST_API_RELATIONS_H

#include "test_api_helpers.h"

TEST test_api_find_all_and_find_with_relations_deep(void) {
  struct ExtendedParent parents[2];
  struct Generic_Array out_arr;
  c_orm_column_meta_t p_cols[3];
  c_orm_column_meta_t c_cols[3];
  c_orm_relation_meta_t rels[3];
  c_orm_table_meta_t p_meta;
  c_orm_table_meta_t c_meta;
  c_orm_driver_vtable_t custom_vt;
  c_orm_db_t custom_db;

  memset(parents, 0, sizeof(parents));
  memset(&out_arr, 0, sizeof(out_arr));

  parents[0].id = 1;
  parents[0].belongs_to_id = 10;
  parents[1].id = 2;
  parents[1].belongs_to_id = 20;

  memset(p_cols, 0, sizeof(p_cols));
  p_cols[0].name = "id";
  p_cols[0].type = C_ORM_TYPE_INT32;
  p_cols[0].offset = offsetof(struct ExtendedParent, id);
  p_cols[0].is_pk = 1;

  p_cols[1].name = "belongs_to_id";
  p_cols[1].type = C_ORM_TYPE_INT32;
  p_cols[1].offset = offsetof(struct ExtendedParent, belongs_to_id);

  p_cols[2].name = "name";
  p_cols[2].type = C_ORM_TYPE_STRING;
  p_cols[2].offset = offsetof(struct ExtendedParent, name);

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
  c_meta.name = "children";
  c_meta.columns = c_cols;
  c_meta.num_columns = 3;
  c_meta.struct_size = sizeof(struct NestedChild);
  c_meta.query_select_by_pk = "SELECT * FROM children WHERE id=?";
  c_meta.query_select_all = "SELECT * FROM children";

  memset(rels, 0, sizeof(rels));
  /* rels[0]: ONE_TO_MANY */
  rels[0].field_name = "children_o2m";
  rels[0].type = C_ORM_RELATION_ONE_TO_MANY;
  rels[0].local_key = "id";
  rels[0].foreign_key = "parent_id";
  rels[0].data_offset = offsetof(struct ExtendedParent, children_o2m);
  rels[0].struct_offset = offsetof(struct ExtendedParent, children_o2m);
  rels[0].target_meta = &c_meta;
  rels[0].custom_filter = "deleted_at IS NULL";
  rels[0].order_by = "id ASC";

  /* rels[1]: ONE_TO_ONE */
  rels[1].field_name = "child_o2o";
  rels[1].type = C_ORM_RELATION_ONE_TO_ONE;
  rels[1].local_key = "id";
  rels[1].foreign_key = "parent_id";
  rels[1].data_offset = offsetof(struct ExtendedParent, child_o2o);
  rels[1].struct_offset = offsetof(struct ExtendedParent, child_o2o);
  rels[1].target_meta = &c_meta;

  /* rels[2]: MANY_TO_MANY */
  rels[2].field_name = "tags_m2m";
  rels[2].type = C_ORM_RELATION_MANY_TO_MANY;
  rels[2].local_key = "id";
  rels[2].foreign_key = "id";
  rels[2].join_table = "parent_tags";
  rels[2].join_local_key = "parent_id";
  rels[2].join_foreign_key = "tag_id";
  rels[2].data_offset = offsetof(struct ExtendedParent, tags_m2m);
  rels[2].struct_offset = offsetof(struct ExtendedParent, tags_m2m);
  rels[2].target_meta = &c_meta;

  memset(&p_meta, 0, sizeof(p_meta));
  p_meta.name = "parents";
  p_meta.columns = p_cols;
  p_meta.num_columns = 3;
  p_meta.relations = rels;
  p_meta.num_relations = 3;
  p_meta.struct_size = sizeof(struct ExtendedParent);
  p_meta.query_select_by_pk = "SELECT * FROM parents WHERE id=?";
  p_meta.query_select_all = "SELECT * FROM parents";

  custom_vt = g_vt;
  custom_db = g_db;
  custom_vt.step = mock_step_sequence;
  custom_vt.get_int32 = mock_get_int32_fk;
  custom_db.vtable = &custom_vt;

  /* 1. Test c_orm_find_all_with_relation with ONE_TO_MANY, custom filter, and
   * order_by */
  out_arr.data = malloc(2 * sizeof(struct ExtendedParent));
  memcpy(out_arr.data, parents, 2 * sizeof(struct ExtendedParent));
  out_arr.length = 2;
  out_arr.capacity = 2;
  g_step_count = 2;       /* Returns 2 child rows, then stops */
  g_mock_child_fk_id = 1; /* Both belongs to parent 1 */
  ASSERT_EQ(C_ORM_OK, c_orm_find_all_with_relation(&custom_db, &p_meta,
                                                   "children_o2m", &out_arr));
  {
    struct ExtendedParent *p_res = (struct ExtendedParent *)out_arr.data;
    if (p_res[0].children_o2m.data) {
      free(p_res[0].children_o2m.data);
      p_res[0].children_o2m.data = NULL;
    }
  }
  free(out_arr.data);
  out_arr.data = NULL;

  /* 1b. Test c_orm_find_all_with_relation ONE_TO_MANY with normal step
   * (target_meta FK search) */
  out_arr.data = malloc(2 * sizeof(struct ExtendedParent));
  memcpy(out_arr.data, parents, 2 * sizeof(struct ExtendedParent));
  out_arr.length = 2;
  out_arr.capacity = 2;
  g_step_count = 0; /* Standard mock_step returns 1 row */
  ASSERT_EQ(C_ORM_OK, c_orm_find_all_with_relation(&g_db, &p_meta,
                                                   "children_o2m", &out_arr));
  {
    struct ExtendedParent *p_res = (struct ExtendedParent *)out_arr.data;
    if (p_res[0].children_o2m.data) {
      free(p_res[0].children_o2m.data);
      p_res[0].children_o2m.data = NULL;
    }
  }
  free(out_arr.data);
  out_arr.data = NULL;

  /* 2. Test c_orm_find_all_with_relation with ONE_TO_ONE */
  out_arr.data = malloc(2 * sizeof(struct ExtendedParent));
  memcpy(out_arr.data, parents, 2 * sizeof(struct ExtendedParent));
  out_arr.length = 2;
  out_arr.capacity = 2;
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_find_all_with_relation(&g_db, &p_meta, "child_o2o",
                                                   &out_arr));
  {
    struct ExtendedParent *p_res = (struct ExtendedParent *)out_arr.data;
    if (p_res[0].child_o2o) {
      free(p_res[0].child_o2o);
      p_res[0].child_o2o = NULL;
    }
  }
  free(out_arr.data);
  out_arr.data = NULL;

  /* 3. Test c_orm_find_all_with_relation with 0 parents count */
  out_arr.data = NULL;
  out_arr.length = 0;
  out_arr.capacity = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_find_all_with_relation(&custom_db, &p_meta,
                                                   "child_o2o", &out_arr));

  PASS();
}

TEST test_api_load_relation_branches(void) {
  struct NullableParent np;
  c_orm_column_meta_t cols[2];
  c_orm_relation_meta_t rels[2];
  c_orm_table_meta_t meta;
  c_orm_table_meta_t target_meta;
  c_orm_column_meta_t t_cols[1];

  memset(&np, 0, sizeof(np));
  memset(cols, 0, sizeof(cols));
  memset(rels, 0, sizeof(rels));
  memset(&meta, 0, sizeof(meta));
  memset(&target_meta, 0, sizeof(target_meta));
  memset(t_cols, 0, sizeof(t_cols));

  t_cols[0].name = "id";
  t_cols[0].type = C_ORM_TYPE_INT32;
  t_cols[0].is_pk = 1;
  target_meta.name = "t_target";
  target_meta.columns = t_cols;
  target_meta.num_columns = 1;
  target_meta.struct_size = sizeof(int32_t);

  cols[0].name = "id";
  cols[0].type = C_ORM_TYPE_INT32;
  cols[0].is_pk = 1;
  cols[0].offset = offsetof(struct NullableParent, id);

  cols[1].name = "belongs_to_id";
  cols[1].type = C_ORM_TYPE_INT32;
  cols[1].is_nullable = 1;
  cols[1].offset = offsetof(struct NullableParent, belongs_to_id);

  /* rels[0]: valid relation with target_meta */
  rels[0].field_name = "target_rel";
  rels[0].type = C_ORM_RELATION_BELONGS_TO;
  rels[0].local_key = "belongs_to_id";
  rels[0].foreign_key = "id";
  rels[0].target_meta = &target_meta;
  rels[0].data_offset = offsetof(struct NullableParent, belongs_to_child);
  rels[0].struct_offset = offsetof(struct NullableParent, belongs_to_child);

  /* rels[1]: relation with NULL target_meta */
  rels[1].field_name = "null_target";
  rels[1].type = C_ORM_RELATION_BELONGS_TO;
  rels[1].target_meta = NULL;

  meta.name = "np_test";
  meta.columns = cols;
  meta.num_columns = 2;
  meta.relations = rels;
  meta.num_relations = 2;
  meta.struct_size = sizeof(np);

  /* 1. target_relation >= num_relations */
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_load_relation_ext(&g_db, &np, &meta, 5, 0, 0));

  /* 2. target_meta is NULL */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_load_relation_ext(&g_db, &np, &meta, 1, 0, 0));

  /* 3. Nullable FK is NULL -> returns C_ORM_OK inherently empty */
  np.belongs_to_id = NULL;
  ASSERT_EQ(C_ORM_OK, c_orm_load_relation_ext(&g_db, &np, &meta, 0, 0, 0));

  PASS();
}

TEST test_api_attach_detach_sync_error_branches(void) {
  struct ExtendedParent parent;
  struct NestedChild child;
  c_orm_relation_meta_t bad_rel[1];
  c_orm_table_meta_t p_meta;
  c_orm_column_meta_t p_cols[1];
  c_orm_table_meta_t c_meta;
  c_orm_column_meta_t c_cols[1];

  memset(&parent, 0, sizeof(parent));
  memset(&child, 0, sizeof(child));
  memset(bad_rel, 0, sizeof(bad_rel));
  memset(&p_meta, 0, sizeof(p_meta));
  memset(p_cols, 0, sizeof(p_cols));
  memset(&c_meta, 0, sizeof(c_meta));
  memset(c_cols, 0, sizeof(c_cols));

  p_cols[0].name = "id";
  p_cols[0].type = C_ORM_TYPE_INT32;
  p_cols[0].is_pk = 1;
  p_cols[0].offset = offsetof(struct ExtendedParent, id);
  p_meta.name = "par";
  p_meta.columns = p_cols;
  p_meta.num_columns = 1;
  p_meta.struct_size = sizeof(struct ExtendedParent);

  c_cols[0].name = "id";
  c_cols[0].type = C_ORM_TYPE_INT32;
  c_cols[0].is_pk = 1;
  c_cols[0].offset = offsetof(struct NestedChild, id);
  c_meta.name = "ch";
  c_meta.columns = c_cols;
  c_meta.num_columns = 1;
  c_meta.struct_size = sizeof(struct NestedChild);

  /* 1. c_orm_attach and c_orm_detach with MANY_TO_MANY missing join_table info
   */
  bad_rel[0].field_name = "m2m";
  bad_rel[0].type = C_ORM_RELATION_MANY_TO_MANY;
  bad_rel[0].target_meta = &c_meta;
  bad_rel[0].local_key = "id";
  bad_rel[0].foreign_key = "id";
  bad_rel[0].join_table = NULL; /* missing */
  p_meta.relations = bad_rel;
  p_meta.num_relations = 1;

  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_attach(&g_db, &p_meta, &parent, "m2m", &child));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_detach(&g_db, &p_meta, &parent, "m2m", &child));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_sync(&g_db, &p_meta, &parent, "m2m", &child, 1));

  /* 2. c_orm_attach and c_orm_detach with unsupported relation type */
  bad_rel[0].type = (c_orm_relation_type_t)99;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_attach(&g_db, &p_meta, &parent, "m2m", &child));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_detach(&g_db, &p_meta, &parent, "m2m", &child));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_sync(&g_db, &p_meta, &parent, "m2m", &child, 1));

  /* 3. c_orm_attach ONE_TO_MANY with invalid local_key or foreign_key */
  bad_rel[0].type = C_ORM_RELATION_ONE_TO_MANY;
  bad_rel[0].local_key = "nonexistent";
  bad_rel[0].foreign_key = "id";
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_attach(&g_db, &p_meta, &parent, "m2m", &child));

  bad_rel[0].local_key = "id";
  bad_rel[0].foreign_key = "nonexistent";
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_attach(&g_db, &p_meta, &parent, "m2m", &child));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_detach(&g_db, &p_meta, &parent, "m2m", &child));

  /* 4. c_orm_attach MANY_TO_MANY with invalid local_key or foreign_key */
  bad_rel[0].type = C_ORM_RELATION_MANY_TO_MANY;
  bad_rel[0].join_table = "join_t";
  bad_rel[0].join_local_key = "pid";
  bad_rel[0].join_foreign_key = "cid";
  bad_rel[0].local_key = "nonexistent";
  bad_rel[0].foreign_key = "id";
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_attach(&g_db, &p_meta, &parent, "m2m", &child));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_detach(&g_db, &p_meta, &parent, "m2m", &child));

  bad_rel[0].local_key = "id";
  bad_rel[0].foreign_key = "nonexistent";
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_attach(&g_db, &p_meta, &parent, "m2m", &child));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_detach(&g_db, &p_meta, &parent, "m2m", &child));

  PASS();
}

/* ========================================================================= */
/* --- CRUD Relations Error Branches Coverage --- */
/* ========================================================================= */

TEST test_api_crud_relations_deep_errors(void) {
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
  memset(p_cols, 0, sizeof(p_cols));
  memset(c_cols, 0, sizeof(c_cols));
  memset(rels, 0, sizeof(rels));
  memset(&p_meta, 0, sizeof(p_meta));
  memset(&c_meta, 0, sizeof(c_meta));

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

  c_meta.name = "nested_children";
  c_meta.columns = c_cols;
  c_meta.num_columns = 3;
  c_meta.struct_size = sizeof(struct NestedChild);
  c_meta.query_insert = "INSERT INTO nested_children VALUES (?, ?, ?)";
  c_meta.query_update = "UPDATE nested_children SET parent_id=? WHERE id=?";
  c_meta.query_delete_by_pk = "DELETE FROM nested_children WHERE id=?";
  c_meta.query_select_by_pk = "SELECT * FROM nested_children WHERE id=?";

  p_cols[0].name = "id";
  p_cols[0].type = C_ORM_TYPE_INT32;
  p_cols[0].offset = offsetof(struct NestedParent, id);
  p_cols[0].is_pk = 1;
  p_cols[1].name = "belongs_to_id";
  p_cols[1].type = C_ORM_TYPE_INT32;
  p_cols[1].offset = offsetof(struct NestedParent, belongs_to_id);
  p_cols[2].name = "name";
  p_cols[2].type = C_ORM_TYPE_STRING;
  p_cols[2].offset = offsetof(struct NestedParent, name);

  rels[0].field_name = "belongs_to_child";
  rels[0].type = C_ORM_RELATION_BELONGS_TO;
  rels[0].local_key = "belongs_to_id";
  rels[0].foreign_key = "id";
  rels[0].data_offset = offsetof(struct NestedParent, belongs_to_child);
  rels[0].struct_offset = offsetof(struct NestedParent, belongs_to_child);
  rels[0].target_meta = &c_meta;
  rels[0].on_update = C_ORM_CASCADE_UPDATE;
  rels[0].on_delete = C_ORM_CASCADE_DELETE;

  rels[1].field_name = "o2o_child";
  rels[1].type = C_ORM_RELATION_ONE_TO_ONE;
  rels[1].local_key = "id";
  rels[1].foreign_key = "parent_id";
  rels[1].data_offset = offsetof(struct NestedParent, o2o_child);
  rels[1].struct_offset = offsetof(struct NestedParent, o2o_child);
  rels[1].target_meta = &c_meta;
  rels[1].on_update = C_ORM_CASCADE_UPDATE;
  rels[1].on_delete = C_ORM_CASCADE_SET_NULL;

  rels[2].field_name = "o2m_children";
  rels[2].type = C_ORM_RELATION_ONE_TO_MANY;
  rels[2].local_key = "id";
  rels[2].foreign_key = "parent_id";
  rels[2].data_offset = offsetof(struct NestedParent, o2m_children);
  rels[2].struct_offset = offsetof(struct NestedParent, o2m_children);
  rels[2].target_meta = &c_meta;
  rels[2].on_update = C_ORM_CASCADE_UPDATE;
  rels[2].on_delete = C_ORM_CASCADE_DELETE;

  rels[3].field_name = "m2m";
  rels[3].type = C_ORM_RELATION_MANY_TO_MANY;
  rels[3].local_key = "id";
  rels[3].foreign_key = "id";
  rels[3].join_table = "join_t";
  rels[3].join_local_key = "p_id";
  rels[3].join_foreign_key = "c_id";
  rels[3].target_meta = &c_meta;
  rels[3].on_delete = C_ORM_CASCADE_DELETE;

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

  /* 1. BelongsTo child insert fails */
  c_meta.hooks[C_ORM_HOOK_BEFORE_INSERT] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&g_db, &p_meta, &p));
  c_meta.hooks[C_ORM_HOOK_BEFORE_INSERT] = NULL;

  /* 2. BelongsTo get_last_insert_rowid returns 0 fallback */
  g_mock_last_id = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_insert(&g_db, &p_meta, &p));
  g_mock_last_id = 1;

  /* 3. BelongsTo c_orm_exists_int32 error in insert and update */
  p.belongs_to_child = NULL;
  p.belongs_to_id = 999;
  c_meta.query_select_by_pk = NULL;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&g_db, &p_meta, &p));
  printf("update rc = %d\n", (int)c_orm_update(&g_db, &p_meta, &p));
  c_meta.query_select_by_pk = "SELECT * FROM nested_children WHERE id=?";

  /* 4. BelongsTo nullable FK error and validation */
  {
    struct NullableParent {
      int32_t id;
      int32_t *belongs_to_id;
      struct NestedChild *belongs_to_child;
    } np;
    c_orm_column_meta_t np_cols[2];
    c_orm_relation_meta_t np_rel[1];
    c_orm_table_meta_t np_meta;
    int32_t fk_val = 999;

    memset(&np, 0, sizeof(np));
    memset(np_cols, 0, sizeof(np_cols));
    memset(np_rel, 0, sizeof(np_rel));
    memset(&np_meta, 0, sizeof(np_meta));

    np.id = 1;
    np.belongs_to_id = &fk_val;

    np_cols[0].name = "id";
    np_cols[0].type = C_ORM_TYPE_INT32;
    np_cols[0].is_pk = 1;
    np_cols[0].offset = offsetof(struct NullableParent, id);
    np_cols[1].name = "belongs_to_id";
    np_cols[1].type = C_ORM_TYPE_INT32;
    np_cols[1].is_nullable = 1;
    np_cols[1].offset = offsetof(struct NullableParent, belongs_to_id);

    np_rel[0].field_name = "belongs_to_child";
    np_rel[0].type = C_ORM_RELATION_BELONGS_TO;
    np_rel[0].local_key = "belongs_to_id";
    np_rel[0].foreign_key = "id";
    np_rel[0].data_offset = offsetof(struct NullableParent, belongs_to_child);
    np_rel[0].struct_offset = offsetof(struct NullableParent, belongs_to_child);
    np_rel[0].target_meta = &c_meta;

    np_meta.name = "np_parents";
    np_meta.columns = np_cols;
    np_meta.num_columns = 2;
    np_meta.relations = np_rel;
    np_meta.num_relations = 1;
    np_meta.struct_size = sizeof(np);
    np_meta.query_insert = "INSERT INTO np_parents VALUES (?, ?)";
    np_meta.query_update = "UPDATE np_parents SET belongs_to_id=? WHERE id=?";

    c_meta.query_select_by_pk = NULL;
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&g_db, &np_meta, &np));
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_update(&g_db, &np_meta, &np));
    c_meta.query_select_by_pk = "SELECT * FROM nested_children WHERE id=?";
  }

  /* 5. OneToOne child insert fails */
  {
    c_orm_table_meta_t o2o_fail_meta;
    memcpy(&o2o_fail_meta, &c_meta, sizeof(c_meta));
    o2o_fail_meta.hooks[C_ORM_HOOK_BEFORE_INSERT] = dummy_failing_hook;
    rels[1].target_meta = &o2o_fail_meta;
    p.belongs_to_child = &bt_child;
    p.belongs_to_id = 10;
    p_cols[1].is_nullable = 0;
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&g_db, &p_meta, &p));
    rels[1].target_meta = &c_meta;
  }

  /* 6. Update failure and batch AFTER_UPDATE hook */
  p_meta.query_update = NULL;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_update(&g_db, &p_meta, &p));
  p_meta.query_update = "UPDATE nested_parents SET belongs_to_id=? WHERE id=?";

  p_meta.hooks[C_ORM_HOOK_AFTER_UPDATE] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_update_batch(&g_db, &p_meta, &p, 1, 0));
  p_meta.hooks[C_ORM_HOOK_AFTER_UPDATE] = NULL;

  /* 7. Delete prepare failure */
  p_meta.query_delete_by_pk = NULL;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_delete(&g_db, &p_meta, &p));
  p_meta.query_delete_by_pk = "DELETE FROM nested_parents WHERE id=?";

  /* 8. Delete AFTER_DELETE hook failure */
  p_meta.hooks[C_ORM_HOOK_AFTER_DELETE] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_delete(&g_db, &p_meta, &p));
  p_meta.hooks[C_ORM_HOOK_AFTER_DELETE] = NULL;

  PASS();
}

/* ========================================================================= */
/* --- Relation Loading and Sync Deep Coverage --- */
/* ========================================================================= */

TEST test_api_relation_loading_and_sync_deep(void) {
  struct ExtendedParent p;
  struct NestedChild c_items[2];
  c_orm_column_meta_t p_cols[2];
  c_orm_column_meta_t c_cols[3];
  c_orm_relation_meta_t rels[3];
  c_orm_table_meta_t p_meta;
  c_orm_table_meta_t c_meta;
  struct Generic_Array out_arr;
  const char *rel_names[2] = {"child_o2o.subchild", "nonexistent"};

  memset(&p, 0, sizeof(p));
  memset(c_items, 0, sizeof(c_items));
  memset(p_cols, 0, sizeof(p_cols));
  memset(c_cols, 0, sizeof(c_cols));
  memset(rels, 0, sizeof(rels));
  memset(&p_meta, 0, sizeof(p_meta));
  memset(&c_meta, 0, sizeof(c_meta));
  memset(&out_arr, 0, sizeof(out_arr));

  p.id = 1;
  c_items[0].id = 10;
  c_items[0].parent_id = 1;
  c_items[1].id = 11;
  c_items[1].parent_id = 1;

  p_cols[0].name = "id";
  p_cols[0].type = C_ORM_TYPE_INT32;
  p_cols[0].is_pk = 1;
  p_cols[0].offset = offsetof(struct ExtendedParent, id);
  p_cols[1].name = "name";
  p_cols[1].type = C_ORM_TYPE_STRING;
  p_cols[1].offset = offsetof(struct ExtendedParent, name);

  c_cols[0].name = "id";
  c_cols[0].type = C_ORM_TYPE_INT32;
  c_cols[0].is_pk = 1;
  c_cols[0].offset = offsetof(struct NestedChild, id);
  c_cols[1].name = "parent_id";
  c_cols[1].type = C_ORM_TYPE_INT32;
  c_cols[1].offset = offsetof(struct NestedChild, parent_id);
  c_cols[2].name = "name";
  c_cols[2].type = C_ORM_TYPE_STRING;
  c_cols[2].offset = offsetof(struct NestedChild, name);

  c_meta.name = "echild";
  c_meta.columns = c_cols;
  c_meta.num_columns = 3;
  c_meta.struct_size = sizeof(struct NestedChild);
  c_meta.query_select_all = "SELECT * FROM echild";
  c_meta.query_select_by_pk = "SELECT * FROM echild WHERE id=?";
  c_meta.query_insert = "INSERT INTO echild VALUES (?, ?, ?)";
  c_meta.query_update = "UPDATE echild SET parent_id=? WHERE id=?";
  c_meta.query_delete_by_pk = "DELETE FROM echild WHERE id=?";

  rels[0].field_name = "child_o2o";
  rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
  rels[0].local_key = "id";
  rels[0].foreign_key = "parent_id";
  rels[0].struct_offset = offsetof(struct ExtendedParent, child_o2o);
  rels[0].data_offset = offsetof(struct ExtendedParent, child_o2o);
  rels[0].target_meta = &c_meta;

  rels[1].field_name = "children_o2m";
  rels[1].type = C_ORM_RELATION_ONE_TO_MANY;
  rels[1].local_key = "id";
  rels[1].foreign_key = "parent_id";
  rels[1].struct_offset = offsetof(struct ExtendedParent, children_o2m);
  rels[1].data_offset = offsetof(struct ExtendedParent, children_o2m);
  rels[1].target_meta = &c_meta;

  rels[2].field_name = "tags_m2m";
  rels[2].type = C_ORM_RELATION_MANY_TO_MANY;
  rels[2].local_key = "id";
  rels[2].foreign_key = "id";
  rels[2].join_table = "join_tbl";
  rels[2].join_local_key = "p_id";
  rels[2].join_foreign_key = "c_id";
  rels[2].struct_offset = offsetof(struct ExtendedParent, tags_m2m);
  rels[2].data_offset = offsetof(struct ExtendedParent, tags_m2m);
  rels[2].target_meta = &c_meta;

  p_meta.name = "eparent";
  p_meta.columns = p_cols;
  p_meta.num_columns = 2;
  p_meta.relations = rels;
  p_meta.num_relations = 3;
  p_meta.struct_size = sizeof(struct ExtendedParent);
  p_meta.query_select_all = "SELECT * FROM eparent";
  p_meta.query_select_by_pk = "SELECT * FROM eparent WHERE id=?";
  p_meta.query_insert = "INSERT INTO eparent VALUES (?, ?)";
  p_meta.query_update = "UPDATE eparent SET name=? WHERE id=?";

  out_arr.data = malloc(sizeof(struct ExtendedParent));
  memcpy(out_arr.data, &p, sizeof(struct ExtendedParent));
  out_arr.length = 1;
  out_arr.capacity = 1;

  /* 1. c_orm_find_all_with_relation without PK */
  p_cols[0].is_pk = 0;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_find_all_with_relation(
                                     &g_db, &p_meta, "child_o2o", &out_arr));
  p_cols[0].is_pk = 1;

  /* 2. c_orm_find_all_with_relation M2M without target PK */
  c_cols[0].is_pk = 0;
  g_step_count = 0;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_find_all_with_relation(&g_db, &p_meta, "tags_m2m", &out_arr));
  c_cols[0].is_pk = 1;

  /* 3. c_orm_find_all_with_relation O2M without matching foreign key column */
  rels[1].foreign_key = "nonexistent";
  g_step_count = 1;
  ASSERT_EQ(C_ORM_OK, c_orm_find_all_with_relation(&g_db, &p_meta,
                                                   "children_o2m", &out_arr));
  rels[1].foreign_key = "parent_id";

  /* 4. c_orm_find_with_relations_int32 nested paths */
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, c_orm_find_with_relations_int32(
                                       &g_db, &p_meta, 1, rel_names, 2, &p));

  /* 5. c_orm_find_all_with_relations nested paths */
  g_step_count = 0;
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, c_orm_find_all_with_relations(
                                       &g_db, &p_meta, rel_names, 2, &out_arr));

  /* 6. c_orm_load_relation_ext with soft_delete_aware */
  rels[1].soft_delete_aware = 1;
  ASSERT_EQ(C_ORM_OK, c_orm_load_relation_ext(&g_db, &p, &p_meta, 1, 0, 0));
  rels[1].soft_delete_aware = 0;

  /* 7. c_orm_sync O2M and M2M */
  ASSERT_EQ(C_ORM_OK,
            c_orm_sync(&g_db, &p_meta, &p, "children_o2m", c_items, 2));
  ASSERT_EQ(C_ORM_OK, c_orm_sync(&g_db, &p_meta, &p, "tags_m2m", c_items, 2));

  /* 8. c_orm_sync invalid relation name */
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_sync(&g_db, &p_meta, &p, "nonexistent", c_items, 2));

  if (out_arr.data) {
    free(out_arr.data);
    out_arr.data = NULL;
  }

  PASS();
}

/* ========================================================================= */
/* --- Final Deep Coverage Suites --- */
/* ========================================================================= */

static int g_mock_child_is_null = 0;
static c_orm_error_t mock_is_null_child_check(c_orm_query_t *q, int index,
                                              int *is_null) {
  (void)q;
  if (!is_null)
    return C_ORM_ERROR_UNKNOWN;
  if (g_mock_child_is_null && index >= 3) {
    *is_null = 1;
  } else {
    *is_null = 0;
  }
  return C_ORM_OK;
}

TEST test_api_find_with_relation_int32_deep(void) {
  struct ExtendedParent p;
  c_orm_column_meta_t p_cols[3];
  c_orm_column_meta_t c_cols[3];
  c_orm_relation_meta_t rels[2];
  c_orm_table_meta_t p_meta;
  c_orm_table_meta_t c_meta;
  c_orm_driver_vtable_t custom_vt;
  c_orm_db_t custom_db;
  void *(*orig_m)(size_t) = c_orm_malloc;
  void *(*orig_r)(void *, size_t) = c_orm_realloc;
  void (*orig_f)(void *) = c_orm_free;

  memset(&p, 0, sizeof(p));
  memset(p_cols, 0, sizeof(p_cols));
  memset(c_cols, 0, sizeof(c_cols));
  memset(rels, 0, sizeof(rels));
  memset(&p_meta, 0, sizeof(p_meta));
  memset(&c_meta, 0, sizeof(c_meta));

  p.id = 1;
  p_cols[0].name = "id";
  p_cols[0].type = C_ORM_TYPE_INT32;
  p_cols[0].is_pk = 1;
  p_cols[0].offset = offsetof(struct ExtendedParent, id);
  p_cols[1].name = "belongs_to_id";
  p_cols[1].type = C_ORM_TYPE_INT32;
  p_cols[1].offset = offsetof(struct ExtendedParent, belongs_to_id);
  p_cols[2].name = "name";
  p_cols[2].type = C_ORM_TYPE_STRING;
  p_cols[2].offset = offsetof(struct ExtendedParent, name);

  c_cols[0].name = "id";
  c_cols[0].type = C_ORM_TYPE_INT32;
  c_cols[0].is_pk = 1;
  c_cols[0].offset = offsetof(struct NestedChild, id);
  c_cols[1].name = "parent_id";
  c_cols[1].type = C_ORM_TYPE_INT32;
  c_cols[1].offset = offsetof(struct NestedChild, parent_id);
  c_cols[2].name = "name";
  c_cols[2].type = C_ORM_TYPE_STRING;
  c_cols[2].offset = offsetof(struct NestedChild, name);

  c_meta.name = "children";
  c_meta.columns = c_cols;
  c_meta.num_columns = 3;
  c_meta.struct_size = sizeof(struct NestedChild);

  rels[0].field_name = "child_o2o";
  rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
  rels[0].local_key = "id";
  rels[0].foreign_key = "parent_id";
  rels[0].data_offset = offsetof(struct ExtendedParent, child_o2o);
  rels[0].struct_offset = offsetof(struct ExtendedParent, child_o2o);
  rels[0].target_meta = &c_meta;

  rels[1].field_name = "children_o2m";
  rels[1].type = C_ORM_RELATION_ONE_TO_MANY;
  rels[1].local_key = "id";
  rels[1].foreign_key = "parent_id";
  rels[1].data_offset = offsetof(struct ExtendedParent, children_o2m);
  rels[1].struct_offset = offsetof(struct ExtendedParent, children_o2m);
  rels[1].target_meta = &c_meta;

  p_meta.name = "parents";
  p_meta.columns = p_cols;
  p_meta.num_columns = 3;
  p_meta.relations = rels;
  p_meta.num_relations = 2;
  p_meta.struct_size = sizeof(struct ExtendedParent);

  custom_vt = g_vt;
  custom_db = g_db;
  custom_vt.step = mock_step_sequence;
  custom_vt.is_null = mock_is_null_child_check;
  custom_db.vtable = &custom_vt;

  /* 1. ONE_TO_ONE success */
  g_step_count = 1;
  g_mock_child_is_null = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_find_with_relation_int32(&custom_db, &p_meta, 1,
                                                     "child_o2o", &p));
  ASSERT(p.child_o2o != NULL);
  free(p.child_o2o);
  p.child_o2o = NULL;

  /* 2. ONE_TO_ONE child is null */
  g_step_count = 1;
  g_mock_child_is_null = 1;
  ASSERT_EQ(C_ORM_OK, c_orm_find_with_relation_int32(&custom_db, &p_meta, 1,
                                                     "child_o2o", &p));
  ASSERT(p.child_o2o == NULL);
  g_mock_child_is_null = 0;

  /* 3. ONE_TO_ONE OOM */
  c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
  g_deep_fail_oom = 0;
  g_deep_alloc_cnt = 0;
  g_step_count = 1;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_find_with_relation_int32(
                                    &custom_db, &p_meta, 1, "child_o2o", &p));
  c_orm_set_allocators(orig_m, orig_r, orig_f);
  g_deep_fail_oom = -1;

  /* 4. ONE_TO_MANY success (2 rows) */
  g_step_count = 2;
  ASSERT_EQ(C_ORM_OK, c_orm_find_with_relation_int32(&custom_db, &p_meta, 1,
                                                     "children_o2m", &p));
  ASSERT_EQ(2, p.children_o2m.length);
  free(p.children_o2m.data);
  p.children_o2m.data = NULL;

  /* 5. ONE_TO_MANY child is null */
  g_step_count = 1;
  g_mock_child_is_null = 1;
  ASSERT_EQ(C_ORM_OK, c_orm_find_with_relation_int32(&custom_db, &p_meta, 1,
                                                     "children_o2m", &p));
  g_mock_child_is_null = 0;

  /* 6. ONE_TO_MANY OOM */
  c_orm_set_allocators(orig_m, mock_deep_realloc, orig_f);
  g_deep_fail_realloc = 0;
  g_deep_realloc_cnt = 0;
  g_step_count = 2;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_with_relation_int32(&custom_db, &p_meta, 1,
                                           "children_o2m", &p));
  c_orm_set_allocators(orig_m, orig_r, orig_f);
  g_deep_fail_realloc = -1;

  /* 7. Initial step returns no rows -> C_ORM_ERROR_NOT_FOUND */
  g_step_count = 0;
  ASSERT_EQ(
      C_ORM_ERROR_NOT_FOUND,
      c_orm_find_with_relation_int32(&custom_db, &p_meta, 1, "child_o2o", &p));

  /* 8. Initial step fails */
  custom_vt.step = mock_always_step_fail;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_find_with_relation_int32(
                                     &custom_db, &p_meta, 1, "child_o2o", &p));
  custom_vt.step = mock_step_sequence;

  /* 9. bind_int32 fails */
  custom_vt.bind_int32 = mock_always_bind_int32_fail;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_find_with_relation_int32(
                                     &custom_db, &p_meta, 1, "child_o2o", &p));
  custom_vt.bind_int32 = mock_bind_int32;

  PASS();
}

TEST test_api_load_relation_deep_errors(void) {
  struct ExtendedParent p;
  c_orm_table_meta_t meta;
  c_orm_column_meta_t cols[1];
  c_orm_relation_meta_t rel;
  c_orm_table_meta_t target_meta;
  c_orm_column_meta_t tcols[1];

  memset(&p, 0, sizeof(p));
  memset(&meta, 0, sizeof(meta));
  memset(cols, 0, sizeof(cols));
  memset(&rel, 0, sizeof(rel));
  memset(&target_meta, 0, sizeof(target_meta));
  memset(tcols, 0, sizeof(tcols));

  cols[0].name = "id";
  cols[0].type = C_ORM_TYPE_INT32;
  cols[0].is_pk = 1;
  meta.name = "p";
  meta.columns = cols;
  meta.num_columns = 1;
  meta.relations = &rel;
  meta.num_relations = 1;
  meta.struct_size = sizeof(p);

  tcols[0].name = "id";
  tcols[0].type = C_ORM_TYPE_INT32;
  tcols[0].is_pk = 1;
  target_meta.name = "t";
  target_meta.columns = tcols;
  target_meta.num_columns = 1;
  target_meta.struct_size = sizeof(int32_t);

  /* M2M missing target_pk */
  rel.type = C_ORM_RELATION_MANY_TO_MANY;
  rel.local_key = "id";
  rel.target_meta = &target_meta;
  tcols[0].is_pk = 0;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_load_relation_ext(&g_db, &p, &meta, 0, 0, 0));
  tcols[0].is_pk = 1;

  /* M2M missing join_table info */
  rel.join_table = NULL;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_load_relation_ext(&g_db, &p, &meta, 0, 0, 0));

  PASS();
}

/* ========================================================================= */
/* --- Eager Loading, Sync, Generic, and Scatter-Gather Full Coverage --- */
/* ========================================================================= */

static int g_eager_fail_bind = 0;
static int g_eager_fail_step_init = 0;
static int g_eager_fail_step_loop = 0;
static int g_eager_fail_get_int32 = 0;
static int g_eager_step_cnt = 0;

static c_orm_error_t mock_eager_bind_int32(c_orm_query_t *q, int i,
                                           int32_t val) {
  (void)q;
  (void)i;
  (void)val;
  if (g_eager_fail_bind)
    return C_ORM_ERROR_UNKNOWN;
  return C_ORM_OK;
}

static c_orm_error_t mock_eager_step(c_orm_query_t *q, int *has_row) {
  (void)q;
  if (!has_row)
    return C_ORM_ERROR_UNKNOWN;
  if (g_eager_fail_step_init && g_eager_step_cnt == 1) {
    return C_ORM_ERROR_UNKNOWN;
  }
  if (g_eager_fail_step_loop && g_eager_step_cnt >= 2) {
    return C_ORM_ERROR_UNKNOWN;
  }
  if (g_eager_step_cnt++ < 3) {
    *has_row = 1;
  } else {
    *has_row = 0;
  }
  return C_ORM_OK;
}

static c_orm_error_t mock_eager_get_int32(c_orm_query_t *q, int i, int32_t *o) {
  (void)q;
  (void)i;
  if (g_eager_fail_get_int32)
    return C_ORM_ERROR_UNKNOWN;
  if (o)
    *o = 1;
  return C_ORM_OK;
}

TEST test_api_find_all_with_relation_eager_errors(void) {
  struct ExtendedParent parents[1];
  struct Generic_Array out_arr;
  c_orm_column_meta_t p_cols[2];
  c_orm_column_meta_t c_cols[3];
  c_orm_relation_meta_t rels[3];
  c_orm_table_meta_t p_meta;
  c_orm_table_meta_t c_meta;
  c_orm_driver_vtable_t custom_vt;
  c_orm_db_t custom_db;
  void *(*orig_m)(size_t) = c_orm_malloc;
  void *(*orig_r)(void *, size_t) = c_orm_realloc;
  void (*orig_f)(void *) = c_orm_free;

  memset(parents, 0, sizeof(parents));
  memset(&out_arr, 0, sizeof(out_arr));
  memset(p_cols, 0, sizeof(p_cols));
  memset(c_cols, 0, sizeof(c_cols));
  memset(rels, 0, sizeof(rels));
  memset(&p_meta, 0, sizeof(p_meta));
  memset(&c_meta, 0, sizeof(c_meta));

  parents[0].id = 1;

  p_cols[0].name = "id";
  p_cols[0].type = C_ORM_TYPE_INT32;
  p_cols[0].is_pk = 1;
  p_cols[0].offset = offsetof(struct ExtendedParent, id);
  p_cols[1].name = "name";
  p_cols[1].type = C_ORM_TYPE_STRING;
  p_cols[1].offset = offsetof(struct ExtendedParent, name);

  c_cols[0].name = "id";
  c_cols[0].type = C_ORM_TYPE_INT32;
  c_cols[0].is_pk = 1;
  c_cols[0].offset = offsetof(struct NestedChild, id);
  c_cols[1].name = "parent_id";
  c_cols[1].type = C_ORM_TYPE_INT32;
  c_cols[1].offset = offsetof(struct NestedChild, parent_id);
  c_cols[2].name = "name";
  c_cols[2].type = C_ORM_TYPE_STRING;
  c_cols[2].offset = offsetof(struct NestedChild, name);

  c_meta.name = "children";
  c_meta.columns = c_cols;
  c_meta.num_columns = 2;
  c_meta.struct_size = sizeof(struct NestedChild);

  rels[0].field_name = "child_o2o";
  rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
  rels[0].local_key = "id";
  rels[0].foreign_key = "parent_id";
  rels[0].data_offset = offsetof(struct ExtendedParent, child_o2o);
  rels[0].struct_offset = offsetof(struct ExtendedParent, child_o2o);
  rels[0].lazy_ctx_offset = offsetof(struct ExtendedParent, child_o2o);
  rels[0].target_meta = &c_meta;

  rels[1].field_name = "children_o2m";
  rels[1].type = C_ORM_RELATION_ONE_TO_MANY;
  rels[1].local_key = "id";
  rels[1].foreign_key = "parent_id";
  rels[1].data_offset = offsetof(struct ExtendedParent, children_o2m);
  rels[1].struct_offset = offsetof(struct ExtendedParent, children_o2m);
  rels[1].lazy_ctx_offset = offsetof(struct ExtendedParent, children_o2m);
  rels[1].target_meta = &c_meta;

  rels[2].field_name = "tags_m2m";
  rels[2].type = C_ORM_RELATION_MANY_TO_MANY;
  rels[2].local_key = "id";
  rels[2].foreign_key = "id";
  rels[2].join_table = "join_tbl";
  rels[2].join_local_key = "p_id";
  rels[2].join_foreign_key = "c_id";
  rels[2].struct_offset = offsetof(struct ExtendedParent, tags_m2m);
  rels[2].data_offset = offsetof(struct ExtendedParent, tags_m2m);
  rels[2].lazy_ctx_offset = offsetof(struct ExtendedParent, tags_m2m);
  rels[2].target_meta = &c_meta;

  p_meta.name = "parents";
  p_meta.columns = p_cols;
  p_meta.num_columns = 1;
  p_meta.relations = rels;
  p_meta.num_relations = 3;
  p_meta.struct_size = sizeof(struct ExtendedParent);
  p_meta.query_select_all = "SELECT * FROM parents";

  custom_vt = g_vt;
  custom_db = g_db;
  custom_vt.bind_int32 = mock_eager_bind_int32;
  custom_vt.step = mock_eager_step;
  custom_vt.get_int32 = mock_eager_get_int32;
  custom_db.vtable = &custom_vt;

  /* 1. bind_int32 error during eager chunk */
  if (out_arr.data) {
    free(out_arr.data);
    out_arr.data = NULL;
  }
  out_arr.length = 0;
  out_arr.capacity = 0;
  g_eager_step_cnt = 0;
  g_eager_fail_bind = 1;
  ASSERT_EQ(
      C_ORM_ERROR_UNKNOWN,
      c_orm_find_all_with_relation(&custom_db, &p_meta, "child_o2o", &out_arr));
  g_eager_fail_bind = 0;

  /* 2. step init error during eager chunk */
  if (out_arr.data) {
    free(out_arr.data);
    out_arr.data = NULL;
  }
  out_arr.length = 0;
  out_arr.capacity = 0;
  g_eager_step_cnt = 0;
  g_eager_fail_step_init = 1;
  ASSERT_EQ(
      C_ORM_ERROR_UNKNOWN,
      c_orm_find_all_with_relation(&custom_db, &p_meta, "child_o2o", &out_arr));
  g_eager_fail_step_init = 0;

  /* 3. M2M get_int32 failure in loop */
  if (out_arr.data) {
    free(out_arr.data);
    out_arr.data = NULL;
  }
  out_arr.length = 0;
  out_arr.capacity = 0;
  g_eager_step_cnt = 0;
  g_eager_fail_get_int32 = 1;
  ASSERT_EQ(
      C_ORM_ERROR_UNKNOWN,
      c_orm_find_all_with_relation(&custom_db, &p_meta, "tags_m2m", &out_arr));
  g_eager_fail_get_int32 = 0;

  /* 4. OneToOne calloc OOM */
  if (out_arr.data) {
    free(out_arr.data);
    out_arr.data = NULL;
  }
  out_arr.length = 0;
  out_arr.capacity = 0;
  c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
  g_deep_fail_oom = 1;
  g_deep_alloc_cnt = 0;
  g_eager_step_cnt = 0;
  ASSERT_EQ(
      C_ORM_ERROR_MEMORY,
      c_orm_find_all_with_relation(&custom_db, &p_meta, "child_o2o", &out_arr));
  c_orm_set_allocators(orig_m, orig_r, orig_f);
  g_deep_fail_oom = -1;

  /* 5. OneToMany realloc OOM */
  if (out_arr.data) {
    free(out_arr.data);
    out_arr.data = NULL;
  }
  out_arr.length = 0;
  out_arr.capacity = 0;
  c_orm_set_allocators(orig_m, mock_deep_realloc, orig_f);
  g_deep_fail_realloc = 1;
  g_deep_realloc_cnt = 0;
  g_eager_step_cnt = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_all_with_relation(&custom_db, &p_meta, "children_o2m",
                                         &out_arr));
  c_orm_set_allocators(orig_m, orig_r, orig_f);
  g_deep_fail_realloc = -1;

  /* 6. OneToMany loop step error */
  if (out_arr.data) {
    free(out_arr.data);
    out_arr.data = NULL;
  }
  out_arr.length = 0;
  out_arr.capacity = 0;
  g_eager_step_cnt = 0;
  g_eager_fail_step_loop = 1;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_find_all_with_relation(&custom_db, &p_meta, "children_o2m",
                                         &out_arr));
  g_eager_fail_step_loop = 0;

  if (out_arr.data) {
    free(out_arr.data);
    out_arr.data = NULL;
  }

  PASS();
}

static int g_sync_fail_step = 0;

static c_orm_error_t mock_sync_step(c_orm_query_t *q, int *has_row) {
  (void)q;
  if (!has_row)
    return C_ORM_ERROR_UNKNOWN;
  if (g_sync_fail_step) {
    return C_ORM_ERROR_UNKNOWN;
  }
  *has_row = 1;
  return C_ORM_OK;
}

TEST test_api_sync_and_attach_detach_errors(void) {
  struct ExtendedParent p;
  struct NestedChild c_items[2];
  c_orm_column_meta_t p_cols[2];
  c_orm_column_meta_t c_cols[3];
  c_orm_relation_meta_t rels[2];
  c_orm_table_meta_t p_meta;
  c_orm_table_meta_t c_meta;
  c_orm_driver_vtable_t custom_vt;
  c_orm_db_t custom_db;

  memset(&p, 0, sizeof(p));
  memset(c_items, 0, sizeof(c_items));
  memset(p_cols, 0, sizeof(p_cols));
  memset(c_cols, 0, sizeof(c_cols));
  memset(rels, 0, sizeof(rels));
  memset(&p_meta, 0, sizeof(p_meta));
  memset(&c_meta, 0, sizeof(c_meta));

  p.id = 1;
  c_items[0].id = 10;
  c_items[0].parent_id = 1;
  c_items[1].id = 11;
  c_items[1].parent_id = 1;

  p_cols[0].name = "id";
  p_cols[0].type = C_ORM_TYPE_INT32;
  p_cols[0].is_pk = 1;
  p_cols[0].offset = offsetof(struct ExtendedParent, id);
  p_cols[1].name = "name";
  p_cols[1].type = C_ORM_TYPE_STRING;
  p_cols[1].offset = offsetof(struct ExtendedParent, name);

  c_cols[0].name = "id";
  c_cols[0].type = C_ORM_TYPE_INT32;
  c_cols[0].is_pk = 1;
  c_cols[0].offset = offsetof(struct NestedChild, id);
  c_cols[1].name = "parent_id";
  c_cols[1].type = C_ORM_TYPE_INT32;
  c_cols[1].offset = offsetof(struct NestedChild, parent_id);
  c_cols[2].name = "name";
  c_cols[2].type = C_ORM_TYPE_STRING;
  c_cols[2].offset = offsetof(struct NestedChild, name);

  c_meta.name = "children";
  c_meta.columns = c_cols;
  c_meta.num_columns = 3;
  c_meta.struct_size = sizeof(struct NestedChild);
  c_meta.query_insert = "INSERT INTO children VALUES (?, ?, ?)";
  c_meta.query_update = "UPDATE children SET parent_id=? WHERE id=?";

  rels[0].field_name = "children_o2m";
  rels[0].type = C_ORM_RELATION_ONE_TO_MANY;
  rels[0].local_key = "id";
  rels[0].foreign_key = "parent_id";
  rels[0].data_offset = offsetof(struct ExtendedParent, children_o2m);
  rels[0].struct_offset = offsetof(struct ExtendedParent, children_o2m);
  rels[0].target_meta = &c_meta;

  rels[1].field_name = "tags_m2m";
  rels[1].type = C_ORM_RELATION_MANY_TO_MANY;
  rels[1].local_key = "id";
  rels[1].foreign_key = "id";
  rels[1].join_table = "join_tbl";
  rels[1].join_local_key = "p_id";
  rels[1].join_foreign_key = "c_id";
  rels[1].struct_offset = offsetof(struct ExtendedParent, tags_m2m);
  rels[1].data_offset = offsetof(struct ExtendedParent, tags_m2m);
  rels[1].target_meta = &c_meta;

  p_meta.name = "parents";
  p_meta.columns = p_cols;
  p_meta.num_columns = 2;
  p_meta.relations = rels;
  p_meta.num_relations = 2;
  p_meta.struct_size = sizeof(struct ExtendedParent);

  custom_vt = g_vt;
  custom_db = g_db;
  custom_vt.step = mock_sync_step;
  custom_db.vtable = &custom_vt;

  /* 1. Sync O2M step failure */
  g_sync_fail_step = 1;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_sync(&custom_db, &p_meta, &p, "children_o2m", c_items, 2));
  g_sync_fail_step = 0;

  /* 2. Sync M2M step failure */
  g_sync_fail_step = 1;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_sync(&custom_db, &p_meta, &p, "tags_m2m", c_items, 2));
  g_sync_fail_step = 0;

  /* 3. Attach step failure */
  g_sync_fail_step = 1;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_attach(&custom_db, &p_meta, &p, "tags_m2m", &c_items[0]));
  g_sync_fail_step = 0;

  /* 4. Detach step failure */
  g_sync_fail_step = 1;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_detach(&custom_db, &p_meta, &p, "tags_m2m", &c_items[0]));
  g_sync_fail_step = 0;

  PASS();
}

TEST test_api_find_and_eager_missing_branches(void) {
  c_orm_error_t rc;
  struct ExtendedParent p;
  struct Generic_Array out_arr;
  c_orm_column_meta_t p_cols[2];
  c_orm_column_meta_t c_cols[2];
  c_orm_relation_meta_t rels[2];
  c_orm_table_meta_t p_meta;
  c_orm_table_meta_t c_meta;
  c_orm_driver_vtable_t custom_vt;
  c_orm_db_t custom_db;
  const char *dot_paths[1] = {"children_o2m.subchild"};

  memset(&p, 0, sizeof(p));
  memset(&out_arr, 0, sizeof(out_arr));
  memset(p_cols, 0, sizeof(p_cols));
  memset(c_cols, 0, sizeof(c_cols));
  memset(rels, 0, sizeof(rels));
  memset(&p_meta, 0, sizeof(p_meta));
  memset(&c_meta, 0, sizeof(c_meta));

  p.id = 1;
  p_cols[0].name = "id";
  p_cols[0].type = C_ORM_TYPE_INT32;
  p_cols[0].is_pk = 1;
  p_cols[0].offset = offsetof(struct ExtendedParent, id);
  p_cols[1].name = "belongs_to_id";
  p_cols[1].type = C_ORM_TYPE_INT32;
  p_cols[1].offset = offsetof(struct ExtendedParent, belongs_to_id);

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
  rels[0].struct_offset = offsetof(struct ExtendedParent, child_o2o);
  rels[0].data_offset = offsetof(struct ExtendedParent, child_o2o);
  rels[0].lazy_ctx_offset = offsetof(struct ExtendedParent, child_o2o);
  rels[0].target_meta = &c_meta;

  rels[1].field_name = "children_o2m";
  rels[1].type = C_ORM_RELATION_ONE_TO_MANY;
  rels[1].local_key = "id";
  rels[1].foreign_key = "parent_id";
  rels[1].struct_offset = offsetof(struct ExtendedParent, children_o2m);
  rels[1].data_offset = offsetof(struct ExtendedParent, children_o2m);
  rels[1].lazy_ctx_offset = offsetof(struct ExtendedParent, children_o2m);
  rels[1].target_meta = &c_meta;

  p_meta.name = "p";
  p_meta.columns = p_cols;
  p_meta.num_columns = 2;
  p_meta.relations = rels;
  p_meta.num_relations = 2;
  p_meta.struct_size = sizeof(struct ExtendedParent);
  p_meta.query_select_by_pk = "SELECT * FROM p WHERE id=?";
  p_meta.query_select_all = "SELECT * FROM p";

  custom_vt = g_vt;
  custom_db = g_db;
  custom_vt.step = mock_stage_step;
  custom_vt.prepare = mock_stage_prepare;
  custom_vt.bind_int32 = mock_stage_bind_int32;
  custom_vt.finalize = mock_stage_finalize;
  custom_vt.reset = mock_stage_reset;
  custom_vt.is_null = mock_stage_is_null;
  custom_vt.get_int32 = mock_stage_get_int32;
  custom_db.vtable = &custom_vt;

  /* 1. find_with_relation_int32: parent get fail */
  g_mock_err_stage = 8;
  g_stage_step_cnt = 0;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_find_with_relation_int32(
                                     &custom_db, &p_meta, 1, "child_o2o", &p));

  /* 2. find_with_relation_int32: is_null fail in O2O */
  g_mock_err_stage = 6;
  g_stage_step_cnt = 0;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_find_with_relation_int32(
                                     &custom_db, &p_meta, 1, "child_o2o", &p));

  /* 3. find_with_relation_int32: child get fail in O2O */
  g_mock_err_stage = 7;
  g_stage_step_cnt = 0;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_find_with_relation_int32(
                                     &custom_db, &p_meta, 1, "child_o2o", &p));

  /* 4. find_with_relation_int32: is_null fail in O2M */
  g_mock_err_stage = 6;
  g_stage_step_cnt = 0;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_find_with_relation_int32(&custom_db, &p_meta, 1,
                                           "children_o2m", &p));

  /* 5. find_with_relation_int32: child get fail in O2M */
  g_mock_err_stage = 7;
  g_stage_step_cnt = 0;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_find_with_relation_int32(&custom_db, &p_meta, 1,
                                           "children_o2m", &p));
  g_mock_err_stage = 0;

  /* 6. find_with_relations_int32 and find_all_with_relations with populated O2M
   * children */
  {
    struct NestedChild dummy_children[2];
    memset(dummy_children, 0, sizeof(dummy_children));
    dummy_children[0].id = 10;
    dummy_children[1].id = 11;
    p.children_o2m.data = dummy_children;
    p.children_o2m.length = 2;
    p.children_o2m.capacity = 2;

    g_stage_step_cnt = 0;
    g_stage_step_max = 2;
    rc = c_orm_find_with_relations_int32(&custom_db, &p_meta, 1, dot_paths, 1,
                                         &p);
    ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, rc);
    g_stage_step_max = 1;

    out_arr.data = malloc(sizeof(struct ExtendedParent));
    memcpy(out_arr.data, &p, sizeof(struct ExtendedParent));
    out_arr.length = 1;
    out_arr.capacity = 1;

    g_stage_step_cnt = 0;
    rc = c_orm_find_all_with_relations(&custom_db, &p_meta, dot_paths, 1,
                                       &out_arr);
    printf("find_all_with_rel rc = %d\n", (int)rc);
    ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, rc);

    p.children_o2m.data = NULL;
    p.children_o2m.length = 0;
    free(out_arr.data);
    out_arr.data = NULL;
  }

  PASS();
}

TEST test_api_sync_attach_detach_all_errors(void) {
  struct ExtendedParent p;
  struct NestedChild c_items[2];
  c_orm_column_meta_t p_cols[2];
  c_orm_column_meta_t c_cols[3];
  c_orm_relation_meta_t rels[2];
  c_orm_table_meta_t p_meta;
  c_orm_table_meta_t c_meta;
  c_orm_driver_vtable_t custom_vt;
  c_orm_db_t custom_db;

  memset(&p, 0, sizeof(p));
  memset(c_items, 0, sizeof(c_items));
  memset(p_cols, 0, sizeof(p_cols));
  memset(c_cols, 0, sizeof(c_cols));
  memset(rels, 0, sizeof(rels));
  memset(&p_meta, 0, sizeof(p_meta));
  memset(&c_meta, 0, sizeof(c_meta));

  p.id = 1;
  c_items[0].id = 10;
  c_items[0].parent_id = 1;
  c_items[1].id = 11;
  c_items[1].parent_id = 1;

  p_cols[0].name = "id";
  p_cols[0].type = C_ORM_TYPE_INT32;
  p_cols[0].is_pk = 1;
  p_cols[0].offset = offsetof(struct ExtendedParent, id);
  p_cols[1].name = "belongs_to_id";
  p_cols[1].type = C_ORM_TYPE_INT32;
  p_cols[1].offset = offsetof(struct ExtendedParent, belongs_to_id);

  c_cols[0].name = "id";
  c_cols[0].type = C_ORM_TYPE_INT32;
  c_cols[0].is_pk = 1;
  c_cols[0].offset = offsetof(struct NestedChild, id);
  c_cols[1].name = "parent_id";
  c_cols[1].type = C_ORM_TYPE_INT32;
  c_cols[1].offset = offsetof(struct NestedChild, parent_id);
  c_cols[2].name = "name";
  c_cols[2].type = C_ORM_TYPE_STRING;
  c_cols[2].offset = offsetof(struct NestedChild, name);

  c_meta.name = "children";
  c_meta.columns = c_cols;
  c_meta.num_columns = 3;
  c_meta.struct_size = sizeof(struct NestedChild);
  c_meta.query_insert = "INSERT INTO children VALUES (?, ?, ?)";
  c_meta.query_update = "UPDATE children SET parent_id=? WHERE id=?";

  rels[0].field_name = "children_o2m";
  rels[0].type = C_ORM_RELATION_ONE_TO_MANY;
  rels[0].local_key = "id";
  rels[0].foreign_key = "parent_id";
  rels[0].struct_offset = offsetof(struct ExtendedParent, children_o2m);
  rels[0].data_offset = offsetof(struct ExtendedParent, children_o2m);
  rels[0].target_meta = &c_meta;

  rels[1].field_name = "tags_m2m";
  rels[1].type = C_ORM_RELATION_MANY_TO_MANY;
  rels[1].local_key = "id";
  rels[1].foreign_key = "id";
  rels[1].join_table = "join_tbl";
  rels[1].join_local_key = "p_id";
  rels[1].join_foreign_key = "c_id";
  rels[1].struct_offset = offsetof(struct ExtendedParent, tags_m2m);
  rels[1].data_offset = offsetof(struct ExtendedParent, tags_m2m);
  rels[1].target_meta = &c_meta;

  p_meta.name = "parents";
  p_meta.columns = p_cols;
  p_meta.num_columns = 2;
  p_meta.relations = rels;
  p_meta.num_relations = 2;
  p_meta.struct_size = sizeof(struct ExtendedParent);

  custom_vt = g_vt;
  custom_db = g_db;
  custom_vt.step = mock_stage_step;
  custom_vt.prepare = mock_stage_prepare;
  custom_vt.bind_int32 = mock_stage_bind_int32;
  custom_vt.finalize = mock_stage_finalize;
  custom_vt.reset = mock_stage_reset;
  custom_db.vtable = &custom_vt;

  /* 1. O2M sync errors */
  g_mock_err_stage = 2; /* prepare fails */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_sync(&custom_db, &p_meta, &p, "children_o2m", c_items, 2));

  g_mock_err_stage = 4; /* finalize fails */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_sync(&custom_db, &p_meta, &p, "children_o2m", c_items, 2));

  /* 2. M2M sync errors */
  g_mock_err_stage = 2; /* prepare fails on delete */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_sync(&custom_db, &p_meta, &p, "tags_m2m", c_items, 2));

  g_mock_err_stage = 4; /* finalize fails on delete */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_sync(&custom_db, &p_meta, &p, "tags_m2m", c_items, 2));

  g_mock_err_stage = 5; /* reset fails on insert loop */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_sync(&custom_db, &p_meta, &p, "tags_m2m", c_items, 2));

  /* 3. M2M sync insert links errors */
  g_mock_err_stage = 9; /* prepare fails on insert links */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_sync(&custom_db, &p_meta, &p, "tags_m2m", c_items, 2));

  g_mock_err_stage = 10; /* step fails on insert links */
  g_stage_step_cnt = 0;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_sync(&custom_db, &p_meta, &p, "tags_m2m", c_items, 2));

  g_mock_err_stage = 11; /* finalize fails on insert links */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_sync(&custom_db, &p_meta, &p, "tags_m2m", c_items, 2));
  g_mock_err_stage = 0;

  /* 4. M2M attach / detach prepare error */
  g_mock_err_stage = 2;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_attach(&custom_db, &p_meta, &p, "tags_m2m", &c_items[0]));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_detach(&custom_db, &p_meta, &p, "tags_m2m", &c_items[0]));
  g_mock_err_stage = 0;

  /* 5. Field resolution errors */
  rels[0].local_key = "nonexistent";
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_sync(&custom_db, &p_meta, &p, "children_o2m", c_items, 2));
  rels[0].local_key = "id";

  rels[0].foreign_key = "nonexistent";
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_sync(&custom_db, &p_meta, &p, "children_o2m", c_items, 2));
  rels[0].foreign_key = "parent_id";

  rels[1].local_key = "nonexistent";
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_sync(&custom_db, &p_meta, &p, "tags_m2m", c_items, 2));
  rels[1].local_key = "id";

  rels[1].foreign_key = "nonexistent";
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_sync(&custom_db, &p_meta, &p, "tags_m2m", c_items, 2));
  rels[1].foreign_key = "id";

  PASS();
}

#endif /* TEST_API_RELATIONS_H */
