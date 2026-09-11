/**
 * @file test_api_hydration.h
 * @brief Field hydration, column binding, and data conversion tests for C ORM
 * API.
 */

#ifndef TEST_API_HYDRATION_H
#define TEST_API_HYDRATION_H

#include "test_api_helpers.h"

TEST test_api_field_helpers_and_misc(void) {
  struct NestedParent p;
  c_orm_column_meta_t cols[1];
  c_orm_table_meta_t meta;

  memset(&p, 0, sizeof(p));
  memset(cols, 0, sizeof(cols));
  memset(&meta, 0, sizeof(meta));

  cols[0].name = "id";
  cols[0].type = C_ORM_TYPE_INT32;
  cols[0].offset = offsetof(struct NestedParent, id);
  cols[0].is_pk = 1;

  meta.name = "v_test";
  meta.columns = cols;
  meta.num_columns = 1;
  meta.struct_size = sizeof(p);

  /* 1. c_orm_save with view */
  meta.is_view = 1;
  ASSERT_EQ(C_ORM_ERROR_READ_ONLY, c_orm_save(&g_db, &meta, &p));
  meta.is_view = 0;

  /* 2. c_orm_save with string PK set */
  {
    struct StringPkObj sobj;
    c_orm_column_meta_t scols[1];
    c_orm_table_meta_t smeta;
    memset(&sobj, 0, sizeof(sobj));
    memset(scols, 0, sizeof(scols));
    memset(&smeta, 0, sizeof(smeta));
    sobj.id = "valid_pk";
    scols[0].name = "id";
    scols[0].type = C_ORM_TYPE_STRING;
    scols[0].is_pk = 1;
    scols[0].offset = offsetof(struct StringPkObj, id);
    smeta.name = "s_table";
    smeta.columns = scols;
    smeta.num_columns = 1;
    smeta.struct_size = sizeof(sobj);
    smeta.query_update = "UPDATE s_table SET id=? WHERE id=?";
    g_step_count = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_save(&g_db, &smeta, &sobj));
  }

  /* 3. Hydrate BLOB column with NULL / 0 size (exercises lines 433-435) */
  {
    struct BlobTestObj {
      int32_t id;
      c_orm_blob_t blob_val;
    } bobj;
    c_orm_column_meta_t bcols[2];
    c_orm_table_meta_t bmeta;

    memset(&bobj, 0, sizeof(bobj));
    memset(bcols, 0, sizeof(bcols));
    memset(&bmeta, 0, sizeof(bmeta));

    bcols[0].name = "id";
    bcols[0].type = C_ORM_TYPE_INT32;
    bcols[0].is_pk = 1;
    bcols[0].offset = offsetof(struct BlobTestObj, id);

    bcols[1].name = "blob_val";
    bcols[1].type = C_ORM_TYPE_BLOB;
    bcols[1].offset = offsetof(struct BlobTestObj, blob_val);

    bmeta.name = "blob_table";
    bmeta.columns = bcols;
    bmeta.num_columns = 2;
    bmeta.struct_size = sizeof(bobj);
    bmeta.query_select_by_pk = "SELECT id, blob_val FROM blob_table WHERE id=?";

    g_mock_blob_null = 1;
    g_step_count = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_find_by_id_int32(&g_db, &bmeta, 1, &bobj));
    ASSERT_EQ(NULL, bobj.blob_val.data);
    ASSERT_EQ(0, bobj.blob_val.size);
    g_mock_blob_null = 0;
  }

  PASS();
}

/* ========================================================================= */
/* --- Deep Hydrate and Bind Coverage --- */
/* ========================================================================= */

struct DeepHydrateObj {
  int32_t id;
  bool *b_nullable;
  unsigned char b_val;
  int64_t *i64_nullable;
  int64_t i64_val;
  float *f_nullable;
  float f_val;
  double *d_nullable;
  double d_val;
  char *ts_val;
  char *sec_val;
  c_orm_point_t pt;
  c_orm_polygon_t poly;
  c_orm_blob_t blob;
};

static void free_deep_obj(struct DeepHydrateObj *obj) {
  if (obj->b_nullable) {
    free(obj->b_nullable);
    obj->b_nullable = NULL;
  }
  if (obj->i64_nullable) {
    free(obj->i64_nullable);
    obj->i64_nullable = NULL;
  }
  if (obj->f_nullable) {
    free(obj->f_nullable);
    obj->f_nullable = NULL;
  }
  if (obj->d_nullable) {
    free(obj->d_nullable);
    obj->d_nullable = NULL;
  }
  if (obj->ts_val) {
    free(obj->ts_val);
    obj->ts_val = NULL;
  }
  if (obj->sec_val) {
    free(obj->sec_val);
    obj->sec_val = NULL;
  }
  if (obj->poly.points) {
    free(obj->poly.points);
    obj->poly.points = NULL;
  }
}

struct PrefixChildObj {
  int32_t id;
  char *name;
  c_orm_blob_t blob;
  unsigned char is_flag;
};

struct PrefixParentObj {
  int32_t id;
  struct PrefixChildObj *child;
  c_orm_lazy_load_context_t child_ctx;
};

static c_orm_error_t mock_prefix_col_count_err(c_orm_query_t *q, int *cnt) {
  (void)q;
  (void)cnt;
  return C_ORM_ERROR_UNKNOWN;
}
static c_orm_error_t mock_prefix_col_name_err(c_orm_query_t *q, int i,
                                              const char **n) {
  (void)q;
  (void)i;
  (void)n;
  return C_ORM_ERROR_UNKNOWN;
}
static c_orm_error_t mock_prefix_col_name_null(c_orm_query_t *q, int i,
                                               const char **n) {
  (void)q;
  (void)i;
  if (n)
    *n = NULL;
  return C_ORM_OK;
}
static c_orm_error_t mock_prefix_col_count_ok(c_orm_query_t *q, int *cnt) {
  (void)q;
  if (cnt)
    *cnt = 5;
  return C_ORM_OK;
}
static c_orm_error_t mock_prefix_col_name_ok(c_orm_query_t *q, int i,
                                             const char **n) {
  (void)q;
  if (!n)
    return C_ORM_OK;
  if (i == 1)
    *n = "child_id";
  else if (i == 2)
    *n = "child_name";
  else if (i == 3)
    *n = "child_blob";
  else if (i == 4)
    *n = "child_is_flag";
  else
    *n = "id";
  return C_ORM_OK;
}

struct DirtyTrackedObj {
  uint64_t flags;
  int32_t id;
  char *name;
  c_orm_blob_t sec_blob;
  c_orm_polygon_t poly;
};

