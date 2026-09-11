/**
 * @file test_api_transactions.h
 * @brief Transaction, savepoint, lifecycle hook, and validation tests for C ORM
 * API.
 */

#ifndef TEST_API_TRANSACTIONS_H
#define TEST_API_TRANSACTIONS_H

#include "test_api_helpers.h"

TEST test_api_validation_and_relations_misc(void) {
  const c_orm_table_meta_t *tables[2];
  c_orm_table_meta_t t1, t2;
  c_orm_relation_meta_t r1[1];

  memset(&t1, 0, sizeof(t1));
  memset(&t2, 0, sizeof(t2));
  memset(r1, 0, sizeof(r1));

  t1.name = "t1";
  t2.name = "t2";

  r1[0].field_name = "t2_rel";
  r1[0].target_table = "t2";
  t1.relations = r1;
  t1.num_relations = 1;

  tables[0] = &t1;
  tables[1] = &t2;

  /* 1. c_orm_validate_relations success */
  ASSERT_EQ(C_ORM_OK, c_orm_validate_relations(tables, 2));

  /* 2. c_orm_identity_map_get_or_set_str multiple entries in bucket */
  {
    c_orm_identity_map_t imap;
    void *dummy_obj1 = (void *)0x1234;
    void *dummy_obj2 = (void *)0x5678;
    void *out_obj = NULL;

    memset(&imap, 0, sizeof(imap));
    ASSERT_EQ(C_ORM_OK, c_orm_identity_map_init(&imap));
    ASSERT_EQ(C_ORM_OK, c_orm_identity_map_get_or_set_str(
                            &imap, &t1, "pk1", dummy_obj1, &out_obj));
    ASSERT_EQ(C_ORM_OK, c_orm_identity_map_get_or_set_str(
                            &imap, &t1, "pk2", dummy_obj2, &out_obj));
    /* Search for second entry (exercises entry = entry->next loop) */
    ASSERT_EQ(C_ORM_OK, c_orm_identity_map_get_or_set_str(&imap, &t1, "pk2",
                                                          NULL, &out_obj));
    ASSERT_EQ(dummy_obj2, out_obj);
    c_orm_identity_map_free(&imap);
  }

  PASS();
}

