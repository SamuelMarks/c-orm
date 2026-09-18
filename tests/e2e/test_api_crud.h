/**
 * @file test_api_crud.h
 * @brief CRUD operations, primary key, and batch operation tests for C ORM API.
 */

#ifndef TEST_API_CRUD_H
#define TEST_API_CRUD_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "test_api_helpers.h"

/** @brief Object representation with string primary key. */
struct StringPkObj {
  char *id;
  char *name;
};

/** @brief Object representation with int64 primary key. */
struct Int64PkObj {
  int64_t id;
  char *name;
};

/** @brief Model object representing a record with multiple columns for batch
 * updates. */
struct MultiColObj {
  int32_t id;
  char *name;
  int32_t score;
};

/**
 * @brief Forward declaration for test_api_crud_string_and_int64_pks.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_api_crud_string_and_int64_pks(void);

/**
 * @brief Tests CRUD operations with string and int64 primary keys and cascades.
 * @return GREATEST test result.
 */
TEST test_api_crud_string_and_int64_pks(void) {
  struct StringPkObj sobj;
  struct Int64PkObj iobj;
  c_orm_column_meta_t scols[2];
  c_orm_column_meta_t icols[2];
  c_orm_table_meta_t smeta;
  c_orm_table_meta_t imeta;
  c_orm_relation_meta_t rels[2];

  memset(&sobj, 0, sizeof(sobj));
  memset(&iobj, 0, sizeof(iobj));
  memset(scols, 0, sizeof(scols));
  memset(icols, 0, sizeof(icols));
  memset(&smeta, 0, sizeof(smeta));
  memset(&imeta, 0, sizeof(imeta));

  sobj.id = "str_pk";
  sobj.name = "str_name";
  scols[0].name = "id";
  scols[0].type = C_ORM_TYPE_STRING;
  scols[0].offset = offsetof(struct StringPkObj, id);
  scols[0].is_pk = 1;
  scols[1].name = "name";
  scols[1].type = C_ORM_TYPE_STRING;
  scols[1].offset = offsetof(struct StringPkObj, name);

  smeta.name = "str_table";
  smeta.columns = scols;
  smeta.num_columns = 2;
  smeta.struct_size = sizeof(sobj);
  smeta.query_update = "UPDATE str_table SET name=? WHERE id=?";
  smeta.query_delete_by_pk = "DELETE FROM str_table WHERE id=?";
  smeta.query_select_by_pk = "SELECT * FROM str_table WHERE id=?";

  iobj.id = 123456789;
  iobj.name = "int64_name";
  icols[0].name = "id";
  icols[0].type = C_ORM_TYPE_INT64;
  icols[0].offset = offsetof(struct Int64PkObj, id);
  icols[0].is_pk = 1;
  icols[1].name = "name";
  icols[1].type = C_ORM_TYPE_STRING;
  icols[1].offset = offsetof(struct Int64PkObj, name);

  imeta.name = "int64_table";
  imeta.columns = icols;
  imeta.num_columns = 2;
  imeta.struct_size = sizeof(iobj);
  imeta.query_update = "UPDATE int64_table SET name=? WHERE id=?";
  imeta.query_delete_by_pk = "DELETE FROM int64_table WHERE id=?";
  imeta.query_select_by_pk = "SELECT * FROM int64_table WHERE id=?";

  /* 1. Update with STRING and INT64 PK */
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_update(&g_db, &smeta, &sobj));
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_update(&g_db, &imeta, &iobj));

  /* Update error branches: missing query_update */
  smeta.query_update = NULL;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_update(&g_db, &smeta, &sobj));
  smeta.query_update = "UPDATE str_table SET name=? WHERE id=?";

  /* Delete error branches: missing query_delete_by_pk, view */
  smeta.query_delete_by_pk = NULL;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_delete(&g_db, &smeta, &sobj));
  smeta.query_delete_by_pk = "DELETE FROM str_table WHERE id=?";
  smeta.is_view = 1;
  ASSERT_EQ(C_ORM_ERROR_READ_ONLY, c_orm_delete(&g_db, &smeta, &sobj));
  smeta.is_view = 0;

  /* Delete error branches: unsupported PK type */
  scols[0].type = C_ORM_TYPE_BLOB;
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, c_orm_delete(&g_db, &smeta, &sobj));
  /* Delete error branches: no PK column found */
  scols[0].is_pk = 0;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_delete(&g_db, &smeta, &sobj));
  scols[0].is_pk = 1;
  scols[0].type = C_ORM_TYPE_STRING;

  /* 2. Delete with STRING and INT64 PK */
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_delete(&g_db, &smeta, &sobj));
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_delete(&g_db, &imeta, &iobj));

  /* Test c_orm_insert with get_last_insert_rowid returning 0 to exercise
   * get_int_field fallback */
  {
    struct ExtendedParent parent_obj;
    struct NestedChild child_obj;
    c_orm_column_meta_t pcols[2];
    c_orm_column_meta_t ccols[2];
    c_orm_relation_meta_t crels[1];
    c_orm_table_meta_t par_meta;
    c_orm_table_meta_t ch_meta;

    memset(&parent_obj, 0, sizeof(parent_obj));
    memset(&child_obj, 0, sizeof(child_obj));
    memset(pcols, 0, sizeof(pcols));
    memset(ccols, 0, sizeof(ccols));
    memset(crels, 0, sizeof(crels));
    memset(&par_meta, 0, sizeof(par_meta));
    memset(&ch_meta, 0, sizeof(ch_meta));

    parent_obj.id = 55;
    parent_obj.belongs_to_id = 0;
    parent_obj.child_o2o = &child_obj;

    pcols[0].name = "id";
    pcols[0].type = C_ORM_TYPE_INT32;
    pcols[0].offset = offsetof(struct ExtendedParent, id);
    pcols[0].is_pk = 1;

    pcols[1].name = "belongs_to_id";
    pcols[1].type = C_ORM_TYPE_INT32;
    pcols[1].offset = offsetof(struct ExtendedParent, belongs_to_id);

    ccols[0].name = "id";
    ccols[0].type = C_ORM_TYPE_INT32;
    ccols[0].offset = offsetof(struct NestedChild, id);
    ccols[0].is_pk = 1;

    ccols[1].name = "parent_id";
    ccols[1].type = C_ORM_TYPE_INT32;
    ccols[1].offset = offsetof(struct NestedChild, parent_id);

    ch_meta.name = "ch_meta";
    ch_meta.columns = ccols;
    ch_meta.num_columns = 2;
    ch_meta.struct_size = sizeof(struct NestedChild);
    ch_meta.query_insert = "INSERT INTO ch_meta (id, parent_id) VALUES (?, ?)";

    crels[0].field_name = "child";
    crels[0].type = C_ORM_RELATION_ONE_TO_ONE;
    crels[0].local_key = "id";
    crels[0].foreign_key = "parent_id";
    crels[0].target_meta = &ch_meta;
    crels[0].data_offset = offsetof(struct ExtendedParent, child_o2o);
    crels[0].struct_offset = offsetof(struct ExtendedParent, child_o2o);

    par_meta.name = "par_meta";
    par_meta.columns = pcols;
    par_meta.num_columns = 2;
    par_meta.relations = crels;
    par_meta.num_relations = 1;
    par_meta.struct_size = sizeof(struct ExtendedParent);
    par_meta.query_insert =
        "INSERT INTO par_meta (id, belongs_to_id) VALUES (?, ?)";

    g_mock_last_id =
        0; /* Forces parent_id <= 0, triggering get_int_field fallback! */
    ASSERT_EQ(C_ORM_OK, c_orm_insert(&g_db, &par_meta, &parent_obj));
    g_mock_last_id = 123;
  }

  /* 3. Delete with cascade and STRING/INT64 PK */
  memset(rels, 0, sizeof(rels));
  rels[0].field_name = "cascade_child";
  rels[0].type = C_ORM_RELATION_MANY_TO_MANY;
  rels[0].local_key = "id";
  rels[0].foreign_key = "child_id";
  rels[0].join_table = "parent_children";
  rels[0].join_local_key = "parent_id";
  rels[0].join_foreign_key = "child_id";
  rels[0].target_meta = &smeta;
  rels[0].on_delete = C_ORM_CASCADE_DELETE;

  rels[1].field_name = "null_child";
  rels[1].type = C_ORM_RELATION_ONE_TO_MANY;
  rels[1].local_key = "id";
  rels[1].foreign_key = "parent_id";
  rels[1].target_meta = &imeta;
  rels[1].on_delete = C_ORM_CASCADE_SET_NULL;

  smeta.relations = rels;
  smeta.num_relations = 2;
  imeta.relations = rels;
  imeta.num_relations = 2;

  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_delete(&g_db, &smeta, &sobj));
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_delete(&g_db, &imeta, &iobj));

  /* 4. Update batch with STRING and INT64 PK */
  {
    struct StringPkObj sarr[2];
    struct Int64PkObj iarr[2];
    sarr[0] = sobj;
    sarr[1] = sobj;
    iarr[0] = iobj;
    iarr[1] = iobj;
    g_step_count = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_update_batch(&g_db, &smeta, sarr, 2, 800));
    g_step_count = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_update_batch(&g_db, &imeta, iarr, 2, 800));
  }

  /* Multi-column update batch */
  {
    struct MultiColObj marr[2];
    c_orm_column_meta_t mcols[3];
    c_orm_table_meta_t mmeta;

    memset(marr, 0, sizeof(marr));
    memset(mcols, 0, sizeof(mcols));
    memset(&mmeta, 0, sizeof(mmeta));

    marr[0].id = 1;
    marr[0].name = "A";
    marr[0].score = 100;
    marr[1].id = 2;
    marr[1].name = "B";
    marr[1].score = 200;

    mcols[0].name = "id";
    mcols[0].type = C_ORM_TYPE_INT32;
    mcols[0].offset = offsetof(struct MultiColObj, id);
    mcols[0].is_pk = 1;

    mcols[1].name = "name";
    mcols[1].type = C_ORM_TYPE_STRING;
    mcols[1].offset = offsetof(struct MultiColObj, name);

    mcols[2].name = "score";
    mcols[2].type = C_ORM_TYPE_INT32;
    mcols[2].offset = offsetof(struct MultiColObj, score);

    mmeta.name = "multi_table";
    mmeta.columns = mcols;
    mmeta.num_columns = 3;
    mmeta.struct_size = sizeof(struct MultiColObj);

    g_step_count = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_update_batch(&g_db, &mmeta, marr, 2, 800));
  }

  PASS();
}