TEST test_api_hydrate_and_bind_deep(void) {
  struct DeepHydrateObj dobj;
  c_orm_column_meta_t cols[14];
  c_orm_table_meta_t dmeta;
  c_orm_driver_vtable_t dvt;
  c_orm_db_t ddb;
  c_orm_error_t rc;
  int i;
  void *(*orig_m)(size_t) = c_orm_malloc;
  void *(*orig_r)(void *, size_t) = c_orm_realloc;
  void (*orig_f)(void *) = c_orm_free;

  memset(&dobj, 0, sizeof(dobj));
  memset(cols, 0, sizeof(cols));
  memset(&dmeta, 0, sizeof(dmeta));
  memset(&dvt, 0, sizeof(dvt));
  memset(&ddb, 0, sizeof(ddb));

  cols[0].name = "id";
  cols[0].type = C_ORM_TYPE_INT32;
  cols[0].is_pk = 1;
  cols[0].offset = offsetof(struct DeepHydrateObj, id);
  cols[1].name = "b_null";
  cols[1].type = C_ORM_TYPE_BOOL;
  cols[1].is_nullable = 1;
  cols[1].offset = offsetof(struct DeepHydrateObj, b_nullable);
  cols[2].name = "b_val";
  cols[2].type = C_ORM_TYPE_BOOL;
  cols[2].offset = offsetof(struct DeepHydrateObj, b_val);
  cols[3].name = "i64_null";
  cols[3].type = C_ORM_TYPE_INT64;
  cols[3].is_nullable = 1;
  cols[3].offset = offsetof(struct DeepHydrateObj, i64_nullable);
  cols[4].name = "i64_val";
  cols[4].type = C_ORM_TYPE_INT64;
  cols[4].offset = offsetof(struct DeepHydrateObj, i64_val);
  cols[5].name = "f_null";
  cols[5].type = C_ORM_TYPE_FLOAT;
  cols[5].is_nullable = 1;
  cols[5].offset = offsetof(struct DeepHydrateObj, f_nullable);
  cols[6].name = "f_val";
  cols[6].type = C_ORM_TYPE_FLOAT;
  cols[6].offset = offsetof(struct DeepHydrateObj, f_val);
  cols[7].name = "d_null";
  cols[7].type = C_ORM_TYPE_DOUBLE;
  cols[7].is_nullable = 1;
  cols[7].offset = offsetof(struct DeepHydrateObj, d_nullable);
  cols[8].name = "d_val";
  cols[8].type = C_ORM_TYPE_DOUBLE;
  cols[8].offset = offsetof(struct DeepHydrateObj, d_val);
  cols[9].name = "ts_val";
  cols[9].type = C_ORM_TYPE_TIMESTAMP;
  cols[9].offset = offsetof(struct DeepHydrateObj, ts_val);
  cols[10].name = "sec_val";
  cols[10].type = C_ORM_TYPE_STRING;
  cols[10].is_secure = 1;
  cols[10].offset = offsetof(struct DeepHydrateObj, sec_val);
  cols[11].name = "pt";
  cols[11].type = C_ORM_TYPE_POINT;
  cols[11].offset = offsetof(struct DeepHydrateObj, pt);
  cols[12].name = "poly";
  cols[12].type = C_ORM_TYPE_POLYGON;
  cols[12].offset = offsetof(struct DeepHydrateObj, poly);
  cols[13].name = "blob";
  cols[13].type = C_ORM_TYPE_BLOB;
  cols[13].is_secure = 1;
  cols[13].offset = offsetof(struct DeepHydrateObj, blob);

  dmeta.name = "deep_table";
  dmeta.columns = cols;
  dmeta.num_columns = 14;
  dmeta.struct_size = sizeof(dobj);

  dvt.is_null = mock_deep_is_null_false;
  dvt.get_int32 = mock_deep_get_int32;
  dvt.get_int64 = mock_deep_get_int64;
  dvt.get_double = mock_deep_get_double;
  dvt.get_blob = mock_deep_get_blob;
  dvt.get_string = mock_deep_get_string;
  dvt.bind_int32 = mock_bind_int32;
  dvt.bind_int64 = mock_bind_int64;
  dvt.bind_double = mock_bind_double;
  dvt.bind_string = mock_bind_string;
  dvt.bind_blob = mock_bind_blob;
  dvt.bind_null = mock_bind_null;
  dvt.prepare = mock_prepare;
  dvt.step = mock_step;
  dvt.finalize = mock_finalize;
  dvt.reset = mock_reset;

  ddb.vtable = &dvt;
  ddb.timezone.offset_minutes = 60;
  ddb.decrypt_hook = mock_decrypt;
  ddb.encrypt_hook = mock_encrypt;

  /* 1. Normal successful hydration of all types */
  g_deep_fail_get = -1;
  g_deep_fail_oom = -1;
  rc = c_orm_hydrate_row_from(&ddb, (c_orm_query_t *)1, &dmeta, &dobj, 0);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(dobj.b_nullable != NULL);
  ASSERT_EQ(1, *dobj.b_nullable);
  ASSERT(dobj.i64_nullable != NULL);
  ASSERT(dobj.f_nullable != NULL);
  ASSERT(dobj.d_nullable != NULL);
  ASSERT(dobj.ts_val != NULL);
  ASSERT(dobj.sec_val != NULL);
  ASSERT(dobj.poly.points != NULL);
  free_deep_obj(&dobj);

  /* 2. Getter failures */
  for (i = 0; i < 5; i++) {
    g_deep_fail_get = i;
    memset(&dobj, 0, sizeof(dobj));
    rc = c_orm_hydrate_row_from(&ddb, (c_orm_query_t *)1, &dmeta, &dobj, 0);
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
    free_deep_obj(&dobj);
  }
  g_deep_fail_get = -1;

  /* 2b. Invalid timestamp string to reach line 281 */
  g_deep_ts_str = "invalid_timestamp";
  memset(&dobj, 0, sizeof(dobj));
  rc = c_orm_hydrate_row_from(&ddb, (c_orm_query_t *)1, &dmeta, &dobj, 0);
  ASSERT_EQ(C_ORM_OK, rc);
  free_deep_obj(&dobj);
  g_deep_ts_str = "2024-01-01 12:00:00";

  /* 3. Decrypt hook failure */
  ddb.decrypt_hook = mock_failing_decrypt_hook;
  memset(&dobj, 0, sizeof(dobj));
  rc = c_orm_hydrate_row_from(&ddb, (c_orm_query_t *)1, &dmeta, &dobj, 0);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  free_deep_obj(&dobj);
  ddb.decrypt_hook = mock_decrypt;

  /* 4. OOM failures during hydration */
  c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
  cols[13].is_secure = 0; /* Plain blob for lines 423-428 */
  for (i = 0; i < 9; i++) {
    g_deep_fail_oom = i;
    g_deep_alloc_cnt = 0;
    memset(&dobj, 0, sizeof(dobj));
    rc = c_orm_hydrate_row_from(&ddb, (c_orm_query_t *)1, &dmeta, &dobj, 0);
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
    free_deep_obj(&dobj);
  }
  cols[13].is_secure = 1;
  c_orm_set_allocators(orig_m, orig_r, orig_f);
  g_deep_fail_oom = -1;

  /* 5. Prefix column hydration in c_orm_hydrate_row */
  {
    struct PrefixParentObj pobj;
    struct PrefixChildObj cobj;
    c_orm_column_meta_t p_col;
    c_orm_column_meta_t c_cols[4];
    c_orm_relation_meta_t prel;
    c_orm_table_meta_t p_meta;
    c_orm_table_meta_t c_meta;

    memset(&pobj, 0, sizeof(pobj));
    memset(&cobj, 0, sizeof(cobj));
    memset(&p_col, 0, sizeof(p_col));
    memset(c_cols, 0, sizeof(c_cols));
    memset(&prel, 0, sizeof(prel));
    memset(&p_meta, 0, sizeof(p_meta));
    memset(&c_meta, 0, sizeof(c_meta));

    p_col.name = "id";
    p_col.type = C_ORM_TYPE_INT32;
    p_col.is_pk = 1;
    p_meta.name = "parent";
    p_meta.columns = &p_col;
    p_meta.num_columns = 1;
    p_meta.struct_size = sizeof(pobj);

    c_cols[0].name = "id";
    c_cols[0].type = C_ORM_TYPE_INT32;
    c_cols[0].is_pk = 1;
    c_cols[0].offset = offsetof(struct PrefixChildObj, id);
    c_cols[1].name = "name";
    c_cols[1].type = C_ORM_TYPE_STRING;
    c_cols[1].offset = offsetof(struct PrefixChildObj, name);
    c_cols[2].name = "blob";
    c_cols[2].type = C_ORM_TYPE_BLOB;
    c_cols[2].offset = offsetof(struct PrefixChildObj, blob);
    c_cols[3].name = "is_flag";
    c_cols[3].type = C_ORM_TYPE_BOOL;
    c_cols[3].offset = offsetof(struct PrefixChildObj, is_flag);

    c_meta.name = "child";
    c_meta.columns = c_cols;
    c_meta.num_columns = 4;
    c_meta.struct_size = sizeof(cobj);

    prel.field_name = "child";
    prel.type = C_ORM_RELATION_ONE_TO_ONE;
    prel.target_meta = &c_meta;
    prel.struct_offset = offsetof(struct PrefixParentObj, child);
    prel.data_offset = offsetof(struct PrefixParentObj, child);
    prel.lazy_ctx_offset = offsetof(struct PrefixParentObj, child_ctx);

    p_meta.relations = &prel;
    p_meta.num_relations = 1;

    dvt.get_column_name = mock_prefix_col_name_err;
    dvt.get_column_count = mock_prefix_col_count_err;
    rc = c_orm_hydrate_row(&ddb, (c_orm_query_t *)1, &p_meta, &pobj);
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

    dvt.get_column_count = mock_prefix_col_count_ok;
    dvt.get_column_name = mock_prefix_col_name_err;
    rc = c_orm_hydrate_row(&ddb, (c_orm_query_t *)1, &p_meta, &pobj);
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

    dvt.get_column_name = mock_prefix_col_name_null;
    rc = c_orm_hydrate_row(&ddb, (c_orm_query_t *)1, &p_meta, &pobj);
    ASSERT_EQ(C_ORM_OK, rc);

    dvt.get_column_name = mock_prefix_col_name_ok;
    rc = c_orm_hydrate_row(&ddb, (c_orm_query_t *)1, &p_meta, &pobj);
    ASSERT_EQ(C_ORM_OK, rc);
    if (pobj.child) {
      if (pobj.child->name)
        free(pobj.child->name);
      free(pobj.child);
      pobj.child = NULL;
    }
  }

  /* 6. bind_row branches: dirty tracking, encryption errors, and polygon wkb */
  {
    struct DirtyTrackedObj tobj;
    c_orm_column_meta_t tcols[4];
    c_orm_table_meta_t tmeta;
    c_orm_point_t pts[2];

    memset(&tobj, 0, sizeof(tobj));
    memset(tcols, 0, sizeof(tcols));
    memset(&tmeta, 0, sizeof(tmeta));
    memset(pts, 0, sizeof(pts));

    pts[0].x = 1.0;
    pts[0].y = 2.0;
    pts[1].x = 3.0;
    pts[1].y = 4.0;
    tobj.flags = 0; /* Not dirty */
    tobj.id = 10;
    tobj.name = "test_name";
    tobj.sec_blob.data = "rawbytes";
    tobj.sec_blob.size = 8;
    tobj.poly.points = pts;
    tobj.poly.num_points = 2;

    tcols[0].name = "id";
    tcols[0].type = C_ORM_TYPE_INT32;
    tcols[0].is_pk = 1;
    tcols[0].offset = offsetof(struct DirtyTrackedObj, id);
    tcols[1].name = "name";
    tcols[1].type = C_ORM_TYPE_STRING;
    tcols[1].is_secure = 1;
    tcols[1].offset = offsetof(struct DirtyTrackedObj, name);
    tcols[2].name = "sec_blob";
    tcols[2].type = C_ORM_TYPE_BLOB;
    tcols[2].is_secure = 1;
    tcols[2].offset = offsetof(struct DirtyTrackedObj, sec_blob);
    tcols[3].name = "poly";
    tcols[3].type = C_ORM_TYPE_POLYGON;
    tcols[3].offset = offsetof(struct DirtyTrackedObj, poly);

    tmeta.name = "tracked";
    tmeta.columns = tcols;
    tmeta.num_columns = 4;
    tmeta.struct_size = sizeof(tobj);
    tmeta.query_insert =
        "INSERT INTO tracked (id, name, sec_blob, poly) VALUES (?, ?, ?, ?)";

    /* skip_clean = 1 with flags = 0 exercises line 2022 */
    rc = c_orm_insert(&ddb, &tmeta, &tobj);
    ASSERT_EQ(C_ORM_OK, rc);

    /* Encryption hook failure on string */
    ddb.encrypt_hook = mock_failing_encrypt_hook;
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&ddb, &tmeta, &tobj));
    ddb.encrypt_hook = mock_encrypt;

    /* Encryption hook failure on blob (turn off secure for string to reach
     * blob) */
    tcols[1].is_secure = 0;
    ddb.encrypt_hook = mock_failing_encrypt_hook;
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert(&ddb, &tmeta, &tobj));
    ddb.encrypt_hook = mock_encrypt;
    tcols[1].is_secure = 1;

    /* Polygon WKB OOM */
    c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
    g_deep_fail_oom = 0;
    g_deep_alloc_cnt = 0;
    rc = c_orm_insert(&ddb, &tmeta, &tobj);
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
    c_orm_set_allocators(orig_m, orig_r, orig_f);
    g_deep_fail_oom = -1;
  }

  PASS();
}

/* ========================================================================= */
/* --- Field Helpers, Introspection, and Validation Coverage --- */
/* ========================================================================= */

struct FieldHelperParent {
  int32_t id;
  int32_t *i32_null;
  int64_t *i64_null;
  int64_t i64_val;
  char *str_val;
};

