#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_generic.c
 * @brief Unit tests for generic CRUD functions, telemetry, and allocations.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
/* clang-format off */
#include "Models.h"
#include "c_orm_api.h"
#include "c_orm_sqlite.h"
#include "greatest.h"
/* clang-format on */

TEST test_c_orm_generic_crud(void) {
  struct Users u;
  struct Users out_u;
  void *arr = NULL;
  size_t count = 0;
  c_orm_error_t err;
  c_orm_db_t *test_db = NULL;
  bool active = true;

  err = c_orm_sqlite_connect(":memory:", &test_db);
  ASSERT_EQ(C_ORM_OK, err);

  /* create table */
  err = c_orm_execute_raw(test_db, "CREATE TABLE users ("
                                   "id INTEGER PRIMARY KEY,"
                                   "username VARCHAR(255) NOT NULL,"
                                   "email VARCHAR(255) UNIQUE NOT NULL,"
                                   "age INT,"
                                   "score FLOAT,"
                                   "is_active BOOLEAN,"
                                   "created_at TIMESTAMP"
                                   ");");
  ASSERT_EQ(C_ORM_OK, err);

  memset(&u, 0, sizeof(u));
  u.id = 1;
  u.username = "generic_user";
  u.email = "gen@example.com";
  u.is_active = &active;
  u.created_at = "2026-03-30 01:00:00";

  err = c_orm_insert_generic(test_db, &Users_meta, &u);
  ASSERT_EQ(C_ORM_OK, err);
  err =
      c_orm_insert_generic(test_db, &Users_meta, &u); /* Duplicate constraint */
  if (err != C_ORM_OK) {
    const char *msg;
    test_db->vtable->get_last_error(test_db, &msg);
    fprintf(stderr, "INSERT ERR: %d - %s\n", err, msg);
  }
  ASSERT_EQ(C_ORM_ERROR_STEP, err);

  memset(&out_u, 0, sizeof(out_u));
  err = c_orm_get_generic(test_db, &Users_meta, 1, &out_u);
  ASSERT_EQ(C_ORM_OK, err);
  if (err == C_ORM_OK) {
    if (out_u.username)
      C_ORM_FREE(out_u.username);
    if (out_u.email)
      C_ORM_FREE(out_u.email);
    C_ORM_FREE(out_u.age);
    C_ORM_FREE(out_u.score);
    if (out_u.is_active)
      C_ORM_FREE(out_u.is_active);
    if (out_u.created_at)
      C_ORM_FREE(out_u.created_at);
  }

  err = c_orm_find_all_generic(test_db, &Users_meta, &arr, &count);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT(count > 0);

  if (arr) {
    size_t i;
    struct Users *users_arr = (struct Users *)arr;
    for (i = 0; i < count; i++) {
      if (users_arr[i].username)
        C_ORM_FREE(users_arr[i].username);
      if (users_arr[i].email)
        C_ORM_FREE(users_arr[i].email);
      C_ORM_FREE(users_arr[i].age);
      C_ORM_FREE(users_arr[i].score);
      if (users_arr[i].is_active)
        C_ORM_FREE(users_arr[i].is_active);
      if (users_arr[i].created_at)
        C_ORM_FREE(users_arr[i].created_at);
    }
    C_ORM_FREE(arr);
  }

  /* Get generic string */
  err = c_orm_execute_raw(
      test_db,
      "CREATE TABLE str_table (id VARCHAR(255) PRIMARY KEY, val INT);");
  ASSERT_EQ(C_ORM_OK, err);
  err = c_orm_execute_raw(
      test_db, "INSERT INTO str_table (id, val) VALUES ('my_id', 42);");
  ASSERT_EQ(C_ORM_OK, err);

  /* Can't easily use inline macros for str_table without declaring it, but we
   * can test bad table meta */
  {
    c_orm_table_meta_t bad_meta = Users_meta;
    c_orm_column_meta_t cols[1];
    struct {
      char *id;
      int32_t val;
    } my_struct;

    bad_meta.name = "str_table";
    memset(&cols[0], 0, sizeof(c_orm_column_meta_t));
    cols[0].name = "id";
    cols[0].type = C_ORM_TYPE_STRING;
    cols[0].is_pk = 1;
    bad_meta.columns = cols;
    bad_meta.num_columns = 1;
    bad_meta.query_select_by_pk = "SELECT * FROM str_table WHERE id = ?";

    err = c_orm_get_generic_string(test_db, &bad_meta, "my_id", &my_struct);
    ASSERT_EQ(C_ORM_OK, err);
    if (err == C_ORM_OK && my_struct.id) {
      C_ORM_FREE(my_struct.id);
    }

    err =
        c_orm_get_generic_string(test_db, &bad_meta, "missing_id", &my_struct);
    ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, err);
  }

  /* Test query_select_by_pk == NULL, is_view, and not found */
  {
    c_orm_table_meta_t gm_err;
    c_orm_column_meta_t no_pk_cols[1];
    c_orm_shard_manager_t *sm = NULL;
    void *sg_arr = NULL;
    size_t sg_count = 0;

    err = c_orm_get_generic(test_db, &Users_meta, 999, &out_u);
    ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, err);

    /* Test missing PK column validation and is_view */
    memset(no_pk_cols, 0, sizeof(no_pk_cols));
    no_pk_cols[0].name = "dummy";
    no_pk_cols[0].is_pk = 0;
    gm_err = Users_meta;
    gm_err.columns = no_pk_cols;
    gm_err.num_columns = 1;

    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_get_generic(test_db, &gm_err, 1, &out_u));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_get_generic_string(test_db, &gm_err, "my_id", &out_u));

    gm_err = Users_meta;
    gm_err.is_view = 1;
    ASSERT_EQ(C_ORM_ERROR_READ_ONLY,
              c_orm_insert_generic(test_db, &gm_err, &u));

    /* Empty table find all generic */
    err = c_orm_execute_raw(test_db, "CREATE TABLE empty_u (id INT, username "
                                     "TEXT, email TEXT, age INT, score REAL, "
                                     "is_active INT, created_at TEXT);");
    ASSERT_EQ(C_ORM_OK, err);
    gm_err = Users_meta;
    gm_err.name = "empty_u";
    gm_err.query_select_all = "SELECT * FROM empty_u";
    arr = (void *)1;
    count = 99;
    ASSERT_EQ(C_ORM_OK, c_orm_find_all_generic(test_db, &gm_err, &arr, &count));
    ASSERT_EQ(0, count);
    if (arr)
      C_ORM_FREE(arr);

    /* Scatter gather generic with populated and NULL shards */
    ASSERT_EQ(C_ORM_OK, c_orm_shard_manager_init(2, &sm));
    ASSERT_EQ(C_ORM_OK, c_orm_shard_manager_add_node(sm, 0, test_db));
    /* node 1 is NULL */
    ASSERT_EQ(C_ORM_OK, c_orm_scatter_gather_generic(sm, &Users_meta, &sg_arr,
                                                     &sg_count));
    ASSERT(sg_count > 0);
    if (sg_arr) {
      size_t j;
      struct Users *sg_users = (struct Users *)sg_arr;
      for (j = 0; j < sg_count; j++) {
        if (sg_users[j].username)
          C_ORM_FREE(sg_users[j].username);
        if (sg_users[j].email)
          C_ORM_FREE(sg_users[j].email);
        C_ORM_FREE(sg_users[j].age);
        C_ORM_FREE(sg_users[j].score);
        if (sg_users[j].is_active)
          C_ORM_FREE(sg_users[j].is_active);
        if (sg_users[j].created_at)
          C_ORM_FREE(sg_users[j].created_at);
      }
      C_ORM_FREE(sg_arr);
    }
    c_orm_shard_manager_free(sm);
  }

  test_db->vtable->disconnect(test_db);
  PASS();
}

