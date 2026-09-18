/**
 * @file test_api_helpers.h
 * @brief Common mock helpers, structures, and stage drivers for API test
 * suites.
 */

#ifndef TEST_API_HELPERS_H
#define TEST_API_HELPERS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief Mock query step callback producing a finite sequence of rows.
 * @param q Query pointer.
 * @param has_row Output receiving 1 if row exists, 0 otherwise.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t mock_step_sequence(c_orm_query_t *q, int *has_row) {
  (void)q;
  if (!has_row)
    return C_ORM_ERROR_UNKNOWN;
  if (g_step_count > 0) {
    *has_row = 1;
    g_step_count--;
  } else {
    *has_row = 0;
  }
  return C_ORM_OK;
}

/** @brief Foreign key ID returned by mock_get_int32_fk. */
static int32_t g_mock_child_fk_id = 1;

/**
 * @brief Mock get_int32 returning g_mock_child_fk_id.
 * @param q Query pointer.
 * @param index Column index.
 * @param val Output pointer receiving foreign key value.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t mock_get_int32_fk(c_orm_query_t *q, int index,
                                       int32_t *val) {
  (void)q;
  (void)index;
  if (val)
    *val = g_mock_child_fk_id;
  return C_ORM_OK;
}

/** @brief Extended parent structure with various relational mappings. */
struct ExtendedParent {
  int32_t id;
  int32_t belongs_to_id;
  struct NestedChild *child_o2o;
  struct Generic_Array children_o2m;
  struct Generic_Array tags_m2m;
  char name[32];
};

/** @brief Full parent structure with lazy load context fields. */
struct FullParentObj {
  int32_t id;
  int32_t belongs_to_id;
  struct NestedChild *child_o2o;
  struct Generic_Array children_o2m;
  struct Generic_Array tags_m2m;
  char name[32];
  c_orm_lazy_load_context_t o2o_ctx;
  c_orm_lazy_load_context_t o2m_ctx;
  c_orm_lazy_load_context_t m2m_ctx;
};

/** @brief Object representation with a string primary key. */
struct StrPkObj {
  char *name;
  char *id;
};

/** @brief Model object for testing TTL expiration and type conversions. */
struct TtlTestUser {
  int64_t created_at;
  int32_t expires_in;
  char *username;
  char *email;
  int32_t *age;
  float *score;
  bool *is_active;
  char *created_at_str;
};

/** @brief Error stage selector for mock stage drivers. */
static int g_mock_err_stage = 0;
/** @brief Counter tracking step calls in mock stage. */
static int g_stage_step_cnt = 0;
/** @brief Maximum step calls returning rows in mock stage. */
static int g_stage_step_max = 1;
/** @brief Flag controlling simulated primary key bind failures. */
static int g_mock_fail_pk_bind = 0;
/** @brief Flag controlling 801-row sequence return. */
static int g_mock_return_801 = 0;

/**
 * @brief Mock hook returning success.
 * @param st Struct pointer.
 * @param ctx Context pointer.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_hook_success(void *st, void *ctx) {
  (void)st;
  (void)ctx;
  return C_ORM_OK;
}

/**
 * @brief Mock hook returning error.
 * @param st Struct pointer.
 * @param ctx Context pointer.
 * @return C_ORM_ERROR_UNKNOWN.
 */
static c_orm_error_t mock_hook_fail(void *st, void *ctx) {
  (void)st;
  (void)ctx;
  return C_ORM_ERROR_UNKNOWN;
}

/**
 * @brief Expiration callback stub for API tests.
 * @param db Database handle.
 * @param meta Table metadata.
 * @param rec Expired record pointer.
 * @param ud User data pointer.
 */
static void test_api_expire_cb(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                               void *rec, void *ud) {
  (void)db;
  (void)meta;
  (void)rec;
  (void)ud;
}

/**
 * @brief Configurable mock step callback for multi-stage execution testing.
 * @param q Query pointer.
 * @param has_row Output receiving 1 if row exists, 0 otherwise.
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN on injected error.
 */