struct FieldHelperChild {
  int32_t id;
  int32_t *i32_null;
  int64_t *i64_null;
  int64_t i64_val;
  float f_val;
  double d_val;
};

TEST test_api_field_helpers_and_introspection(void) {
  struct FieldHelperParent parent;
  struct FieldHelperChild child;
  c_orm_column_meta_t p_cols[5];
  c_orm_column_meta_t c_cols[6];
  c_orm_relation_meta_t rel;
  c_orm_table_meta_t p_meta;
  c_orm_table_meta_t c_meta;
  c_orm_driver_vtable_t dvt;
  c_orm_db_t ddb;
  c_orm_error_t rc;
  int64_t i64_v = 200;
  void *(*orig_m)(size_t) = c_orm_malloc;
  void *(*orig_r)(void *, size_t) = c_orm_realloc;
  void (*orig_f)(void *) = c_orm_free;

  memset(&parent, 0, sizeof(parent));
  memset(&child, 0, sizeof(child));
  memset(p_cols, 0, sizeof(p_cols));
  memset(c_cols, 0, sizeof(c_cols));
  memset(&rel, 0, sizeof(rel));
  memset(&p_meta, 0, sizeof(p_meta));
  memset(&c_meta, 0, sizeof(c_meta));
  memset(&dvt, 0, sizeof(dvt));
  memset(&ddb, 0, sizeof(ddb));

  p_cols[0].name = "id";
  p_cols[0].type = C_ORM_TYPE_INT32;
  p_cols[0].is_pk = 1;
  p_cols[0].offset = offsetof(struct FieldHelperParent, id);
  p_cols[1].name = "i32_null";
  p_cols[1].type = C_ORM_TYPE_INT32;
  p_cols[1].is_nullable = 1;
  p_cols[1].offset = offsetof(struct FieldHelperParent, i32_null);
  p_cols[2].name = "i64_null";
  p_cols[2].type = C_ORM_TYPE_INT64;
  p_cols[2].is_nullable = 1;
  p_cols[2].offset = offsetof(struct FieldHelperParent, i64_null);
  p_cols[3].name = "i64_val";
  p_cols[3].type = C_ORM_TYPE_INT64;
  p_cols[3].offset = offsetof(struct FieldHelperParent, i64_val);
  p_cols[4].name = "str_val";
  p_cols[4].type = C_ORM_TYPE_STRING;
  p_cols[4].offset = offsetof(struct FieldHelperParent, str_val);

  p_meta.name = "fparent";
  p_meta.columns = p_cols;
  p_meta.num_columns = 5;
  p_meta.struct_size = sizeof(parent);
  p_meta.query_insert = "INSERT INTO fparent (id) VALUES (?)";
  p_meta.query_update = "UPDATE fparent SET id=? WHERE id=?";

  c_cols[0].name = "id";
  c_cols[0].type = C_ORM_TYPE_INT32;
  c_cols[0].is_pk = 1;
  c_cols[0].offset = offsetof(struct FieldHelperChild, id);
  c_cols[1].name = "i32_null";
  c_cols[1].type = C_ORM_TYPE_INT32;
  c_cols[1].is_nullable = 1;
  c_cols[1].offset = offsetof(struct FieldHelperChild, i32_null);
  c_cols[2].name = "i64_null";
  c_cols[2].type = C_ORM_TYPE_INT64;
  c_cols[2].is_nullable = 1;
  c_cols[2].offset = offsetof(struct FieldHelperChild, i64_null);
  c_cols[3].name = "i64_val";
  c_cols[3].type = C_ORM_TYPE_INT64;
  c_cols[3].offset = offsetof(struct FieldHelperChild, i64_val);
  c_cols[4].name = "f_val";
  c_cols[4].type = C_ORM_TYPE_FLOAT;
  c_cols[4].offset = offsetof(struct FieldHelperChild, f_val);
  c_cols[5].name = "d_val";
  c_cols[5].type = C_ORM_TYPE_DOUBLE;
  c_cols[5].offset = offsetof(struct FieldHelperChild, d_val);

  c_meta.name = "fchild";
  c_meta.columns = c_cols;
  c_meta.num_columns = 6;
  c_meta.struct_size = sizeof(child);
  c_meta.query_insert = "INSERT INTO fchild (id) VALUES (?)";
  c_meta.query_update = "UPDATE fchild SET id=? WHERE id=?";

  rel.field_name = "fchild_rel";
  rel.type = C_ORM_RELATION_ONE_TO_MANY;
  rel.target_meta = &c_meta;
  rel.local_key = "id";
  rel.foreign_key = "id";

  p_meta.relations = &rel;
  p_meta.num_relations = 1;

  dvt.prepare = mock_prepare;
  dvt.step = mock_step;
  dvt.finalize = mock_finalize;
  dvt.reset = mock_reset;
  dvt.bind_int32 = mock_bind_int32;
  dvt.bind_int64 = mock_bind_int64;
  dvt.bind_double = mock_bind_double;
  dvt.bind_string = mock_bind_string;
  dvt.bind_blob = mock_bind_blob;
  dvt.bind_null = mock_bind_null;
  dvt.is_null = mock_deep_is_null_false;
  dvt.get_int32 = mock_deep_get_int32;
  ddb.vtable = &dvt;

  /* 1. set_null_field via c_orm_detach */
  rel.foreign_key = "i64_val";
  child.i64_val = 999;
  ASSERT_EQ(C_ORM_OK,
            c_orm_detach(&ddb, &p_meta, &parent, "fchild_rel", &child));
  ASSERT_EQ(0, child.i64_val);

  rel.foreign_key = "f_val";
  child.f_val = 99.0f;
  ASSERT_EQ(C_ORM_OK,
            c_orm_detach(&ddb, &p_meta, &parent, "fchild_rel", &child));
  ASSERT_EQ(0, (int)child.f_val);

  rel.foreign_key = "d_val";
  child.d_val = 99.0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_detach(&ddb, &p_meta, &parent, "fchild_rel", &child));
  ASSERT_EQ(0, (int)child.d_val);

  rel.foreign_key = "nonexistent";
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_detach(&ddb, &p_meta, &parent, "fchild_rel", &child));

  /* 2. get_int_field via c_orm_attach */
  parent.id = 55;
  rel.foreign_key = "id";

  /* 2a. i32_null is NULL -> NOT_FOUND */
  rel.local_key = "i32_null";
  parent.i32_null = NULL;
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_attach(&ddb, &p_meta, &parent, "fchild_rel", &child));

  /* 2b. i64_null is NULL -> NOT_FOUND (lines 1961-1962) */
  rel.local_key = "i64_null";
  parent.i64_null = NULL;
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_attach(&ddb, &p_meta, &parent, "fchild_rel", &child));

  /* 2c. i64_null is non-NULL -> success (line 1964) */
  parent.i64_null = &i64_v;
  ASSERT_EQ(C_ORM_OK,
            c_orm_attach(&ddb, &p_meta, &parent, "fchild_rel", &child));

  /* 2d. i64_val is non-nullable -> success (line 1966-1967) */
  rel.local_key = "i64_val";
  parent.i64_val = 777;
  ASSERT_EQ(C_ORM_OK,
            c_orm_attach(&ddb, &p_meta, &parent, "fchild_rel", &child));

  /* 2e. str_val -> TYPE_MISMATCH (line 1972) */
  rel.local_key = "str_val";
  parent.str_val = "str";
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH,
            c_orm_attach(&ddb, &p_meta, &parent, "fchild_rel", &child));

  /* 2f. nonexistent local_key -> NOT_FOUND (line 1974) */
  rel.local_key = "nonexistent";
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_attach(&ddb, &p_meta, &parent, "fchild_rel", &child));

  /* 3. set_int_field via c_orm_attach */
  rel.local_key = "id";
  parent.id = 42;

  /* 3a. i32_null is NULL -> malloc succeeds (lines 1892-1894) */
  rel.foreign_key = "i32_null";
  child.i32_null = NULL;
  ASSERT_EQ(C_ORM_OK,
            c_orm_attach(&ddb, &p_meta, &parent, "fchild_rel", &child));
  ASSERT(child.i32_null != NULL);
  ASSERT_EQ(42, *child.i32_null);
  free(child.i32_null);
  child.i32_null = NULL;

  /* 3b. i32_null is NULL -> malloc fails (line 1896) */
  c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
  g_deep_fail_oom = 0;
  g_deep_alloc_cnt = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_attach(&ddb, &p_meta, &parent, "fchild_rel", &child));
  c_orm_set_allocators(orig_m, orig_r, orig_f);
  g_deep_fail_oom = -1;

  /* 3c. i64_null is NULL -> malloc succeeds (lines 1898, 1900-1902) */
  rel.foreign_key = "i64_null";
  child.i64_null = NULL;
  ASSERT_EQ(C_ORM_OK,
            c_orm_attach(&ddb, &p_meta, &parent, "fchild_rel", &child));
  ASSERT(child.i64_null != NULL);
  ASSERT_EQ(42, *child.i64_null);
  free(child.i64_null);
  child.i64_null = NULL;

  /* 3d. i64_null is NULL -> malloc fails (lines 1911, 1913) */
  c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
  g_deep_fail_oom = 0;
  g_deep_alloc_cnt = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_attach(&ddb, &p_meta, &parent, "fchild_rel", &child));
  c_orm_set_allocators(orig_m, orig_r, orig_f);
  g_deep_fail_oom = -1;

  /* 3e. i64_val non-nullable -> sets val (line 1918) */
  rel.foreign_key = "i64_val";
  child.i64_val = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_attach(&ddb, &p_meta, &parent, "fchild_rel", &child));
  ASSERT_EQ(42, child.i64_val);

  /* 4. c_orm_dfs_validate_table & c_orm_validate_relations */
  {
    c_orm_table_meta_t ta, tb, tc, td;
    c_orm_relation_meta_t ra[2], rb[1], rc_rel[1];
    const c_orm_table_meta_t *tables[4];
    struct sql_table_t sql_tbl;
    struct sql_column_t sql_col;
    struct sql_constraint_t sql_c;
    c_orm_relation_meta_t *built_rels = NULL;
    size_t n_built = 0;

    memset(&ta, 0, sizeof(ta));
    memset(&tb, 0, sizeof(tb));
    memset(&tc, 0, sizeof(tc));
    memset(&td, 0, sizeof(td));
    memset(ra, 0, sizeof(ra));
    memset(rb, 0, sizeof(rb));
    memset(rc_rel, 0, sizeof(rc_rel));

    ta.name = "A";
    tb.name = "B";
    tc.name = "C";
    td.name = "D";

    /* Cycle: A -> B -> A */
    ra[0].target_table = "B";
    ta.relations = ra;
    ta.num_relations = 1;
    rb[0].target_table = "A";
    tb.relations = rb;
    tb.num_relations = 1;
    tables[0] = &ta;
    tables[1] = &tb;
    printf("validate_rel ret = %d\n", (int)c_orm_validate_relations(tables, 2));
    ASSERT_EQ(C_ORM_ERROR_RECURSION, c_orm_validate_relations(tables, 2));

    /* Diamond: A -> B, A -> C; B -> D, C -> D */
    ra[0].target_table = "B";
    ra[1].target_table = "C";
    ta.relations = ra;
    ta.num_relations = 2;
    rb[0].target_table = "D";
    tb.relations = rb;
    tb.num_relations = 1;
    rc_rel[0].target_table = "D";
    tc.relations = rc_rel;
    tc.num_relations = 1;
    td.relations = NULL;
    td.num_relations = 0;
    tables[0] = &ta;
    tables[1] = &tb;
    tables[2] = &tc;
    tables[3] = &td;
    ASSERT_EQ(C_ORM_OK, c_orm_validate_relations(tables, 4));

    /* c_orm_validate_relations OOM */
    c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
    g_deep_fail_oom = 0;
    g_deep_alloc_cnt = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_validate_relations(tables, 4));
    c_orm_set_allocators(orig_m, orig_r, orig_f);
    g_deep_fail_oom = -1;

    /* c_orm_build_relation_meta OOM */
    memset(&sql_tbl, 0, sizeof(sql_tbl));
    memset(&sql_col, 0, sizeof(sql_col));
    memset(&sql_c, 0, sizeof(sql_c));
    sql_c.type = SQL_CONSTRAINT_FOREIGN_KEY;
    sql_col.constraints = &sql_c;
    sql_col.n_constraints = 1;
    sql_tbl.columns = &sql_col;
    sql_tbl.n_columns = 1;

    c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
    g_deep_fail_oom = 0;
    g_deep_alloc_cnt = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              c_orm_build_relation_meta(&sql_tbl, &built_rels, &n_built));
    c_orm_set_allocators(orig_m, orig_r, orig_f);
    g_deep_fail_oom = -1;
  }

  /* 5. c_orm_deep_copy stub */
  {
    c_orm_table_meta_t d_meta;
    char src_b[8] = {0}, dst_b[8] = {0};
    memset(&d_meta, 0, sizeof(d_meta));
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
              c_orm_deep_copy((const struct cdd_c_meta *)1, dst_b, src_b));
  }

  /* 6. c_orm_update_partial with INT64 PK */
  {
    struct T64Obj {
      char *name;
      int64_t id;
    } t64;
    c_orm_column_meta_t pk64_cols[2];
    c_orm_table_meta_t pk64_meta;
    const char *fields[1] = {"name"};

    memset(&t64, 0, sizeof(t64));
    memset(pk64_cols, 0, sizeof(pk64_cols));
    memset(&pk64_meta, 0, sizeof(pk64_meta));
    t64.name = "test";
    t64.id = 12345;

    pk64_cols[0].name = "name";
    pk64_cols[0].type = C_ORM_TYPE_STRING;
    pk64_cols[0].offset = offsetof(struct T64Obj, name);
    pk64_cols[1].name = "id";
    pk64_cols[1].type = C_ORM_TYPE_INT64;
    pk64_cols[1].is_pk = 1;
    pk64_cols[1].offset = offsetof(struct T64Obj, id);

    pk64_meta.name = "t64";
    pk64_meta.columns = pk64_cols;
    pk64_meta.num_columns = 2;
    pk64_meta.struct_size = sizeof(t64);
    pk64_meta.query_update = "UPDATE t64 SET name=? WHERE id=?";
    pk64_meta.query_insert = "INSERT INTO t64 (name, id) VALUES (?, ?)";

    ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
              c_orm_update_partial(&g_db, &pk64_meta, &t64, fields, 1));

    /* 7. c_orm_save with INT64 PK */
    ASSERT_EQ(C_ORM_OK, c_orm_save(&g_db, &pk64_meta, &t64));
  }

  /* 8. c_orm_lazy_load_paginated match */
  {
    ASSERT_EQ(C_ORM_OK, c_orm_lazy_load_paginated(&ddb, &p_meta, &parent,
                                                  "fchild_rel", 5, 0));
  }

  /* 9. c_orm_find_one_by_string builder failures */
  {
    c_orm_column_meta_t scol;
    c_orm_table_meta_t smeta;
    int32_t out_obj = 0;
    memset(&scol, 0, sizeof(scol));
    memset(&smeta, 0, sizeof(smeta));
    scol.name = "id";
    scol.type = C_ORM_TYPE_INT32;
    scol.is_pk = 1;
    smeta.name = "stbl";
    smeta.columns = &scol;
    smeta.num_columns = 1;
    smeta.struct_size = sizeof(out_obj);

    c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
    g_deep_fail_oom = 1;
    g_deep_alloc_cnt = 0;
    rc = c_orm_find_one_by_string(&ddb, &smeta, "id", "val", &out_obj);
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

    g_deep_fail_oom = 2;
    g_deep_alloc_cnt = 0;
    rc = c_orm_find_one_by_string(&ddb, &smeta, "id", "val", &out_obj);
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
    c_orm_set_allocators(orig_m, orig_r, orig_f);
    g_deep_fail_oom = -1;
  }

  PASS();
}