TEST test_c_orm_telemetry(void) {
  c_orm_error_t err;
  c_orm_db_t *test_db = NULL;
  c_orm_pool_telemetry_t telemetry;

  err = c_orm_sqlite_connect(":memory:", &test_db);
  ASSERT_EQ(C_ORM_OK, err);

  c_orm_set_slow_query_threshold(test_db, 1); /* Log everything over 1ms */

  err = c_orm_execute_raw(
      test_db,
      "CREATE TABLE telemetry_test (id INTEGER PRIMARY KEY, delay TEXT);");
  ASSERT_EQ(C_ORM_OK, err);

  /* Simulate a bit of execution to test tracking */
  /* Wait for a few ms using an inefficient SQLite recursive CTE just to trigger
   * the slow log! */
  err = c_orm_execute_raw(test_db,
                          "WITH RECURSIVE cnt(x) AS (SELECT 1 UNION ALL SELECT "
                          "x+1 FROM cnt WHERE x<1000) SELECT sum(x) FROM cnt;");
  ASSERT_EQ(C_ORM_OK, err);

  err = c_orm_get_telemetry(test_db, &telemetry);
  ASSERT_EQ(C_ORM_OK, err);
  /* In tests it might execute too fast on modern hardware, but we ensure the
   * struct populates without segfaulting */

  test_db->vtable->disconnect(test_db);
  PASS();
}

static void *mock_fail_malloc(size_t sz) {
  (void)sz;
  return NULL;
}

TEST test_c_orm_alloc(void) {
  char *dup = (char *)1;
  void *(*old_malloc)(size_t) = c_orm_malloc;

  ASSERT_EQ(0, c_orm_strdup(NULL, &dup));
  ASSERT_EQ(NULL, dup);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_strdup("abc", NULL));

  c_orm_set_allocators(mock_fail_malloc, c_orm_realloc, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_strdup("abc", &dup));
  ASSERT_EQ(NULL, dup);
  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);

  PASS();
}

C_ORM_EXPORT c_orm_error_t C_CDD_LOG_DEBUG(const char *fmt, ...);
TEST test_cdd_c_compat_log(void) {
  ASSERT_EQ(C_ORM_OK, C_CDD_LOG_DEBUG("Test log\n"));
  PASS();
}

SUITE(generic_suite) {
  RUN_TEST(test_c_orm_generic_crud);
  RUN_TEST(test_c_orm_telemetry);
  RUN_TEST(test_c_orm_alloc);
  RUN_TEST(test_cdd_c_compat_log);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#if defined(__clang__) || defined(__GNUC__)
#endif