static c_orm_error_t mock_stage_step(c_orm_query_t *q, int *has_row) {
  (void)q;
  if (!has_row)
    return C_ORM_ERROR_UNKNOWN;
  if (g_mock_err_stage == 1)
    return C_ORM_ERROR_UNKNOWN;
  if (g_mock_err_stage == 10 && g_stage_step_cnt >= 2)
    return C_ORM_ERROR_UNKNOWN;
  if (g_mock_return_801 && g_stage_step_cnt++ < 801) {
    *has_row = 1;
    return C_ORM_OK;
  }
  if (g_stage_step_cnt++ < g_stage_step_max) {
    *has_row = 1;
  } else {
    *has_row = 0;
  }
  return C_ORM_OK;
}

/**
 * @brief Mock prepare callback with stage error simulation.
 * @param db Database handle.
 * @param sql SQL string.
 * @param q Output query pointer.
 * @return C_ORM_OK on success or C_ORM_ERROR_UNKNOWN on injected error.
 */
static c_orm_error_t mock_stage_prepare(c_orm_db_t *db, const char *sql,
                                        c_orm_query_t **q) {
  (void)db;
  (void)sql;
  if (g_mock_err_stage == 2)
    return C_ORM_ERROR_UNKNOWN;
  if (g_mock_err_stage == 9 && sql && strstr(sql, "INSERT"))
    return C_ORM_ERROR_UNKNOWN;
  if (q)
    *q = (c_orm_query_t *)1;
  return C_ORM_OK;
}

/**
 * @brief Mock bind_int32 callback with stage error simulation.
 * @param q Query pointer.
 * @param i Parameter index.
 * @param val Parameter value.
 * @return C_ORM_OK on success or C_ORM_ERROR_UNKNOWN on injected error.
 */
static c_orm_error_t mock_stage_bind_int32(c_orm_query_t *q, int i,
                                           int32_t val) {
  (void)q;
  (void)val;
  if (g_mock_err_stage == 3)
    return C_ORM_ERROR_UNKNOWN;
  if (g_mock_fail_pk_bind && i >= 3)
    return C_ORM_ERROR_UNKNOWN;
  return C_ORM_OK;
}

/**
 * @brief Mock bind_string callback with stage error simulation.
 * @param q Query pointer.
 * @param i Parameter index.
 * @param val String parameter value.
 * @return C_ORM_OK on success or C_ORM_ERROR_UNKNOWN on injected error.
 */
static c_orm_error_t mock_stage_bind_string(c_orm_query_t *q, int i,
                                            const char *val) {
  (void)q;
  (void)val;
  if (g_mock_fail_pk_bind == 1 && i >= 3)
    return C_ORM_ERROR_UNKNOWN;
  if (g_mock_fail_pk_bind == 2 && i == 2)
    return C_ORM_ERROR_UNKNOWN;
  return C_ORM_OK;
}

/**
 * @brief Mock bind_int64 callback with stage error simulation.
 * @param q Query pointer.
 * @param i Parameter index.
 * @param val Int64 parameter value.
 * @return C_ORM_OK on success or C_ORM_ERROR_UNKNOWN on injected error.
 */
static c_orm_error_t mock_stage_bind_int64(c_orm_query_t *q, int i,
                                           int64_t val) {
  (void)q;
  (void)val;
  if (g_mock_fail_pk_bind == 1 && i >= 3)
    return C_ORM_ERROR_UNKNOWN;
  return C_ORM_OK;
}

/** @brief Counter tracking finalize invocations in mock stage. */
static int g_stage_finalize_cnt = 0;

/**
 * @brief Mock finalize callback with stage error simulation.
 * @param q Query pointer.
 * @return C_ORM_OK on success or C_ORM_ERROR_UNKNOWN on injected error.
 */
static c_orm_error_t mock_stage_finalize(c_orm_query_t *q) {
  (void)q;
  if (g_mock_err_stage == 4)
    return C_ORM_ERROR_UNKNOWN;
  if (g_mock_err_stage == 11 && g_stage_finalize_cnt++ >= 1)
    return C_ORM_ERROR_UNKNOWN;
  return C_ORM_OK;
}

