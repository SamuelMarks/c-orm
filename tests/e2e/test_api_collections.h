/**
 * @file test_api_collections.h
 * @brief Generic collection, scatter-gather, and identity map tests for C ORM
 * API.
 */

#ifndef TEST_API_COLLECTIONS_H
#define TEST_API_COLLECTIONS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "test_api_helpers.h"

/**
 * @brief Forward declaration for test_api_find_all_generic_realloc.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_api_find_all_generic_realloc(void);

/**
 * @brief Tests find_all_generic array capacity expansion and reallocation.
 * @return GREATEST test result.
 */
TEST test_api_find_all_generic_realloc(void) {
  void *out_arr;
  size_t out_cnt;
  c_orm_column_meta_t cols[1];
  c_orm_table_meta_t meta;
  c_orm_driver_vtable_t custom_vt;
  c_orm_db_t custom_db;
  c_orm_error_t rc;

  out_arr = NULL;
  out_cnt = 0;
  memset(cols, 0, sizeof(cols));
  memset(&meta, 0, sizeof(meta));
  cols[0].name = "id";
  cols[0].type = C_ORM_TYPE_INT32;
  cols[0].offset = 0;
  cols[0].is_pk = 1;

  meta.name = "gen_test";
  meta.columns = cols;
  meta.num_columns = 1;
  meta.struct_size = sizeof(int32_t);

  custom_vt = g_vt;
  custom_db = g_db;
  custom_vt.step = mock_step_sequence;
  custom_db.vtable = &custom_vt;

  /* Step count = 20 > initial cap (16) to trigger cap *= 2 reallocation */
  g_step_count = 20;
  rc = c_orm_find_all_generic(&custom_db, &meta, &out_arr, &out_cnt);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(20, out_cnt);
  ASSERT(out_arr != NULL);
  free(out_arr);

  PASS();
}

/**
 * @brief Forward declaration for test_api_scatter_gather_realloc_and_err.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_api_scatter_gather_realloc_and_err(void);

/**
 * @brief Tests scatter_gather_generic reallocation across shards and shard
 * error handling.
 * @return GREATEST test result.
 */
TEST test_api_scatter_gather_realloc_and_err(void) {
  c_orm_shard_manager_t *sm;
  void *out_arr;
  size_t out_cnt;
  c_orm_column_meta_t cols[1];
  c_orm_table_meta_t meta;
  c_orm_driver_vtable_t custom_vt;
  c_orm_db_t db1;
  c_orm_db_t db2;
  c_orm_error_t rc;

  sm = NULL;
  out_arr = NULL;
  out_cnt = 0;
  memset(cols, 0, sizeof(cols));
  memset(&meta, 0, sizeof(meta));
  cols[0].name = "id";
  cols[0].type = C_ORM_TYPE_INT32;
  cols[0].offset = 0;
  cols[0].is_pk = 1;

  meta.name = "sg_test";
  meta.columns = cols;
  meta.num_columns = 1;
  meta.struct_size = sizeof(int32_t);

  custom_vt = g_vt;
  custom_vt.step = mock_step_sequence;
  db1 = g_db;
  db1.vtable = &custom_vt;
  db2 = g_db;
  db2.vtable = &custom_vt;

  /* Initialize shard manager with 2 nodes */
  rc = c_orm_shard_manager_init(2, &sm);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_shard_manager_add_node(sm, 0, &db1);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_shard_manager_add_node(sm, 1, &db2);
  ASSERT_EQ(C_ORM_OK, rc);

  /* Shard 1 returns 20 items (cap starts at 16, so triggers total_count +
   * shard_count > total_cap) */
  g_step_count = 20;
  rc = c_orm_scatter_gather_generic(sm, &meta, &out_arr, &out_cnt);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(20, out_cnt);
  ASSERT(out_arr != NULL);
  free(out_arr);
  out_arr = NULL;

  /* Error case: shards return error */
  g_db_fail = 1;
  g_db_count = 0;
  g_db_target = 0;
  rc = c_orm_scatter_gather_generic(sm, &meta, &out_arr, &out_cnt);
  ASSERT_NEQ(C_ORM_OK, rc);
  g_db_fail = 0;

  c_orm_shard_manager_free(sm);
  PASS();
}

/**
 * @brief Forward declaration for test_api_identity_map_and_generic_deep.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_api_identity_map_and_generic_deep(void);

/**
 * @brief Tests identity map lookups, OOM error paths, and step/bind query
 * failures.
 * @return GREATEST test result.
 */
