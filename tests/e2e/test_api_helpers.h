/**
 * @file test_api_helpers.h
 * @brief Common mock helpers, structures, and stage drivers for API test
 * suites.
 */

#ifndef TEST_API_HELPERS_H
#define TEST_API_HELPERS_H

/* clang-format off */
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief Async query completion callback for testing.
 * @param err Error enum.
 * @param ctx Context pointer.
 * @return C_ORM_OK.
 */
static c_orm_error_t test_dummy_cb(c_orm_error_t err, void *ctx) {
  (void)err;
  (void)ctx;
  return C_ORM_OK;
}
/**
 * @brief Batch operation progress callback.
 *
 * @param p Number of items processed so far.
 * @param t Total items to process.
 * @param ctx User context pointer.
 * @return C_ORM_OK, or C_ORM_ERROR_VALIDATION if ctx points to -1.
 */
static c_orm_error_t dummy_batch_progress(size_t p, size_t t, void *ctx) {
  (void)p;
  (void)t;
  if (ctx && *(int *)ctx == -1) {
    return C_ORM_ERROR_VALIDATION;
  }
  return C_ORM_OK;
}
/**
 * @brief Lifecycle hook callback that succeeds.
 *
 * @param st Struct instance pointer.
 * @param user_data User data context pointer.
 */
static c_orm_error_t dummy_lifecycle_hook(void *st, void *user_data) {
  (void)st;
  (void)user_data;
  return C_ORM_OK;
}
/**
 * @brief Lifecycle hook callback that fails.
 *
 * @param st Struct instance pointer.
 * @param user_data User data context pointer.
 */
static c_orm_error_t dummy_failing_hook(void *st, void *user_data) {
  (void)st;
  (void)user_data;
  return C_ORM_ERROR_UNKNOWN;
}
/**
 * @brief Generic array container for test relations.
 * @var data Pointer to array data buffer.
 * @var length Current count of items.
 * @var capacity Allocated capacity.
 */
struct Generic_Array {
  void *data;
  size_t length;
  size_t capacity;
};

/**
 * @brief Buffer union for memory alignment testing.
 * @var buf Byte buffer.
 * @var ptr Pointer for alignment.
 * @var d Double for alignment.
 */
typedef union MegaBufUnion {
  char buf[256];
  void *ptr;
  double d;
} MegaBufUnion_t;

/**
 * @brief Simple struct with int32 identifier.
 * @var id 32-bit integer identifier.
 */
typedef struct CoverageIntObj {
  int32_t id;
} CoverageIntObj_t;

/**
 * @brief Simple struct with string identifier.
 * @var id String identifier.
 */
typedef struct CoverageStrObj {
  char *id;
} CoverageStrObj_t;

/**
 * @brief Test object for meta free testing.
 * @var s String field.
 * @var b Blob field.
 * @var p Polygon field.
 * @var null_ptr Null pointer field.
 * @var i Integer field.
 */
typedef struct CoverageMetaFreeObj {
  char *s;
  c_orm_blob_t b;
  c_orm_polygon_t p;
  void *null_ptr;
  int i;
} CoverageMetaFreeObj_t;

/**
 * @brief Batch item for batch operations test.
 * @var id Identifier field.
 * @var name Name buffer.
 */
typedef struct CoverageBatchItem {
  int32_t id;
  char name[32];
} CoverageBatchItem_t;

/**
 * @brief String batch item for batch operations test.
 * @var id String identifier.
 * @var val Integer value.
 */
typedef struct CoverageStrBatchItem {
  char *id;
  int32_t val;
} CoverageStrBatchItem_t;

/**
 * @brief Object for partial update testing.
 * @var id String identifier.
 * @var count 32-bit integer counter.
 * @var score Floating point score.
 */
typedef struct CoverageUpdateObj {
  char *id;
  int32_t count;
  float score;
} CoverageUpdateObj_t;

/**
 * @brief Parent entity for relation testing.
 * @var id Parent ID.
 * @var name Parent name.
 * @var ctx Lazy loading context.
 * @var children_arr Children array.
 * @var child_ptr Child pointer.
 */