/**
 * @brief Mock reset callback with stage error simulation.
 * @param q Query pointer.
 * @return C_ORM_OK on success or C_ORM_ERROR_UNKNOWN on injected error.
 */
static c_orm_error_t mock_stage_reset(c_orm_query_t *q) {
  (void)q;
  if (g_mock_err_stage == 5)
    return C_ORM_ERROR_UNKNOWN;
  return C_ORM_OK;
}

/**
 * @brief Mock is_null callback with stage error simulation.
 * @param q Query pointer.
 * @param i Column index.
 * @param out_null Output receiving 1 if NULL, 0 otherwise.
 * @return C_ORM_OK on success or C_ORM_ERROR_UNKNOWN on injected error.
 */
static c_orm_error_t mock_stage_is_null(c_orm_query_t *q, int i,
                                        int *out_null) {
  (void)q;
  (void)i;
  if (g_mock_err_stage == 6)
    return C_ORM_ERROR_UNKNOWN;
  if (out_null)
    *out_null = 0;
  return C_ORM_OK;
}

/**
 * @brief Mock get_int32 callback with stage error simulation.
 * @param q Query pointer.
 * @param i Column index.
 * @param o Output receiving int32 value.
 * @return C_ORM_OK on success or C_ORM_ERROR_UNKNOWN on injected error.
 */
static c_orm_error_t mock_stage_get_int32(c_orm_query_t *q, int i, int32_t *o) {
  (void)q;
  if (g_mock_err_stage == 7 && i >= 2)
    return C_ORM_ERROR_UNKNOWN;
  if (g_mock_err_stage == 8 && i == 0)
    return C_ORM_ERROR_UNKNOWN;
  if (o)
    *o = 1;
  return C_ORM_OK;
}

/**
 * @brief Mock step callback always returning 0 rows.
 * @param q Query pointer.
 * @param has_row Output receiving 0.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_always_step_zero(c_orm_query_t *q, int *has_row) {
  (void)q;
  if (has_row)
    *has_row = 0;
  return C_ORM_OK;
}

/**
 * @brief Mock step callback always returning 1 row.
 * @param q Query pointer.
 * @param has_row Output receiving 1.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_always_step_one(c_orm_query_t *q, int *has_row) {
  (void)q;
  if (has_row)
    *has_row = 1;
  return C_ORM_OK;
}

/**
 * @brief Mock prepare callback always failing.
 * @param db Database handle.
 * @param sql SQL string.
 * @param q Output query pointer.
 * @return C_ORM_ERROR_UNKNOWN.
 */
static c_orm_error_t mock_always_fail_prepare(c_orm_db_t *db, const char *sql,
                                              c_orm_query_t **q) {
  (void)db;
  (void)sql;
  (void)q;
  return C_ORM_ERROR_UNKNOWN;
}

/**
 * @brief Mock bind_string callback always failing.
 * @param q Query pointer.
 * @param i Parameter index.
 * @param val String value.
 * @return C_ORM_ERROR_UNKNOWN.
 */
static c_orm_error_t mock_always_fail_bind_string(c_orm_query_t *q, int i,
                                                  const char *val) {
  (void)q;
  (void)i;
  (void)val;
  return C_ORM_ERROR_UNKNOWN;
}

/**
 * @brief Mock bind_null callback always failing.
 * @param q Query pointer.
 * @param i Parameter index.
 * @return C_ORM_ERROR_UNKNOWN.
 */
static c_orm_error_t mock_always_fail_bind_null(c_orm_query_t *q, int i) {
  (void)q;
  (void)i;
  return C_ORM_ERROR_UNKNOWN;
}

/**
 * @brief Mock bind_blob callback always failing.
 * @param q Query pointer.
 * @param i Parameter index.
 * @param v Blob data pointer.
 * @param s Blob data size.
 * @return C_ORM_ERROR_UNKNOWN.
 */
static c_orm_error_t mock_always_fail_bind_blob(c_orm_query_t *q, int i,
                                                const void *v, size_t s) {
  (void)q;
  (void)i;
  (void)v;
  (void)s;
  return C_ORM_ERROR_UNKNOWN;
}