/**
 * @brief Forward declaration for test_api_batch_and_iterator_branches.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_api_batch_and_iterator_branches(void);

/**
 * @brief Tests batch find operations and query iterator lifecycle.
 * @return GREATEST test result.
 */
TEST test_api_batch_and_iterator_branches(void) {
  struct c_orm_iterator *iter;
  size_t fetched;
  char out_buf[128];
  c_orm_column_meta_t cols[1];
  c_orm_table_meta_t meta;
  c_orm_driver_vtable_t custom_vt;
  c_orm_db_t custom_db;

  iter = NULL;
  fetched = 0;
  memset(cols, 0, sizeof(cols));
  memset(&meta, 0, sizeof(meta));
  cols[0].name = "id";
  cols[0].type = C_ORM_TYPE_INT32;
  cols[0].offset = 0;
  cols[0].is_pk = 1;

  meta.name = "iter_test";
  meta.columns = cols;
  meta.num_columns = 1;
  meta.struct_size = sizeof(int32_t);
  meta.query_select_all = "SELECT id FROM iter_test";

  custom_vt = g_vt;
  custom_db = g_db;
  custom_vt.step = mock_step_sequence;
  custom_db.vtable = &custom_vt;

  /* 1. c_orm_find_batch_init */
  ASSERT_EQ(C_ORM_OK, c_orm_find_batch_init(&custom_db, &meta, NULL, 5, &iter));
  ASSERT(iter != NULL);

  /* 2. c_orm_iterator_next with rows */
  g_step_count = 3;
  ASSERT_EQ(C_ORM_OK, c_orm_iterator_next(iter, out_buf, &fetched));
  ASSERT_EQ(3, fetched);

  /* 3. c_orm_iterator_close */
  ASSERT_EQ(C_ORM_OK, c_orm_iterator_close(iter));

  /* 4. c_orm_find_one_by_string */
  g_step_count = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_find_one_by_string(&g_db, &mega_meta, "username",
                                               "Alice", out_buf));

  /* 5. c_orm_find_by_id_string missing query_select_by_pk */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_find_by_id_string(&g_db, &meta, "1", out_buf));

  /* 6. c_orm_update_partial with multiple fields */
  {
    const char *fields[2];
    c_orm_column_meta_t pcols[3];
    c_orm_table_meta_t pmeta;
    char pbuf[64];

    fields[0] = "name";
    fields[1] = "status";
    memset(pcols, 0, sizeof(pcols));
    memset(&pmeta, 0, sizeof(pmeta));
    memset(pbuf, 0, sizeof(pbuf));

    pcols[0].name = "id";
    pcols[0].type = C_ORM_TYPE_INT32;
    pcols[0].is_pk = 1;
    pcols[1].name = "name";
    pcols[1].type = C_ORM_TYPE_STRING;
    pcols[1].offset = 4;
    pcols[2].name = "status";
    pcols[2].type = C_ORM_TYPE_STRING;
    pcols[2].offset = 8;

    pmeta.name = "part_test";
    pmeta.columns = pcols;
    pmeta.num_columns = 3;
    pmeta.struct_size = 64;

    g_step_count = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_update_partial(&g_db, &pmeta, pbuf, fields, 2));
  }

  PASS();
}