struct Parent {
  int32_t id;
  char name[32];
  c_orm_lazy_load_context_t ctx;
  struct Generic_Array children_arr;
  void *child_ptr;
};

/**
 * @brief Child entity for relation testing.
 * @var id Child ID.
 * @var parent_id Parent foreign key.
 * @var val Child value string.
 */
struct Child {
  int64_t id;
  int64_t parent_id;
  char val[32];
};

/**
 * @brief Tag entity for relation testing.
 * @var tag_id Tag ID.
 * @var tag_name Tag name.
 */
struct Tag {
  int64_t tag_id;
  char tag_name[32];
};

/**
 * @brief Validation parent entity.
 * @var id Entity ID.
 * @var fk_id Foreign key ID.
 * @var child_ptr Child pointer.
 * @var ctx Lazy load context.
 */
struct ValParent {
  int64_t id;
  int64_t fk_id;
  void *child_ptr;
  c_orm_lazy_load_context_t ctx;
};

/**
 * @brief Simple test entity.
 * @var id Entity ID.
 * @var name Entity name.
 */
struct S {
  int32_t id;
  char name[32];
};

/**
 * @brief Target child entity for free relations testing.
 * @var name Child name.
 */
struct TargetChild {
  char *name;
};

/**
 * @brief Free parent entity for free relations testing.
 * @var single_child Single child entity.
 * @var single_ctx Single child lazy loading context.
 * @var children_arr Array of children.
 * @var multi_ctx Multi-child lazy loading context.
 */
struct FreeParent {
  struct TargetChild *single_child;
  c_orm_lazy_load_context_t single_ctx;
  struct Generic_Array children_arr;
  c_orm_lazy_load_context_t multi_ctx;
};
/**
 * @brief Mock decryption routine for testing.
 *
 * @param in Input encrypted bytes.
 * @param ins Input byte length.
 * @param ctx Encryption context pointer.
 * @param out Output decrypted buffer pointer.
 * @param outs Output decrypted byte length pointer.
 */
static c_orm_error_t mock_decrypt(const void *in, size_t ins, void *ctx,
                                  void **out, size_t *outs) {
  (void)ctx;
  *out = NULL;
  if (c_orm_system_malloc(ins, out) != C_ORM_OK || !*out)
    return C_ORM_ERROR_MEMORY;
  memcpy(*out, in, ins);
  *outs = ins;
  return C_ORM_OK;
}
/**
 * @brief Mock encryption routine for testing.
 *
 * @param in Input plaintext bytes.
 * @param ins Input byte length.
 * @param ctx Encryption context pointer.
 * @param out Output encrypted buffer pointer.
 * @param outs Output encrypted byte length pointer.
 */
static c_orm_error_t mock_encrypt(const void *in, size_t ins, void *ctx,
                                  void **out, size_t *outs) {
  (void)ctx;
  *out = NULL;
  if (c_orm_system_malloc(ins, out) != C_ORM_OK || !*out)
    return C_ORM_ERROR_MEMORY;
  memcpy(*out, in, ins);
  *outs = ins;
  return C_ORM_OK;
}

static int g_malloc_fail = 0;
static int g_malloc_count = 0;
static int g_malloc_target = -1;
/**
 * @brief Helper to free table metadata columns.
 *
 * @param meta Table metadata pointer.
 * @param obj Structure instance pointer.
 */
static void test_free_meta_data(const c_orm_table_meta_t *meta, void *obj) {
  c_orm_free_columns(meta, obj);
}

static void *tracked_allocs[20000];
static int tracked_count = 0;

/**
 * @brief Mock malloc with injection failure support.
 *
 * @param size Number of bytes to allocate.
 * @param out_ptr Output pointer receiving allocated address.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on failure.
 */