/**
 * @brief Mock encryption hook always failing.
 * @param in_data Input buffer.
 * @param in_size Input size.
 * @param ctx Context pointer.
 * @param out_data Output buffer pointer.
 * @param out_size Output size pointer.
 * @return C_ORM_ERROR_UNKNOWN.
 */
static c_orm_error_t mock_test_encrypt_hook_fail(const void *in_data,
                                                 size_t in_size, void *ctx,
                                                 void **out_data,
                                                 size_t *out_size) {
  (void)in_data;
  (void)in_size;
  (void)ctx;
  (void)out_data;
  (void)out_size;
  return C_ORM_ERROR_UNKNOWN;
}

/**
 * @brief Mock is_null callback always returning 0.
 * @param q Query pointer.
 * @param i Column index.
 * @param out Output receiving 0.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_always_zero_is_null(c_orm_query_t *q, int i,
                                              int *out) {
  (void)q;
  (void)i;
  if (out)
    *out = 0;
  return C_ORM_OK;
}

/** @brief Counter tracking prepare calls in stage testing. */
static int g_stage_prepare_cnt = 0;
/** @brief Counter tracking bind calls in stage testing. */
static int g_stage_bind_cnt = 0;

/**
 * @brief Mock step callback alternating parent and child row sequences.
 * @param q Query pointer.
 * @param has_row Output receiving row status.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_step_parent2_and_child2(c_orm_query_t *q,
                                                  int *has_row) {
  (void)q;
  if (!has_row)
    return C_ORM_ERROR_UNKNOWN;
  if (g_stage_step_cnt == 0 || g_stage_step_cnt == 1 || g_stage_step_cnt == 3)
    *has_row = 1;
  else
    *has_row = 0;
  g_stage_step_cnt++;
  return C_ORM_OK;
}

/**
 * @brief Mock get_int32 returning 2 for second column and 1 otherwise.
 * @param q Query pointer.
 * @param i Column index.
 * @param o Output int32 pointer.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_stage_get_int32_2(c_orm_query_t *q, int i,
                                            int32_t *o) {
  (void)q;
  if (i == 1 && o) {
    *o = 2;
    return C_ORM_OK;
  }
  if (o)
    *o = 1;
  return C_ORM_OK;
}

/**
 * @brief Mock step callback returning rows for parent and child queries.
 * @param q Query pointer.
 * @param has_row Output receiving row status.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_step_parent_and_child(c_orm_query_t *q,
                                                int *has_row) {
  (void)q;
  if (!has_row)
    return C_ORM_ERROR_UNKNOWN;
  if (g_stage_step_cnt == 0 || g_stage_step_cnt == 2)
    *has_row = 1;
  else
    *has_row = 0;
  g_stage_step_cnt++;
  return C_ORM_OK;
}

/**
 * @brief Mock step callback failing on third invocation.
 * @param q Query pointer.
 * @param has_row Output receiving row status.
 * @return C_ORM_OK on first two steps, error on third.
 */
static c_orm_error_t mock_step_fail_third(c_orm_query_t *q, int *has_row) {
  (void)q;
  if (!has_row)
    return C_ORM_ERROR_UNKNOWN;
  if (g_stage_step_cnt == 0 || g_stage_step_cnt == 1) {
    *has_row = 1;
    g_stage_step_cnt++;
    return C_ORM_OK;
  }
  g_stage_step_cnt++;
  return C_ORM_ERROR_UNKNOWN;
}

/**
 * @brief Mock step callback failing on second invocation.
 * @param q Query pointer.
 * @param has_row Output receiving row status.
 * @return C_ORM_OK on first step, then error.
 */
static c_orm_error_t mock_step_fail_on_second(c_orm_query_t *q, int *has_row) {
  (void)q;
  if (!has_row)
    return C_ORM_ERROR_UNKNOWN;
  if (g_stage_step_cnt == 0) {
    *has_row = 1;
  } else if (g_stage_step_cnt == 1) {
    *has_row = 0;
  } else {
    return C_ORM_ERROR_UNKNOWN;
  }
  g_stage_step_cnt++;
  return C_ORM_OK;
}