TEST test_api_belongs_to_validation_branches(void) {
  struct NestedParent p;
  struct NullableParent np;
  c_orm_column_meta_t p_cols[2];
  c_orm_column_meta_t np_cols[2];
  c_orm_relation_meta_t rels[1];
  c_orm_relation_meta_t np_rels[1];
  c_orm_table_meta_t p_meta;
  c_orm_table_meta_t np_meta;
  c_orm_table_meta_t target_meta;
  c_orm_column_meta_t t_cols[1];
  int32_t fk_val;
  int32_t *ptr_fk;

  fk_val = 999;
  ptr_fk = &fk_val;

  memset(&p, 0, sizeof(p));
  memset(&np, 0, sizeof(np));
  memset(p_cols, 0, sizeof(p_cols));
  memset(np_cols, 0, sizeof(np_cols));
  memset(rels, 0, sizeof(rels));
  memset(np_rels, 0, sizeof(np_rels));
  memset(&p_meta, 0, sizeof(p_meta));
  memset(&np_meta, 0, sizeof(np_meta));
  memset(&target_meta, 0, sizeof(target_meta));
  memset(t_cols, 0, sizeof(t_cols));

  t_cols[0].name = "id";
  t_cols[0].type = C_ORM_TYPE_INT32;
  t_cols[0].is_pk = 1;
  target_meta.name = "target";
  target_meta.columns = t_cols;
  target_meta.num_columns = 1;
  target_meta.struct_size = sizeof(int32_t);
  target_meta.query_select_by_pk = "SELECT id FROM target WHERE id=?";

  p_cols[0].name = "id";
  p_cols[0].type = C_ORM_TYPE_INT32;
  p_cols[0].is_pk = 1;
  p_cols[0].offset = offsetof(struct NestedParent, id);

  p_cols[1].name = "belongs_to_id";
  p_cols[1].type = C_ORM_TYPE_INT32;
  p_cols[1].is_nullable = 0; /* required */
  p_cols[1].offset = offsetof(struct NestedParent, belongs_to_id);

  rels[0].field_name = "target_rel";
  rels[0].type = C_ORM_RELATION_BELONGS_TO;
  rels[0].local_key = "belongs_to_id";
  rels[0].foreign_key = "id";
  rels[0].target_meta = &target_meta;
  rels[0].data_offset = offsetof(struct NestedParent, belongs_to_child);
  rels[0].struct_offset = offsetof(struct NestedParent, belongs_to_child);

  p_meta.name = "parent_bt";
  p_meta.columns = p_cols;
  p_meta.num_columns = 2;
  p_meta.relations = rels;
  p_meta.num_relations = 1;
  p_meta.struct_size = sizeof(p);
  p_meta.query_insert =
      "INSERT INTO parent_bt (id, belongs_to_id) VALUES (?, ?)";
  p_meta.query_update = "UPDATE parent_bt SET belongs_to_id=? WHERE id=?";

  /* Case 1: non-nullable FK with existing_fk != 0, but does not exist in DB */
  p.id = 1;
  p.belongs_to_id = 999; /* Non-zero */
  p.belongs_to_child = NULL;

  /* mock_step returns 0 rows (has_row=0), so c_orm_exists_int32 returns
   * exists=0 */
  g_step_count = 1; /* returns 0 rows from step */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_insert(&g_db, &p_meta, &p));

  g_step_count = 1;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_update(&g_db, &p_meta, &p));

  /* Case 2: nullable FK with existing_fk != 0, does not exist in DB */
  np_cols[0].name = "id";
  np_cols[0].type = C_ORM_TYPE_INT32;
  np_cols[0].is_pk = 1;
  np_cols[0].offset = offsetof(struct NullableParent, id);

  np_cols[1].name = "belongs_to_id";
  np_cols[1].type = C_ORM_TYPE_INT32;
  np_cols[1].is_nullable = 1;
  np_cols[1].offset = offsetof(struct NullableParent, belongs_to_id);

  np_rels[0].field_name = "target_rel";
  np_rels[0].type = C_ORM_RELATION_BELONGS_TO;
  np_rels[0].local_key = "belongs_to_id";
  np_rels[0].foreign_key = "id";
  np_rels[0].target_meta = &target_meta;
  np_rels[0].data_offset = offsetof(struct NullableParent, belongs_to_child);
  np_rels[0].struct_offset = offsetof(struct NullableParent, belongs_to_child);

  np_meta.name = "parent_np";
  np_meta.columns = np_cols;
  np_meta.num_columns = 2;
  np_meta.relations = np_rels;
  np_meta.num_relations = 1;
  np_meta.struct_size = sizeof(np);
  np_meta.query_insert =
      "INSERT INTO parent_np (id, belongs_to_id) VALUES (?, ?)";
  np_meta.query_update = "UPDATE parent_np SET belongs_to_id=? WHERE id=?";

  np.id = 1;
  np.belongs_to_id = ptr_fk;

  g_step_count = 1;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_insert(&g_db, &np_meta, &np));

  g_step_count = 1;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_update(&g_db, &np_meta, &np));

  PASS();
}
TEST test_api_transactions_savepoints_and_softdelete(void) {
  struct Users u;
  struct Generic_Array arr;
  struct Generic_Array garr;
  c_orm_driver_vtable_t custom_vt;
  c_orm_db_t custom_db;
  size_t cnt;

  memset(&u, 0, sizeof(u));
  memset(&arr, 0, sizeof(arr));
  memset(&garr, 0, sizeof(garr));
  u.id = 1;
  u.username = "test";

  custom_vt = g_vt;
  custom_db = g_db;
  custom_db.vtable = &custom_vt;

  /* 1. c_orm_insert_generic finalize error */
  fflush(stdout);
  custom_vt.finalize = mock_stage_finalize;
  g_mock_err_stage = 4;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_insert_generic(&custom_db, &Users_meta, &u));

  /* 2. c_orm_get_generic finalize errors */
  fflush(stdout);
  custom_vt.step = mock_always_step_zero;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_get_generic(&custom_db, &Users_meta, 1, &u));
  custom_vt.step = mock_stage_step;
  g_stage_step_cnt = 0;
  g_stage_step_max = 1;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_get_generic(&custom_db, &Users_meta, 1, &u));

  /* 3. c_orm_find_all_generic finalize error */
  fflush(stdout);
  g_stage_step_cnt = 0;
  g_stage_step_max = 1;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_find_all_generic(&custom_db, &Users_meta, &arr.data, &cnt));

  /* 4. c_orm_get_generic_string finalize errors */
  fflush(stdout);
  custom_vt.step = mock_always_step_zero;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_get_generic_string(&custom_db, &Users_meta, "1", &u));
  custom_vt.step = mock_stage_step;
  g_stage_step_cnt = 0;
  g_stage_step_max = 1;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_get_generic_string(&custom_db, &Users_meta, "1", &u));
  g_mock_err_stage = 0;
  custom_vt.finalize = g_vt.finalize;
  custom_vt.step = g_vt.step;

  /* 5. Expire callback in c_orm_find_all_generic and c_orm_hydrate_all */
  fflush(stdout);
  {
    c_orm_table_meta_t exp_meta;
    c_orm_column_meta_t exp_cols[7];
    memcpy(&exp_meta, &Users_meta, sizeof(exp_meta));
    memcpy(exp_cols, Users_meta.columns, sizeof(exp_cols));
    exp_cols[0].type = C_ORM_TYPE_INT64;
    exp_cols[0].offset = offsetof(struct TtlTestUser, created_at);
    exp_cols[1].offset = offsetof(struct TtlTestUser, username);
    exp_cols[2].offset = offsetof(struct TtlTestUser, email);
    exp_cols[3].offset = offsetof(struct TtlTestUser, age);
    exp_cols[4].offset = offsetof(struct TtlTestUser, score);
    exp_cols[5].offset = offsetof(struct TtlTestUser, is_active);
    exp_cols[6].offset = offsetof(struct TtlTestUser, created_at_str);
    exp_meta.columns = exp_cols;
    exp_meta.struct_size = sizeof(struct TtlTestUser);
    exp_meta.has_ttl = 1;
    exp_meta.created_at_offset = offsetof(struct TtlTestUser, created_at);
    exp_meta.expires_in_offset = offsetof(struct TtlTestUser, expires_in);

    custom_vt.step = mock_stage_step;
    g_stage_step_cnt = 0;
    g_stage_step_max = 2;
    c_orm_set_expire_callback(&custom_db, test_api_expire_cb, NULL);
    ASSERT_EQ(C_ORM_OK,
              c_orm_find_all_generic(&custom_db, &exp_meta, &arr.data, &cnt));
    ASSERT_EQ(0, cnt);
    if (arr.data) {
      C_ORM_FREE(arr.data);
      arr.data = NULL;
    }

    g_stage_step_cnt = 0;
    g_stage_step_max = 2;
    ASSERT_EQ(C_ORM_OK, c_orm_find_all(&custom_db, &exp_meta, &garr));
    ASSERT_EQ(0, garr.length);
    if (garr.data) {
      C_ORM_FREE(garr.data);
      garr.data = NULL;
    }
    c_orm_set_expire_callback(&custom_db, NULL, NULL);
    custom_vt.step = g_vt.step;
  }

  /* 6. String and Int64 PK update, and update_partial */
  fflush(stdout);
  {
    c_orm_table_meta_t spk_meta;
    c_orm_column_meta_t spk_cols[2];
    struct StrPkObj sobj;
    const char *partial_fields[1];
    partial_fields[0] = "name";

    sobj.name = "test_name";
    sobj.id = "pk1";

    memset(&spk_meta, 0, sizeof(spk_meta));
    memset(spk_cols, 0, sizeof(spk_cols));
    spk_cols[0].name = "name";
    spk_cols[0].type = C_ORM_TYPE_STRING;
    spk_cols[0].offset = offsetof(struct StrPkObj, name);
    spk_cols[1].name = "id";
    spk_cols[1].type = C_ORM_TYPE_STRING;
    spk_cols[1].is_pk = 1;
    spk_cols[1].offset = offsetof(struct StrPkObj, id);
    spk_meta.name = "spk";
    spk_meta.columns = spk_cols;
    spk_meta.num_columns = 2;
    spk_meta.struct_size = sizeof(struct StrPkObj);
    spk_meta.query_update = "UPDATE spk SET name=? WHERE id=?";

    custom_vt.bind_string = mock_stage_bind_string;
    g_mock_fail_pk_bind = 1;
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_update(&custom_db, &spk_meta, &sobj));

    g_mock_fail_pk_bind = 2;
    ASSERT_EQ(
        C_ORM_ERROR_UNKNOWN,
        c_orm_update_partial(&custom_db, &spk_meta, &sobj, partial_fields, 1));
    g_mock_fail_pk_bind = 0;
    custom_vt.bind_string = g_vt.bind_string;
  }
  {
    c_orm_table_meta_t i64_meta;
    c_orm_column_meta_t i64_cols[2];
    struct Int64PkObj i64_obj;

    i64_obj.name = "test_name";
    i64_obj.id = 100;

    memset(&i64_meta, 0, sizeof(i64_meta));
    memset(i64_cols, 0, sizeof(i64_cols));
    i64_cols[0].name = "name";
    i64_cols[0].type = C_ORM_TYPE_STRING;
    i64_cols[0].offset = offsetof(struct Int64PkObj, name);
    i64_cols[1].name = "id";
    i64_cols[1].type = C_ORM_TYPE_INT64;
    i64_cols[1].is_pk = 1;
    i64_cols[1].offset = offsetof(struct Int64PkObj, id);
    i64_meta.name = "i64pk";
    i64_meta.columns = i64_cols;
    i64_meta.num_columns = 2;
    i64_meta.struct_size = sizeof(struct Int64PkObj);
    i64_meta.query_update = "UPDATE i64pk SET name=? WHERE id=?";

    custom_vt.bind_int64 = mock_stage_bind_int64;
    g_mock_fail_pk_bind = 1;
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
              c_orm_update(&custom_db, &i64_meta, &i64_obj));
    g_mock_fail_pk_bind = 0;
    custom_vt.bind_int64 = g_vt.bind_int64;
  }

  /* 7. c_orm_bind_row error branches */
  {
    struct TestBindRowObj {
      int32_t id;
      char *str;
      c_orm_blob_t blob;
      c_orm_blob_t sec_blob;
    };
    struct TestBindRowObj b_obj;
    c_orm_table_meta_t b_meta;
    c_orm_column_meta_t b_cols[4];

    memset(&b_obj, 0, sizeof(b_obj));
    b_obj.id = 1;
    b_obj.str = "hello";
    b_obj.blob.data = NULL;
    b_obj.blob.size = 0;
    b_obj.sec_blob.data = "secret";
    b_obj.sec_blob.size = 6;

    memset(&b_meta, 0, sizeof(b_meta));
    memset(b_cols, 0, sizeof(b_cols));
    b_cols[0].name = "id";
    b_cols[0].type = C_ORM_TYPE_INT32;
    b_cols[0].is_pk = 1;
    b_cols[0].offset = offsetof(struct TestBindRowObj, id);

    b_cols[1].name = "str";
    b_cols[1].type = C_ORM_TYPE_STRING;
    b_cols[1].offset = offsetof(struct TestBindRowObj, str);

    b_cols[2].name = "blob";
    b_cols[2].type = C_ORM_TYPE_BLOB;
    b_cols[2].offset = offsetof(struct TestBindRowObj, blob);

    b_cols[3].name = "sec_blob";
    b_cols[3].type = C_ORM_TYPE_BLOB;
    b_cols[3].is_secure = 1;
    b_cols[3].offset = offsetof(struct TestBindRowObj, sec_blob);

    b_meta.name = "b";
    b_meta.columns = b_cols;
    b_meta.num_columns = 4;
    b_meta.struct_size = sizeof(b_obj);
    b_meta.query_insert = "INSERT INTO b VALUES (?, ?, ?, ?)";

    /* String bind fail */
    custom_vt.bind_string = mock_always_fail_bind_string;
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&custom_db, &b_meta, &b_obj));
    custom_vt.bind_string = g_vt.bind_string;

    /* Null blob bind fail */
    custom_vt.bind_null = mock_always_fail_bind_null;
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&custom_db, &b_meta, &b_obj));
    custom_vt.bind_null = g_vt.bind_null;

    /* Encrypt hook fail */
    custom_db.encrypt_hook = mock_test_encrypt_hook_fail;
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&custom_db, &b_meta, &b_obj));

    /* Encrypted blob bind fail */
    custom_db.encrypt_hook = mock_test_encrypt_hook_ok;
    custom_vt.bind_blob = mock_always_fail_bind_blob;
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&custom_db, &b_meta, &b_obj));
    custom_db.encrypt_hook = NULL;
    custom_vt.bind_blob = g_vt.bind_blob;
  }

  /* 8. BelongsTo validation exists = 1 */
  {
    struct TestParentBelongsTo {
      int32_t id;
      int32_t req_fk;
      int32_t *opt_fk;
      void *req_rel;
      void *opt_rel;
    };
    struct TestParentBelongsTo parent_obj;
    int32_t opt_val = 20;
    c_orm_table_meta_t p_meta;
    c_orm_table_meta_t c_meta;
    c_orm_column_meta_t p_cols[3];
    c_orm_column_meta_t c_cols[1];
    c_orm_relation_meta_t rels[2];

    memset(&parent_obj, 0, sizeof(parent_obj));
    parent_obj.id = 1;
    parent_obj.req_fk = 10;
    parent_obj.opt_fk = &opt_val;

    memset(p_cols, 0, sizeof(p_cols));
    p_cols[0].name = "id";
    p_cols[0].type = C_ORM_TYPE_INT32;
    p_cols[0].is_pk = 1;
    p_cols[0].offset = offsetof(struct TestParentBelongsTo, id);

    p_cols[1].name = "req_fk";
    p_cols[1].type = C_ORM_TYPE_INT32;
    p_cols[1].is_nullable = 0;
    p_cols[1].offset = offsetof(struct TestParentBelongsTo, req_fk);

    p_cols[2].name = "opt_fk";
    p_cols[2].type = C_ORM_TYPE_INT32;
    p_cols[2].is_nullable = 1;
    p_cols[2].offset = offsetof(struct TestParentBelongsTo, opt_fk);

    memset(c_cols, 0, sizeof(c_cols));
    c_cols[0].name = "id";
    c_cols[0].type = C_ORM_TYPE_INT32;
    c_cols[0].is_pk = 1;
    c_cols[0].offset = 0;

    memset(&c_meta, 0, sizeof(c_meta));
    c_meta.name = "child";
    c_meta.columns = c_cols;
    c_meta.num_columns = 1;
    c_meta.struct_size = sizeof(int32_t);
    c_meta.query_select_by_pk = "SELECT 1 FROM child WHERE id=?";

    memset(rels, 0, sizeof(rels));
    rels[0].field_name = "req_rel";
    rels[0].type = C_ORM_RELATION_BELONGS_TO;
    rels[0].local_key = "req_fk";
    rels[0].foreign_key = "id";
    rels[0].target_meta = &c_meta;
    rels[0].struct_offset = offsetof(struct TestParentBelongsTo, req_rel);
    rels[0].data_offset = offsetof(struct TestParentBelongsTo, req_rel);

    rels[1].field_name = "opt_rel";
    rels[1].type = C_ORM_RELATION_BELONGS_TO;
    rels[1].local_key = "opt_fk";
    rels[1].foreign_key = "id";
    rels[1].target_meta = &c_meta;
    rels[1].struct_offset = offsetof(struct TestParentBelongsTo, opt_rel);
    rels[1].data_offset = offsetof(struct TestParentBelongsTo, opt_rel);

    memset(&p_meta, 0, sizeof(p_meta));
    p_meta.name = "parent";
    p_meta.columns = p_cols;
    p_meta.num_columns = 3;
    p_meta.relations = rels;
    p_meta.num_relations = 2;
    p_meta.struct_size = sizeof(parent_obj);
    p_meta.query_insert = "INSERT INTO parent VALUES (?, ?, ?)";
    p_meta.query_update = "UPDATE parent SET req_fk=?, opt_fk=? WHERE id=?";

    custom_vt.step = mock_always_step_one;
    ASSERT_EQ(C_ORM_OK, c_orm_insert(&custom_db, &p_meta, &parent_obj));
    ASSERT_EQ(C_ORM_OK, c_orm_update(&custom_db, &p_meta, &parent_obj));
    custom_vt.step = g_vt.step;
  }

  /* 9. Cascade delete target finalize fail & local key at index 1 */
  {
    c_orm_table_meta_t p_meta;
    c_orm_table_meta_t c_meta;
    c_orm_column_meta_t p_cols[2];
    c_orm_column_meta_t c_cols[1];
    c_orm_relation_meta_t rels[1];
    struct {
      char *other;
      int32_t id;
    } p_obj;

    p_obj.other = "other";
    p_obj.id = 1;

    memset(p_cols, 0, sizeof(p_cols));
    p_cols[0].name = "other";
    p_cols[0].type = C_ORM_TYPE_STRING;
    p_cols[0].offset = 0;

    p_cols[1].name = "id";
    p_cols[1].type = C_ORM_TYPE_INT32;
    p_cols[1].is_pk = 1;
    p_cols[1].offset = sizeof(char *);

    memset(c_cols, 0, sizeof(c_cols));
    c_cols[0].name = "id";
    c_cols[0].type = C_ORM_TYPE_INT32;
    c_cols[0].is_pk = 1;
    c_cols[0].offset = 0;

    memset(&c_meta, 0, sizeof(c_meta));
    c_meta.name = "c";
    c_meta.columns = c_cols;
    c_meta.num_columns = 1;
    c_meta.struct_size = sizeof(int32_t);

    memset(rels, 0, sizeof(rels));
    rels[0].field_name = "tags_m2m";
    rels[0].type = C_ORM_RELATION_MANY_TO_MANY;
    rels[0].local_key = "id";
    rels[0].foreign_key = "id";
    rels[0].join_table = "join_t";
    rels[0].join_local_key = "p_id";
    rels[0].join_foreign_key = "c_id";
    rels[0].target_meta = &c_meta;
    rels[0].on_delete = C_ORM_CASCADE_DELETE;

    memset(&p_meta, 0, sizeof(p_meta));
    p_meta.name = "p";
    p_meta.columns = p_cols;
    p_meta.num_columns = 2;
    p_meta.relations = rels;
    p_meta.num_relations = 1;
    p_meta.struct_size = sizeof(p_obj);
    p_meta.query_delete_by_pk = "DELETE FROM p WHERE id=?";

    custom_vt.finalize = mock_stage_finalize;
    g_mock_err_stage = 4;
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_delete(&custom_db, &p_meta, &p_obj));
    g_mock_err_stage = 0;
    custom_vt.finalize = g_vt.finalize;
  }

  /* 10. c_orm_find_with_relation_int32 error branches */
  {
    struct TestExtendedWithOther {
      char *other;
      int32_t id;
      int32_t belongs_to_id;
      void *child_o2o;
      struct Generic_Array children_o2m;
      struct Generic_Array tags_m2m;
    };
    struct TestExtendedWithOther p;
    c_orm_table_meta_t p_meta;
    c_orm_column_meta_t p_cols[2];
    c_orm_relation_meta_t rels[2];
    c_orm_table_meta_t c_meta;
    c_orm_column_meta_t c_cols[2];
    int k;
    c_orm_error_t rc;
    void *(*orig_m)(size_t) = c_orm_malloc;
    void *(*orig_r)(void *, size_t) = c_orm_realloc;
    void (*orig_f)(void *) = c_orm_free;

    memset(&p, 0, sizeof(p));
    memset(&p_meta, 0, sizeof(p_meta));
    memset(p_cols, 0, sizeof(p_cols));
    memset(rels, 0, sizeof(rels));
    memset(&c_meta, 0, sizeof(c_meta));
    memset(c_cols, 0, sizeof(c_cols));

    p.id = 1;
    p_cols[0].name = "other";
    p_cols[0].type = C_ORM_TYPE_STRING;
    p_cols[0].is_pk = 0;
    p_cols[0].offset = offsetof(struct TestExtendedWithOther, other);

    p_cols[1].name = "id";
    p_cols[1].type = C_ORM_TYPE_INT32;
    p_cols[1].is_pk = 1;
    p_cols[1].offset = offsetof(struct TestExtendedWithOther, id);

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
    rels[0].struct_offset = offsetof(struct TestExtendedWithOther, child_o2o);
    rels[0].data_offset = offsetof(struct TestExtendedWithOther, child_o2o);
    rels[0].lazy_ctx_offset = offsetof(struct TestExtendedWithOther, child_o2o);

    rels[1].field_name = "children_o2m";
    rels[1].type = C_ORM_RELATION_ONE_TO_MANY;
    rels[1].local_key = "id";
    rels[1].foreign_key = "parent_id";
    rels[1].target_meta = &c_meta;
    rels[1].struct_offset =
        offsetof(struct TestExtendedWithOther, children_o2m);
    rels[1].data_offset = offsetof(struct TestExtendedWithOther, children_o2m);
    rels[1].lazy_ctx_offset =
        offsetof(struct TestExtendedWithOther, children_o2m);

    p_meta.name = "p";
    p_meta.columns = p_cols;
    p_meta.num_columns = 2;
    p_meta.relations = rels;
    p_meta.num_relations = 2;
    p_meta.struct_size = sizeof(struct TestExtendedWithOther);

    /* Covers for loop without breaking on first iteration (line 994) */
    custom_vt.step = mock_always_step_one;
    rc =
        c_orm_find_with_relation_int32(&custom_db, &p_meta, 1, "child_o2o", &p);
    ASSERT_EQ(C_ORM_OK, rc);

    /* Prepare fail (line 1060) */
    custom_vt.prepare = mock_always_fail_prepare;
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
              c_orm_find_with_relation_int32(&custom_db, &p_meta, 1,
                                             "child_o2o", &p));
    custom_vt.prepare = g_vt.prepare;

    /* Lines 95, 97: non-nullable column is null */
    custom_vt.is_null = mock_is_null_nonnull_fail;
    ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH,
              c_orm_find_with_relation_int32(&custom_db, &p_meta, 1,
                                             "child_o2o", &p));
    custom_vt.is_null = g_vt.is_null;

    /* Line 326: string column val is NULL */
    custom_vt.get_string = mock_get_string_null_val;
    (void)c_orm_find_with_relation_int32(&custom_db, &p_meta, 1, "child_o2o",
                                         &p);
    custom_vt.get_string = g_vt.get_string;

    /* is_null fail in O2O and O2M (lines 1103, 1105, 1141, 1143) */
    custom_vt.step = mock_always_step_one;
    custom_vt.is_null = mock_is_null_child_fail;
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
              c_orm_find_with_relation_int32(&custom_db, &p_meta, 1,
                                             "child_o2o", &p));
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
              c_orm_find_with_relation_int32(&custom_db, &p_meta, 1,
                                             "children_o2m", &p));
    custom_vt.is_null = g_vt.is_null;
    custom_vt.step = g_vt.step;

    /* Step error inside O2M loop (lines 1175, 1178) */
    custom_vt.step = mock_step_fail_third;
    custom_vt.is_null = mock_always_zero_is_null;
    g_stage_step_cnt = 0;
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
              c_orm_find_with_relation_int32(&custom_db, &p_meta, 1,
                                             "children_o2m", &p));
    custom_vt.step = g_vt.step;
    custom_vt.is_null = g_vt.is_null;

    /* OOM in find_with_relation_int32 O2O (lines 1106-1109) and O2M (lines
     * 1147-1150) */
    custom_vt.step = mock_stage_step;
    custom_vt.is_null = mock_always_zero_is_null;
    for (k = 0; k < 6; k++) {
      g_stage_step_cnt = 0;
      g_stage_step_max = 2;
      c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
      g_deep_fail_oom = k;
      g_deep_alloc_cnt = 0;
      (void)c_orm_find_with_relation_int32(&custom_db, &p_meta, 1, "child_o2o",
                                           &p);
      c_orm_set_allocators(orig_m, orig_r, orig_f);
    }
    for (k = 0; k < 6; k++) {
      g_stage_step_cnt = 0;
      g_stage_step_max = 2;
      c_orm_set_allocators(orig_m, mock_deep_realloc, orig_f);
      g_deep_fail_realloc = k;
      g_deep_realloc_cnt = 0;
      (void)c_orm_find_with_relation_int32(&custom_db, &p_meta, 1,
                                           "children_o2m", &p);
      c_orm_set_allocators(orig_m, orig_r, orig_f);
    }
    g_deep_fail_oom = -1;
    g_deep_fail_realloc = -1;
    custom_vt.step = g_vt.step;
    custom_vt.is_null = g_vt.is_null;
  }

  /* 11. c_orm_find_all_with_relation 801 items & rows returned */
  {
    struct ExtendedParent parents[1];
    struct Generic_Array out_arr;
    c_orm_table_meta_t p_meta;
    c_orm_column_meta_t p_cols[2];
    c_orm_relation_meta_t rels[2];
    c_orm_table_meta_t c_meta;
    c_orm_column_meta_t c_cols[2];

    memset(parents, 0, sizeof(parents));
    memset(&out_arr, 0, sizeof(out_arr));
    memset(&p_meta, 0, sizeof(p_meta));
    memset(p_cols, 0, sizeof(p_cols));
    memset(rels, 0, sizeof(rels));
    memset(&c_meta, 0, sizeof(c_meta));
    memset(c_cols, 0, sizeof(c_cols));

    parents[0].id = 1;
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
    p_meta.num_columns = 1;
    p_meta.relations = rels;
    p_meta.num_relations = 2;
    p_meta.struct_size = sizeof(struct ExtendedParent);
    p_meta.query_select_all = "SELECT 1 FROM p";

    /* 801 items test for chunk_size branch */
    custom_vt.step = mock_stage_step;
    g_mock_return_801 = 1;
    g_stage_step_cnt = 0;
    (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "child_o2o",
                                       &out_arr);
    g_mock_return_801 = 0;
    if (out_arr.data) {
      free(out_arr.data);
      out_arr.data = NULL;
    }
    memset(&out_arr, 0, sizeof(out_arr));

    /* ONE_TO_ONE with rows returned */
    custom_vt.step = mock_step_parent_and_child;
    custom_vt.get_int32 = mock_stage_get_int32;
    g_stage_step_cnt = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_find_all_with_relation(&custom_db, &p_meta,
                                                     "child_o2o", &out_arr));
    if (out_arr.data) {
      free(out_arr.data);
      out_arr.data = NULL;
    }
    memset(&out_arr, 0, sizeof(out_arr));

    /* ONE_TO_MANY with rows returned */
    g_stage_step_cnt = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_find_all_with_relation(&custom_db, &p_meta,
                                                     "children_o2m", &out_arr));
    if (out_arr.data) {
      free(out_arr.data);
      out_arr.data = NULL;
    }
    memset(&out_arr, 0, sizeof(out_arr));

    /* Child query prepare error (lines 1383, 1386) */
    custom_vt.prepare = mock_prepare_fail_on_second;
    custom_vt.step = mock_step_parent_and_child;
    g_stage_step_cnt = 0;
    g_stage_prepare_cnt = 0;
    (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "child_o2o",
                                       &out_arr);
    if (out_arr.data) {
      free(out_arr.data);
      out_arr.data = NULL;
    }
    memset(&out_arr, 0, sizeof(out_arr));
    custom_vt.prepare = g_vt.prepare;

    /* Child query bind error (lines 1406-1407) */
    custom_vt.bind_int32 = mock_bind_fail_on_second;
    custom_vt.step = mock_step_parent_and_child;
    g_stage_step_cnt = 0;
    g_stage_bind_cnt = 0;
    (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "child_o2o",
                                       &out_arr);
    if (out_arr.data) {
      free(out_arr.data);
      out_arr.data = NULL;
    }
    memset(&out_arr, 0, sizeof(out_arr));
    custom_vt.bind_int32 = g_vt.bind_int32;

    /* Child query step error before while loop (line 1410) */
    custom_vt.step = mock_step_fail_on_second;
    g_stage_step_cnt = 0;
    (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "child_o2o",
                                       &out_arr);
    if (out_arr.data) {
      free(out_arr.data);
      out_arr.data = NULL;
    }
    memset(&out_arr, 0, sizeof(out_arr));

    /* Child query get_int32 error reading parent_id (lines 1437, 1439) */
    custom_vt.step = mock_step_parent_and_child;
    custom_vt.get_int32 = mock_get_fail_child_fk;
    g_stage_step_cnt = 0;
    (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "child_o2o",
                                       &out_arr);
    if (out_arr.data) {
      free(out_arr.data);
      out_arr.data = NULL;
    }
    memset(&out_arr, 0, sizeof(out_arr));

    /* Child query is_null true on first col (line 1451) */
    custom_vt.is_null = mock_is_null_child_true;
    custom_vt.get_int32 = mock_stage_get_int32;
    g_stage_step_cnt = 0;
    (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "child_o2o",
                                       &out_arr);
    if (out_arr.data) {
      free(out_arr.data);
      out_arr.data = NULL;
    }
    memset(&out_arr, 0, sizeof(out_arr));
    custom_vt.is_null = g_vt.is_null;

    /* Child query hydrate error on O2O (lines 1472-1475) */
    custom_vt.step = mock_step_parent_and_child;
    custom_vt.get_int32 = mock_get_fail_child_hydrate;
    g_stage_step_cnt = 0;
    (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "child_o2o",
                                       &out_arr);
    if (out_arr.data) {
      free(out_arr.data);
      out_arr.data = NULL;
    }
    memset(&out_arr, 0, sizeof(out_arr));

    /* Child query hydrate error on O2M (lines 1507-1509) */
    custom_vt.step = mock_step_parent_and_child;
    custom_vt.get_int32 = mock_get_fail_child_hydrate;
    g_stage_step_cnt = 0;
    (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "children_o2m",
                                       &out_arr);
    if (out_arr.data) {
      free(out_arr.data);
      out_arr.data = NULL;
    }
    memset(&out_arr, 0, sizeof(out_arr));

    /* Child query O2M loop step error (lines 1520-1522) */
    custom_vt.step = mock_step_fail_inside_loop;
    custom_vt.get_int32 = mock_stage_get_int32;
    g_stage_step_cnt = 0;
    (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "children_o2m",
                                       &out_arr);
    if (out_arr.data) {
      free(out_arr.data);
      out_arr.data = NULL;
    }
    memset(&out_arr, 0, sizeof(out_arr));

    custom_vt.step = g_vt.step;
    custom_vt.get_int32 = g_vt.get_int32;
  }

  /* 12. c_orm_load_relation_ext branches */
  {
    struct ExtendedParent p;
    c_orm_table_meta_t p_meta;
    c_orm_column_meta_t p_cols[2];
    c_orm_relation_meta_t rels[3];
    c_orm_table_meta_t c_meta;
    c_orm_column_meta_t c_cols[2];
    int k;
    void *(*orig_m)(size_t) = c_orm_malloc;
    void *(*orig_r)(void *, size_t) = c_orm_realloc;
    void (*orig_f)(void *) = c_orm_free;

    memset(&p, 0, sizeof(p));
    memset(&p_meta, 0, sizeof(p_meta));
    memset(p_cols, 0, sizeof(p_cols));
    memset(rels, 0, sizeof(rels));
    memset(&c_meta, 0, sizeof(c_meta));
    memset(c_cols, 0, sizeof(c_cols));

    p.id = 1;
    p.belongs_to_id = 1;

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
    rels[2].lazy_ctx_offset = offsetof(struct ExtendedParent, tags_m2m);

    p_meta.name = "p";
    p_meta.columns = p_cols;
    p_meta.num_columns = 2;
    p_meta.relations = rels;
    p_meta.num_relations = 3;
    p_meta.struct_size = sizeof(struct ExtendedParent);

    (void)c_orm_load_relation_ext(&custom_db, &p, &p_meta, 2, 0, 0);

    custom_vt.step = mock_always_step_zero;
    ASSERT_EQ(C_ORM_OK,
              c_orm_load_relation_ext(&custom_db, &p, &p_meta, 0, 0, 0));
    custom_vt.step = mock_always_step_fail;
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
              c_orm_load_relation_ext(&custom_db, &p, &p_meta, 0, 0, 0));

    ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
              c_orm_load_relation_ext(&custom_db, &p, &p_meta, 1, 0, 0));

    /* fetch_all returns NOT_FOUND in O2M (line 5514) */
    custom_vt.step = mock_always_step_zero;
    (void)c_orm_load_relation_ext(&custom_db, &p, &p_meta, 1, 0, 0);
    custom_vt.step = g_vt.step;

    for (k = 0; k < 6; k++) {
      c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
      g_deep_fail_oom = k;
      g_deep_alloc_cnt = 0;
      (void)c_orm_load_relation_ext(&custom_db, &p, &p_meta, 0, 0, 0);
      c_orm_set_allocators(orig_m, orig_r, orig_f);
    }
    g_deep_fail_oom = -1;
    custom_vt.step = g_vt.step;
  }

  /* 13. Nested dot path in find_with_relations and find_all_with_relations */
  {
    struct ExtendedParent p;
    struct NestedChild c_obj;
    struct Generic_Array out_arr;
    c_orm_table_meta_t p_meta;
    c_orm_column_meta_t p_cols[2];
    c_orm_relation_meta_t rels[1];
    c_orm_table_meta_t c_meta;
    c_orm_column_meta_t c_cols[1];
    c_orm_relation_meta_t sub_rels[1];
    const char *paths[2];
    c_orm_error_t rc;

    memset(&p, 0, sizeof(p));
    memset(&c_obj, 0, sizeof(c_obj));
    memset(&out_arr, 0, sizeof(out_arr));
    memset(&p_meta, 0, sizeof(p_meta));
    memset(p_cols, 0, sizeof(p_cols));
    memset(rels, 0, sizeof(rels));
    memset(&c_meta, 0, sizeof(c_meta));
    memset(c_cols, 0, sizeof(c_cols));
    memset(sub_rels, 0, sizeof(sub_rels));

    p.id = 1;
    p.child_o2o = &c_obj;

    p_cols[0].name = "id";
    p_cols[0].type = C_ORM_TYPE_INT32;
    p_cols[0].is_pk = 1;
    p_cols[0].offset = offsetof(struct ExtendedParent, id);

    c_cols[0].name = "id";
    c_cols[0].type = C_ORM_TYPE_INT32;
    c_cols[0].is_pk = 1;
    c_cols[0].offset = offsetof(struct NestedChild, id);

    sub_rels[0].field_name = "sub";
    sub_rels[0].type = C_ORM_RELATION_BELONGS_TO;
    sub_rels[0].target_meta = &c_meta;
    sub_rels[0].local_key = "id";
    sub_rels[0].foreign_key = "id";
    sub_rels[0].struct_offset = offsetof(struct NestedChild, id);
    sub_rels[0].data_offset = offsetof(struct NestedChild, id);
    sub_rels[0].lazy_ctx_offset = offsetof(struct NestedChild, id);

    c_meta.name = "c";
    c_meta.columns = c_cols;
    c_meta.num_columns = 1;
    c_meta.relations = sub_rels;
    c_meta.num_relations = 1;
    c_meta.struct_size = sizeof(struct NestedChild);
    c_meta.query_select_by_pk = "SELECT 1 FROM c WHERE id=?";

    rels[0].field_name = "child_o2o";
    rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
    rels[0].local_key = "id";
    rels[0].foreign_key = "id";
    rels[0].target_meta = &c_meta;
    rels[0].struct_offset = offsetof(struct ExtendedParent, child_o2o);
    rels[0].data_offset = offsetof(struct ExtendedParent, child_o2o);
    rels[0].lazy_ctx_offset = offsetof(struct ExtendedParent, child_o2o);

    p_meta.name = "p";
    p_meta.columns = p_cols;
    p_meta.num_columns = 1;
    p_meta.relations = rels;
    p_meta.num_relations = 1;
    p_meta.struct_size = sizeof(struct ExtendedParent);
    p_meta.query_select_by_pk = "SELECT 1 FROM p WHERE id=?";
    p_meta.query_select_all = "SELECT 1 FROM p";

    paths[0] = "child_o2o.sub";
    paths[1] = "child_o2o";

    custom_vt.step = mock_stage_step;
    g_stage_step_cnt = 0;
    g_stage_step_max = 3;
    ASSERT_EQ(C_ORM_OK, c_orm_find_with_relations_int32(&custom_db, &p_meta, 1,
                                                        paths, 2, &p));

    g_stage_step_cnt = 0;
    g_stage_step_max = 3;
    ASSERT_EQ(C_ORM_OK, c_orm_find_all_with_relations(&custom_db, &p_meta,
                                                      paths, 2, &out_arr));
    if (out_arr.data) {
      free(out_arr.data);
      out_arr.data = NULL;
    }

    /* Long relation path (lines 1675, 1776) */
    paths[0] =
        "this_is_a_very_long_relation_name_that_exceeds_sixty_four_characters_"
        "limit_in_c_orm.sub";
    custom_vt.step = mock_stage_step;
    g_stage_step_cnt = 0;
    g_stage_step_max = 1;
    (void)c_orm_find_with_relations_int32(&custom_db, &p_meta, 1, paths, 1, &p);

    g_stage_step_cnt = 0;
    g_stage_step_max = 1;
    rc = c_orm_find_all_with_relations(&custom_db, &p_meta, paths, 1, &out_arr);
    (void)rc;
    if (out_arr.data) {
      C_ORM_FREE(out_arr.data);
      out_arr.data = NULL;
    }
    memset(&out_arr, 0, sizeof(out_arr));

    /* Nonexistent relation (line 1685) */
    paths[0] = "nonexistent_relation";
    g_stage_step_cnt = 0;
    g_stage_step_max = 1;
    (void)c_orm_find_with_relations_int32(&custom_db, &p_meta, 1, paths, 1, &p);

    /* Nonexistent sub-relation in O2M loop (line 1713) */
    {
      struct FullParentObj p_o2m;
      struct NestedChild *c_arr_item;
      c_orm_relation_meta_t o2m_rels[1];
      memset(&p_o2m, 0, sizeof(p_o2m));
      memset(o2m_rels, 0, sizeof(o2m_rels));
      c_arr_item =
          (struct NestedChild *)c_orm_malloc(sizeof(struct NestedChild));
      if (c_arr_item) {
        memset(c_arr_item, 0, sizeof(struct NestedChild));
        p_o2m.id = 1;
        p_o2m.children_o2m.data = c_arr_item;
        p_o2m.children_o2m.length = 1;

        o2m_rels[0].field_name = "children_o2m";
        o2m_rels[0].type = C_ORM_RELATION_ONE_TO_MANY;
        o2m_rels[0].local_key = "id";
        o2m_rels[0].foreign_key = "id";
        o2m_rels[0].target_meta = &c_meta;
        o2m_rels[0].struct_offset =
            offsetof(struct FullParentObj, children_o2m);
        o2m_rels[0].data_offset = offsetof(struct FullParentObj, children_o2m);
        o2m_rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2m_ctx);
        p_meta.relations = o2m_rels;

        paths[0] = "children_o2m.nonexistent_sub";
        custom_vt.step = mock_stage_step;
        g_stage_step_cnt = 0;
        g_stage_step_max = 2;
        (void)c_orm_find_with_relations_int32(&custom_db, &p_meta, 1, paths, 1,
                                              &p_o2m);
        g_stage_step_cnt = 0;
        g_stage_step_max = 2;
        (void)c_orm_find_all_with_relations(&custom_db, &p_meta, paths, 1,
                                            &out_arr);
        if (out_arr.data) {
          C_ORM_FREE(out_arr.data);
          out_arr.data = NULL;
        }
        custom_vt.step = g_vt.step;
        p_meta.relations = rels;
      }
    }
    custom_vt.step = g_vt.step;
  }

  /* 14. OOM polygon, scatter-gather, and identity map */
  {
    c_orm_table_meta_t poly_meta;
    c_orm_column_meta_t poly_cols[2];
    struct {
      int32_t id;
      c_orm_polygon_t poly;
    } poly_obj;
    c_orm_point_t pts[3];
    c_orm_shard_manager_t *sm = NULL;
    c_orm_identity_map_t map;
    void *out_scat = NULL;
    size_t out_cnt = 0;
    void *out_id_obj = NULL;
    int k;
    void *(*orig_m)(size_t) = c_orm_malloc;
    void *(*orig_r)(void *, size_t) = c_orm_realloc;
    void (*orig_f)(void *) = c_orm_free;

    memset(&poly_obj, 0, sizeof(poly_obj));
    poly_obj.id = 1;
    poly_obj.poly.num_points = 3;
    poly_obj.poly.points = pts;

    memset(&poly_meta, 0, sizeof(poly_meta));
    memset(poly_cols, 0, sizeof(poly_cols));
    poly_cols[0].name = "id";
    poly_cols[0].type = C_ORM_TYPE_INT32;
    poly_cols[0].is_pk = 1;
    poly_cols[0].offset = 0;
    poly_cols[1].name = "poly";
    poly_cols[1].type = C_ORM_TYPE_POLYGON;
    poly_cols[1].offset = sizeof(int32_t);
    poly_meta.name = "poly_t";
    poly_meta.columns = poly_cols;
    poly_meta.num_columns = 2;
    poly_meta.struct_size = sizeof(poly_obj);
    poly_meta.query_insert = "INSERT INTO poly_t VALUES (?, ?)";

    (void)c_orm_prepare_cached(&custom_db, poly_meta.query_insert, NULL);

    c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
    g_deep_fail_oom = 0;
    g_deep_alloc_cnt = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              c_orm_insert(&custom_db, &poly_meta, &poly_obj));
    c_orm_set_allocators(orig_m, orig_r, orig_f);
    g_deep_fail_oom = -1;

    /* Scatter gather OOM */
    ASSERT_EQ(C_ORM_OK, c_orm_shard_manager_init(1, &sm));
    ASSERT_EQ(C_ORM_OK, c_orm_shard_manager_add_node(sm, 0, &custom_db));
    c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
    g_deep_fail_oom = 0;
    g_deep_alloc_cnt = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_scatter_gather_generic(
                                      sm, &Users_meta, &out_scat, &out_cnt));
    c_orm_set_allocators(orig_m, orig_r, orig_f);
    g_deep_fail_oom = -1;
    c_orm_shard_manager_free(sm);

    /* Identity map OOM & collision loop (line 5094) */
    memset(&map, 0, sizeof(map));
    for (k = 0; k < 100; k++) {
      char kbuf[16];
      C_ORM_SPRINTF(kbuf, sizeof(kbuf), "key_%d", k);
      (void)c_orm_identity_map_get_or_set_str(&map, &Users_meta, kbuf, &u,
                                              &out_id_obj);
    }
    c_orm_identity_map_free(&map);

    for (k = 0; k < 4; k++) {
      memset(&map, 0, sizeof(map));
      c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
      g_deep_fail_oom = k;
      g_deep_alloc_cnt = 0;
      (void)c_orm_identity_map_get_or_set_int(&map, &Users_meta, 1, &u,
                                              &out_id_obj);
      c_orm_set_allocators(orig_m, orig_r, orig_f);
      c_orm_identity_map_free(&map);
    }
    for (k = 0; k < 5; k++) {
      memset(&map, 0, sizeof(map));
      c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
      g_deep_fail_oom = k;
      g_deep_alloc_cnt = 0;
      (void)c_orm_identity_map_get_or_set_str(&map, &Users_meta, "1", &u,
                                              &out_id_obj);
      c_orm_set_allocators(orig_m, orig_r, orig_f);
      c_orm_identity_map_free(&map);
    }
    g_deep_fail_oom = -1;

    /* Realloc failures in batch operations */
    c_orm_set_allocators(orig_m, mock_deep_realloc, orig_f);
    g_deep_fail_realloc = 0;
    g_deep_realloc_cnt = 0;
    (void)c_orm_insert_batch_ext(&custom_db, &Users_meta, &u, 1, 0,
                                 C_ORM_ON_CONFLICT_DO_NOTHING, NULL, NULL);
    g_deep_fail_realloc = 0;
    g_deep_realloc_cnt = 0;
    (void)c_orm_delete_batch(&custom_db, &Users_meta, &u, 1, 0);
    g_deep_fail_realloc = 0;
    g_deep_realloc_cnt = 0;
    (void)c_orm_update_batch(&custom_db, &Users_meta, &u, 1, 0);
    g_deep_fail_realloc = 0;
    g_deep_realloc_cnt = 0;
    (void)c_orm_insert_generic(&custom_db, &Users_meta, &u);
    g_deep_fail_realloc = 0;
    g_deep_realloc_cnt = 0;
    (void)c_orm_find_all_generic(&custom_db, &Users_meta, &arr.data, &cnt);
    c_orm_set_allocators(orig_m, orig_r, orig_f);
    g_deep_fail_realloc = -1;

    /* Batch step failure (lines 2403, 2405) */
    custom_vt.step = mock_step_begin_ok_then_fail;
    g_stage_step_cnt = 0;
    (void)c_orm_insert_batch_ext(&custom_db, &Users_meta, &u, 1, 0,
                                 C_ORM_ON_CONFLICT_DO_NOTHING, NULL, NULL);
    g_stage_step_cnt = 0;
    (void)c_orm_update_batch(&custom_db, &Users_meta, &u, 1, 0);
    custom_vt.step = g_vt.step;

    /* c_orm_hydrate_cache_row NULL args (lines 5191, 5193) */
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              c_orm_hydrate_cache_row(NULL, NULL, NULL, NULL));

    /* int64 PK in delete_batch (lines 3201, 3203) */
    {
      struct Int64PkObj i64_item;
      c_orm_table_meta_t i64_dmeta;
      c_orm_column_meta_t i64_dcol;
      memset(&i64_item, 0, sizeof(i64_item));
      i64_item.id = 1;
      memset(&i64_dmeta, 0, sizeof(i64_dmeta));
      memset(&i64_dcol, 0, sizeof(i64_dcol));
      i64_dcol.name = "id";
      i64_dcol.type = C_ORM_TYPE_INT64;
      i64_dcol.is_pk = 1;
      i64_dcol.offset = offsetof(struct Int64PkObj, id);
      i64_dmeta.name = "i64d";
      i64_dmeta.columns = &i64_dcol;
      i64_dmeta.num_columns = 1;
      i64_dmeta.struct_size = sizeof(struct Int64PkObj);
      (void)c_orm_delete_batch(&custom_db, &i64_dmeta, &i64_item, 1, 0);
    }

    /* FLOAT PK in update_batch (line 3392) */
    {
      struct {
        float id;
        int32_t val;
      } flt_item;
      c_orm_table_meta_t flt_meta;
      c_orm_column_meta_t flt_cols[2];
      memset(&flt_item, 0, sizeof(flt_item));
      memset(&flt_meta, 0, sizeof(flt_meta));
      memset(flt_cols, 0, sizeof(flt_cols));
      flt_cols[0].name = "id";
      flt_cols[0].type = C_ORM_TYPE_FLOAT;
      flt_cols[0].is_pk = 1;
      flt_cols[0].offset = 0;
      flt_cols[1].name = "val";
      flt_cols[1].type = C_ORM_TYPE_INT32;
      flt_cols[1].offset = sizeof(float);
      flt_meta.name = "fltm";
      flt_meta.columns = flt_cols;
      flt_meta.num_columns = 2;
      flt_meta.struct_size = sizeof(flt_item);
      (void)c_orm_update_batch(&custom_db, &flt_meta, &flt_item, 1, 0);
    }

    /* AFTER_SAVE hook failure in update_batch (line 3452) */
    {
      c_orm_table_meta_t h_meta;
      memcpy(&h_meta, &Users_meta, sizeof(h_meta));
      h_meta.hooks[C_ORM_HOOK_AFTER_SAVE] = dummy_failing_hook;
      (void)c_orm_update_batch(&custom_db, &h_meta, &u, 1, 0);
    }

    /* find_one_by_string OOM (lines 4201, 4204, 4210, 4213) */
    for (k = 0; k < 6; k++) {
      c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
      g_deep_fail_oom = k;
      g_deep_alloc_cnt = 0;
      (void)c_orm_find_one_by_string(&custom_db, &Users_meta, "username", "val",
                                     &u);
      c_orm_set_allocators(orig_m, orig_r, orig_f);
    }
    g_deep_fail_oom = -1;

    for (k = 0; k < 6; k++) {
      c_orm_set_allocators(orig_m, mock_deep_realloc, orig_f);
      g_deep_fail_realloc = k;
      g_deep_realloc_cnt = 0;
      (void)c_orm_find_one_by_string(&custom_db, &Users_meta, "username", "val",
                                     &u);
      c_orm_set_allocators(orig_m, orig_r, orig_f);
    }
    g_deep_fail_realloc = -1;

    /* Secure string bind_blob failure (line 2087) & non-secure blob (line 2136)
     */
    {
      struct TestSecureStringBlobObj {
        int32_t id;
        char *str;
        c_orm_blob_t blob;
      };
      struct TestSecureStringBlobObj sb_obj;
      c_orm_table_meta_t sb_meta;
      c_orm_column_meta_t sb_cols[3];

      memset(&sb_obj, 0, sizeof(sb_obj));
      sb_obj.id = 1;
      sb_obj.str = "hello";
      sb_obj.blob.data = "world";
      sb_obj.blob.size = 5;

      memset(&sb_meta, 0, sizeof(sb_meta));
      memset(sb_cols, 0, sizeof(sb_cols));
      sb_cols[0].name = "id";
      sb_cols[0].type = C_ORM_TYPE_INT32;
      sb_cols[0].is_pk = 1;
      sb_cols[0].offset = offsetof(struct TestSecureStringBlobObj, id);
      sb_cols[1].name = "str";
      sb_cols[1].type = C_ORM_TYPE_STRING;
      sb_cols[1].is_secure = 1;
      sb_cols[1].offset = offsetof(struct TestSecureStringBlobObj, str);
      sb_cols[2].name = "blob";
      sb_cols[2].type = C_ORM_TYPE_BLOB;
      sb_cols[2].offset = offsetof(struct TestSecureStringBlobObj, blob);

      sb_meta.name = "sb";
      sb_meta.columns = sb_cols;
      sb_meta.num_columns = 3;
      sb_meta.struct_size = sizeof(sb_obj);
      sb_meta.query_insert = "INSERT INTO sb VALUES (?, ?, ?)";

      custom_db.encrypt_hook = mock_test_encrypt_hook_ok;
      custom_vt.bind_blob = mock_always_fail_bind_blob;
      (void)c_orm_insert(&custom_db, &sb_meta, &sb_obj);

      custom_db.encrypt_hook = NULL;
      (void)c_orm_insert(&custom_db, &sb_meta, &sb_obj);
      custom_vt.bind_blob = g_vt.bind_blob;
    }
  }

  PASS();
}