/** @brief Failure stage selector for mock batch driver. */
static int g_batch_fail_vtable = -1;
/** @brief Error code flag for iterator hydration tests. */
static int g_iter_hydrate_fail = 0;

/**
 * @brief Mock batch prepare callback with failure injection.
 * @param db Database handle.
 * @param sql SQL string.
 * @param q Output query pointer.
 * @return C_ORM_OK on success or C_ORM_ERROR_UNKNOWN on injected failure.
 */
static c_orm_error_t mock_batch_prepare(c_orm_db_t *db, const char *sql,
                                        c_orm_query_t **q) {
  (void)db;
  if (g_batch_fail_vtable == 0 && strcmp(sql, "BEGIN") == 0)
    return C_ORM_ERROR_UNKNOWN;
  if (g_batch_fail_vtable == 4 && strcmp(sql, "COMMIT") == 0)
    return C_ORM_ERROR_UNKNOWN;
  if (g_batch_fail_vtable == 1 && strncmp(sql, "BEGIN", 5) != 0 &&
      strncmp(sql, "COMMIT", 6) != 0)
    return C_ORM_ERROR_UNKNOWN;
  if (q)
    *q = (c_orm_query_t *)1;
  return C_ORM_OK;
}

/**
 * @brief Mock batch bind_int32 callback with failure injection.
 * @param q Query pointer.
 * @param i Parameter index.
 * @param val Parameter value.
 * @return C_ORM_OK on success or C_ORM_ERROR_UNKNOWN on injected failure.
 */