static c_orm_error_t mock_malloc_fail(size_t size, void **out_ptr) {
  void *ptr;
  if (g_malloc_fail) {
    if (g_malloc_target == g_malloc_count++) {
      if (out_ptr) {
        *out_ptr = NULL;
      }
      return C_ORM_ERROR_MEMORY;
    }
  }
  ptr = malloc(size == 0 ? 1 : size);
  if (!ptr) {
    if (out_ptr) {
      *out_ptr = NULL;
    }
    return C_ORM_ERROR_MEMORY;
  }
  tracked_allocs[tracked_count++] = ptr;
  *out_ptr = ptr;
  return C_ORM_OK;
}

/**
 * @brief Mock realloc with injection failure support.
 *
 * @param ptr Existing buffer pointer.
 * @param size New size in bytes.
 * @param out_ptr Output pointer receiving reallocated address.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on failure.
 */
static c_orm_error_t mock_realloc_fail(void *ptr, size_t size, void **out_ptr) {
  void *new_ptr;
  int i;
  if (g_malloc_fail) {
    if (g_malloc_target == g_malloc_count++) {
      if (out_ptr) {
        *out_ptr = NULL;
      }
      return C_ORM_ERROR_MEMORY;
    }
  }
  new_ptr = realloc(ptr, size == 0 ? 1 : size);
  if (!new_ptr) {
    if (out_ptr) {
      *out_ptr = NULL;
    }
    return C_ORM_ERROR_MEMORY;
  }
  for (i = 0; i < tracked_count; i++) {
    if (tracked_allocs[i] == ptr) {
      tracked_allocs[i] = new_ptr;
      *out_ptr = new_ptr;
      return C_ORM_OK;
    }
  }
  tracked_allocs[tracked_count++] = new_ptr;
  *out_ptr = new_ptr;
  return C_ORM_OK;
}

/**
 * @brief Mock free tracking allocations.
 *
 * @param ptr Pointer to memory to free.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t mock_free(void *ptr) {
  int i;
  if (!ptr)
    return C_ORM_OK;
  for (i = 0; i < tracked_count; i++) {
    if (tracked_allocs[i] == ptr) {
      tracked_allocs[i] = NULL;
    }
  }
  free(ptr);
  return C_ORM_OK;
}

/**
 * @brief Free all tracked mock allocations.
 */
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
/**
 * @brief Check whether database mock call should fail.
 */
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

/**
 * @brief Mock is_null query column check.
 *
 * @param q Query handle.
 * @param i Column index.
 * @param out Output pointer to store is_null boolean flag.
 */
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
/**
 * @brief Mock query preparation.
 *
 * @param db Database handle.
 * @param sql SQL query string.
 * @param out Output query handle pointer.
 */
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

/**
 * @brief Mock query step operation.
 *
 * @param q Query handle.
 * @param out Output step result pointer.
 */
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
/**
 * @brief Mock get_string column retrieval.
 *
 * @param q Query handle.
 * @param i Column index.
 * @param out Output string pointer.
 */
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
/**
 * @brief Mock bind_int32 query parameter.
 *
 * @param q Query handle.
 * @param i Parameter index.
 * @param v 32-bit integer value.
 */
static c_orm_error_t mock_bind_int32(c_orm_query_t *q, int i, int32_t v) {
  (void)q;
  (void)i;
  (void)v;
  if (g_mock_fail_bind)
    return C_ORM_ERROR_UNKNOWN;
  return check_db_fail();
}
/**
 * @brief Mock bind_int64 query parameter.
 *
 * @param q Query handle.
 * @param i Parameter index.
 * @param v 64-bit integer value.
 */
static c_orm_error_t mock_bind_int64(c_orm_query_t *q, int i, int64_t v) {
  (void)q;
  (void)i;
  (void)v;
  return check_db_fail();
}
/**
 * @brief Mock bind_double query parameter.
 *
 * @param q Query handle.
 * @param i Parameter index.
 * @param v Double value.
 */
static c_orm_error_t mock_bind_double(c_orm_query_t *q, int i, double v) {
  (void)q;
  (void)i;
  (void)v;
  return check_db_fail();
}
/**
 * @brief Mock bind_string query parameter.
 *
 * @param q Query handle.
 * @param i Parameter index.
 * @param v String value.
 */