TEST test_api_driver_edge_cases(void) {
  struct Users u;
  struct Generic_Array out_arr;
  c_orm_driver_vtable_t custom_vt;
  c_orm_db_t custom_db;
  c_orm_table_meta_t p_meta;
  c_orm_column_meta_t p_cols[2];
  c_orm_relation_meta_t rels[2];
  c_orm_table_meta_t c_meta;
  c_orm_column_meta_t c_cols[2];
  int k;
  void *(*orig_m)(size_t) = c_orm_malloc;
  void *(*orig_r)(void *, size_t) = c_orm_realloc;
  void (*orig_f)(void *) = c_orm_free;

  memset(&u, 0, sizeof(u));
  memset(&out_arr, 0, sizeof(out_arr));
  memset(&p_meta, 0, sizeof(p_meta));
  memset(p_cols, 0, sizeof(p_cols));
  memset(rels, 0, sizeof(rels));
  memset(&c_meta, 0, sizeof(c_meta));
  memset(c_cols, 0, sizeof(c_cols));

  u.id = 1;
  u.username = "test";

  custom_vt = g_vt;
  custom_db = g_db;
  custom_db.vtable = &custom_vt;

  /* 1. Line 628: c_orm_hydrate_row cache error */
  {
    struct {
      int32_t id;
      int32_t age;
    } i_obj;
    c_orm_table_meta_t i_meta;
    c_orm_column_meta_t i_cols[2];
    c_orm_identity_map_t map;
    c_orm_db_t map_db;

    memset(&i_obj, 0, sizeof(i_obj));
    i_obj.id = 1;
    memset(&i_meta, 0, sizeof(i_meta));
    memset(i_cols, 0, sizeof(i_cols));
    i_cols[0].name = "id";
    i_cols[0].type = C_ORM_TYPE_INT32;
    i_cols[0].is_pk = 1;
    i_cols[0].offset = 0;
    i_cols[1].name = "age";
    i_cols[1].type = C_ORM_TYPE_INT32;
    i_cols[1].offset = sizeof(int32_t);
    i_meta.name = "int_table";
    i_meta.columns = i_cols;
    i_meta.num_columns = 2;
    i_meta.struct_size = sizeof(i_obj);

    memcpy(&map_db, &custom_db, sizeof(map_db));
    memset(&map, 0, sizeof(map));
    map_db.identity_map = &map;

    c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
    g_deep_fail_oom = 0;
    g_deep_alloc_cnt = 0;
    (void)c_orm_hydrate_row(&map_db, (c_orm_query_t *)1, &i_meta, &i_obj);
    c_orm_set_allocators(orig_m, orig_r, orig_f);
    g_deep_fail_oom = -1;
    c_orm_identity_map_free(&map);
  }

  /* Set up p_meta and c_meta for relations */
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
  p_meta.num_columns = 1;
  p_meta.relations = rels;
  p_meta.num_relations = 2;
  p_meta.struct_size = sizeof(struct ExtendedParent);
  p_meta.query_select_all = "SELECT 1 FROM p";

  /* 2. Line 1381-1382: string builder get fail in find_all_with_relation */
  c_orm_set_allocators(orig_m, mock_deep_realloc, orig_f);
  g_deep_fail_realloc = 0;
  g_deep_realloc_cnt = 0;
  custom_vt.step = mock_step_parent_and_child;
  g_stage_step_cnt = 0;
  (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "child_o2o",
                                     &out_arr);
  c_orm_set_allocators(orig_m, orig_r, orig_f);
  g_deep_fail_realloc = -1;
  if (out_arr.data) {
    free(out_arr.data);
    out_arr.data = NULL;
  }
  memset(&out_arr, 0, sizeof(out_arr));

  /* 3. Line 1427: get_int32 error in M2M child query */
  rels[0].type = C_ORM_RELATION_MANY_TO_MANY;
  rels[0].join_table = "join_t";
  rels[0].join_local_key = "p_id";
  rels[0].join_foreign_key = "c_id";
  custom_vt.step = mock_step_parent_and_child;
  custom_vt.get_int32 = mock_get_fail_child_fk;
  g_stage_step_cnt = 0;
  (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "child_o2o",
                                     &out_arr);
  if (out_arr.data) {
    free(out_arr.data);
    out_arr.data = NULL;
  }
  memset(&out_arr, 0, sizeof(out_arr));
  rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
  custom_vt.get_int32 = g_vt.get_int32;
  custom_vt.step = g_vt.step;

  /* 4. Lines 1441, 1443: foreign_key not found in target_meta */
  rels[0].foreign_key = "nonexistent_fk";
  custom_vt.step = mock_step_parent_and_child;
  g_stage_step_cnt = 0;
  (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "child_o2o",
                                     &out_arr);
  if (out_arr.data) {
    free(out_arr.data);
    out_arr.data = NULL;
  }
  memset(&out_arr, 0, sizeof(out_arr));
  rels[0].foreign_key = "parent_id";
  custom_vt.step = g_vt.step;

  /* 5. Line 1455: parent_id does not match parent in list */
  custom_vt.step = mock_step_parent_and_child;
  custom_vt.get_int32 = mock_stage_get_int32_999;
  g_stage_step_cnt = 0;
  (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "child_o2o",
                                     &out_arr);
  if (out_arr.data) {
    free(out_arr.data);
    out_arr.data = NULL;
  }
  memset(&out_arr, 0, sizeof(out_arr));
  custom_vt.get_int32 = g_vt.get_int32;
  custom_vt.step = g_vt.step;

  /* Line 1451: parent matching on second item in list */
  {
    struct ExtendedParent parents_2[2];
    memset(parents_2, 0, sizeof(parents_2));
    parents_2[0].id = 1;
    parents_2[1].id = 2;
    custom_vt.step = mock_step_parent2_and_child2;
    custom_vt.get_int32 = mock_stage_get_int32_2;
    g_stage_step_cnt = 0;
    (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "child_o2o",
                                       &out_arr);
    if (out_arr.data) {
      free(out_arr.data);
      out_arr.data = NULL;
    }
    memset(&out_arr, 0, sizeof(out_arr));
    custom_vt.get_int32 = g_vt.get_int32;
    custom_vt.step = g_vt.step;
  }

  /* Child hydrate error on O2O and O2M (lines 1476-1479, 1511-1513) */
  custom_vt.step = mock_step_parent_and_child;
  custom_vt.get_int32 = mock_get_fail_child_hydrate;
  g_stage_step_cnt = 0;
  (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "child_o2o",
                                     &out_arr);
  if (out_arr.data) {
    free(out_arr.data);
    out_arr.data = NULL;
  }
  memset(&out_arr, 0, sizeof(out_arr));

  g_stage_step_cnt = 0;
  (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "children_o2m",
                                     &out_arr);
  if (out_arr.data) {
    free(out_arr.data);
    out_arr.data = NULL;
  }
  memset(&out_arr, 0, sizeof(out_arr));
  custom_vt.get_int32 = g_vt.get_int32;
  custom_vt.step = g_vt.step;

  /* 6. Lines 1467-1469 (O2O malloc fail) and lines 1495-1497 (O2M realloc fail)
   */
  custom_vt.step = mock_step_parent_and_child;
  custom_vt.get_int32 = mock_stage_get_int32;
  for (k = 0; k < 6; k++) {
    c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
    g_deep_fail_oom = k;
    g_deep_alloc_cnt = 0;
    g_stage_step_cnt = 0;
    (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "child_o2o",
                                       &out_arr);
    c_orm_set_allocators(orig_m, orig_r, orig_f);
    if (out_arr.data) {
      free(out_arr.data);
      out_arr.data = NULL;
    }
    memset(&out_arr, 0, sizeof(out_arr));
  }
  for (k = 0; k < 6; k++) {
    c_orm_set_allocators(orig_m, mock_deep_realloc, orig_f);
    g_deep_fail_realloc = k;
    g_deep_realloc_cnt = 0;
    g_stage_step_cnt = 0;
    (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "children_o2m",
                                       &out_arr);
    c_orm_set_allocators(orig_m, orig_r, orig_f);
    if (out_arr.data) {
      free(out_arr.data);
      out_arr.data = NULL;
    }
    memset(&out_arr, 0, sizeof(out_arr));
  }
  g_deep_fail_oom = -1;
  g_deep_fail_realloc = -1;
  custom_vt.step = g_vt.step;
  custom_vt.get_int32 = g_vt.get_int32;

  /* 7. Lines 1717, 1780, 1812-1818: nested lazy load errors */
  {
    const char *paths[1];
    paths[0] = "nonexistent";
    custom_vt.step = mock_stage_step;
    g_stage_step_cnt = 0;
    g_stage_step_max = 1;
    (void)c_orm_find_all_with_relations(&custom_db, &p_meta, paths, 1,
                                        &out_arr);
    if (out_arr.data) {
      free(out_arr.data);
      out_arr.data = NULL;
    }
    memset(&out_arr, 0, sizeof(out_arr));

    paths[0] = "children_o2m.nonexistent";
    g_stage_step_cnt = 0;
    g_stage_step_max = 1;
    (void)c_orm_find_all_with_relations(&custom_db, &p_meta, paths, 1,
                                        &out_arr);
    if (out_arr.data) {
      free(out_arr.data);
      out_arr.data = NULL;
    }
    memset(&out_arr, 0, sizeof(out_arr));
    custom_vt.step = g_vt.step;
  }

  /* 8. Line 2873: cascade update failure */
  {
    struct ExtendedParent ep;
    struct NestedChild nc;
    memset(&ep, 0, sizeof(ep));
    memset(&nc, 0, sizeof(nc));
    ep.id = 1;
    ep.child_o2o = &nc;
    nc.id = 1;
    p_meta.query_update = "UPDATE p SET belongs_to_id=? WHERE id=?";
    c_meta.query_update = "UPDATE c SET parent_id=? WHERE id=?";

    rels[0].type = C_ORM_RELATION_BELONGS_TO;
    rels[0].on_update = C_ORM_CASCADE_UPDATE;
    custom_vt.step = mock_always_step_fail;
    (void)c_orm_update(&custom_db, &p_meta, &ep);
    rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
    rels[0].on_update = C_ORM_CASCADE_NONE;
    custom_vt.step = g_vt.step;
  }

  /* 9. Line 3205, 3207: step fail in delete_batch */
  custom_vt.step = mock_step_begin_ok_then_fail;
  g_stage_step_cnt = 0;
  (void)c_orm_delete_batch(&custom_db, &Users_meta, &u, 1, 0);
  custom_vt.step = g_vt.step;

  /* 10. Line 3410: bind error in update_batch */
  custom_vt.bind_string = mock_always_fail_bind_string;
  (void)c_orm_update_batch(&custom_db, &Users_meta, &u, 1, 0);
  custom_vt.bind_string = g_vt.bind_string;

  /* Line 3456: AFTER_SAVE hook failure in update_batch */
  {
    c_orm_table_meta_t h_meta;
    memcpy(&h_meta, &Users_meta, sizeof(h_meta));
    h_meta.hooks[C_ORM_HOOK_AFTER_SAVE] = dummy_failing_hook;
    custom_vt.step = mock_always_step_zero;
    (void)c_orm_update_batch(&custom_db, &h_meta, &u, 1, 0);
    custom_vt.step = g_vt.step;
  }

  /* 11. Line 3569, 3626, 3670: cascade delete finalize failures */
  {
    struct ExtendedParent ep;
    memset(&ep, 0, sizeof(ep));
    ep.id = 1;
    p_meta.query_delete_by_pk = "DELETE FROM p WHERE id=?";

    /* Line 3557: O2O cascade delete finalize fail */
    rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
    rels[0].on_delete = C_ORM_CASCADE_DELETE;
    custom_vt.finalize = mock_stage_finalize;
    g_mock_err_stage = 4;
    (void)c_orm_delete(&custom_db, &p_meta, &ep);
    rels[0].on_delete = C_ORM_CASCADE_NONE;

    /* Line 3614: M2M target cascade delete finalize fail */
    rels[0].type = C_ORM_RELATION_MANY_TO_MANY;
    rels[0].join_table = "join_t";
    rels[0].join_local_key = "p_id";
    rels[0].join_foreign_key = "c_id";
    rels[0].on_delete = C_ORM_CASCADE_DELETE;
    (void)c_orm_delete(&custom_db, &p_meta, &ep);

    /* Line 3625: M2M join table cascade delete finalize fail */
    rels[0].on_delete = C_ORM_CASCADE_NONE;
    (void)c_orm_delete(&custom_db, &p_meta, &ep);
    rels[0].type = C_ORM_RELATION_ONE_TO_ONE;

    /* Line 3670: delete without PK finalize fail */
    p_cols[0].is_pk = 0;
    p_cols[1].is_pk = 0;
    (void)c_orm_delete(&custom_db, &p_meta, &ep);
    p_cols[0].is_pk = 1;

    g_mock_err_stage = 0;
    custom_vt.finalize = g_vt.finalize;
  }

  /* 12. Line 5512: O2M fetch_all success in load_relation_ext */
  {
    struct ExtendedParent ep;
    memset(&ep, 0, sizeof(ep));
    ep.id = 1;
    custom_vt.step = mock_stage_step;
    g_stage_step_cnt = 0;
    g_stage_step_max = 1;
    (void)c_orm_load_relation_ext(&custom_db, &ep, &p_meta, 1, 0, 0);
    custom_vt.step = g_vt.step;
  }

  /* 13. Lines 6654, 6743 in sync */
  {
    struct ExtendedParent ep;
    struct NestedChild c_items[2];
    memset(&ep, 0, sizeof(ep));
    memset(c_items, 0, sizeof(c_items));
    ep.id = 1;
    c_items[0].id = 1;
    c_items[1].id = 2;

    /* Line 6654: c_orm_save fails in O2M sync */
    custom_vt.step = mock_step_begin_ok_then_fail;
    g_stage_step_cnt = 0;
    (void)c_orm_sync(&custom_db, &p_meta, &ep, "children_o2m", c_items, 2);

    /* Line 6708: finalize fails on insert links */
    rels[0].type = C_ORM_RELATION_MANY_TO_MANY;
    rels[0].join_table = "join_t";
    rels[0].join_local_key = "p_id";
    rels[0].join_foreign_key = "c_id";
    custom_vt.step = mock_always_step_zero;
    custom_vt.finalize = mock_stage_finalize;
    g_mock_err_stage = 11;
    g_stage_finalize_cnt = 0;
    (void)c_orm_sync(&custom_db, &p_meta, &ep, "child_o2o", c_items, 2);
    g_mock_err_stage = 0;

    /* Line 6731: finalize fails in M2M sync delete links */
    g_mock_err_stage = 4;
    (void)c_orm_sync(&custom_db, &p_meta, &ep, "child_o2o", c_items, 2);
    g_mock_err_stage = 0;
    rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
    custom_vt.finalize = g_vt.finalize;
    custom_vt.step = g_vt.step;
  }

  /* Lines 7058-7063: find_all_generic realloc OOM */
  {
    void *g_data = NULL;
    size_t g_cnt = 0;
    for (k = 0; k < 4; k++) {
      custom_vt.step = mock_stage_step;
      g_stage_step_cnt = 0;
      g_stage_step_max = 20;
      c_orm_set_allocators(orig_m, mock_deep_realloc, orig_f);
      g_deep_fail_realloc = k;
      g_deep_realloc_cnt = 0;
      (void)c_orm_find_all_generic(&custom_db, &Users_meta, &g_data, &g_cnt);
      c_orm_set_allocators(orig_m, orig_r, orig_f);
      if (g_data) {
        free(g_data);
        g_data = NULL;
      }
    }
    g_deep_fail_realloc = -1;
    custom_vt.step = g_vt.step;
  }

  /* Lines 5864-5870: scatter_gather_generic realloc OOM */
  {
    c_orm_shard_manager_t *sm = NULL;
    void *scat_data = NULL;
    size_t scat_cnt = 0;
    c_orm_shard_manager_init(1, &sm);
    c_orm_shard_manager_add_node(sm, 0, &custom_db);
    for (k = 0; k < 4; k++) {
      custom_vt.step = mock_stage_step;
      g_stage_step_cnt = 0;
      g_stage_step_max = 20;
      c_orm_set_allocators(orig_m, mock_deep_realloc, orig_f);
      g_deep_fail_realloc = k;
      g_deep_realloc_cnt = 0;
      (void)c_orm_scatter_gather_generic(sm, &Users_meta, &scat_data,
                                         &scat_cnt);
      c_orm_set_allocators(orig_m, orig_r, orig_f);
      if (scat_data) {
        free(scat_data);
        scat_data = NULL;
      }
    }
    g_deep_fail_realloc = -1;
    c_orm_shard_manager_free(sm);
    custom_vt.step = g_vt.step;
  }

  /* Line 4940: bucket OOM */
  {
    c_orm_identity_map_t map;
    void *out_id_obj = NULL;
    memset(&map, 0, sizeof(map));
    c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
    g_deep_fail_oom = 1;
    g_deep_alloc_cnt = 0;
    (void)c_orm_identity_map_get_or_set_int(&map, &Users_meta, 1, &u,
                                            &out_id_obj);
    c_orm_set_allocators(orig_m, orig_r, orig_f);
    g_deep_fail_oom = -1;
    c_orm_identity_map_free(&map);
  }

  PASS();
}
/* Tests bringing src/c_orm_api.c to 100% function, line, and branch coverage */