static c_orm_error_t mock_batch_bind_int32(c_orm_query_t *q, int i,
                                           int32_t val) {
  (void)q;
  (void)i;
  (void)val;
  if (g_batch_fail_vtable == 2)
    return C_ORM_ERROR_UNKNOWN;
  return C_ORM_OK;
}

/**
 * @brief Mock batch step callback with failure injection.
 * @param q Query pointer.
 * @param has_row Output receiving 1 if row exists.
 * @return C_ORM_OK on success or C_ORM_ERROR_UNKNOWN on injected failure.
 */
static c_orm_error_t mock_batch_step(c_orm_query_t *q, int *has_row) {
  (void)q;
  if (g_batch_fail_vtable == 3)
    return C_ORM_ERROR_UNKNOWN;
  if (has_row)
    *has_row = 1;
  return C_ORM_OK;
}

/**
 * @brief Mock batch finalize callback with failure injection.
 * @param q Query pointer.
 * @return C_ORM_OK on success or C_ORM_ERROR_UNKNOWN on injected failure.
 */
static c_orm_error_t mock_batch_finalize(c_orm_query_t *q) {
  (void)q;
  if (g_batch_fail_vtable == 5)
    return C_ORM_ERROR_UNKNOWN;
  return C_ORM_OK;
}