static c_orm_error_t mock_bind_string(c_orm_query_t *q, int i, const char *v) {
  (void)q;
  (void)i;
  (void)v;
  return check_db_fail();
}
/**
 * @brief Mock bind_blob query parameter.
 *
 * @param q Query handle.
 * @param i Parameter index.
 * @param v Blob data pointer.
 * @param s Blob byte length.
 */
static c_orm_error_t mock_bind_blob(c_orm_query_t *q, int i, const void *v,
                                    size_t s) {
  (void)q;
  (void)i;
  (void)v;
  (void)s;
  return check_db_fail();
}
/**
 * @brief Mock bind_null query parameter.
 *
 * @param q Query handle.
 * @param i Parameter index.
 */
static c_orm_error_t mock_bind_null(c_orm_query_t *q, int i) {
  (void)q;
  (void)i;
  return check_db_fail();
}
/**
 * @brief Mock get_int32 column retrieval.
 *
 * @param q Query handle.
 * @param i Column index.
 * @param o Output 32-bit integer pointer.
 */
static c_orm_error_t mock_get_int32(c_orm_query_t *q, int i, int32_t *o) {
  (void)q;
  (void)i;
  *o = 0;
  return check_db_fail();
}
/**
 * @brief Mock get_int64 column retrieval.
 *
 * @param q Query handle.
 * @param i Column index.
 * @param o Output 64-bit integer pointer.
 */
static c_orm_error_t mock_get_int64(c_orm_query_t *q, int i, int64_t *o) {
  (void)q;
  (void)i;
  *o = 0;
  return check_db_fail();
}
/**
 * @brief Mock get_double column retrieval.
 *
 * @param q Query handle.
 * @param i Column index.
 * @param o Output double pointer.
 */
static c_orm_error_t mock_get_double(c_orm_query_t *q, int i, double *o) {
  (void)q;
  (void)i;
  *o = 0.0;
  return check_db_fail();
}
static int g_mock_blob_null = 0;
static const void *g_custom_blob_data = NULL;
static size_t g_custom_blob_size = 0;

/**
 * @brief Mock get_blob column retrieval.
 *
 * @param q Query handle.
 * @param i Column index.
 * @param o Output blob data pointer.
 * @param s Output blob size pointer.
 */
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
/**
 * @brief Mock query finalization.
 *
 * @param q Query handle to finalize.
 */
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
/**
 * @brief Mock query reset operation.
 *
 * @param q Query handle to reset.
 */
static c_orm_error_t mock_reset(c_orm_query_t *q) {
  (void)q;
  return C_ORM_OK;
}
static int64_t g_mock_last_id = 123;
/**
 * @brief Mock get_last_insert_rowid operation.
 *
 * @param db Database handle.
 * @param out_id Output pointer to receive last insert rowid.
 */
static c_orm_error_t mock_get_last_insert_rowid(c_orm_db_t *db,
                                                int64_t *out_id) {
  (void)db;
  *out_id = g_mock_last_id;
  return C_ORM_OK;
}

static c_orm_driver_vtable_t g_vt = {0};
static c_orm_db_t g_db = {0};

static c_orm_column_meta_t my_cols[20];
static c_orm_table_meta_t mega_meta;

/**
 * @brief Initialize mock database vtable functions.
 */
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
  *val = g_mock_child_fk_id;
  return C_ORM_OK;
}