static c_orm_error_t mock_cov_blob_null_100(c_orm_query_t *q, int i,
                                            const void **val, size_t *size) {
  (void)q;
  (void)i;
  *val = NULL;
  *size = 0;
  return C_ORM_OK;
}

static c_orm_error_t mock_cov_blob_zero_100(c_orm_query_t *q, int i,
                                            const void **val, size_t *size) {
  (void)q;
  (void)i;
  *val = g_dummy_blob_data_100;
  *size = 0;
  return C_ORM_OK;
}

static c_orm_error_t mock_prefix_col_name_no_underscore(c_orm_query_t *q, int i,
                                                        const char **n) {
  (void)q;
  (void)i;
  if (n)
    *n = "childxyz";
  return C_ORM_OK;
}

static c_orm_error_t mock_always_step_not_found(c_orm_query_t *q,
                                                int *has_row) {
  (void)q;
  if (has_row)
    *has_row = 0;
  return C_ORM_ERROR_NOT_FOUND;
}

static int g_scatter_step_call = 0;
static c_orm_error_t mock_scatter_mixed_step(c_orm_query_t *q, int *has_row) {
  (void)q;
  if (!has_row)
    return C_ORM_ERROR_UNKNOWN;
  g_scatter_step_call++;
  if (g_scatter_step_call == 1) {
    *has_row = 0;
    return C_ORM_ERROR_NOT_FOUND;
  }
  if (g_scatter_step_call == 2) {
    *has_row = 0;
    return C_ORM_ERROR_SQL;
  }
  if (g_scatter_step_call == 3) {
    *has_row = 1;
    return C_ORM_OK;
  }
  *has_row = 0;
  return C_ORM_OK;
}

static c_orm_error_t mock_get_last_rowid_zero(c_orm_db_t *db, int64_t *out_id) {
  (void)db;
  if (out_id)
    *out_id = 0;
  return C_ORM_OK;
}

static c_orm_error_t mock_get_last_rowid_fail(c_orm_db_t *db, int64_t *out_id) {
  (void)db;
  (void)out_id;
  return C_ORM_ERROR_UNKNOWN;
}

static c_orm_error_t mock_is_null_only_prefix(c_orm_query_t *q, int i,
                                              int *out) {
  (void)q;
  if (out)
    *out = (i == 1) ? 1 : 0;
  return C_ORM_OK;
}

static int g_step_parent_only_cnt = 0;
static c_orm_error_t mock_step_parent_only(c_orm_query_t *q, int *has_row) {
  (void)q;
  if (!has_row)
    return C_ORM_ERROR_UNKNOWN;
  if (g_step_parent_only_cnt++ == 0) {
    *has_row = 1;
  } else {
    *has_row = 0;
  }
  return C_ORM_OK;
}

static int g_step_rel_cnt = 0;
static c_orm_error_t mock_step_rel_o2m(c_orm_query_t *q, int *has_row) {
  (void)q;
  if (!has_row)
    return C_ORM_ERROR_UNKNOWN;
  g_step_rel_cnt++;
  if (g_step_rel_cnt == 1) {
    *has_row = 1;
    return C_ORM_OK;
  }
  if (g_step_rel_cnt == 2) {
    *has_row = 0;
    return C_ORM_OK;
  }
  if (g_step_rel_cnt >= 3 && g_step_rel_cnt <= 7) {
    *has_row = 1;
    return C_ORM_OK;
  }
  *has_row = 0;
  return C_ORM_OK;
}

static c_orm_error_t mock_step_rel_o2o(c_orm_query_t *q, int *has_row) {
  (void)q;
  if (!has_row)
    return C_ORM_ERROR_UNKNOWN;
  g_step_rel_cnt++;
  if (g_step_rel_cnt == 1) {
    *has_row = 1;
    return C_ORM_OK;
  }
  if (g_step_rel_cnt == 2) {
    *has_row = 0;
    return C_ORM_OK;
  }
  if (g_step_rel_cnt == 3 || g_step_rel_cnt == 4) {
    *has_row = 1;
    return C_ORM_OK;
  }
  *has_row = 0;
  return C_ORM_OK;
}