/**
 * @brief Mock batch get_int32 callback with hydration error simulation.
 * @param q Query pointer.
 * @param i Column index.
 * @param out Output receiving int32 value.
 * @return C_ORM_OK on success or simulated error code.
 */
static c_orm_error_t mock_batch_get_int32(c_orm_query_t *q, int i,
                                          int32_t *out) {
  (void)q;
  (void)i;
  if (g_iter_hydrate_fail == 1) {
    g_iter_hydrate_fail = 0; /* Only fail once to hit expired */
    return C_ORM_ERROR_EXPIRED;
  }
  if (g_iter_hydrate_fail == 2) {
    return C_ORM_ERROR_TYPE_MISMATCH;
  }
  if (out)
    *out = 123;
  return C_ORM_OK;
}

/**
 * @brief Forward declaration for test_api_batch_crud_deep_errors.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_api_batch_crud_deep_errors(void);

/**
 * @brief Tests deep error branches for batch insert, update, delete, and
 * iteration.
 * @return GREATEST test result.
 */
TEST test_api_batch_crud_deep_errors(void) {
  struct Users items[2];
  c_orm_driver_vtable_t bvt;
  c_orm_db_t bdb;
  struct c_orm_iterator *iter;
  size_t has_row;
  void *(*orig_m)(size_t);
  void *(*orig_r)(void *, size_t);
  void (*orig_f)(void *);

  iter = NULL;
  has_row = 0;
  orig_m = c_orm_malloc;
  orig_r = c_orm_realloc;
  orig_f = c_orm_free;

  memset(items, 0, sizeof(items));
  memset(&bvt, 0, sizeof(bvt));
  memset(&bdb, 0, sizeof(bdb));

  items[0].id = 1;
  items[0].username = "u1";
  items[1].id = 2;
  items[1].username = "u2";

  bvt.prepare = mock_batch_prepare;
  bvt.step = mock_batch_step;
  bvt.bind_int32 = mock_batch_bind_int32;
  bvt.bind_string = mock_bind_string;
  bvt.bind_null = mock_bind_null;
  bvt.finalize = mock_batch_finalize;
  bvt.reset = mock_reset;
  bvt.is_null = mock_deep_is_null_false;
  bvt.get_int32 = mock_batch_get_int32;
  bvt.get_int64 = mock_deep_get_int64;
  bvt.get_double = mock_deep_get_double;
  bvt.get_blob = mock_deep_get_blob;
  bvt.get_string = mock_deep_get_string;

  bdb.vtable = &bvt;

  /* 1. insert_batch_ext failures */
  /* tx_begin fails */
  g_batch_fail_vtable = 0;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_insert_batch_ext(&bdb, &Users_meta, items, 2, 0,
                                   C_ORM_ON_CONFLICT_DO_NOTHING, NULL, NULL));

  /* prepare fails */
  g_batch_fail_vtable = 1;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_insert_batch_ext(&bdb, &Users_meta, items, 2, 0,
                                   C_ORM_ON_CONFLICT_DO_NOTHING, NULL, NULL));

  /* bind fails */
  g_batch_fail_vtable = 2;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_insert_batch_ext(&bdb, &Users_meta, items, 2, 0,
                                   C_ORM_ON_CONFLICT_DO_NOTHING, NULL, NULL));

  /* step fails */
  g_batch_fail_vtable = 3;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_insert_batch_ext(&bdb, &Users_meta, items, 2, 0,
                                   C_ORM_ON_CONFLICT_DO_NOTHING, NULL, NULL));

  /* tx_commit fails */
  g_batch_fail_vtable = 4;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_insert_batch_ext(&bdb, &Users_meta, items, 2, 0,
                                   C_ORM_ON_CONFLICT_DO_NOTHING, NULL, NULL));

  /* DO_UPDATE policy */
  g_batch_fail_vtable = -1;
  ASSERT_EQ(C_ORM_OK,
            c_orm_insert_batch_ext(&bdb, &Users_meta, items, 2, 0,
                                   C_ORM_ON_CONFLICT_DO_UPDATE, NULL, NULL));

  /* string_builder_init OOM */
  c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
  g_deep_fail_oom = 0;
  g_deep_alloc_cnt = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_insert_batch_ext(&bdb, &Users_meta, items, 2, 0,
                                   C_ORM_ON_CONFLICT_DO_NOTHING, NULL, NULL));
  c_orm_set_allocators(orig_m, orig_r, orig_f);
  g_deep_fail_oom = -1;

  /* 2. update_batch failures */
  /* tx_begin fails */
  g_batch_fail_vtable = 0;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_update_batch(&bdb, &Users_meta, items, 2, 0));

  /* prepare fails */
  g_batch_fail_vtable = 1;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_update_batch(&bdb, &Users_meta, items, 2, 0));

  /* bind fails */
  g_batch_fail_vtable = 2;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_update_batch(&bdb, &Users_meta, items, 2, 0));

  /* step fails */
  g_batch_fail_vtable = 3;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_update_batch(&bdb, &Users_meta, items, 2, 0));

  /* tx_commit fails */
  g_batch_fail_vtable = 4;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_update_batch(&bdb, &Users_meta, items, 2, 0));

  /* string_builder_init OOM */
  c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
  g_deep_fail_oom = 0;
  g_deep_alloc_cnt = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_update_batch(&bdb, &Users_meta, items, 2, 0));
  c_orm_set_allocators(orig_m, orig_r, orig_f);
  g_deep_fail_oom = -1;

  /* 3. delete_batch failures */
  /* tx_begin fails */
  g_batch_fail_vtable = 0;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_delete_batch(&bdb, &Users_meta, items, 2, 0));

  /* prepare fails */
  g_batch_fail_vtable = 1;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_delete_batch(&bdb, &Users_meta, items, 2, 0));

  /* bind fails */
  g_batch_fail_vtable = 2;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_delete_batch(&bdb, &Users_meta, items, 2, 0));

  /* tx_commit fails */
  g_batch_fail_vtable = 4;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_delete_batch(&bdb, &Users_meta, items, 2, 0));

  /* string_builder_init OOM */
  c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
  g_deep_fail_oom = 0;
  g_deep_alloc_cnt = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_delete_batch(&bdb, &Users_meta, items, 2, 0));
  c_orm_set_allocators(orig_m, orig_r, orig_f);
  g_deep_fail_oom = -1;

  /* 4. Iterator errors */
  /* find_batch_init OOM */
  c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
  g_deep_fail_oom = 0;
  g_deep_alloc_cnt = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_find_batch_init(&bdb, &Users_meta, "SELECT * FROM users", 1,
                                  &iter));
  c_orm_set_allocators(orig_m, orig_r, orig_f);
  g_deep_fail_oom = -1;

  /* find_batch_init prepare error */
  g_batch_fail_vtable = 1;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_find_batch_init(&bdb, &Users_meta, "SELECT * FROM users", 1,
                                  &iter));

  /* init iterator successfully */
  g_batch_fail_vtable = -1;
  ASSERT_EQ(C_ORM_OK, c_orm_find_batch_init(&bdb, &Users_meta,
                                            "SELECT * FROM users", 1, &iter));
  ASSERT(iter != NULL);

  /* iterator_next step error */
  g_batch_fail_vtable = 3;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_iterator_next(iter, &items[0], &has_row));
  g_batch_fail_vtable = -1;

  /* iterator_next EXPIRED hydrate branch */
  g_iter_hydrate_fail = 1;
  ASSERT_EQ(C_ORM_OK, c_orm_iterator_next(iter, &items[0], &has_row));

  /* iterator_next other hydrate error */
  g_iter_hydrate_fail = 2;
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH,
            c_orm_iterator_next(iter, &items[0], &has_row));
  g_iter_hydrate_fail = 0;

  /* iterator_close finalize error */
  g_batch_fail_vtable = 5;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_iterator_close(iter));
  g_batch_fail_vtable = -1;

  PASS();
}