/**
 * @brief Mock step callback failing inside query loop.
 * @param q Query pointer.
 * @param has_row Output receiving row status.
 * @return C_ORM_OK on valid steps, error otherwise.
 */
static c_orm_error_t mock_step_fail_inside_loop(c_orm_query_t *q,
                                                int *has_row) {
  (void)q;
  if (!has_row)
    return C_ORM_ERROR_UNKNOWN;
  if (g_stage_step_cnt == 0 || g_stage_step_cnt == 2) {
    *has_row = 1;
  } else if (g_stage_step_cnt == 1) {
    *has_row = 0;
  } else {
    return C_ORM_ERROR_UNKNOWN;
  }
  g_stage_step_cnt++;
  return C_ORM_OK;
}

/**
 * @brief Mock prepare callback failing on second invocation.
 * @param db Database handle.
 * @param sql SQL statement.
 * @param q Output query pointer.
 * @return C_ORM_OK on first call, error thereafter.
 */
static c_orm_error_t mock_prepare_fail_on_second(c_orm_db_t *db,
                                                 const char *sql,
                                                 c_orm_query_t **q) {
  (void)db;
  (void)sql;
  if (g_stage_prepare_cnt++ >= 1)
    return C_ORM_ERROR_UNKNOWN;
  if (q)
    *q = (c_orm_query_t *)1;
  return C_ORM_OK;
}

/**
 * @brief Mock bind callback failing on second invocation.
 * @param q Query pointer.
 * @param i Parameter index.
 * @param val Value to bind.
 * @return C_ORM_OK on first call, error thereafter.
 */
static c_orm_error_t mock_bind_fail_on_second(c_orm_query_t *q, int i,
                                              int32_t val) {
  (void)q;
  (void)val;
  if (g_stage_bind_cnt++ >= 1 && i > 0)
    return C_ORM_ERROR_UNKNOWN;
  return C_ORM_OK;
}

/**
 * @brief Mock get callback failing for child foreign keys.
 * @param q Query pointer.
 * @param i Column index.
 * @param o Output int32 pointer.
 * @return C_ORM_OK for index 0, error thereafter.
 */
static c_orm_error_t mock_get_fail_child_fk(c_orm_query_t *q, int i,
                                            int32_t *o) {
  (void)q;
  if (i >= 1)
    return C_ORM_ERROR_UNKNOWN;
  if (o)
    *o = 1;
  return C_ORM_OK;
}

/**
 * @brief Mock get callback failing on child hydration step.
 * @param q Query pointer.
 * @param i Column index.
 * @param o Output int32 pointer.
 * @return Error on second step index 0, C_ORM_OK otherwise.
 */
static c_orm_error_t mock_get_fail_child_hydrate(c_orm_query_t *q, int i,
                                                 int32_t *o) {
  (void)q;
  if (g_stage_step_cnt >= 2 && i == 0)
    return C_ORM_ERROR_UNKNOWN;
  if (o)
    *o = 1;
  return C_ORM_OK;
}

/**
 * @brief Mock is_null callback failing for child columns.
 * @param q Query pointer.
 * @param i Column index.
 * @param out Output receiving null indicator.
 * @return Error for index >= 2, C_ORM_OK otherwise.
 */
static c_orm_error_t mock_is_null_child_fail(c_orm_query_t *q, int i,
                                             int *out) {
  (void)q;
  if (i >= 2)
    return C_ORM_ERROR_UNKNOWN;
  if (out)
    *out = 0;
  return C_ORM_OK;
}