TEST test_api_hydration_types_and_boundaries(void) {
  c_orm_driver_vtable_t custom_vt;
  c_orm_db_t custom_db;
  void *(*orig_m)(size_t);
  void *(*orig_r)(void *, size_t);
  void (*orig_f)(void *);

  orig_m = c_orm_malloc;
  orig_r = c_orm_realloc;
  orig_f = c_orm_free;

  custom_vt = g_vt;
  custom_db = g_db;
  custom_db.vtable = &custom_vt;

  /* 1. c_orm_hydrate_row_from: BLOB null, Point/Poly/Blob NULL val, Blob zero
   * size, Expire NULL cb */
  {
    struct MultiColBlobObj {
      c_orm_blob_t b_col;
      c_orm_point_t pt_col;
      c_orm_polygon_t poly_col;
    } m_obj;
    c_orm_table_meta_t m_meta;
    c_orm_column_meta_t m_cols[3];

    struct TtlObj {
      int32_t id;
      int64_t created_at;
      int32_t expires_in;
    } t_obj;
    c_orm_table_meta_t t_meta;

    memset(&m_obj, 0, sizeof(m_obj));
    memset(&m_meta, 0, sizeof(m_meta));
    memset(m_cols, 0, sizeof(m_cols));

    m_cols[0].name = "b_col";
    m_cols[0].type = C_ORM_TYPE_BLOB;
    m_cols[0].is_nullable = 1;
    m_cols[0].offset = offsetof(struct MultiColBlobObj, b_col);
    m_cols[1].name = "pt_col";
    m_cols[1].type = C_ORM_TYPE_POINT;
    m_cols[1].offset = offsetof(struct MultiColBlobObj, pt_col);
    m_cols[2].name = "poly_col";
    m_cols[2].type = C_ORM_TYPE_POLYGON;
    m_cols[2].offset = offsetof(struct MultiColBlobObj, poly_col);

    m_meta.name = "blob_test";
    m_meta.columns = m_cols;
    m_meta.num_columns = 3;

    /* Line 115: is_null true with b_col.data == NULL and is_nullable == 1 */
    m_obj.b_col.data = NULL;
    custom_vt.is_null = mock_is_null_true;
    (void)c_orm_hydrate_row_from(&custom_db, (c_orm_query_t *)1, &m_meta,
                                 &m_obj, 0);
    custom_vt.is_null = mock_is_null_false;

    /* Lines 346, 362, 401: mock_cov_blob_null_100 */
    custom_vt.get_blob = mock_cov_blob_null_100;
    (void)c_orm_hydrate_row_from(&custom_db, (c_orm_query_t *)1, &m_meta,
                                 &m_obj, 0);

    /* Line 401: mock_cov_blob_zero_100 */
    custom_vt.get_blob = mock_cov_blob_zero_100;
    (void)c_orm_hydrate_row_from(&custom_db, (c_orm_query_t *)1, &m_meta,
                                 &m_obj, 0);
    custom_vt.get_blob = g_vt.get_blob;

    /* Line 466: expire_cb is NULL with num_columns == 0 */
    memset(&t_obj, 0, sizeof(t_obj));
    memset(&t_meta, 0, sizeof(t_meta));
    t_meta.name = "ttl_test";
    t_meta.num_columns = 0;
    t_meta.has_ttl = 1;
    t_meta.created_at_offset = offsetof(struct TtlObj, created_at);
    t_meta.expires_in_offset = offsetof(struct TtlObj, expires_in);

    t_obj.created_at = 1;
    t_obj.expires_in = 1;
    custom_db.expire_cb = NULL;
    (void)c_orm_hydrate_row_from(&custom_db, (c_orm_query_t *)1, &t_meta,
                                 &t_obj, 0);
  }

  /* 2. c_orm_hydrate_row: Prefix hydration branches (lines 524, 551, 554, 563)
   */
  {
    struct FullParentObj p;
    c_orm_table_meta_t p_meta;
    c_orm_column_meta_t p_cols[2];
    c_orm_relation_meta_t rels[2];
    c_orm_table_meta_t c_meta;
    c_orm_column_meta_t c_cols[2];

    memset(&p, 0, sizeof(p));
    memset(&p_meta, 0, sizeof(p_meta));
    memset(p_cols, 0, sizeof(p_cols));
    memset(rels, 0, sizeof(rels));
    memset(&c_meta, 0, sizeof(c_meta));
    memset(c_cols, 0, sizeof(c_cols));

    p_cols[0].name = "id";
    p_cols[0].type = C_ORM_TYPE_INT32;
    p_cols[0].is_nullable = 0;
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

    custom_vt.get_column_count = mock_prefix_col_count_ok;

    /* Line 524: col_name prefix matches but col_name[prefix_len] != '_' */
    custom_vt.get_column_name = mock_prefix_col_name_no_underscore;
    (void)c_orm_hydrate_row(&custom_db, (c_orm_query_t *)1, &p_meta, &p);

    /* Line 551: p.child_o2o == NULL and is_null is true for prefix */
    custom_vt.get_column_name = mock_prefix_col_name_ok;
    custom_vt.is_null = mock_is_null_only_prefix;
    p.child_o2o = NULL;
    (void)c_orm_hydrate_row(&custom_db, (c_orm_query_t *)1, &p_meta, &p);

    /* Line 563: nested_struct != NULL but is_null is true for prefix */
    p.child_o2o =
        (struct NestedChild *)c_orm_malloc(sizeof(struct NestedChild));
    if (p.child_o2o) {
      memset(p.child_o2o, 0, sizeof(struct NestedChild));
      (void)c_orm_hydrate_row(&custom_db, (c_orm_query_t *)1, &p_meta, &p);
      C_ORM_FREE(p.child_o2o);
      p.child_o2o = NULL;
    }

    /* Line 554: nested_struct allocation fails */
    custom_vt.is_null = mock_is_null_false;
    p.child_o2o = NULL;
    c_orm_set_allocators(cov_always_null_malloc, orig_r, orig_f);
    (void)c_orm_hydrate_row(&custom_db, (c_orm_query_t *)1, &p_meta, &p);
    c_orm_set_allocators(orig_m, orig_r, orig_f);

    custom_vt.get_column_count = g_vt.get_column_count;
    custom_vt.get_column_name = g_vt.get_column_name;
    custom_vt.is_null = g_vt.is_null;
  }

  /* 3. Lines 992, 999: c_orm_find_with_relation_int32 column loops */
  {
    c_orm_table_meta_t empty_meta;
    struct Users u_obj;
    c_orm_column_meta_t str_pk_col;
    c_orm_table_meta_t c_meta;
    c_orm_relation_meta_t rels[1];
    memset(&empty_meta, 0, sizeof(empty_meta));
    memset(&u_obj, 0, sizeof(u_obj));
    memset(&c_meta, 0, sizeof(c_meta));
    memset(rels, 0, sizeof(rels));

    c_meta.name = "c";
    c_meta.num_columns = 0;
    rels[0].field_name = "rel";
    rels[0].target_meta = &c_meta;
    rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
    empty_meta.relations = rels;
    empty_meta.num_relations = 1;

    /* Line 992 branch 0 and line 999 branch 0: num_columns == 0 */
    empty_meta.name = "empty";
    empty_meta.num_columns = 0;
    (void)c_orm_find_with_relation_int32(&custom_db, &empty_meta, 1, "rel",
                                         &u_obj);

    /* Line 999 branch 1: pk_col->type is STRING */
    str_pk_col = Users_meta.columns[0];
    str_pk_col.type = C_ORM_TYPE_STRING;
    str_pk_col.is_pk = 1;
    empty_meta.columns = &str_pk_col;
    empty_meta.num_columns = 1;
    (void)c_orm_find_with_relation_int32(&custom_db, &empty_meta, 1, "rel",
                                         &u_obj);
  }

  /* 4. Lines 1368, 1372, 1459, 1461, 1488: c_orm_find_all_with_relation */
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

    p.id = 0;
    p_cols[0].name = "id";
    p_cols[0].type = C_ORM_TYPE_INT32;
    p_cols[0].is_pk = 1;
    p_cols[0].offset = offsetof(struct FullParentObj, id);
    p_meta.name = "p";
    p_meta.columns = p_cols;
    p_meta.num_columns = 1;
    p_meta.struct_size = sizeof(struct FullParentObj);
    p_meta.query_select_all = "SELECT 1 FROM p";

    c_cols[0].name = "id";
    c_cols[0].type = C_ORM_TYPE_INT32;
    c_cols[0].is_pk = 1;
    c_cols[0].offset = offsetof(struct NestedChild, id);
    c_meta.name = "child";
    c_meta.columns = c_cols;
    c_meta.num_columns = 1;
    c_meta.struct_size = sizeof(struct NestedChild);
    c_meta.query_select_by_pk = "SELECT 1 FROM child WHERE id = ?";

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

    /* Lines 1368, 1372 empty strings, and Line 1491: 5 rows to trigger cap*2 */
    out_arr.data = c_orm_malloc(sizeof(struct FullParentObj));
    if (out_arr.data) {
      memcpy(out_arr.data, &p, sizeof(struct FullParentObj));
      out_arr.length = 1;
      out_arr.capacity = 1;
      rels[0].custom_filter = "";
      rels[0].order_by = "";
      custom_vt.step = mock_step_rel_o2m;
      g_step_rel_cnt = 0;
      (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "children_o2m",
                                         &out_arr);
      if (out_arr.data) {
        struct FullParentObj *fpo = (struct FullParentObj *)out_arr.data;
        if (fpo->children_o2m.data)
          C_ORM_FREE(fpo->children_o2m.data);
        C_ORM_FREE(out_arr.data);
        out_arr.data = NULL;
      }
      custom_vt.step = g_vt.step;
    }
    rels[0].custom_filter = NULL;
    rels[0].order_by = NULL;

    /* Line 1462: target_data_ptr already non-NULL in BELONGS_TO */
    rels[0].type = C_ORM_RELATION_BELONGS_TO;
    rels[0].field_name = "child_o2o";
    rels[0].struct_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].data_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2o_ctx);

    p.child_o2o = NULL;
    out_arr.data = c_orm_malloc(sizeof(struct FullParentObj));
    if (out_arr.data) {
      memcpy(out_arr.data, &p, sizeof(struct FullParentObj));
      out_arr.length = 1;
      out_arr.capacity = 1;
      custom_vt.step = mock_step_rel_o2o;
      g_step_rel_cnt = 0;
      (void)c_orm_find_all_with_relation(&custom_db, &p_meta, "child_o2o",
                                         &out_arr);
      custom_vt.step = g_vt.step;
      if (out_arr.data) {
        struct FullParentObj *fpo = (struct FullParentObj *)out_arr.data;
        if (fpo->child_o2o)
          C_ORM_FREE(fpo->child_o2o);
        C_ORM_FREE(out_arr.data);
        out_arr.data = NULL;
      }
    }
  }

  /* 5. Lines 1647, 1648, 1721: find_relation_meta args and nested null */
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
    c_meta.query_select_by_pk = "SELECT 1 FROM child WHERE id = ?";

    rels[0].field_name = "child_o2o";
    rels[0].type = C_ORM_RELATION_BELONGS_TO;
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

    paths[0] = "child_o2o.nonexistent";

    /* Line 1725 branch 0: nested_obj is NULL */
    p.id = 1;
    p.child_o2o = NULL;
    g_step_parent_only_cnt = 0;
    custom_vt.step = mock_step_parent_only;
    (void)c_orm_find_with_relations_int32(&custom_db, &p_meta, 1, paths, 1, &p);
    custom_vt.step = g_vt.step;
  }

  /* 6. Lines 1865, 1871, 1885, 1915, 1918, 1949: set_null_field and
   * set_int_field via detach/attach */
  {
    struct FullParentObj par;
    struct NestedChild ch;
    c_orm_table_meta_t par_meta, ch_meta;
    c_orm_column_meta_t par_c[1], ch_c[1];
    c_orm_relation_meta_t p_rel[1];

    memset(&par, 0, sizeof(par));
    memset(&ch, 0, sizeof(ch));
    memset(&par_meta, 0, sizeof(par_meta));
    memset(&ch_meta, 0, sizeof(ch_meta));
    memset(par_c, 0, sizeof(par_c));
    memset(ch_c, 0, sizeof(ch_c));
    memset(p_rel, 0, sizeof(p_rel));

    par_c[0].name = "id";
    par_c[0].type = C_ORM_TYPE_INT32;
    par_c[0].is_pk = 1;
    par_c[0].offset = offsetof(struct FullParentObj, id);
    par_meta.name = "par";
    par_meta.columns = par_c;
    par_meta.num_columns = 1;

    ch_c[0].name = "id";
    ch_c[0].type = C_ORM_TYPE_INT32;
    ch_c[0].is_pk = 1;
    ch_c[0].offset = offsetof(struct NestedChild, id);
    ch_meta.name = "ch";
    ch_meta.columns = ch_c;
    ch_meta.num_columns = 1;

    p_rel[0].field_name = "kids";
    p_rel[0].type = C_ORM_RELATION_ONE_TO_MANY;
    p_rel[0].target_meta = &ch_meta;
    p_rel[0].local_key = "id";
    p_rel[0].foreign_key = "id";
    par_meta.relations = p_rel;
    par_meta.num_relations = 1;

    /* Line 1865 branch 0: is_nullable == 1 and pointer is NULL */
    ch_c[0].is_nullable = 1;
    (void)c_orm_detach(&custom_db, &par_meta, &par, "kids", &ch);

    /* Line 1885: non-nullable STRING column in set_null_field */
    ch_c[0].is_nullable = 0;
    ch_c[0].type = C_ORM_TYPE_STRING;
    (void)c_orm_detach(&custom_db, &par_meta, &par, "kids", &ch);

    /* Line 1949: STRING column in set_int_field */
    (void)c_orm_attach(&custom_db, &par_meta, &par, "kids", &ch);

    /* Line 1918 branch 0: is_nullable == 1 and already allocated in
     * set_int_field */
    {
      int32_t val = 99;
      int32_t *pval = &val;
      ch_c[0].type = C_ORM_TYPE_INT32;
      ch_c[0].is_nullable = 1;
      memcpy((char *)&ch + ch_c[0].offset, &pval, sizeof(int32_t *));
      (void)c_orm_attach(&custom_db, &par_meta, &par, "kids", &ch);
    }
  }

  /* 7. Lines 2045, 2071: UUID and Timestamp binding */
  {
    struct UuidTsObj {
      char *id;
      char *ts;
    } ut_obj;
    c_orm_table_meta_t ut_meta;
    c_orm_column_meta_t ut_cols[2];

    c_orm_table_meta_t ts_pk_meta;
    c_orm_column_meta_t ts_pk_col[1];
    struct {
      char *ts;
    } ts_pk_obj;

    memset(&ut_obj, 0, sizeof(ut_obj));
    memset(&ut_meta, 0, sizeof(ut_meta));
    memset(ut_cols, 0, sizeof(ut_cols));
    memset(&ts_pk_meta, 0, sizeof(ts_pk_meta));
    memset(ts_pk_col, 0, sizeof(ts_pk_col));

    ut_cols[0].name = "id";
    ut_cols[0].type = C_ORM_TYPE_STRING;
    ut_cols[0].is_pk = 1;
    ut_cols[0].offset = offsetof(struct UuidTsObj, id);

    ut_cols[1].name = "ts";
    ut_cols[1].type = C_ORM_TYPE_TIMESTAMP;
    ut_cols[1].offset = offsetof(struct UuidTsObj, ts);

    ut_meta.name = "uuid_ts";
    ut_meta.columns = ut_cols;
    ut_meta.num_columns = 2;
    ut_meta.struct_size = sizeof(ut_obj);
    ut_meta.query_insert = "INSERT INTO uuid_ts VALUES (?, ?)";

    /* Line 2045 branch 3: PK is TIMESTAMP (string-like but not STRING) */
    ts_pk_obj.ts = "2026-09-11 12:00:00";
    ts_pk_col[0].name = "ts";
    ts_pk_col[0].type = C_ORM_TYPE_TIMESTAMP;
    ts_pk_col[0].is_pk = 1;
    ts_pk_col[0].offset = 0;
    ts_pk_meta.name = "ts_pk";
    ts_pk_meta.columns = ts_pk_col;
    ts_pk_meta.num_columns = 1;
    ts_pk_meta.query_insert = "INSERT INTO ts_pk VALUES (?)";
    (void)c_orm_insert(&custom_db, &ts_pk_meta, &ts_pk_obj);

    /* Line 2071: valid timestamp with timezone offset != 0 */
    custom_db.timezone.offset_minutes = 60;
    ut_obj.id = "existing-uuid-123";
    ut_obj.ts = "2026-09-11 12:00:00";
    (void)c_orm_insert(&custom_db, &ut_meta, &ut_obj);
    custom_db.timezone.offset_minutes = 0;
  }

  /* 8. Line 2594: iterator_close with iter->query == NULL */
  {
    struct c_orm_iterator_int {
      c_orm_db_t *db;
      const c_orm_table_meta_t *meta;
      c_orm_query_t *query;
      size_t chunk_size;
    };
    struct c_orm_iterator *iter = NULL;
    (void)c_orm_find_batch_init(&custom_db, &Users_meta, NULL, 10, &iter);
    if (iter) {
      struct c_orm_iterator_int *it = (struct c_orm_iterator_int *)(void *)iter;
      it->query = NULL;
      c_orm_iterator_close(iter);
    }
  }

  /* 9. Lines 2686, 2689, 2741, 2803: insert relation branches */
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

    rels[0].field_name = "child_o2o";
    rels[0].type = C_ORM_RELATION_BELONGS_TO;
    rels[0].target_meta = &c_meta;
    rels[0].local_key = "id";
    rels[0].foreign_key = "id";
    rels[0].struct_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].data_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2o_ctx);
    p_meta.relations = rels;
    p_meta.num_relations = 1;

    /* Lines 2689, 2808, 2895: target_meta is NULL */
    rels[0].target_meta = NULL;
    (void)c_orm_insert(&custom_db, &p_meta, &p);
    (void)c_orm_update(&custom_db, &p_meta, &p);
    rels[0].target_meta = &c_meta;

    /* Line 2686 branch 0: get_last_insert_rowid is NULL */
    p.child_o2o = &child;
    custom_vt.get_last_insert_rowid = NULL;
    (void)c_orm_insert(&custom_db, &p_meta, &p);

    /* Line 2689 branch 1: get_last_insert_rowid returns 0 */
    custom_vt.get_last_insert_rowid = mock_get_last_rowid_zero;
    (void)c_orm_insert(&custom_db, &p_meta, &p);

    /* Lines 2816 & 2824: parent_id == 0 in ONE_TO_ONE */
    rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
    p.child_o2o = &child;
    p.id = 0;
    custom_vt.get_last_insert_rowid = mock_get_last_rowid_zero;
    (void)c_orm_insert(&custom_db, &p_meta, &p);

    /* Line 2816 branch 1: get_last_insert_rowid returns error in ONE_TO_ONE */
    custom_vt.get_last_insert_rowid = mock_get_last_rowid_fail;
    (void)c_orm_insert(&custom_db, &p_meta, &p);

    /* Line 2816: get_last_insert_rowid == NULL in ONE_TO_ONE */
    custom_vt.get_last_insert_rowid = NULL;
    (void)c_orm_insert(&custom_db, &p_meta, &p);
    custom_vt.get_last_insert_rowid = g_vt.get_last_insert_rowid;

    /* Lines 2754 & 2938: nullable FK with local_key not found and with 0 */
    {
      struct NullableFkObj {
        int32_t *id;
        void *child_ptr;
      } n_obj;
      int32_t actual_id;
      c_orm_table_meta_t n_meta;
      c_orm_column_meta_t n_cols[1];
      c_orm_relation_meta_t n_rels[1];

      actual_id = 0;
      memset(&n_obj, 0, sizeof(n_obj));
      memset(&n_meta, 0, sizeof(n_meta));
      memset(n_cols, 0, sizeof(n_cols));
      memset(n_rels, 0, sizeof(n_rels));

      n_cols[0].name = "id";
      n_cols[0].type = C_ORM_TYPE_INT32;
      n_cols[0].is_nullable = 1;
      n_cols[0].offset = offsetof(struct NullableFkObj, id);

      n_rels[0].field_name = "child";
      n_rels[0].type = C_ORM_RELATION_BELONGS_TO;
      n_rels[0].target_meta = &c_meta;
      n_rels[0].foreign_key = "id";
      n_rels[0].local_key = "nonexistent";
      n_rels[0].struct_offset = offsetof(struct NullableFkObj, child_ptr);
      n_rels[0].data_offset = offsetof(struct NullableFkObj, child_ptr);

      n_meta.name = "n_table";
      n_meta.columns = n_cols;
      n_meta.num_columns = 1;
      n_meta.relations = n_rels;
      n_meta.num_relations = 1;
      n_meta.struct_size = sizeof(n_obj);
      n_meta.query_insert = "INSERT INTO n_table (id) VALUES (?)";
      n_meta.query_update = "UPDATE n_table SET id = ? WHERE id = ?";

      n_obj.id = &actual_id;
      n_obj.child_ptr = NULL;
      /* local_key not found */
      (void)c_orm_insert(&custom_db, &n_meta, &n_obj);
      (void)c_orm_update(&custom_db, &n_meta, &n_obj);

      /* existing_fk == 0 */
      n_rels[0].local_key = "id";
      actual_id = 0;
      (void)c_orm_insert(&custom_db, &n_meta, &n_obj);
      (void)c_orm_update(&custom_db, &n_meta, &n_obj);

      /* Lines 2754 & 2938 branch 1: id == NULL */
      n_obj.id = NULL;
      (void)c_orm_insert(&custom_db, &n_meta, &n_obj);
      (void)c_orm_update(&custom_db, &n_meta, &n_obj);
    }
  }

  /* 10. Lines 2925, 2992: update branches */
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
    p_meta.query_update = "UPDATE p SET id = ? WHERE id = ?";

    c_cols[0].name = "id";
    c_cols[0].type = C_ORM_TYPE_INT32;
    c_cols[0].is_pk = 1;
    c_cols[0].offset = offsetof(struct NestedChild, id);
    c_meta.name = "child";
    c_meta.columns = c_cols;
    c_meta.num_columns = 1;
    c_meta.struct_size = sizeof(struct NestedChild);

    rels[0].field_name = "child_o2o";
    rels[0].type = C_ORM_RELATION_BELONGS_TO;
    rels[0].target_meta = &c_meta;
    rels[0].local_key = "id";
    rels[0].foreign_key = "id";
    rels[0].struct_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].data_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2o_ctx);
    p_meta.relations = rels;
    p_meta.num_relations = 1;

    /* Line 2925 branch 1: local_key not found in update */
    rels[0].local_key = "nonexistent";
    (void)c_orm_update(&custom_db, &p_meta, &p);
    rels[0].local_key = "id";

    /* Line 2925 branch 2: existing_fk == 0 in update */
    p.id = 0;
    (void)c_orm_update(&custom_db, &p_meta, &p);
    p.id = 1;

    /* Line 2985 default case: BOOL PK in update with 0 relations */
    p_meta.num_relations = 0;
    p_cols[0].type = C_ORM_TYPE_BOOL;
    (void)c_orm_update(&custom_db, &p_meta, &p);
  }

  /* 11. Lines 3589, 3612, 3634, 3662: delete cascade with STRING PK and
   * POLYMORPHIC */
  {
    struct FullParentObj p;
    c_orm_table_meta_t p_meta;
    c_orm_column_meta_t p_cols[1];
    c_orm_relation_meta_t rels[1];
    c_orm_table_meta_t c_meta;
    c_orm_column_meta_t c_cols[1];

    memset(&p, 0, sizeof(p));
    memset(&p_meta, 0, sizeof(p_meta));
    memset(p_cols, 0, sizeof(p_cols));
    memset(rels, 0, sizeof(rels));
    memset(&c_meta, 0, sizeof(c_meta));
    memset(c_cols, 0, sizeof(c_cols));

    p_cols[0].name = "id";
    p_cols[0].type = C_ORM_TYPE_STRING;
    p_cols[0].is_pk = 1;
    p_cols[0].offset = offsetof(struct FullParentObj, id);
    p_meta.name = "p_str";
    p_meta.columns = p_cols;
    p_meta.num_columns = 1;
    p_meta.struct_size = sizeof(struct FullParentObj);
    p_meta.query_delete_by_pk = "DELETE FROM p_str WHERE id = ?";

    c_cols[0].name = "id";
    c_cols[0].type = C_ORM_TYPE_INT32;
    c_cols[0].is_pk = 1;
    c_cols[0].offset = offsetof(struct NestedChild, id);
    c_meta.name = "child";
    c_meta.columns = c_cols;
    c_meta.num_columns = 1;
    c_meta.struct_size = sizeof(struct NestedChild);

    /* Line 3589 case STRING: ONE_TO_ONE cascade with STRING PK */
    rels[0].field_name = "child_o2o";
    rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
    rels[0].target_meta = &c_meta;
    rels[0].local_key = "id";
    rels[0].foreign_key = "id";
    rels[0].on_delete = C_ORM_CASCADE_DELETE;
    p_meta.relations = rels;
    p_meta.num_relations = 1;
    (void)c_orm_delete(&custom_db, &p_meta, &p);

    /* Lines 3634, 3662 case STRING: M2M CASCADE_DELETE with STRING PK */
    rels[0].type = C_ORM_RELATION_MANY_TO_MANY;
    rels[0].join_table = "j";
    rels[0].join_local_key = "p_id";
    rels[0].join_foreign_key = "c_id";
    rels[0].on_delete = C_ORM_CASCADE_DELETE;
    (void)c_orm_delete(&custom_db, &p_meta, &p);

    /* Case INT64: cascade delete with INT64 PK */
    p_cols[0].type = C_ORM_TYPE_INT64;
    rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
    (void)c_orm_delete(&custom_db, &p_meta, &p);
    rels[0].type = C_ORM_RELATION_MANY_TO_MANY;
    (void)c_orm_delete(&custom_db, &p_meta, &p);

    /* Case default: cascade delete with BOOL PK */
    p_cols[0].type = C_ORM_TYPE_BOOL;
    rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
    (void)c_orm_delete(&custom_db, &p_meta, &p);
    rels[0].type = C_ORM_RELATION_MANY_TO_MANY;
    (void)c_orm_delete(&custom_db, &p_meta, &p);

    /* Line 3612 branch 0: POLYMORPHIC relation */
    rels[0].type = C_ORM_RELATION_POLYMORPHIC;
    (void)c_orm_delete(&custom_db, &p_meta, &p);
  }

  /* 12. Lines 4561, 4580: non-FK alongside FK constraints in
   * build_relation_meta */
  {
    struct sql_table_t sql_tbl;
    struct sql_column_t sql_col;
    struct sql_constraint_t col_cons[2];
    struct sql_constraint_t tbl_cons[2];
    c_orm_relation_meta_t *out_rels = NULL;
    size_t out_num = 0;

    memset(&sql_tbl, 0, sizeof(sql_tbl));
    memset(&sql_col, 0, sizeof(sql_col));
    memset(col_cons, 0, sizeof(col_cons));
    memset(tbl_cons, 0, sizeof(tbl_cons));

    col_cons[0].type = SQL_CONSTRAINT_FOREIGN_KEY;
    col_cons[0].reference_table = "tbl_target";
    col_cons[0].reference_column = "col_target";
    col_cons[1].type = SQL_CONSTRAINT_PRIMARY_KEY;
    sql_col.name = "fk_id";
    sql_col.constraints = col_cons;
    sql_col.n_constraints = 2;

    tbl_cons[0].type = SQL_CONSTRAINT_FOREIGN_KEY;
    tbl_cons[0].reference_table = "tbl_target";
    tbl_cons[0].reference_column = "col_target";
    tbl_cons[1].type = SQL_CONSTRAINT_PRIMARY_KEY;
    sql_tbl.name = "my_table";
    sql_tbl.columns = &sql_col;
    sql_tbl.n_columns = 1;
    sql_tbl.table_constraints = tbl_cons;
    sql_tbl.n_table_constraints = 2;

    (void)c_orm_build_relation_meta(&sql_tbl, &out_rels, &out_num);
    if (out_rels) {
      C_ORM_FREE(out_rels);
      out_rels = NULL;
    }
  }

  /* 13. Line 5564 branch 0: free_relations with NULL str_name */
  {
    struct FullParentObj p;
    c_orm_table_meta_t p_meta;
    c_orm_column_meta_t p_cols[1];
    c_orm_relation_meta_t rels[1];
    c_orm_lazy_load_context_t *ctx;

    struct StringChildObj {
      int32_t id;
      char *str_name;
    };
    c_orm_table_meta_t str_c_meta;
    c_orm_column_meta_t str_c_cols[2];

    memset(&p, 0, sizeof(p));
    memset(&p_meta, 0, sizeof(p_meta));
    memset(p_cols, 0, sizeof(p_cols));
    memset(rels, 0, sizeof(rels));
    memset(&str_c_meta, 0, sizeof(str_c_meta));
    memset(str_c_cols, 0, sizeof(str_c_cols));

    p_cols[0].name = "id";
    p_cols[0].type = C_ORM_TYPE_INT32;
    p_cols[0].is_pk = 1;
    p_cols[0].offset = offsetof(struct FullParentObj, id);
    p_meta.name = "p";
    p_meta.columns = p_cols;
    p_meta.num_columns = 1;

    str_c_cols[0].name = "id";
    str_c_cols[0].type = C_ORM_TYPE_INT32;
    str_c_cols[0].offset = offsetof(struct StringChildObj, id);
    str_c_cols[1].name = "str_name";
    str_c_cols[1].type = C_ORM_TYPE_STRING;
    str_c_cols[1].offset = offsetof(struct StringChildObj, str_name);
    str_c_meta.name = "str_child";
    str_c_meta.columns = str_c_cols;
    str_c_meta.num_columns = 2;
    str_c_meta.struct_size = sizeof(struct StringChildObj);

    rels[0].type = C_ORM_RELATION_ONE_TO_MANY;
    rels[0].target_meta = &str_c_meta;
    rels[0].field_name = "children_o2m";
    rels[0].struct_offset = offsetof(struct FullParentObj, children_o2m);
    rels[0].data_offset = offsetof(struct FullParentObj, children_o2m);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2m_ctx);
    p_meta.relations = rels;
    p_meta.num_relations = 1;

    ctx = (c_orm_lazy_load_context_t *)(void *)((char *)&p +
                                                rels[0].lazy_ctx_offset);
    ctx->is_loaded = 1;
    p.children_o2m.data = c_orm_malloc(sizeof(struct StringChildObj) * 2);
    if (p.children_o2m.data) {
      struct StringChildObj *scs = (struct StringChildObj *)p.children_o2m.data;
      memset(scs, 0, sizeof(struct StringChildObj) * 2);
      scs[0].str_name = (char *)c_orm_malloc(8);
      if (scs[0].str_name)
        C_ORM_STRCPY(scs[0].str_name, 8, "test");
      scs[1].str_name = NULL;
      p.children_o2m.length = 2;
      p.children_o2m.capacity = 2;
      c_orm_free_relations(&p_meta, &p);
    }
  }

  /* 14. Line 5919: scatter_gather_generic with 3 shards: NOT_FOUND, SQL, OK */
  {
    c_orm_shard_manager_t *sm = NULL;
    void *out_arr = NULL;
    size_t out_cnt = 0;

    c_orm_shard_manager_init(3, &sm);
    c_orm_shard_manager_add_node(sm, 0, &custom_db);
    c_orm_shard_manager_add_node(sm, 1, &custom_db);
    c_orm_shard_manager_add_node(sm, 2, &custom_db);

    g_scatter_step_call = 0;
    custom_vt.step = mock_scatter_mixed_step;
    (void)c_orm_scatter_gather_generic(sm, &Users_meta, &out_arr, &out_cnt);
    if (out_arr) {
      C_ORM_FREE(out_arr);
      out_arr = NULL;
    }

    custom_vt.step = g_vt.step;
    c_orm_shard_manager_free(sm);
  }

  /* 15. Line 6812: insert_generic with step returning NOT_FOUND */
  {
    struct Users u_obj;
    memset(&u_obj, 0, sizeof(u_obj));
    custom_vt.step = mock_always_step_not_found;
    (void)c_orm_insert_generic(&custom_db, &Users_meta, &u_obj);
    custom_vt.step = g_vt.step;
  }

  /* 16. dfs_validate_table branches (lines 4305, 4312, 4315) */
  {
    c_orm_table_meta_t vt1, vt2;
    const c_orm_table_meta_t *vtables[2];
    c_orm_relation_meta_t vrels1[2];

    memset(&vt1, 0, sizeof(vt1));
    memset(&vt2, 0, sizeof(vt2));
    memset(vrels1, 0, sizeof(vrels1));

    vt1.name = "vt1";
    vt2.name = "vt2";
    vtables[0] = &vt1;
    vtables[1] = &vt2;

    /* Line 4312 branch 3: target found from target_table */
    vrels1[0].target_table = "vt2";
    /* Line 4305 branch 0 & line 4315 branch 0: target_table not found and
     * target_meta NULL */
    vrels1[1].target_table = "nonexistent_table";
    vrels1[1].target_meta = NULL;

    vt1.relations = vrels1;
    vt1.num_relations = 2;

    (void)c_orm_validate_relations(vtables, 2);
  }

  /* 17. load_relation_ext branches (lines 5280, 5481) */
  {
    struct FullParentObj p;
    c_orm_table_meta_t p_meta;
    c_orm_column_meta_t p_cols[2];
    c_orm_relation_meta_t rels[2];
    struct NestedChild child;
    c_orm_table_meta_t c_meta;
    c_orm_column_meta_t c_cols[1];
    c_orm_lazy_load_context_t *ctx;

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

    c_cols[0].name = "id";
    c_cols[0].type = C_ORM_TYPE_INT32;
    c_cols[0].offset = offsetof(struct NestedChild, id);
    c_meta.name = "child";
    c_meta.columns = c_cols;
    c_meta.num_columns = 1;

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

    /* Line 5280 branch 4: ctx->is_loaded == 1, limit == 0, offset == 5 */
    ctx = (c_orm_lazy_load_context_t *)(void *)((char *)&p +
                                                rels[0].lazy_ctx_offset);
    ctx->is_loaded = 1;
    (void)c_orm_load_relation_ext(&custom_db, &p, &p_meta, 0, 0, 5);
    ctx->is_loaded = 0;

    /* Line 5481 branch 2: limit == 0 and offset > 0 on O2M */
    rels[0].type = C_ORM_RELATION_ONE_TO_MANY;
    rels[0].field_name = "children_o2m";
    rels[0].struct_offset = offsetof(struct FullParentObj, children_o2m);
    rels[0].data_offset = offsetof(struct FullParentObj, children_o2m);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2m_ctx);
    (void)c_orm_load_relation_ext(&custom_db, &p, &p_meta, 0, 0, 5);

    /* Line 5530 branch 0: ptr == NULL in free_relations */
    rels[0].type = C_ORM_RELATION_BELONGS_TO;
    rels[0].field_name = "child_o2o";
    rels[0].struct_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].data_offset = offsetof(struct FullParentObj, child_o2o);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2o_ctx);
    ctx = (c_orm_lazy_load_context_t *)(void *)((char *)&p +
                                                rels[0].lazy_ctx_offset);
    ctx->is_loaded = 1;
    p.child_o2o = NULL;
    c_orm_free_relations(&p_meta, &p);
  }

  /* 18. Line 6057: update_partial with field not matching any column */
  {
    const char *fields[1];
    struct Users u_obj;
    memset(&u_obj, 0, sizeof(u_obj));
    fields[0] = "nonexistent_field_xyz";
    (void)c_orm_update_partial(&custom_db, &Users_meta, &u_obj, fields, 1);
  }

  /* 19. Line 6556: sync with children_array == NULL and num_children == 0 */
  {
    struct FullParentObj p;
    c_orm_table_meta_t p_meta;
    c_orm_relation_meta_t rels[1];
    memset(&p, 0, sizeof(p));
    memset(&p_meta, 0, sizeof(p_meta));
    memset(rels, 0, sizeof(rels));
    rels[0].field_name = "tags";
    rels[0].type = C_ORM_RELATION_MANY_TO_MANY;
    p_meta.relations = rels;
    p_meta.num_relations = 1;
    (void)c_orm_sync(&custom_db, &p_meta, &p, "tags", NULL, 0);
  }

  /* 20. Line 2046 and 2069: UUID OOM and invalid timestamp */
  {
    struct UuidTsObj2 {
      char *id;
      char *ts;
    } ut_obj2;
    c_orm_table_meta_t ut_meta2;
    c_orm_column_meta_t ut_cols2[2];
    int k;

    memset(&ut_obj2, 0, sizeof(ut_obj2));
    memset(&ut_meta2, 0, sizeof(ut_meta2));
    memset(ut_cols2, 0, sizeof(ut_cols2));

    ut_cols2[0].name = "id";
    ut_cols2[0].type = C_ORM_TYPE_STRING;
    ut_cols2[0].is_pk = 1;
    ut_cols2[0].offset = offsetof(struct UuidTsObj2, id);

    ut_cols2[1].name = "ts";
    ut_cols2[1].type = C_ORM_TYPE_TIMESTAMP;
    ut_cols2[1].offset = offsetof(struct UuidTsObj2, ts);

    ut_meta2.name = "uuid_ts2";
    ut_meta2.columns = ut_cols2;
    ut_meta2.num_columns = 2;
    ut_meta2.struct_size = sizeof(ut_obj2);
    ut_meta2.query_insert = "INSERT INTO uuid_ts2 VALUES (?, ?)";

    /* Line 2069 branch 0: invalid timestamp format */
    custom_db.timezone.offset_minutes = 60;
    ut_obj2.id = "my-uuid";
    ut_obj2.ts = "invalid-date-format";
    (void)c_orm_insert(&custom_db, &ut_meta2, &ut_obj2);
    custom_db.timezone.offset_minutes = 0;

    /* Line 2046 branch 0: countdown OOM for new_uuid allocation */
    ut_obj2.id = "";
    for (k = 0; k < 6; k++) {
      g_deep_fail_oom = k;
      g_deep_alloc_cnt = 0;
      c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
      (void)c_orm_insert(&custom_db, &ut_meta2, &ut_obj2);
      c_orm_set_allocators(orig_m, orig_r, orig_f);
    }
    g_deep_fail_oom = -1;
  }

  /* 21. Lines 2674 branch 1, 2793 branch 1, 2798 branch 0: relation type false
   * branches */
  {
    struct FullParentObj p;
    c_orm_table_meta_t p_meta;
    c_orm_column_meta_t p_cols[1];
    c_orm_relation_meta_t rels[2];
    struct NestedChild child;
    c_orm_table_meta_t c_meta;
    c_orm_column_meta_t c_cols[1];

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
    p_meta.name = "p_multi";
    p_meta.columns = p_cols;
    p_meta.num_columns = 1;
    p_meta.struct_size = sizeof(struct FullParentObj);
    p_meta.query_insert = "INSERT INTO p_multi (id) VALUES (?)";
    p_meta.query_update = "UPDATE p_multi SET id = ? WHERE id = ?";

    c_cols[0].name = "id";
    c_cols[0].type = C_ORM_TYPE_INT32;
    c_cols[0].offset = offsetof(struct NestedChild, id);
    c_meta.name = "child";
    c_meta.columns = c_cols;
    c_meta.num_columns = 1;
    c_meta.struct_size = sizeof(struct NestedChild);
    c_meta.query_insert = "INSERT INTO child (id) VALUES (?)";

    /* rels[0] is ONE_TO_MANY: target_meta != NULL, but type != BELONGS_TO and
     * type != ONE_TO_ONE */
    rels[0].field_name = "children_o2m";
    rels[0].type = C_ORM_RELATION_ONE_TO_MANY;
    rels[0].target_meta = &c_meta;
    rels[0].local_key = "id";
    rels[0].foreign_key = "id";
    rels[0].struct_offset = offsetof(struct FullParentObj, children_o2m);
    rels[0].data_offset = offsetof(struct FullParentObj, children_o2m);
    rels[0].lazy_ctx_offset = offsetof(struct FullParentObj, o2m_ctx);

    /* rels[1] is ONE_TO_ONE with nested_ptr == NULL */
    rels[1].field_name = "child_o2o";
    rels[1].type = C_ORM_RELATION_ONE_TO_ONE;
    rels[1].target_meta = &c_meta;
    rels[1].local_key = "id";
    rels[1].foreign_key = "id";
    rels[1].struct_offset = offsetof(struct FullParentObj, child_o2o);
    rels[1].data_offset = offsetof(struct FullParentObj, child_o2o);
    rels[1].lazy_ctx_offset = offsetof(struct FullParentObj, o2o_ctx);

    p_meta.relations = rels;
    p_meta.num_relations = 2;

    p.child_o2o = NULL;
    (void)c_orm_insert(&custom_db, &p_meta, &p);
    (void)c_orm_update(&custom_db, &p_meta, &p);
  }

  setup_vt();
  c_orm_set_allocators(orig_m, orig_r, orig_f);

  PASS();
}

#endif /* TEST_API_HYDRATION_H */