/**
 * @brief Forward declaration for test_api_crud_missing_error_branches.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_api_crud_missing_error_branches(void);

/**
 * @brief Tests missing error branches for hooks, bindings, and multi-stage
 * operations.
 * @return GREATEST test result.
 */
TEST test_api_crud_missing_error_branches(void) {
  struct Users u;
  c_orm_driver_vtable_t custom_vt;
  c_orm_db_t custom_db;
  c_orm_table_meta_t u_meta;

  memset(&u, 0, sizeof(u));
  u.id = 1;
  u.username = "test_user";

  memcpy(&u_meta, &Users_meta, sizeof(u_meta));

  custom_vt = g_vt;
  custom_db = g_db;
  custom_vt.step = mock_stage_step;
  custom_vt.prepare = mock_stage_prepare;
  custom_vt.bind_int32 = mock_stage_bind_int32;
  custom_vt.finalize = mock_stage_finalize;
  custom_db.vtable = &custom_vt;

  /* 1. Insert hooks: BEFORE_INSERT, BEFORE_SAVE, AFTER_INSERT, AFTER_SAVE */
  u_meta.hooks[C_ORM_HOOK_BEFORE_INSERT] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&custom_db, &u_meta, &u));
  u_meta.hooks[C_ORM_HOOK_BEFORE_INSERT] = NULL;

  u_meta.hooks[C_ORM_HOOK_BEFORE_SAVE] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&custom_db, &u_meta, &u));
  u_meta.hooks[C_ORM_HOOK_BEFORE_SAVE] = NULL;

  u_meta.hooks[C_ORM_HOOK_AFTER_INSERT] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&custom_db, &u_meta, &u));
  u_meta.hooks[C_ORM_HOOK_AFTER_INSERT] = NULL;

  u_meta.hooks[C_ORM_HOOK_AFTER_SAVE] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&custom_db, &u_meta, &u));
  u_meta.hooks[C_ORM_HOOK_AFTER_SAVE] = NULL;

  /* 2. Insert bind failure and step failure */
  g_mock_err_stage = 3; /* bind fails */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&custom_db, &u_meta, &u));

  g_mock_err_stage = 1; /* step fails */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&custom_db, &u_meta, &u));
  g_mock_err_stage = 0;

  /* 3. Update bind failure, step failure, finalize failure */
  g_mock_err_stage = 3; /* bind fails */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_update(&custom_db, &u_meta, &u));

  g_mock_err_stage = 1; /* step fails */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_update(&custom_db, &u_meta, &u));

  g_mock_err_stage = 4; /* finalize fails */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_update(&custom_db, &u_meta, &u));
  g_mock_err_stage = 0;

  /* 4. Delete hooks and driver failures */
  u_meta.hooks[C_ORM_HOOK_BEFORE_DELETE] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_delete(&custom_db, &u_meta, &u));
  u_meta.hooks[C_ORM_HOOK_BEFORE_DELETE] = NULL;

  u_meta.hooks[C_ORM_HOOK_AFTER_DELETE] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_delete(&custom_db, &u_meta, &u));
  u_meta.hooks[C_ORM_HOOK_AFTER_DELETE] = NULL;

  g_mock_err_stage = 3; /* bind fails */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_delete(&custom_db, &u_meta, &u));

  g_mock_err_stage = 1; /* step fails */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_delete(&custom_db, &u_meta, &u));
  g_mock_err_stage = 0;

  /* 5. Hook successes (mock_hook_success returns C_ORM_OK) */
  u_meta.hooks[C_ORM_HOOK_BEFORE_SAVE] = mock_hook_success;
  u_meta.hooks[C_ORM_HOOK_BEFORE_INSERT] = mock_hook_success;
  u_meta.hooks[C_ORM_HOOK_AFTER_INSERT] = mock_hook_success;
  u_meta.hooks[C_ORM_HOOK_AFTER_SAVE] = mock_hook_success;
  ASSERT_EQ(C_ORM_OK, c_orm_insert(&custom_db, &u_meta, &u));
  u_meta.hooks[C_ORM_HOOK_BEFORE_SAVE] = NULL;
  u_meta.hooks[C_ORM_HOOK_BEFORE_INSERT] = NULL;
  u_meta.hooks[C_ORM_HOOK_AFTER_INSERT] = NULL;
  u_meta.hooks[C_ORM_HOOK_AFTER_SAVE] = NULL;

  /* Insert finalize failure */
  g_mock_err_stage = 4;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&custom_db, &u_meta, &u));
  g_mock_err_stage = 0;

  /* Update hooks returning C_ORM_OK */
  u_meta.hooks[C_ORM_HOOK_BEFORE_UPDATE] = mock_hook_success;
  u_meta.hooks[C_ORM_HOOK_BEFORE_SAVE] = mock_hook_success;
  u_meta.hooks[C_ORM_HOOK_AFTER_UPDATE] = mock_hook_success;
  u_meta.hooks[C_ORM_HOOK_AFTER_SAVE] = mock_hook_success;
  ASSERT_EQ(C_ORM_OK, c_orm_update(&custom_db, &u_meta, &u));
  u_meta.hooks[C_ORM_HOOK_BEFORE_UPDATE] = NULL;
  u_meta.hooks[C_ORM_HOOK_BEFORE_SAVE] = NULL;
  u_meta.hooks[C_ORM_HOOK_AFTER_UPDATE] = NULL;
  u_meta.hooks[C_ORM_HOOK_AFTER_SAVE] = NULL;

  /* Delete hooks returning C_ORM_OK and finalize failure */
  u_meta.hooks[C_ORM_HOOK_BEFORE_DELETE] = mock_hook_success;
  u_meta.hooks[C_ORM_HOOK_AFTER_DELETE] = mock_hook_success;
  ASSERT_EQ(C_ORM_OK, c_orm_delete(&custom_db, &u_meta, &u));
  u_meta.hooks[C_ORM_HOOK_BEFORE_DELETE] = NULL;
  u_meta.hooks[C_ORM_HOOK_AFTER_DELETE] = NULL;

  g_mock_err_stage = 4;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_delete(&custom_db, &u_meta, &u));
  g_mock_err_stage = 0;

  /* 6. PK bind failure on update */
  g_mock_fail_pk_bind = 1;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_update(&custom_db, &u_meta, &u));
  g_mock_fail_pk_bind = 0;

  /* 7. Batch step and hook failures */
  g_mock_err_stage = 1;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_delete_batch(&custom_db, &u_meta, &u, 1, 0));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_update_batch(&custom_db, &u_meta, &u, 1, 0));
  g_mock_err_stage = 0;

  u_meta.hooks[C_ORM_HOOK_AFTER_DELETE] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_delete_batch(&custom_db, &u_meta, &u, 1, 0));
  u_meta.hooks[C_ORM_HOOK_AFTER_DELETE] = NULL;

  u_meta.hooks[C_ORM_HOOK_AFTER_UPDATE] = dummy_failing_hook;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_update_batch(&custom_db, &u_meta, &u, 1, 0));
  u_meta.hooks[C_ORM_HOOK_AFTER_UPDATE] = NULL;

  /* 8. c_orm_find_one_by_string prepare failure */
  g_mock_err_stage = 2;
  ASSERT_EQ(
      C_ORM_ERROR_UNKNOWN,
      c_orm_find_one_by_string(&custom_db, &u_meta, "username", "val", &u));
  g_mock_err_stage = 0;

  /* 9. c_orm_validate_relations external meta */
  {
    const c_orm_table_meta_t *tbls[1];
    c_orm_table_meta_t ext_parent;
    c_orm_relation_meta_t ext_rel;
    c_orm_table_meta_t ext_target;
    memset(&ext_parent, 0, sizeof(ext_parent));
    memset(&ext_rel, 0, sizeof(ext_rel));
    memset(&ext_target, 0, sizeof(ext_target));
    ext_parent.name = "ext_p";
    ext_parent.num_relations = 1;
    ext_parent.relations = &ext_rel;
    ext_rel.target_meta = &ext_target;
    ext_target.name = "ext_t";
    tbls[0] = &ext_parent;
    ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, c_orm_validate_relations(tbls, 1));
  }

  PASS();
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* TEST_API_CRUD_H */