/**
 * @brief Mock get_string returning NULL string.
 *
 * @param q Query handle.
 * @param index Column index.
 * @param val Output pointer to receive string pointer.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t mock_get_string_null(c_orm_query_t *q, int index,
                                          const char **val) {
  (void)q;
  (void)index;
  *val = NULL;
  return C_ORM_OK;
}

/**
 * @brief Mock is_null always returning false.
 *
 * @param q Query handle.
 * @param index Column index.
 * @param is_null Output pointer to receive 0 flag.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t mock_is_null_false(c_orm_query_t *q, int index,
                                        int *is_null) {
  (void)q;
  (void)index;
  *is_null = 0;
  return C_ORM_OK;
}

/**
 * @brief Mock is_null always returning true.
 *
 * @param q Query handle.
 * @param index Column index.
 * @param is_null Output pointer to receive 1 flag.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t mock_is_null_true(c_orm_query_t *q, int index,
                                       int *is_null) {
  (void)q;
  (void)index;
  *is_null = 1;
  return C_ORM_OK;
}

/**
 * @brief NestedChild entity for nested relation tests.
 * @var id Child ID.
 * @var parent_id Parent foreign key.
 * @var name Child name.
 */
struct NestedChild {
  int32_t id;
  int32_t parent_id;
  char name[32];
};

/**
 * @brief NestedParent entity for nested relation tests.
 * @var id Parent ID.
 * @var belongs_to_id Belongs-to foreign key.
 * @var belongs_to_child Belongs-to child pointer.
 * @var o2o_child One-to-one child pointer.
 * @var o2m_children One-to-many children array.
 * @var name Parent name.
 */
struct NestedParent {
  int32_t id;
  int32_t belongs_to_id;
  struct NestedChild *belongs_to_child;
  struct NestedChild *o2o_child;
  struct Generic_Array o2m_children;
  char name[32];
};

/** @brief Extended parent structure with various relational mappings. */
struct ExtendedParent {
  /** @brief Primary key ID. */
  int32_t id;
  /** @brief Belongs-to foreign key ID. */
  int32_t belongs_to_id;
  /** @brief Pointer to one-to-one child structure. */
  struct NestedChild *child_o2o;
  /** @brief Array of one-to-many children structures. */
  struct Generic_Array children_o2m;
  /** @brief Array of many-to-many tags structures. */
  struct Generic_Array tags_m2m;
  /** @brief Parent name string buffer. */
  char name[32];
};

/** @brief Full parent structure with lazy load context fields. */
struct FullParentObj {
  /** @brief Primary key ID. */
  int32_t id;
  /** @brief Belongs-to foreign key ID. */
  int32_t belongs_to_id;
  /** @brief Pointer to one-to-one child structure. */
  struct NestedChild *child_o2o;
  /** @brief Array of one-to-many children structures. */
  struct Generic_Array children_o2m;
  /** @brief Array of many-to-many tags structures. */
  struct Generic_Array tags_m2m;
  /** @brief Parent name string buffer. */
  char name[32];
  /** @brief Lazy load context for one-to-one child. */
  c_orm_lazy_load_context_t o2o_ctx;
  /** @brief Lazy load context for one-to-many children. */
  c_orm_lazy_load_context_t o2m_ctx;
  /** @brief Lazy load context for many-to-many tags. */
  c_orm_lazy_load_context_t m2m_ctx;
};

/** @brief Object representation with a string primary key. */
struct StrPkObj {
  /** @brief Name string value. */
  char *name;
  /** @brief String primary key identifier. */
  char *id;
};