TEST test_api_identity_map_and_generic_deep(void) {
  c_orm_identity_map_t map;
  void *out_ptr;
  c_orm_driver_vtable_t gvt;
  c_orm_db_t gdb;
  int32_t val1;
  int32_t val2;
  void *(*orig_m)(size_t);
  void *(*orig_r)(void *, size_t);
  void (*orig_f)(void *);
  c_orm_error_t rc;

  out_ptr = NULL;
  val1 = 111;
  val2 = 222;
  orig_m = c_orm_malloc;
  orig_r = c_orm_realloc;
  orig_f = c_orm_free;

  memset(&map, 0, sizeof(map));
  gvt = g_vt;
  gdb = g_db;
  gdb.vtable = &gvt;

  /* 1. Identity Map string lookup and insertion */
  rc = c_orm_identity_map_init(&map);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_identity_map_get_or_set_str(&map, &Users_meta, "key1", &val1,
                                         &out_ptr);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(&val1, out_ptr);
  /* Same key found */
  rc = c_orm_identity_map_get_or_set_str(&map, &Users_meta, "key1", &val2,
                                         &out_ptr);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(&val1, out_ptr);
  /* Different key */
  rc = c_orm_identity_map_get_or_set_str(&map, &Users_meta, "key2", &val2,
                                         &out_ptr);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(&val2, out_ptr);

  /* 2. Identity Map OOM failures */
  c_orm_set_allocators(mock_deep_malloc, orig_r, orig_f);
  /* Entry malloc failure for str */
  g_deep_fail_oom = 0;
  g_deep_alloc_cnt = 0;
  rc = c_orm_identity_map_get_or_set_str(&map, &Users_meta, "key_oom", &val1,
                                         &out_ptr);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  /* Entry strdup failure for str */
  g_deep_fail_oom = 1;
  g_deep_alloc_cnt = 0;
  rc = c_orm_identity_map_get_or_set_str(&map, &Users_meta, "key_oom2", &val1,
                                         &out_ptr);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  /* Entry malloc failure for int */
  g_deep_fail_oom = 0;
  g_deep_alloc_cnt = 0;
  rc = c_orm_identity_map_get_or_set_int(&map, &Users_meta, 9999, &val1,
                                         &out_ptr);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  c_orm_set_allocators(orig_m, orig_r, orig_f);
  g_deep_fail_oom = -1;

  c_orm_identity_map_free(&map);

  /* 3. Generic CRUD query errors */
  {
    struct Users u;
    memset(&u, 0, sizeof(u));
    u.id = 1;
    u.username = "test";

    /* insert_generic step failure */
    gvt.step = mock_always_step_fail;
    rc = c_orm_insert_generic(&gdb, &Users_meta, &u);
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
    /* get_generic step failure */
    rc = c_orm_get_generic(&gdb, &Users_meta, 1, &u);
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
    /* get_generic_string step failure */
    rc = c_orm_get_generic_string(&gdb, &Users_meta, "1", &u);
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
    gvt.step = mock_step;

    /* bind failures */
    gvt.bind_int32 = mock_always_bind_int32_fail;
    rc = c_orm_get_generic(&gdb, &Users_meta, 1, &u);
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
    gvt.bind_int32 = mock_bind_int32;

    gvt.bind_string = mock_always_bind_string_fail;
    rc = c_orm_get_generic_string(&gdb, &Users_meta, "1", &u);
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
    gvt.bind_string = mock_bind_string;
  }

  PASS();
}

/**
 * @brief Forward declaration for test_api_generic_and_scatter_gather_deep.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_api_generic_and_scatter_gather_deep(void);

/**
 * @brief Tests deep find_all_generic OOM and scatter-gather error paths.
 * @return GREATEST test result.
 */
TEST test_api_generic_and_scatter_gather_deep(void) {
  c_orm_shard_manager_t *sm;
  c_orm_driver_vtable_t custom_vt;
  c_orm_db_t custom_db;
  c_orm_db_t fail_db;
  void *out_arr;
  size_t out_cnt;
  void *(*orig_m)(size_t);
  void *(*orig_r)(void *, size_t);
  void (*orig_f)(void *);
  c_orm_error_t rc;

  sm = NULL;
  out_arr = NULL;
  out_cnt = 0;
  orig_m = c_orm_malloc;
  orig_r = c_orm_realloc;
  orig_f = c_orm_free;

  custom_vt = g_vt;
  custom_db = g_db;
  custom_vt.step = mock_step_sequence;
  custom_db.vtable = &custom_vt;

  /* 1. find_all_generic realloc OOM */
  c_orm_set_allocators(orig_m, mock_deep_realloc, orig_f);
  g_deep_fail_realloc = 0;
  g_deep_realloc_cnt = 0;
  g_step_count = 20;
  rc = c_orm_find_all_generic(&custom_db, &Users_meta, &out_arr, &out_cnt);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  c_orm_set_allocators(orig_m, orig_r, orig_f);
  g_deep_fail_realloc = -1;

  /* 2. find_all_generic step failure in loop */
  custom_vt.step = mock_always_step_fail;
  rc = c_orm_find_all_generic(&custom_db, &Users_meta, &out_arr, &out_cnt);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  custom_vt.step = mock_step_sequence;

  /* 3. scatter_gather_generic node execution failure */
  fail_db = g_db;
  fail_db.vtable = &custom_vt;
  custom_vt.step = mock_always_step_fail;

  rc = c_orm_shard_manager_init(1, &sm);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_shard_manager_add_node(sm, 0, &fail_db);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_scatter_gather_generic(sm, &Users_meta, &out_arr, &out_cnt);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  c_orm_shard_manager_free(sm);
  custom_vt.step = mock_step_sequence;

  PASS();
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* TEST_API_COLLECTIONS_H */