struct GrandChildObj {
  int32_t id;
  struct GrandChildObj *child_o2o;
  c_orm_lazy_load_context_t o2o_ctx;
};

static int g_step_countdown_100 = 0;
static c_orm_error_t mock_step_countdown_100(c_orm_query_t *q, int *has_row) {
  (void)q;
  if (!has_row)
    return C_ORM_ERROR_UNKNOWN;
  if (g_step_countdown_100 > 0) {
    *has_row = 1;
    g_step_countdown_100--;
  } else {
    *has_row = 0;
  }
  return C_ORM_OK;
}

static int g_step_pattern_idx_100 = 0;
static const int g_step_pattern_vals_100[6] = {1, 0, 1, 0, 0, 0};
static c_orm_error_t mock_step_pattern_100(c_orm_query_t *q, int *has_row) {
  (void)q;
  if (!has_row)
    return C_ORM_ERROR_UNKNOWN;
  if (g_step_pattern_idx_100 < 6) {
    *has_row = g_step_pattern_vals_100[g_step_pattern_idx_100++];
  } else {
    *has_row = 0;
  }
  return C_ORM_OK;
}

static c_orm_error_t mock_get_int32_one_100(c_orm_query_t *q, int i,
                                            int32_t *o) {
  (void)q;
  (void)i;
  if (o)
    *o = 1;
  return C_ORM_OK;
}