/**
 * @brief Mock is_null callback reporting non-null for index 1.
 * @param q Query pointer.
 * @param i Column index.
 * @param out Output receiving null indicator.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_is_null_nonnull_fail(c_orm_query_t *q, int i,
                                               int *out) {
  (void)q;
  if (i == 1 && out) {
    *out = 1;
    return C_ORM_OK;
  }
  if (out)
    *out = 0;
  return C_ORM_OK;
}

/**
 * @brief Mock get_string returning NULL string value.
 * @param q Query pointer.
 * @param i Column index.
 * @param out Output receiving NULL pointer.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_get_string_null_val(c_orm_query_t *q, int i,
                                              const char **out) {
  (void)q;
  (void)i;
  if (out)
    *out = NULL;
  return C_ORM_OK;
}

/**
 * @brief Mock step callback succeeding on first step, then failing.
 * @param q Query pointer.
 * @param has_row Output receiving row status.
 * @return C_ORM_OK on first call, error thereafter.
 */
static c_orm_error_t mock_step_begin_ok_then_fail(c_orm_query_t *q,
                                                  int *has_row) {
  (void)q;
  if (!has_row)
    return C_ORM_ERROR_UNKNOWN;
  if (g_stage_step_cnt++ == 0) {
    *has_row = 1;
    return C_ORM_OK;
  }
  return C_ORM_ERROR_UNKNOWN;
}

/**
 * @brief Mock get_int32 returning 999 on second step.
 * @param q Query pointer.
 * @param i Column index.
 * @param o Output int32 pointer.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_stage_get_int32_999(c_orm_query_t *q, int i,
                                              int32_t *o) {
  (void)q;
  (void)i;
  if (g_stage_step_cnt >= 2 && o) {
    *o = 999;
    return C_ORM_OK;
  }
  if (o)
    *o = 1;
  return C_ORM_OK;
}

/**
 * @brief Mock is_null returning 1 for child columns.
 * @param q Query pointer.
 * @param i Column index.
 * @param out Output receiving null indicator.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_is_null_child_true(c_orm_query_t *q, int i,
                                             int *out) {
  (void)q;
  if (i >= 2 && out) {
    *out = 1;
    return C_ORM_OK;
  }
  if (out)
    *out = 0;
  return C_ORM_OK;
}

/**
 * @brief Mock encryption hook copying data directly.
 * @param in_data Input buffer.
 * @param in_size Input size.
 * @param ctx Context pointer.
 * @param out_data Output buffer pointer.
 * @param out_size Output size pointer.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t mock_test_encrypt_hook_ok(const void *in_data,
                                               size_t in_size, void *ctx,
                                               void **out_data,
                                               size_t *out_size) {
  (void)ctx;
  if (out_data && out_size) {
    *out_data = malloc(in_size);
    if (!*out_data)
      return C_ORM_ERROR_MEMORY;
    memcpy(*out_data, in_data, in_size);
    *out_size = in_size;
  }
  return C_ORM_OK;
}

/** @brief Structure representing a parent with nullable belongs_to relation. */
struct NullableParent {
  int32_t id;
  int32_t *belongs_to_id;
  struct NestedChild *belongs_to_child;
};

/** @brief Index of column that should trigger simulated getter failure. */
static int g_deep_fail_get = -1;
/** @brief Index of allocation that should trigger simulated OOM in
 * mock_deep_malloc. */
static int g_deep_fail_oom = -1;
/** @brief Allocation invocation counter for mock_deep_malloc. */
static int g_deep_alloc_cnt = 0;

/**
 * @brief Mock malloc with targeted allocation failure injection.
 * @param sz Allocation size in bytes.
 * @return Pointer or NULL on simulated failure.
 */
static void *mock_deep_malloc(size_t sz) {
  if (g_deep_fail_oom == g_deep_alloc_cnt++) {
    return NULL;
  }
  return malloc(sz);
}

/** @brief Index of realloc call that should trigger failure in
 * mock_deep_realloc. */
static int g_deep_fail_realloc = -1;
/** @brief Reallocation invocation counter for mock_deep_realloc. */
static int g_deep_realloc_cnt = 0;

/**
 * @brief Mock realloc with targeted allocation failure injection.
 * @param ptr Pointer to existing memory block.
 * @param sz New allocation size in bytes.
 * @return Pointer or NULL on simulated failure.
 */
static void *mock_deep_realloc(void *ptr, size_t sz) {
  if (g_deep_fail_realloc == g_deep_realloc_cnt++ ||
      (g_deep_fail_realloc == 99 && sz >= 200)) {
    return NULL;
  }
  return realloc(ptr, sz);
}