/** @brief Model object for testing TTL expiration and type conversions. */
struct TtlTestUser {
  /** @brief Created at timestamp in seconds. */
  int64_t created_at;
  /** @brief TTL expiration time in seconds. */
  int32_t expires_in;
  /** @brief Username string. */
  char *username;
  /** @brief Email address string. */
  char *email;
  /** @brief Nullable age integer pointer. */
  int32_t *age;
  /** @brief Nullable score float pointer. */
  float *score;
  /** @brief Nullable active boolean pointer. */
  bool *is_active;
  /** @brief Created at string representation. */
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
 * @return C_ORM_OK.
 */
static c_orm_error_t test_api_expire_cb(c_orm_db_t *db,
                                        const c_orm_table_meta_t *meta,
                                        void *rec, void *ud) {
  (void)db;
  (void)meta;
  (void)rec;
  (void)ud;
  return C_ORM_OK;
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
  if (g_mock_err_stage == 9) {
    if (strstr(sql, "INSERT"))
      return C_ORM_ERROR_UNKNOWN;
  }
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
  if (g_mock_fail_pk_bind == 1) {
    if (i >= 3)
      return C_ORM_ERROR_UNKNOWN;
  }
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
  if (g_mock_err_stage == 8) {
    if (i == 0)
      return C_ORM_ERROR_UNKNOWN;
  }
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
  if (i == 1) {
    *o = 2;
    return C_ORM_OK;
  }
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
  if (g_stage_bind_cnt++ >= 1) {
    if (i > 0)
      return C_ORM_ERROR_UNKNOWN;
  }
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
  if (i == 1) {
    *out = 1;
    return C_ORM_OK;
  }
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
  if (g_stage_step_cnt >= 2) {
    *o = 999;
    return C_ORM_OK;
  }
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
  if (i >= 2) {
    *out = 1;
    return C_ORM_OK;
  }
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
  if (!in_data)
    return C_ORM_ERROR_MEMORY;
  *out_data = malloc(in_size);
  if (!*out_data)
    return C_ORM_ERROR_MEMORY;
  memcpy(*out_data, in_data, in_size);
  *out_size = in_size;
  return C_ORM_OK;
}

/** @brief Structure representing a parent with nullable belongs_to relation. */
struct NullableParent {
  /** @brief Primary key ID. */
  int32_t id;
  /** @brief Nullable belongs-to foreign key ID pointer. */
  int32_t *belongs_to_id;
  /** @brief Pointer to associated child struct. */
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
 * @param out_ptr Output pointer receiving allocated block or NULL.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on simulated failure.
 */
static c_orm_error_t mock_deep_malloc(size_t sz, void **out_ptr) {
  if (g_deep_fail_oom == g_deep_alloc_cnt++) {
    if (out_ptr) {
      *out_ptr = NULL;
    }
    return C_ORM_ERROR_MEMORY;
  }
  *out_ptr = malloc(sz == 0 ? 1 : sz);
  return *out_ptr ? C_ORM_OK : C_ORM_ERROR_MEMORY;
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
 * @param out_ptr Output pointer receiving reallocated block or NULL.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on simulated failure.
 */
static c_orm_error_t mock_deep_realloc(void *ptr, size_t sz, void **out_ptr) {
  if (g_deep_fail_realloc == g_deep_realloc_cnt++ ||
      (g_deep_fail_realloc == 99 && sz >= 200)) {
    if (out_ptr) {
      *out_ptr = NULL;
    }
    return C_ORM_ERROR_MEMORY;
  }
  *out_ptr = realloc(ptr, sz == 0 ? 1 : sz);
  return *out_ptr ? C_ORM_OK : C_ORM_ERROR_MEMORY;
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
  if (g_deep_fail_get == 3) {
    if (i == 11)
      return C_ORM_ERROR_UNKNOWN;
  }
  if (g_deep_fail_get == 4) {
    if (i == 12)
      return C_ORM_ERROR_UNKNOWN;
  }
  if (i == 11) {
    *o = g_deep_pt_wkb;
    *s = sizeof(g_deep_pt_wkb);
  } else if (i == 12) {
    *o = g_deep_poly_wkb;
    *s = sizeof(g_deep_poly_wkb);
  } else {
    *o = "blobdata";
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
 * @return C_ORM_OK.
 */
static c_orm_error_t
dummy_cov_expire_callback_100(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                              void *obj, void *ud) {
  (void)db;
  (void)meta;
  (void)obj;
  (void)ud;
  return C_ORM_OK;
}

/**
 * @brief Mock malloc that always returns C_ORM_ERROR_MEMORY.
 * @param s Allocation size in bytes.
 * @param out_ptr Output pointer receiving NULL.
 * @return C_ORM_ERROR_MEMORY.
 */
static c_orm_error_t cov_always_null_malloc(size_t s, void **out_ptr) {
  (void)s;
  if (out_ptr) {
    *out_ptr = NULL;
  }
  return C_ORM_ERROR_MEMORY;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* TEST_API_HELPERS_H */