TEST test_api_transactions_and_error_injection(void) {
  c_orm_driver_vtable_t custom_vt;
  c_orm_db_t custom_db;
  void *(*orig_m)(size_t);
  void *(*orig_r)(void *, size_t);
  void (*orig_f)(void *);

  orig_m = c_orm_malloc;
  orig_r = c_orm_realloc;
  orig_f = c_orm_free;
  (void)orig_m;
  (void)orig_r;
  (void)orig_f;

  custom_vt = g_vt;
  custom_db = g_db;
  custom_db.vtable = &custom_vt;

  /* 1. Missing lines 3433: update_batch with failing hooks */
  {
    struct Users u_batch[2];
    c_orm_table_meta_t u_meta;
    memset(u_batch, 0, sizeof(u_batch));
    u_batch[0].id = 1;
    u_batch[0].username = "u1";
    u_batch[0].email = "u1@e.com";
    u_batch[1].id = 2;
    u_batch[1].username = "u2";
    u_batch[1].email = "u2@e.com";
    u_meta = Users_meta;

    u_meta.hooks[C_ORM_HOOK_AFTER_UPDATE] = dummy_lifecycle_hook;
    u_meta.hooks[C_ORM_HOOK_AFTER_SAVE] = dummy_lifecycle_hook;
    (void)c_orm_update_batch(&custom_db, &u_meta, u_batch, 2, 0);

    u_meta.hooks[C_ORM_HOOK_AFTER_UPDATE] = dummy_failing_hook;
    (void)c_orm_update_batch(&custom_db, &u_meta, u_batch, 2, 0);

    u_meta.hooks[C_ORM_HOOK_AFTER_UPDATE] = NULL;
    u_meta.hooks[C_ORM_HOOK_AFTER_SAVE] = dummy_failing_hook;
    (void)c_orm_update_batch(&custom_db, &u_meta, u_batch, 2, 0);
  }

  /* 2. Line 1767: find_all_with_relations with long relation prefix (>64 chars)
   */
  {
    struct Generic_Array out_arr;
    char long_path[128];
    const char *paths[1];
    struct Users *items;
    size_t idx;

    memset(&out_arr, 0, sizeof(out_arr));
    memset(long_path, 'a', 80);
    long_path[80] = '.';
    long_path[81] = 'b';
    long_path[82] = '\0';
    paths[0] = long_path;

    custom_vt.step = mock_step_sequence;
    g_step_count = 1;
    (void)c_orm_find_all_with_relations(&custom_db, &Users_meta, paths, 1,
                                        &out_arr);
    items = (struct Users *)out_arr.data;
    if (items) {
      for (idx = 0; idx < out_arr.length; idx++) {
        c_orm_free_columns(&Users_meta, &items[idx]);
      }
      C_ORM_FREE(out_arr.data);
    }
  }

  /* 3. Line 1704: find_with_relations_int32 with nested path failing lazy_load
   */
  {
    struct FullParentObj p;
    c_orm_table_meta_t p_meta;
    c_orm_column_meta_t p_cols[2];
    c_orm_relation_meta_t rels[2];
    struct NestedChild child;
    c_orm_table_meta_t c_meta;
    c_orm_column_meta_t c_cols[2];
    const char *paths[1];

    memset(&p, 0, sizeof(p));
    memset(&child, 0, sizeof(child));
    memset(&p_meta, 0, sizeof(p_meta));
    memset(p_cols, 0, sizeof(p_cols));
    memset(rels, 0, sizeof(rels));
    memset(&c_meta, 0, sizeof(c_meta));
    memset(c_cols, 0, sizeof(c_cols));

    p.id = 1;
    p.child_o2o = &child;
    p_cols[0].name = "id";
    p_cols[0].type = C_ORM_TYPE_INT32;
    p_cols[0].is_pk = 1;
    p_cols[0].offset = offsetof(struct FullParentObj, id);

    c_cols[0].name = "id";
    c_cols[0].type = C_ORM_TYPE_INT32;
    c_cols[0].is_pk = 1;
    c_cols[0].offset = offsetof(struct NestedChild, id);
    c_meta.name = "child";
    c_meta.columns = c_cols;
    c_meta.num_columns = 1;
    c_meta.struct_size = sizeof(struct NestedChild);

    rels[0].field_name = "child_o2o";
    rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
    rels[0].target_meta = &c_meta;
    rels[0].local_key = "id";
    rels[0].foreign_key = "id";
    rels[0].struct_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].data_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2o_ctx);

    p_meta.name = "p";
    p_meta.columns = p_cols;
    p_meta.num_columns = 1;
    p_meta.relations = rels;
    p_meta.num_relations = 1;
    p_meta.struct_size = sizeof(struct FullParentObj);
    p_meta.query_select_by_pk = "SELECT 1 FROM p WHERE id = ?";
    c_meta.query_select_by_pk = "SELECT 1 FROM child WHERE id = ?";

    custom_vt.step = mock_step_countdown_100;
    g_step_countdown_100 = 2;
    paths[0] = "child_o2o.invalid_nested";
    (void)c_orm_find_with_relations_int32(&custom_db, &p_meta, 1, paths, 1, &p);
    c_orm_free_relations(&p_meta, &p);

    /* Also test nested_obj == NULL branch (line 1699) */
    p.child_o2o = NULL;
    (void)c_orm_find_with_relations_int32(&custom_db, &p_meta, 1, paths, 1, &p);

    /* Also test ONE_TO_MANY nested path on find_with_relations_int32 */
    rels[0].type = C_ORM_RELATION_ONE_TO_MANY;
    p.children_o2m.data = c_orm_malloc(sizeof(struct NestedChild));
    p.children_o2m.length = 1;
    p.children_o2m.capacity = 1;
    rels[0].field_name = "children_o2m";
    rels[0].struct_offset = offsetof(struct FullParentObj, children_o2m);
    rels[0].data_offset = offsetof(struct FullParentObj, children_o2m);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2m_ctx);
    paths[0] = "children_o2m.invalid_nested";
    (void)c_orm_find_with_relations_int32(&custom_db, &p_meta, 1, paths, 1, &p);

    /* Also test meta->num_relations == 0 and rel == NULL */
    p_meta.num_relations = 0;
    paths[0] = "child_o2o";
    (void)c_orm_find_with_relations_int32(&custom_db, &p_meta, 1, paths, 1, &p);
    p_meta.num_relations = 1;
    paths[0] = "unknown_rel";
    (void)c_orm_find_with_relations_int32(&custom_db, &p_meta, 1, paths, 1, &p);
  }

  /* 4. Lines 1799-1805: find_all_with_relations with ONE_TO_MANY nested path */
  {
    struct FullParentObj p;
    c_orm_table_meta_t p_meta;
    c_orm_column_meta_t p_cols[2];
    c_orm_relation_meta_t rels[2];
    struct NestedChild children[2];
    c_orm_table_meta_t c_meta;
    c_orm_column_meta_t c_cols[2];
    struct Generic_Array out_arr;
    const char *paths[1];

    memset(&p, 0, sizeof(p));
    memset(children, 0, sizeof(children));
    memset(&p_meta, 0, sizeof(p_meta));
    memset(p_cols, 0, sizeof(p_cols));
    memset(rels, 0, sizeof(rels));
    memset(&c_meta, 0, sizeof(c_meta));
    memset(c_cols, 0, sizeof(c_cols));
    memset(&out_arr, 0, sizeof(out_arr));

    p.id = 1;
    p.children_o2m.data = c_orm_malloc(2 * sizeof(struct NestedChild));
    p.children_o2m.length = 2;
    p.children_o2m.capacity = 2;

    p_cols[0].name = "id";
    p_cols[0].type = C_ORM_TYPE_INT32;
    p_cols[0].is_pk = 1;
    p_cols[0].offset = offsetof(struct FullParentObj, id);

    c_cols[0].name = "id";
    c_cols[0].type = C_ORM_TYPE_INT32;
    c_cols[0].is_pk = 1;
    c_cols[0].offset = offsetof(struct NestedChild, id);
    c_meta.name = "child";
    c_meta.columns = c_cols;
    c_meta.num_columns = 1;
    c_meta.struct_size = sizeof(struct NestedChild);

    rels[0].field_name = "children_o2m";
    rels[0].type = C_ORM_RELATION_ONE_TO_MANY;
    rels[0].target_meta = &c_meta;
    rels[0].local_key = "id";
    rels[0].foreign_key = "id";
    rels[0].struct_offset = offsetof(struct FullParentObj, children_o2m);
    rels[0].data_offset = offsetof(struct FullParentObj, children_o2m);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2m_ctx);

    p_meta.name = "p";
    p_meta.columns = p_cols;
    p_meta.num_columns = 1;
    p_meta.relations = rels;
    p_meta.num_relations = 1;
    p_meta.struct_size = sizeof(struct FullParentObj);
    p_meta.query_select_all = "SELECT 1 FROM p";

    out_arr.data = c_orm_malloc(sizeof(struct FullParentObj));
    memcpy(out_arr.data, &p, sizeof(struct FullParentObj));
    out_arr.length = 1;
    out_arr.capacity = 1;

    custom_vt.step = mock_stage_step;
    g_stage_step_cnt = 0;
    paths[0] = "children_o2m.invalid_nested";
    (void)c_orm_find_all_with_relations(&custom_db, &p_meta, paths, 1,
                                        &out_arr);
    if (out_arr.data) {
      C_ORM_FREE(out_arr.data);
      out_arr.data = NULL;
    }

    /* Also test lines 1789, 1795: num_relations == 0 and rel == NULL */
    out_arr.data = c_orm_malloc(sizeof(struct FullParentObj));
    memcpy(out_arr.data, &p, sizeof(struct FullParentObj));
    out_arr.length = 1;
    out_arr.capacity = 1;
    p_meta.num_relations = 0;
    paths[0] = "children_o2m.nested";
    (void)c_orm_find_all_with_relations(&custom_db, &p_meta, paths, 1,
                                        &out_arr);
    if (out_arr.data) {
      C_ORM_FREE(out_arr.data);
      out_arr.data = NULL;
    }

    out_arr.data = c_orm_malloc(sizeof(struct FullParentObj));
    memcpy(out_arr.data, &p, sizeof(struct FullParentObj));
    out_arr.length = 1;
    out_arr.capacity = 1;
    p_meta.num_relations = 1;
    paths[0] = "unknown_rel.nested";
    (void)c_orm_find_all_with_relations(&custom_db, &p_meta, paths, 1,
                                        &out_arr);
    if (out_arr.data) {
      C_ORM_FREE(out_arr.data);
      out_arr.data = NULL;
    }

    /* Also test line 1803: nested_obj == NULL for ONE_TO_ONE in find_all */
    rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
    rels[0].field_name = "child_o2o";
    rels[0].struct_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].data_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2o_ctx);
    p.child_o2o = NULL;
    out_arr.data = c_orm_malloc(sizeof(struct FullParentObj));
    memcpy(out_arr.data, &p, sizeof(struct FullParentObj));
    out_arr.length = 1;
    out_arr.capacity = 1;
    paths[0] = "child_o2o.nested";
    (void)c_orm_find_all_with_relations(&custom_db, &p_meta, paths, 1,
                                        &out_arr);
    if (out_arr.data) {
      C_ORM_FREE(out_arr.data);
      out_arr.data = NULL;
    }

    /* Lines 1799-1801: nested_obj != NULL for ONE_TO_ONE with failing nested
     * path */
    memset(&out_arr, 0, sizeof(out_arr));
    c_meta.query_select_by_pk = "SELECT 1 FROM child WHERE id = ?";
    custom_vt.step = mock_step_pattern_100;
    custom_vt.get_int32 = mock_get_int32_one_100;
    g_step_pattern_idx_100 = 0;
    paths[0] = "child_o2o.nonexistent";
    (void)c_orm_find_all_with_relations(&custom_db, &p_meta, paths, 1,
                                        &out_arr);
    custom_vt.get_int32 = g_vt.get_int32;
    if (out_arr.data) {
      struct FullParentObj *res = (struct FullParentObj *)out_arr.data;
      size_t ri;
      for (ri = 0; ri < out_arr.length; ri++) {
        c_orm_free_relations(&p_meta, &res[ri]);
        c_orm_free_columns(&p_meta, &res[ri]);
      }
      C_ORM_FREE(out_arr.data);
      out_arr.data = NULL;
    }

    /* Line 1805: nested_obj != NULL with succeeding nested lazy load */
    {
      c_orm_relation_meta_t c_rels[1];
      memset(c_rels, 0, sizeof(c_rels));
      c_rels[0].field_name = "grandchild";
      c_rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
      c_rels[0].target_meta = &c_meta;
      c_rels[0].local_key = "id";
      c_rels[0].foreign_key = "id";
      c_rels[0].struct_offset = offsetof(struct GrandChildObj, child_o2o);
      c_rels[0].data_offset = offsetof(struct GrandChildObj, child_o2o);
      c_rels[0].lazy_ctx_offset = offsetof(struct GrandChildObj, o2o_ctx);
      c_meta.relations = c_rels;
      c_meta.num_relations = 1;
      c_meta.struct_size = sizeof(struct GrandChildObj);
      c_cols[0].offset = offsetof(struct GrandChildObj, id);

      memset(&out_arr, 0, sizeof(out_arr));
      custom_vt.step = mock_step_pattern_100;
      custom_vt.get_int32 = mock_get_int32_one_100;
      g_step_pattern_idx_100 = 0;
      paths[0] = "child_o2o.grandchild";
      (void)c_orm_find_all_with_relations(&custom_db, &p_meta, paths, 1,
                                          &out_arr);
      custom_vt.get_int32 = g_vt.get_int32;
      if (out_arr.data) {
        struct FullParentObj *res = (struct FullParentObj *)out_arr.data;
        size_t ri;
        for (ri = 0; ri < out_arr.length; ri++) {
          c_orm_free_relations(&p_meta, &res[ri]);
          c_orm_free_columns(&p_meta, &res[ri]);
        }
        C_ORM_FREE(out_arr.data);
        out_arr.data = NULL;
      }
      c_meta.relations = NULL;
      c_meta.num_relations = 0;
      c_meta.struct_size = sizeof(struct NestedChild);
      c_cols[0].offset = offsetof(struct NestedChild, id);
    }
  }

  /* 5. Lines 34, 35, 36: c_orm_free_columns with all types */
  {
    struct MultiColObj {
      char *str_col;
      char *date_col;
      char *ts_col;
      char *enum_col;
      char *set_col;
      char *json_col;
      c_orm_blob_t blob_col;
      c_orm_polygon_t poly_col;
      int32_t *nullable_col;
    } m_obj;
    c_orm_table_meta_t m_meta;
    c_orm_column_meta_t m_cols[9];
    memset(&m_obj, 0, sizeof(m_obj));
    memset(&m_meta, 0, sizeof(m_meta));
    memset(m_cols, 0, sizeof(m_cols));

    m_cols[0].name = "str_col";
    m_cols[0].type = C_ORM_TYPE_STRING;
    m_cols[0].offset = offsetof(struct MultiColObj, str_col);
    m_cols[1].name = "date_col";
    m_cols[1].type = C_ORM_TYPE_DATE;
    m_cols[1].offset = offsetof(struct MultiColObj, date_col);
    m_cols[2].name = "ts_col";
    m_cols[2].type = C_ORM_TYPE_TIMESTAMP;
    m_cols[2].offset = offsetof(struct MultiColObj, ts_col);
    m_cols[3].name = "enum_col";
    m_cols[3].type = C_ORM_TYPE_ENUM;
    m_cols[3].offset = offsetof(struct MultiColObj, enum_col);
    m_cols[4].name = "set_col";
    m_cols[4].type = C_ORM_TYPE_SET;
    m_cols[4].offset = offsetof(struct MultiColObj, set_col);
    m_cols[5].name = "json_col";
    m_cols[5].type = C_ORM_TYPE_JSON;
    m_cols[5].offset = offsetof(struct MultiColObj, json_col);
    m_cols[6].name = "blob_col";
    m_cols[6].type = C_ORM_TYPE_BLOB;
    m_cols[6].offset = offsetof(struct MultiColObj, blob_col);
    m_cols[7].name = "poly_col";
    m_cols[7].type = C_ORM_TYPE_POLYGON;
    m_cols[7].offset = offsetof(struct MultiColObj, poly_col);
    m_cols[8].name = "nullable_col";
    m_cols[8].type = C_ORM_TYPE_INT32;
    m_cols[8].is_nullable = 1;
    m_cols[8].offset = offsetof(struct MultiColObj, nullable_col);
    m_meta.name = "multicol";
    m_meta.columns = m_cols;
    m_meta.num_columns = 9;

    m_obj.str_col = (char *)c_orm_malloc(8);
    m_obj.date_col = (char *)c_orm_malloc(8);
    m_obj.ts_col = (char *)c_orm_malloc(8);
    m_obj.enum_col = (char *)c_orm_malloc(8);
    m_obj.set_col = (char *)c_orm_malloc(8);
    m_obj.json_col = (char *)c_orm_malloc(8);
    m_obj.blob_col.data = (unsigned char *)c_orm_malloc(8);
    m_obj.blob_col.size = 8;
    m_obj.poly_col.points =
        (c_orm_point_t *)c_orm_malloc(sizeof(c_orm_point_t));
    m_obj.poly_col.num_points = 1;
    m_obj.nullable_col = (int32_t *)c_orm_malloc(sizeof(int32_t));
    c_orm_free_columns(&m_meta, &m_obj);

    memset(&m_obj, 0, sizeof(m_obj));
    c_orm_free_columns(&m_meta, &m_obj);
    c_orm_free_columns(NULL, &m_obj);
    c_orm_free_columns(&m_meta, NULL);

    /* Lines 75: hydrate_row_from null checks */
    (void)c_orm_hydrate_row_from(NULL, (c_orm_query_t *)1, &m_meta, &m_obj, 0);
    (void)c_orm_hydrate_row_from(&custom_db, (c_orm_query_t *)1, NULL, &m_obj,
                                 0);

    /* Lines 103, 104, 110: is_null true with all types */
    m_obj.str_col = (char *)c_orm_malloc(8);
    m_obj.date_col = (char *)c_orm_malloc(8);
    m_obj.ts_col = (char *)c_orm_malloc(8);
    m_obj.enum_col = (char *)c_orm_malloc(8);
    m_obj.set_col = (char *)c_orm_malloc(8);
    m_obj.json_col = (char *)c_orm_malloc(8);
    m_obj.blob_col.data = (unsigned char *)c_orm_malloc(8);
    m_obj.blob_col.size = 8;
    m_obj.nullable_col = (int32_t *)c_orm_malloc(sizeof(int32_t));
    custom_vt.is_null = mock_is_null_true;
    (void)c_orm_hydrate_row_from(&custom_db, (c_orm_query_t *)1, &m_meta,
                                 &m_obj, 0);
    custom_vt.is_null = g_vt.is_null;

    /* Lines 2013-2014, 2045, 2098: insert to trigger bind_row branches */
    m_meta.query_insert =
        "INSERT INTO multicol VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
    m_meta.struct_size = sizeof(m_obj);
    m_obj.str_col = "val";
    m_obj.date_col = "2020-01-01";
    m_obj.ts_col = "invalid_timestamp_string";
    m_obj.enum_col = "active";
    m_obj.set_col = "a,b";
    m_obj.json_col = "{}";
    m_obj.blob_col.data = (unsigned char *)"x";
    m_obj.blob_col.size = 0; /* size == 0 branch */
    (void)c_orm_insert(&custom_db, &m_meta, &m_obj);
  }

  /* 6. Lines 338, 354, 360, 393, 396: get_blob special branches */
  {
    struct GeoBlobObj {
      c_orm_point_t pt;
      c_orm_polygon_t poly;
      c_orm_blob_t blob;
      c_orm_blob_t sec_blob;
    } gb_obj;
    c_orm_table_meta_t gb_meta;
    c_orm_column_meta_t gb_cols[4];
    memset(&gb_obj, 0, sizeof(gb_obj));
    memset(&gb_meta, 0, sizeof(gb_meta));
    memset(gb_cols, 0, sizeof(gb_cols));

    gb_cols[0].name = "pt";
    gb_cols[0].type = C_ORM_TYPE_POINT;
    gb_cols[0].offset = offsetof(struct GeoBlobObj, pt);
    gb_cols[1].name = "poly";
    gb_cols[1].type = C_ORM_TYPE_POLYGON;
    gb_cols[1].offset = offsetof(struct GeoBlobObj, poly);
    gb_cols[2].name = "blob";
    gb_cols[2].type = C_ORM_TYPE_BLOB;
    gb_cols[2].offset = offsetof(struct GeoBlobObj, blob);
    gb_cols[3].name = "sec_blob";
    gb_cols[3].type = C_ORM_TYPE_BLOB;
    gb_cols[3].is_secure = 1;
    gb_cols[3].offset = offsetof(struct GeoBlobObj, sec_blob);
    gb_meta.name = "geoblob";
    gb_meta.columns = gb_cols;
    gb_meta.num_columns = 4;

    custom_vt.get_blob = mock_cov_blob_100;
    custom_vt.is_null = mock_is_null_false;

    /* size != 21 for point, size < 13 for poly, size == 0 for blob */
    g_cov_blob_size_100 = 5;
    (void)c_orm_hydrate_row_from(&custom_db, (c_orm_query_t *)1, &gb_meta,
                                 &gb_obj, 0);

    /* size >= 13 but num_points == 0 for poly */
    g_cov_blob_size_100 = 14;
    (void)c_orm_hydrate_row_from(&custom_db, (c_orm_query_t *)1, &gb_meta,
                                 &gb_obj, 0);

    /* sec_blob without decrypt_hook */
    custom_db.decrypt_hook = NULL;
    g_cov_blob_size_100 = 10;
    (void)c_orm_hydrate_row_from(&custom_db, (c_orm_query_t *)1, &gb_meta,
                                 &gb_obj, 0);

    custom_vt.get_blob = g_vt.get_blob;
    custom_vt.is_null = g_vt.is_null;
  }

  /* 7. Line 458: TTL callback */
  {
    struct TtlObj {
      int32_t id;
      int64_t created_at;
      int32_t expires_in;
    } t_obj;
    c_orm_table_meta_t t_meta;
    c_orm_column_meta_t t_cols[3];
    memset(&t_obj, 0, sizeof(t_obj));
    memset(&t_meta, 0, sizeof(t_meta));
    memset(t_cols, 0, sizeof(t_cols));

    t_cols[0].name = "id";
    t_cols[0].type = C_ORM_TYPE_INT32;
    t_cols[0].is_pk = 1;
    t_cols[0].offset = offsetof(struct TtlObj, id);
    t_cols[1].name = "created_at";
    t_cols[1].type = C_ORM_TYPE_TIMESTAMP;
    t_cols[1].offset = offsetof(struct TtlObj, created_at);
    t_cols[2].name = "expires_in";
    t_cols[2].type = C_ORM_TYPE_INT32;
    t_cols[2].offset = offsetof(struct TtlObj, expires_in);
    t_meta.name = "ttlobj";
    t_meta.columns = t_cols;
    t_meta.num_columns = 3;
    t_meta.has_ttl = 1;
    t_meta.created_at_offset = offsetof(struct TtlObj, created_at);
    t_meta.expires_in_offset = offsetof(struct TtlObj, expires_in);

    t_obj.created_at = 1;
    t_obj.expires_in = 1;
    custom_db.expire_cb = dummy_cov_expire_callback_100;
    (void)c_orm_hydrate_row_from(&custom_db, (c_orm_query_t *)1, &t_meta,
                                 &t_obj, 0);
    custom_db.expire_cb = NULL;
  }

  /* 8. Lines 501, 517, 521, 535, 544, 547, 556, 577, 580: prefix hydration */
  {
    struct FullParentObj p;
    c_orm_table_meta_t p_meta;
    c_orm_column_meta_t p_cols[2];
    c_orm_relation_meta_t rels[2];
    struct NestedChild child;
    c_orm_table_meta_t c_meta;
    c_orm_column_meta_t c_cols[2];

    memset(&p, 0, sizeof(p));
    memset(&child, 0, sizeof(child));
    memset(&p_meta, 0, sizeof(p_meta));
    memset(p_cols, 0, sizeof(p_cols));
    memset(rels, 0, sizeof(rels));
    memset(&c_meta, 0, sizeof(c_meta));
    memset(c_cols, 0, sizeof(c_cols));

    p_cols[0].name = "id";
    p_cols[0].type = C_ORM_TYPE_INT32;
    p_cols[0].offset = offsetof(struct FullParentObj, id);
    p_meta.name = "p";
    p_meta.columns = p_cols;
    p_meta.num_columns = 1;

    c_cols[0].name = "id";
    c_cols[0].type = C_ORM_TYPE_INT32;
    c_cols[0].offset = offsetof(struct NestedChild, id);
    c_cols[1].name = "name";
    c_cols[1].type = C_ORM_TYPE_STRING;
    c_cols[1].offset = offsetof(struct NestedChild, name);
    c_meta.name = "child";
    c_meta.columns = c_cols;
    c_meta.num_columns = 2;
    c_meta.struct_size = sizeof(struct NestedChild);

    rels[0].field_name = "child";
    rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
    rels[0].target_meta = &c_meta;
    rels[0].local_key = "id";
    rels[0].foreign_key = "id";
    rels[0].struct_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].data_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2o_ctx);
    p_meta.relations = rels;
    p_meta.num_relations = 1;

    /* Line 501: get_column_count set but get_column_name NULL */
    custom_vt.get_column_count = mock_prefix_col_count_ok;
    custom_vt.get_column_name = NULL;
    (void)c_orm_hydrate_row(&custom_db, (c_orm_query_t *)1, &p_meta, &p);

    /* Line 517: col_name prefix matches */
    custom_vt.get_column_name = mock_prefix_col_name_ok; /* returns child_id */
    (void)c_orm_hydrate_row(&custom_db, (c_orm_query_t *)1, &p_meta, &p);

    /* Line 521: target_meta->num_columns == 0 */
    c_meta.num_columns = 0;
    (void)c_orm_hydrate_row(&custom_db, (c_orm_query_t *)1, &p_meta, &p);
    c_meta.num_columns = 2;

    /* Line 535: rel->type == ONE_TO_MANY */
    rels[0].type = C_ORM_RELATION_ONE_TO_MANY;
    (void)c_orm_hydrate_row(&custom_db, (c_orm_query_t *)1, &p_meta, &p);
    rels[0].type = C_ORM_RELATION_ONE_TO_ONE;

    /* Line 544: is_null true */
    custom_vt.is_null = mock_is_null_true;
    (void)c_orm_hydrate_row(&custom_db, (c_orm_query_t *)1, &p_meta, &p);
    custom_vt.is_null = g_vt.is_null;

    /* Line 547: nested_struct malloc fails */
    c_orm_set_allocators(cov_always_null_malloc, orig_r, orig_f);
    (void)c_orm_hydrate_row(&custom_db, (c_orm_query_t *)1, &p_meta, &p);
    c_orm_set_allocators(orig_m, orig_r, orig_f);

    /* Line 577: str_val NULL for string col */
    custom_vt.get_string = mock_get_string_null;
    (void)c_orm_hydrate_row(&custom_db, (c_orm_query_t *)1, &p_meta, &p);
    custom_vt.get_string = g_vt.get_string;

    custom_vt.get_column_count = g_vt.get_column_count;
    custom_vt.get_column_name = g_vt.get_column_name;
    c_orm_free_columns(&p_meta, &p);
  }

  /* 9. Lines 987, 994: c_orm_find_by_id_int32 branches */
  {
    c_orm_table_meta_t empty_meta;
    struct Users u_obj;
    c_orm_column_meta_t str_pk_col;
    memset(&empty_meta, 0, sizeof(empty_meta));
    memset(&u_obj, 0, sizeof(u_obj));
    empty_meta.name = "empty";
    empty_meta.query_select_by_pk = "SELECT 1";
    custom_vt.step = mock_step_sequence;
    (void)c_orm_find_by_id_int32(&custom_db, &empty_meta, 1, &u_obj);

    /* PK col is STRING */
    empty_meta = Users_meta;
    str_pk_col = Users_meta.columns[0];
    str_pk_col.type = C_ORM_TYPE_STRING;
    str_pk_col.is_pk = 1;
    empty_meta.columns = &str_pk_col;
    empty_meta.num_columns = 1;
    (void)c_orm_find_by_id_int32(&custom_db, &empty_meta, 1, &u_obj);
  }

  /* 10. Lines 1143, 1483: capacity doubling branches in relation loads */
  {
    struct FullParentObj p;
    c_orm_table_meta_t p_meta;
    c_orm_column_meta_t p_cols[2];
    c_orm_relation_meta_t rels[2];
    c_orm_table_meta_t c_meta;
    c_orm_column_meta_t c_cols[2];
    struct Generic_Array out_arr;

    memset(&p, 0, sizeof(p));
    memset(&p_meta, 0, sizeof(p_meta));
    memset(p_cols, 0, sizeof(p_cols));
    memset(rels, 0, sizeof(rels));
    memset(&c_meta, 0, sizeof(c_meta));
    memset(c_cols, 0, sizeof(c_cols));
    memset(&out_arr, 0, sizeof(out_arr));

    p.id = 1;
    p_cols[0].name = "id";
    p_cols[0].type = C_ORM_TYPE_INT32;
    p_cols[0].is_pk = 1;
    p_cols[0].offset = offsetof(struct FullParentObj, id);
    p_meta.name = "p";
    p_meta.columns = p_cols;
    p_meta.num_columns = 1;
    p_meta.struct_size = sizeof(struct FullParentObj);
    p_meta.query_select_by_pk = "SELECT 1";
    p_meta.query_select_all = "SELECT 1";

    c_cols[0].name = "id";
    c_cols[0].type = C_ORM_TYPE_INT32;
    c_cols[0].is_pk = 1;
    c_cols[0].offset = offsetof(struct NestedChild, id);
    c_meta.name = "child";
    c_meta.columns = c_cols;
    c_meta.num_columns = 1;
    c_meta.struct_size = sizeof(struct NestedChild);

    rels[0].field_name = "children_o2m";
    rels[0].type = C_ORM_RELATION_ONE_TO_MANY;
    rels[0].target_meta = &c_meta;
    rels[0].local_key = "id";
    rels[0].foreign_key = "id";
    rels[0].struct_offset = offsetof(struct FullParentObj, children_o2m);
    rels[0].data_offset = offsetof(struct FullParentObj, children_o2m);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2m_ctx);
    p_meta.relations = rels;
    p_meta.num_relations = 1;

    /* Run find_with_relation_int32 returning 20 rows to exceed cap=16 */
    custom_vt.step = mock_stage_step;
    g_stage_step_cnt = 0;
    g_stage_step_max = 20;
    (void)c_orm_find_with_relation_int32(&custom_db, &p_meta, 1, "children_o2m",
                                         &p);
    if (p.children_o2m.data) {
      C_ORM_FREE(p.children_o2m.data);
      p.children_o2m.data = NULL;
    }

    /* Lines 1363, 1367: empty custom_filter and order_by */
    rels[0].custom_filter = "";
    rels[0].order_by = "";
    (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "children_o2m",
                                       &out_arr);
    if (out_arr.data) {
      C_ORM_FREE(out_arr.data);
      out_arr.data = NULL;
    }
    rels[0].custom_filter = NULL;
    rels[0].order_by = NULL;

    /* Line 1456: target_data_ptr already non-null in find_all_with_relation */
    rels[0].type = C_ORM_RELATION_BELONGS_TO;
    rels[0].field_name = "child_o2o";
    rels[0].struct_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].data_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2o_ctx);
    (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "child_o2o",
                                       &out_arr);
    if (out_arr.data) {
      C_ORM_FREE(out_arr.data);
      out_arr.data = NULL;
    }
  }

  /* 11. Batch chunk_size calculations and iterator branches */
  {
    struct Users u_batch[2];
    c_orm_table_meta_t b_meta;
    c_orm_column_meta_t cols_60[60];
    struct c_orm_iterator *iter = NULL;
    size_t count = 0;
    void *out_arr = NULL;
    size_t ci;

    memset(u_batch, 0, sizeof(u_batch));
    memset(&b_meta, 0, sizeof(b_meta));
    memset(cols_60, 0, sizeof(cols_60));
    cols_60[0].name = "id";
    cols_60[0].type = C_ORM_TYPE_INT32;
    cols_60[0].is_pk = 1;
    for (ci = 1; ci < 60; ci++) {
      cols_60[ci].name = "c";
      cols_60[ci].type = C_ORM_TYPE_INT32;
    }

    /* c_orm_insert_batch with chunk_size=0 and num_columns=0 (line 2281) */
    b_meta = Users_meta;
    b_meta.num_columns = 0;
    (void)c_orm_insert_batch(&custom_db, &b_meta, u_batch, 1, 0);

    /* c_orm_insert_batch with chunk_size=0 and num_columns=50 (line 2282) */
    b_meta.columns = cols_60;
    b_meta.num_columns = 50;
    (void)c_orm_insert_batch(&custom_db, &b_meta, u_batch, 1, 0);

    /* c_orm_update_batch with chunk_size=0 and num_columns=0 */
    b_meta.num_columns = 0;
    (void)c_orm_update_batch(&custom_db, &b_meta, u_batch, 1, 0);

    /* c_orm_update_batch with chunk_size=0 and num_columns=60 (line 3275) */
    b_meta.columns = cols_60;
    b_meta.num_columns = 60;
    (void)c_orm_update_batch(&custom_db, &b_meta, u_batch, 1, 0);

    /* c_orm_find_batch_init and iterator NULL checks (line 2529, 2591) */
    (void)c_orm_find_batch_init(&custom_db, &Users_meta, NULL, 10, &iter);
    if (iter) {
      (void)c_orm_iterator_next(iter, NULL, &count);
      (void)c_orm_iterator_next(iter, &out_arr, NULL);
      c_orm_iterator_close(iter);
    }
  }

  /* 12. BelongsTo / OneToOne insert/update branches */
  {
    struct FullParentObj p;
    c_orm_table_meta_t p_meta;
    c_orm_column_meta_t p_cols[2];
    c_orm_relation_meta_t rels[2];
    struct NestedChild child;
    c_orm_table_meta_t c_meta;
    c_orm_column_meta_t c_cols[2];

    memset(&p, 0, sizeof(p));
    memset(&child, 0, sizeof(child));
    memset(&p_meta, 0, sizeof(p_meta));
    memset(p_cols, 0, sizeof(p_cols));
    memset(rels, 0, sizeof(rels));
    memset(&c_meta, 0, sizeof(c_meta));
    memset(c_cols, 0, sizeof(c_cols));

    p.id = 1;
    p_cols[0].name = "id";
    p_cols[0].type = C_ORM_TYPE_INT32;
    p_cols[0].is_pk = 1;
    p_cols[0].offset = offsetof(struct FullParentObj, id);
    p_meta.name = "p";
    p_meta.columns = p_cols;
    p_meta.num_columns = 1;
    p_meta.struct_size = sizeof(struct FullParentObj);
    p_meta.query_insert = "INSERT INTO p (id) VALUES (?)";
    p_meta.query_update = "UPDATE p SET id = ? WHERE id = ?";

    c_cols[0].name = "id";
    c_cols[0].type = C_ORM_TYPE_INT32;
    c_cols[0].is_pk = 1;
    c_cols[0].offset = offsetof(struct NestedChild, id);
    c_meta.name = "child";
    c_meta.columns = c_cols;
    c_meta.num_columns = 1;
    c_meta.struct_size = sizeof(struct NestedChild);
    c_meta.query_insert = "INSERT INTO child (id) VALUES (?)";
    c_meta.query_update = "UPDATE child SET id = ? WHERE id = ?";

    rels[0].field_name = "child_o2o";
    rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
    rels[0].target_meta = &c_meta;
    rels[0].local_key = "id";
    rels[0].foreign_key = "id";
    rels[0].struct_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].data_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2o_ctx);
    p_meta.relations = rels;
    p_meta.num_relations = 1;

    /* get_last_insert_rowid NULL during insert (line 2660, 2776) */
    p.child_o2o = &child;
    custom_vt.get_last_insert_rowid = NULL;
    (void)c_orm_insert(&custom_db, &p_meta, &p);

    /* BelongsTo with nested_ptr != NULL and on_update != CASCADE (line 2861) */
    rels[0].type = C_ORM_RELATION_BELONGS_TO;
    rels[0].on_update = C_ORM_CASCADE_SET_NULL;
    (void)c_orm_update(&custom_db, &p_meta, &p);

    /* BelongsTo with nested_ptr == NULL (line 2867) */
    p.child_o2o = NULL;
    (void)c_orm_update(&custom_db, &p_meta, &p);

    /* Update with INT64 PK (line 2966) */
    p_cols[0].type = C_ORM_TYPE_INT64;
    (void)c_orm_update(&custom_db, &p_meta, &p);

    /* Save with num_columns == 0 (line 3016) */
    p_meta.num_columns = 0;
    (void)c_orm_save(&custom_db, &p_meta, &p);

    /* Save with empty string PK (line 3025) */
    p_cols[0].type = C_ORM_TYPE_STRING;
    p_meta.num_columns = 1;
    {
      char *empty_str = (char *)c_orm_malloc(1);
      void *field_addr;
      if (empty_str) {
        empty_str[0] = 0;
        field_addr = (char *)&p + p_cols[0].offset;
        memcpy(field_addr, &empty_str, sizeof(char *));
        (void)c_orm_save(&custom_db, &p_meta, &p);
      }
    }

    custom_vt.get_last_insert_rowid = g_vt.get_last_insert_rowid;
  }

  /* 13. Cascade Delete branches */
  {
    struct FullParentObj p;
    c_orm_table_meta_t p_meta;
    c_orm_column_meta_t p_cols[2];
    c_orm_relation_meta_t rels[2];
    struct NestedChild child;
    c_orm_table_meta_t c_meta;
    c_orm_column_meta_t c_cols[2];

    memset(&p, 0, sizeof(p));
    memset(&child, 0, sizeof(child));
    memset(&p_meta, 0, sizeof(p_meta));
    memset(p_cols, 0, sizeof(p_cols));
    memset(rels, 0, sizeof(rels));
    memset(&c_meta, 0, sizeof(c_meta));
    memset(c_cols, 0, sizeof(c_cols));

    p.id = 1;
    p_cols[0].name = "id";
    p_cols[0].type = C_ORM_TYPE_INT64;
    p_cols[0].is_pk = 1;
    p_cols[0].offset = offsetof(struct FullParentObj, id);
    p_meta.name = "p";
    p_meta.columns = p_cols;
    p_meta.num_columns = 1;
    p_meta.struct_size = sizeof(struct FullParentObj);
    p_meta.query_delete_by_pk = "DELETE FROM p WHERE id = ?";

    c_cols[0].name = "id";
    c_cols[0].type = C_ORM_TYPE_INT32;
    c_cols[0].is_pk = 1;
    c_cols[0].offset = offsetof(struct NestedChild, id);
    c_meta.name = "child";
    c_meta.columns = c_cols;
    c_meta.num_columns = 1;
    c_meta.struct_size = sizeof(struct NestedChild);

    rels[0].field_name = "tags_m2m";
    rels[0].type = C_ORM_RELATION_MANY_TO_MANY;
    rels[0].target_meta = &c_meta;
    rels[0].local_key = "id";
    rels[0].foreign_key = "id";
    rels[0].join_table = "join_tbl";
    rels[0].join_local_key = "p_id";
    rels[0].join_foreign_key = "c_id";
    rels[0].on_delete = C_ORM_CASCADE_DELETE;
    rels[0].struct_offset = offsetof(struct FullParentObj, tags_m2m);
    rels[0].data_offset = offsetof(struct FullParentObj, tags_m2m);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, m2m_ctx);
    p_meta.relations = rels;
    p_meta.num_relations = 1;

    /* Delete with INT64 PK on M2M cascade (line 3584) */
    (void)c_orm_delete(&custom_db, &p_meta, &p);

    /* Delete with INT64 PK on M2M nullify (line 3604) */
    rels[0].on_delete = C_ORM_CASCADE_SET_NULL;
    (void)c_orm_delete(&custom_db, &p_meta, &p);

    /* Delete with INT64 PK on OneToOne cascade (line 3546) */
    rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
    rels[0].on_delete = C_ORM_CASCADE_DELETE;
    rels[0].struct_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].data_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2o_ctx);
    (void)c_orm_delete(&custom_db, &p_meta, &p);

    /* Delete with prepare failure on cascade (lines 3540, 3578, 3599) */
    custom_vt.prepare = mock_always_fail_prepare;
    (void)c_orm_delete(&custom_db, &p_meta, &p);
    rels[0].type = C_ORM_RELATION_MANY_TO_MANY;
    rels[0].on_delete = C_ORM_CASCADE_DELETE;
    rels[0].struct_offset = offsetof(struct FullParentObj, tags_m2m);
    rels[0].data_offset = offsetof(struct FullParentObj, tags_m2m);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, m2m_ctx);
    (void)c_orm_delete(&custom_db, &p_meta, &p);
    rels[0].on_delete = C_ORM_CASCADE_SET_NULL;
    (void)c_orm_delete(&custom_db, &p_meta, &p);
    custom_vt.prepare = g_vt.prepare;

    /* Delete with pk_col == NULL (lines 3529, 3568) */
    rels[0].local_key = "nonexistent";
    rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
    rels[0].struct_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].data_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2o_ctx);
    (void)c_orm_delete(&custom_db, &p_meta, &p);
    rels[0].type = C_ORM_RELATION_MANY_TO_MANY;
    rels[0].struct_offset = offsetof(struct FullParentObj, tags_m2m);
    rels[0].data_offset = offsetof(struct FullParentObj, tags_m2m);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, m2m_ctx);
    (void)c_orm_delete(&custom_db, &p_meta, &p);

    /* Delete with target_meta == NULL on M2M (line 3557) */
    rels[0].target_meta = NULL;
    (void)c_orm_delete(&custom_db, &p_meta, &p);

    /* Delete with num_columns == 0 (lines 3523, 3562) */
    p_meta.num_columns = 0;
    rels[0].target_meta = &c_meta;
    (void)c_orm_delete(&custom_db, &p_meta, &p);
    rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
    rels[0].struct_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].data_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2o_ctx);
    (void)c_orm_delete(&custom_db, &p_meta, &p);
  }

  /* 14. Validation branches (lines 4334, 4336, 4347) */
  {
    c_orm_table_meta_t v_meta;
    c_orm_relation_meta_t rels[2];
    c_orm_column_meta_t cols[2];
    struct FullParentObj p;

    memset(&v_meta, 0, sizeof(v_meta));
    memset(rels, 0, sizeof(rels));
    memset(cols, 0, sizeof(cols));
    memset(&p, 0, sizeof(p));

    cols[0].name = "fk_id";
    cols[0].type = C_ORM_TYPE_INT32;
    cols[0].is_nullable = 0;
    cols[0].offset = offsetof(struct FullParentObj, id);
    v_meta.name = "v";
    v_meta.columns = cols;
    v_meta.num_columns = 1;
    rels[0].field_name = "b_rel";
    rels[0].type = C_ORM_RELATION_BELONGS_TO;
    rels[0].foreign_key = "fk_id";
    v_meta.relations = rels;
    v_meta.num_relations = 1;

    /* Line 4347: fk_val == 0 */
    p.id = 0;
    (void)c_orm_validate(&v_meta, &p);

    /* Line 4415 branch 1: rc != C_ORM_OK (get_int_field fails on STRING column)
     */
    cols[0].type = C_ORM_TYPE_STRING;
    (void)c_orm_validate(&v_meta, &p);
    cols[0].type = C_ORM_TYPE_INT32;

    /* Line 4415 branch 1: data != NULL */
    rels[0].struct_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].data_offset = offsetof(struct FullParentObj, child_o2o);
    p.child_o2o = (struct NestedChild *)(void *)1;
    (void)c_orm_validate(&v_meta, &p);
    p.child_o2o = NULL;

    /* Line 4334: rel->type != BELONGS_TO */
    rels[0].type = C_ORM_RELATION_ONE_TO_MANY;
    (void)c_orm_validate(&v_meta, &p);

    /* Line 4336: num_columns == 0 */
    rels[0].type = C_ORM_RELATION_BELONGS_TO;
    v_meta.num_columns = 0;
    (void)c_orm_validate(&v_meta, &p);
  }

  /* 15. Schema AST constraint branches (lines 4493, 4512) */
  {
    struct sql_table_t sql_tbl;
    struct sql_column_t sql_col;
    struct sql_constraint_t col_con;
    struct sql_constraint_t tbl_con;
    c_orm_relation_meta_t *out_rels = NULL;
    size_t out_num = 0;

    memset(&sql_tbl, 0, sizeof(sql_tbl));
    memset(&sql_col, 0, sizeof(sql_col));
    memset(&col_con, 0, sizeof(col_con));
    memset(&tbl_con, 0, sizeof(tbl_con));

    col_con.type = SQL_CONSTRAINT_PRIMARY_KEY; /* not FK */
    sql_col.name = "id";
    sql_col.constraints = &col_con;
    sql_col.n_constraints = 1;

    tbl_con.type = SQL_CONSTRAINT_UNIQUE; /* not FK */
    sql_tbl.columns = &sql_col;
    sql_tbl.n_columns = 1;
    sql_tbl.table_constraints = &tbl_con;
    sql_tbl.n_table_constraints = 1;

    (void)c_orm_build_relation_meta(&sql_tbl, &out_rels, &out_num);
    if (out_rels)
      C_ORM_FREE(out_rels);
  }

  /* 16. Identity map branches (lines 4872, 5055) */
  {
    c_orm_identity_map_t map;
    c_orm_identity_bucket_t *bucket;
    void *out_obj = NULL;

    memset(&map, 0, sizeof(map));

    /* bucket with entries == NULL (line 4872) */
    bucket = (c_orm_identity_bucket_t *)c_orm_malloc(
        sizeof(c_orm_identity_bucket_t));
    if (bucket) {
      memset(bucket, 0, sizeof(c_orm_identity_bucket_t));
      bucket->entries = NULL;
      map.buckets = bucket;
      c_orm_identity_map_free(&map);
    }

    /* entry with pk_str == NULL (line 5055) */
    memset(&map, 0, sizeof(map));
    (void)c_orm_identity_map_get_or_set_str(&map, &Users_meta, "key1",
                                            (void *)1, &out_obj);
    if (map.buckets && map.buckets->entries) {
      size_t i;
      for (i = 0; i < map.buckets->num_buckets; i++) {
        c_orm_identity_entry_t *e = map.buckets->entries[i];
        while (e) {
          if (e->pk_str) {
            C_ORM_FREE(e->pk_str);
            e->pk_str = NULL; /* line 5055: entry->pk_str == NULL */
          }
          e = e->next;
        }
      }
    }
    (void)c_orm_identity_map_get_or_set_str(&map, &Users_meta, "key1", NULL,
                                            &out_obj);
    c_orm_identity_map_free(&map);
  }

  /* 17. Null checks and parameter validation (lines 5123, 5154, 5245) */
  {
    struct Generic_Array arr;
    struct Users u_obj;
    void *cached = NULL;
    memset(&arr, 0, sizeof(arr));
    memset(&u_obj, 0, sizeof(u_obj));

    (void)c_orm_resolve_n_plus_one(&custom_db, NULL, &Users_meta, 0);
    (void)c_orm_resolve_n_plus_one(&custom_db, &arr, NULL, 0);

    (void)c_orm_hydrate_cache_row(&custom_db, NULL, &u_obj, &cached);
    (void)c_orm_hydrate_cache_row(&custom_db, &Users_meta, NULL, &cached);
    (void)c_orm_hydrate_cache_row(&custom_db, &Users_meta, &u_obj, NULL);

    (void)c_orm_load_relation_ext(&custom_db, NULL, &Users_meta, 0, 0, 0);
    (void)c_orm_load_relation_ext(&custom_db, &u_obj, NULL, 0, 0, 0);
  }

  /* 18. load_relation_ext branches (lines 5263, 5281, 5342, 5374, 5383) */
  {
    struct FullParentObj p;
    c_orm_table_meta_t p_meta;
    c_orm_column_meta_t p_cols[2];
    c_orm_relation_meta_t rels[2];
    struct NestedChild child;
    c_orm_table_meta_t c_meta;
    c_orm_column_meta_t c_cols[2];
    c_orm_lazy_load_context_t *ctx;

    memset(&p, 0, sizeof(p));
    memset(&child, 0, sizeof(child));
    memset(&p_meta, 0, sizeof(p_meta));
    memset(p_cols, 0, sizeof(p_cols));
    memset(rels, 0, sizeof(rels));
    memset(&c_meta, 0, sizeof(c_meta));
    memset(c_cols, 0, sizeof(c_cols));

    p.id = 1;
    p_cols[0].name = "str_pk";
    p_cols[0].type = C_ORM_TYPE_BOOL;
    p_cols[0].is_pk = 1;
    p_cols[0].offset = offsetof(struct FullParentObj, id);
    p_meta.name = "p";
    p_meta.columns = p_cols;
    p_meta.num_columns = 1;

    c_cols[0].name = "id";
    c_cols[0].type = C_ORM_TYPE_INT32;
    c_cols[0].is_pk = 1;
    c_cols[0].offset = offsetof(struct NestedChild, id);
    c_meta.name = "child";
    c_meta.columns = c_cols;
    c_meta.num_columns = 1;
    c_meta.struct_size = sizeof(struct NestedChild);

    rels[0].field_name = "tags_m2m";
    rels[0].type = C_ORM_RELATION_MANY_TO_MANY;
    rels[0].target_meta = &c_meta;
    rels[0].local_key = "str_pk";
    rels[0].foreign_key = "id";
    rels[0].struct_offset = offsetof(struct FullParentObj, tags_m2m);
    rels[0].data_offset = offsetof(struct FullParentObj, tags_m2m);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, m2m_ctx);
    p_meta.relations = rels;
    p_meta.num_relations = 1;

    /* Line 5281: local_key is BOOL (neither INT32 nor INT64 nor STRING) */
    (void)c_orm_load_relation_ext(&custom_db, &p, &p_meta, 0, 0, 0);

    /* Switch to INT32 for remaining tests */
    p_cols[0].type = C_ORM_TYPE_INT32;

    /* Line 5263: is_loaded == 1 but limit > 0 */
    ctx = (c_orm_lazy_load_context_t *)(void *)((char *)&p +
                                                rels[0].lazy_ctx_offset);
    ctx->is_loaded = 1;
    (void)c_orm_load_relation_ext(&custom_db, &p, &p_meta, 0, 5, 0);
    ctx->is_loaded = 0;

    /* Line 5342: missing join_table / join_local_key / join_foreign_key */
    rels[0].join_table = NULL;
    (void)c_orm_load_relation_ext(&custom_db, &p, &p_meta, 0, 0, 0);
    rels[0].join_table = "j";
    rels[0].join_local_key = NULL;
    (void)c_orm_load_relation_ext(&custom_db, &p, &p_meta, 0, 0, 0);
    rels[0].join_local_key = "lk";
    rels[0].join_foreign_key = NULL;
    (void)c_orm_load_relation_ext(&custom_db, &p, &p_meta, 0, 0, 0);
    rels[0].join_foreign_key = "fk";

    /* Lines 5374, 5383: empty custom_filter and order_by */
    rels[0].custom_filter = "";
    rels[0].order_by = "";
    (void)c_orm_load_relation_ext(&custom_db, &p, &p_meta, 0, 0, 0);
  }

  /* 19. free_relations branches (lines 5510, 5513, 5520, 5524, 5535, 5543,
   * 5547) */
  {
    struct FullParentObj p;
    c_orm_table_meta_t p_meta;
    c_orm_relation_meta_t rels[2];
    struct NestedChild child;
    c_orm_table_meta_t c_meta;
    c_orm_column_meta_t c_cols[2];
    c_orm_lazy_load_context_t *ctx;

    memset(&p, 0, sizeof(p));
    memset(&child, 0, sizeof(child));
    memset(&p_meta, 0, sizeof(p_meta));
    memset(rels, 0, sizeof(rels));
    memset(&c_meta, 0, sizeof(c_meta));
    memset(c_cols, 0, sizeof(c_cols));

    c_cols[0].name = "id";
    c_cols[0].type = C_ORM_TYPE_INT32;
    c_cols[0].offset = offsetof(struct NestedChild, id);
    c_cols[1].name = "name";
    c_cols[1].type = C_ORM_TYPE_STRING;
    c_cols[1].offset = offsetof(struct NestedChild, name);
    c_meta.name = "child";
    c_meta.columns = c_cols;
    c_meta.num_columns = 2;
    c_meta.struct_size = sizeof(struct NestedChild);

    rels[0].field_name = "child_o2o";
    rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
    rels[0].target_meta = &c_meta;
    rels[0].local_key = "id";
    rels[0].foreign_key = "id";
    rels[0].struct_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].data_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2o_ctx);
    p_meta.relations = rels;
    p_meta.num_relations = 1;

    /* is_loaded=1, ptr != NULL, string col with *str_ptr == NULL (line 5524) */
    ctx = (c_orm_lazy_load_context_t *)(void *)((char *)&p +
                                                rels[0].lazy_ctx_offset);
    ctx->is_loaded = 1;
    p.child_o2o =
        (struct NestedChild *)c_orm_malloc(sizeof(struct NestedChild));
    if (p.child_o2o) {
      memset(p.child_o2o, 0, sizeof(struct NestedChild));
      c_orm_free_relations(&p_meta, &p);
    }

    /* ONE_TO_MANY with arr->data == NULL (line 5535) */
    rels[0].type = C_ORM_RELATION_ONE_TO_MANY;
    rels[0].field_name = "children_o2m";
    rels[0].struct_offset = offsetof(struct FullParentObj, children_o2m);
    rels[0].data_offset = offsetof(struct FullParentObj, children_o2m);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2m_ctx);
    ctx = (c_orm_lazy_load_context_t *)(void *)((char *)&p +
                                                rels[0].lazy_ctx_offset);
    ctx->is_loaded = 1;
    p.children_o2m.data = NULL;
    c_orm_free_relations(&p_meta, &p);

    /* ONE_TO_MANY with arr->data != NULL, child.name[0] == '\0' (line 5547) */
    p.children_o2m.data = c_orm_malloc(sizeof(struct NestedChild));
    p.children_o2m.length = 1;
    c_orm_free_relations(&p_meta, &p);
  }

  /* 20. Shard manager branches (lines 5658, 5708, 5733) */
  {
    c_orm_shard_manager_t *sm = NULL;
    c_orm_db_t *node = NULL;

    (void)c_orm_shard_manager_init(1, NULL);

    c_orm_shard_manager_init(2, &sm);
    (void)c_orm_shard_manager_add_node(sm, 0, NULL);

    (void)c_orm_shard_route_hash(sm, NULL, &node);
    (void)c_orm_shard_route_hash(sm, "key", NULL);

    c_orm_shard_manager_free(sm);
  }

  /* 21. scatter_gather_generic branches (lines 5789, 5816, 5848, 5853) */
  {
    c_orm_shard_manager_t *sm = NULL;
    void *out_arr = NULL;
    size_t out_cnt = 0;

    c_orm_shard_manager_init(1, &sm);
    (void)c_orm_scatter_gather_generic(sm, NULL, &out_arr, &out_cnt);
    (void)c_orm_scatter_gather_generic(sm, &Users_meta, NULL, &out_cnt);
    (void)c_orm_scatter_gather_generic(sm, &Users_meta, &out_arr, NULL);

    /* Shard returning shard_count == 0 (line 5816) */
    c_orm_shard_manager_add_node(sm, 0, &custom_db);
    custom_vt.step = mock_always_step_zero;
    (void)c_orm_scatter_gather_generic(sm, &Users_meta, &out_arr, &out_cnt);
    if (out_arr)
      C_ORM_FREE(out_arr);
    out_arr = NULL;

    /* Shard returning C_ORM_ERROR_NOT_FOUND (line 5848) */
    custom_vt.step = mock_always_step_fail;
    (void)c_orm_scatter_gather_generic(sm, &Users_meta, &out_arr, &out_cnt);

    c_orm_shard_manager_free(sm);
    custom_vt.step = g_vt.step;
  }

  /* 22. update_partial branches (lines 5986, 6042) */
  {
    const char *fields[1];
    struct Users u_obj;
    c_orm_table_meta_t no_col_meta;
    memset(&u_obj, 0, sizeof(u_obj));
    memset(&no_col_meta, 0, sizeof(no_col_meta));
    fields[0] = "username";

    (void)c_orm_update_partial(&custom_db, &Users_meta, &u_obj, fields, 0);

    no_col_meta = Users_meta;
    no_col_meta.num_columns = 0;
    (void)c_orm_update_partial(&custom_db, &no_col_meta, &u_obj, fields, 1);
  }

  /* 23. Attach / Detach / Sync branches (lines 6339, 6380, 6455, 6489, 6548,
   * 6563, 6629) */
  {
    struct FullParentObj p;
    c_orm_table_meta_t p_meta;
    c_orm_column_meta_t p_cols[2];
    c_orm_relation_meta_t rels[2];
    struct NestedChild child;
    c_orm_table_meta_t c_meta;
    c_orm_column_meta_t c_cols[2];

    memset(&p, 0, sizeof(p));
    memset(&child, 0, sizeof(child));
    memset(&p_meta, 0, sizeof(p_meta));
    memset(p_cols, 0, sizeof(p_cols));
    memset(rels, 0, sizeof(rels));
    memset(&c_meta, 0, sizeof(c_meta));
    memset(c_cols, 0, sizeof(c_cols));

    p_cols[0].name = "id";
    p_cols[0].type = C_ORM_TYPE_INT32;
    p_cols[0].is_pk = 1;
    p_cols[0].offset = offsetof(struct FullParentObj, id);
    p_meta.name = "p";
    p_meta.columns = p_cols;
    p_meta.num_columns = 1;
    c_cols[0].name = "id";
    c_cols[0].type = C_ORM_TYPE_INT32;
    c_cols[0].is_pk = 1;
    c_cols[0].offset = offsetof(struct NestedChild, id);
    c_meta.name = "child";
    c_meta.columns = c_cols;
    c_meta.num_columns = 1;

    rels[0].field_name = "tags_m2m";
    rels[0].type = C_ORM_RELATION_MANY_TO_MANY;
    rels[0].local_key = "id";
    rels[0].foreign_key = "id";
    rels[0].target_meta = NULL; /* line 6339, 6455, 6563 */
    p_meta.relations = rels;
    p_meta.num_relations = 1;

    (void)c_orm_attach(&custom_db, &p_meta, &p, "tags_m2m", &child);
    (void)c_orm_detach(&custom_db, &p_meta, &p, "tags_m2m", &child);
    (void)c_orm_sync(&custom_db, &p_meta, &p, "tags_m2m", &child, 1);

    /* sync with children_array == NULL and num_children > 0 (line 6548) */
    (void)c_orm_sync(&custom_db, &p_meta, &p, "tags_m2m", NULL, 1);

    /* Missing join keys (lines 6380, 6489, 6629) */
    rels[0].target_meta = &c_meta;
    rels[0].join_table = NULL;
    (void)c_orm_attach(&custom_db, &p_meta, &p, "tags_m2m", &child);
    (void)c_orm_detach(&custom_db, &p_meta, &p, "tags_m2m", &child);
    (void)c_orm_sync(&custom_db, &p_meta, &p, "tags_m2m", &child, 1);

    rels[0].join_table = "j";
    rels[0].join_local_key = NULL;
    (void)c_orm_attach(&custom_db, &p_meta, &p, "tags_m2m", &child);
    (void)c_orm_detach(&custom_db, &p_meta, &p, "tags_m2m", &child);
    (void)c_orm_sync(&custom_db, &p_meta, &p, "tags_m2m", &child, 1);

    rels[0].join_local_key = "lk";
    rels[0].join_foreign_key = NULL;
    (void)c_orm_attach(&custom_db, &p_meta, &p, "tags_m2m", &child);
    (void)c_orm_detach(&custom_db, &p_meta, &p, "tags_m2m", &child);
    (void)c_orm_sync(&custom_db, &p_meta, &p, "tags_m2m", &child, 1);
  }

  /* 24. insert_generic NOT_FOUND branch (line 6822) */
  {
    struct Users u_obj;
    memset(&u_obj, 0, sizeof(u_obj));
    custom_vt.step =
        mock_always_step_zero; /* returns NOT_FOUND if has_row == 0 */
    (void)c_orm_insert_generic(&custom_db, &Users_meta, &u_obj);
    custom_vt.step = g_vt.step;
  }

  /* 25. system_calloc OOM branch (line 7201) */
  {
    void *ptr = NULL;
    c_orm_set_allocators(cov_always_null_malloc, orig_r, orig_f);
    (void)c_orm_system_calloc(10, 10, &ptr);
    c_orm_set_allocators(orig_m, orig_r, orig_f);
  }

  setup_vt();
  c_orm_set_allocators(orig_m, orig_r, orig_f);

  PASS();
}

#endif /* TEST_API_TRANSACTIONS_H */