/**
 * @brief Mock step callback that always returns an error.
 * @param q Query pointer.
 * @param has_row Output receiving row status.
 * @return C_ORM_ERROR_UNKNOWN.
 */
static c_orm_error_t mock_always_step_fail(c_orm_query_t *q, int *has_row) {
  (void)q;
  (void)has_row;
  return C_ORM_ERROR_UNKNOWN;
}

/**
 * @brief Mock bind_int32 callback that always returns an error.
 * @param q Query pointer.
 * @param i Parameter index.
 * @param val Parameter value.
 * @return C_ORM_ERROR_UNKNOWN.
 */
static c_orm_error_t mock_always_bind_int32_fail(c_orm_query_t *q, int i,
                                                 int32_t val) {
  (void)q;
  (void)i;
  (void)val;
  return C_ORM_ERROR_UNKNOWN;
}

/**
 * @brief Mock bind_string callback that always returns an error.
 * @param q Query pointer.
 * @param i Parameter index.
 * @param val String value.
 * @return C_ORM_ERROR_UNKNOWN.
 */
static c_orm_error_t mock_always_bind_string_fail(c_orm_query_t *q, int i,
                                                  const char *val) {
  (void)q;
  (void)i;
  (void)val;
  return C_ORM_ERROR_UNKNOWN;
}

/** @brief Well-known binary (WKB) data representation for a Point. */
static unsigned char g_deep_pt_wkb[21] = {
    1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xf0, 0x3f, 0, 0, 0, 0, 0, 0, 0x00, 0x40};

/** @brief Well-known binary (WKB) data representation for a Polygon. */
static unsigned char g_deep_poly_wkb[45] = {
    1, 3, 0, 0, 0,    1,    0,    0, 0, 2, 0, 0, 0,    0,    0,
    0, 0, 0, 0, 0xf0, 0x3f, 0,    0, 0, 0, 0, 0, 0x00, 0x40, 0,
    0, 0, 0, 0, 0,    0x08, 0x40, 0, 0, 0, 0, 0, 0,    0x10, 0x40};

/** @brief Dummy timestamp string for deep coverage tests. */
static const char *g_deep_ts_str = "2024-01-01 12:00:00";

/**
 * @brief Mock get_int32 with failure targeting.
 * @param q Query pointer.
 * @param i Column index.
 * @param o Output int32 pointer.
 * @return C_ORM_OK or error.
 */
static c_orm_error_t mock_deep_get_int32(c_orm_query_t *q, int i, int32_t *o) {
  (void)q;
  if (g_deep_fail_get == 0 && i > 0)
    return C_ORM_ERROR_UNKNOWN;
  if (o)
    *o = 1;
  (void)i;
  return C_ORM_OK;
}

/**
 * @brief Mock get_int64 with failure targeting.
 * @param q Query pointer.
 * @param i Column index.
 * @param o Output int64 pointer.
 * @return C_ORM_OK or error.
 */
static c_orm_error_t mock_deep_get_int64(c_orm_query_t *q, int i, int64_t *o) {
  (void)q;
  (void)i;
  if (g_deep_fail_get == 1)
    return C_ORM_ERROR_UNKNOWN;
  if (o)
    *o = 1234567890;
  return C_ORM_OK;
}

/**
 * @brief Mock get_double with failure targeting.
 * @param q Query pointer.
 * @param i Column index.
 * @param o Output double pointer.
 * @return C_ORM_OK or error.
 */
static c_orm_error_t mock_deep_get_double(c_orm_query_t *q, int i, double *o) {
  (void)q;
  (void)i;
  if (g_deep_fail_get == 2)
    return C_ORM_ERROR_UNKNOWN;
  if (o)
    *o = 2.71828;
  return C_ORM_OK;
}

/**
 * @brief Mock get_blob returning geometry WKB or string data with failure
 * targeting.
 * @param q Query pointer.
 * @param i Column index.
 * @param o Output data pointer.
 * @param s Output data size pointer.
 * @return C_ORM_OK or error.
 */
static c_orm_error_t mock_deep_get_blob(c_orm_query_t *q, int i, const void **o,
                                        size_t *s) {
  (void)q;
  if (g_deep_fail_get == 3 && i == 11)
    return C_ORM_ERROR_UNKNOWN;
  if (g_deep_fail_get == 4 && i == 12)
    return C_ORM_ERROR_UNKNOWN;
  if (i == 11) {
    if (o)
      *o = g_deep_pt_wkb;
    if (s)
      *s = sizeof(g_deep_pt_wkb);
  } else if (i == 12) {
    if (o)
      *o = g_deep_poly_wkb;
    if (s)
      *s = sizeof(g_deep_poly_wkb);
  } else {
    if (o)
      *o = "blobdata";
    if (s)
      *s = 8;
  }
  return C_ORM_OK;
}

/**
 * @brief Mock get_string returning timestamp string.
 * @param q Query pointer.
 * @param i Column index.
 * @param o Output string pointer.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_deep_get_string(c_orm_query_t *q, int i,
                                          const char **o) {
  (void)q;
  (void)i;
  if (o)
    *o = g_deep_ts_str;
  return C_ORM_OK;
}

/**
 * @brief Mock is_null returning 0 for deep coverage tests.
 * @param q Query pointer.
 * @param i Column index.
 * @param o Output null indicator.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_deep_is_null_false(c_orm_query_t *q, int i, int *o) {
  (void)q;
  (void)i;
  if (o)
    *o = 0;
  return C_ORM_OK;
}

/**
 * @brief Mock decryption hook that always fails.
 * @param in Input buffer.
 * @param ins Input size.
 * @param ctx Context pointer.
 * @param out Output buffer.
 * @param outs Output size.
 * @return C_ORM_ERROR_UNKNOWN.
 */
static c_orm_error_t mock_failing_decrypt_hook(const void *in, size_t ins,
                                               void *ctx, void **out,
                                               size_t *outs) {
  (void)in;
  (void)ins;
  (void)ctx;
  (void)out;
  (void)outs;
  return C_ORM_ERROR_UNKNOWN;
}

/**
 * @brief Mock encryption hook that always fails.
 * @param in Input buffer.
 * @param ins Input size.
 * @param ctx Context pointer.
 * @param out Output buffer.
 * @param outs Output size.
 * @return C_ORM_ERROR_UNKNOWN.
 */
static c_orm_error_t mock_failing_encrypt_hook(const void *in, size_t ins,
                                               void *ctx, void **out,
                                               size_t *outs) {
  (void)in;
  (void)ins;
  (void)ctx;
  (void)out;
  (void)outs;
  return C_ORM_ERROR_UNKNOWN;
}

/** @brief Static buffer of dummy blob data. */
static const unsigned char g_dummy_blob_data_100[64] = {
    0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
/** @brief Size parameter for mock_cov_blob_100. */
static size_t g_cov_blob_size_100 = 0;

/**
 * @brief Mock get_blob returning g_dummy_blob_data_100.
 * @param q Query pointer.
 * @param i Column index.
 * @param val Output data pointer.
 * @param size Output size pointer.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_cov_blob_100(c_orm_query_t *q, int i,
                                       const void **val, size_t *size) {
  (void)q;
  (void)i;
  *val = g_dummy_blob_data_100;
  *size = g_cov_blob_size_100;
  return C_ORM_OK;
}

/**
 * @brief Dummy record expiration callback.
 * @param db Database handle.
 * @param meta Table metadata.
 * @param obj Record pointer.
 * @param ud User data pointer.
 */
static void dummy_cov_expire_callback_100(c_orm_db_t *db,
                                          const c_orm_table_meta_t *meta,
                                          void *obj, void *ud) {
  (void)db;
  (void)meta;
  (void)obj;
  (void)ud;
}

/**
 * @brief Mock malloc that always returns NULL.
 * @param s Allocation size in bytes.
 * @return NULL.
 */
static void *cov_always_null_malloc(size_t s) {
  (void)s;
  return NULL;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* TEST_API_HELPERS_H */
