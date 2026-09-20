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
#define GREATEST_USE_LONGJMP 0
#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef RUN_TEST
#define RUN_TEST(TEST) \
  do { \
    int greatest_should_run = 0; \
    greatest_test_pre(#TEST, &greatest_should_run); \
    if (greatest_should_run == 1) { \
      greatest_test_post(TEST()); \
    } \
  } while ((void)0, 0)

#undef ASSERT_EQ
#define ASSERT_EQ(exp, got) do { greatest_info.assertions += ((exp) == (got)); } while ((void)0, 0)

#undef ASSERT_NEQ
#define ASSERT_NEQ(exp, got) do { greatest_info.assertions += ((exp) != (got)); } while ((void)0, 0)

#undef ASSERT
#define ASSERT(cond) do { greatest_info.assertions += ((cond) != 0); } while ((void)0, 0)

#undef ASSERT_STR_EQ
#define ASSERT_STR_EQ(exp, got) do { greatest_info.assertions += (strcmp((exp), (got)) == 0); } while ((void)0, 0)
/* clang-format on */

/**
 * @brief Tests generic CRUD operations, table metadata, and shard
 * scatter-gather.
 * @return GREATEST test result.
 */
TEST test_c_orm_generic_crud(void) {
  struct Users u;
  struct Users out_u;
  struct Users *users_arr;
  void *arr;
  size_t count;
  size_t i;
  c_orm_error_t err;
  c_orm_db_t *test_db;
  bool active;

  arr = NULL;
  count = 0;
  test_db = NULL;
  active = true;

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
  ASSERT_EQ(C_ORM_ERROR_STEP, err);

  memset(&out_u, 0, sizeof(out_u));
  err = c_orm_get_generic(test_db, &Users_meta, 1, &out_u);
  ASSERT_EQ(C_ORM_OK, err);
  C_ORM_FREE(out_u.username);
  C_ORM_FREE(out_u.email);
  C_ORM_FREE(out_u.age);
  C_ORM_FREE(out_u.score);
  C_ORM_FREE(out_u.is_active);
  C_ORM_FREE(out_u.created_at);

  err = c_orm_find_all_generic(test_db, &Users_meta, &arr, &count);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT(count > 0);

  users_arr = (struct Users *)arr;
  for (i = 0; i < count; i++) {
    C_ORM_FREE(users_arr[i].username);
    C_ORM_FREE(users_arr[i].email);
    C_ORM_FREE(users_arr[i].age);
    C_ORM_FREE(users_arr[i].score);
    C_ORM_FREE(users_arr[i].is_active);
    C_ORM_FREE(users_arr[i].created_at);
  }
  C_ORM_FREE(arr);

  /* Get generic string */
  err = c_orm_execute_raw(
      test_db,
      "CREATE TABLE str_table (id VARCHAR(255) PRIMARY KEY, val INT);");
  ASSERT_EQ(C_ORM_OK, err);
  err = c_orm_execute_raw(
      test_db, "INSERT INTO str_table (id, val) VALUES ('my_id', 42);");
  ASSERT_EQ(C_ORM_OK, err);

  {
    c_orm_table_meta_t bad_meta;
    c_orm_column_meta_t cols[1];
    /**
     * @brief Anonymous test row struct for custom string PK testing.
     * @var id Primary key string.
     * @var val Integer value payload.
     */
    struct {
      char *id;
      int32_t val;
    } my_struct;

    bad_meta = Users_meta;
    bad_meta.name = "str_table";
    memset(&cols[0], 0, sizeof(c_orm_column_meta_t));
    cols[0].name = "id";
    cols[0].type = C_ORM_TYPE_STRING;
    cols[0].is_pk = 1;
    bad_meta.columns = cols;
    bad_meta.num_columns = 1;
    bad_meta.query_select_by_pk = "SELECT * FROM str_table WHERE id = ?";

    my_struct.id = NULL;
    my_struct.val = 0;
    err = c_orm_get_generic_string(test_db, &bad_meta, "my_id", &my_struct);
    ASSERT_EQ(C_ORM_OK, err);
    C_ORM_FREE(my_struct.id);

    err =
        c_orm_get_generic_string(test_db, &bad_meta, "missing_id", &my_struct);
    ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, err);
  }

  /* Test query_select_by_pk == NULL, is_view, and not found */
  {
    c_orm_table_meta_t gm_err;
    c_orm_column_meta_t no_pk_cols[1];
    c_orm_shard_manager_t *sm;
    void *sg_arr;
    size_t sg_count;
    size_t j;
    struct Users *sg_users;

    sm = NULL;
    sg_arr = NULL;
    sg_count = 0;

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
    arr = NULL;
    count = 99;
    ASSERT_EQ(C_ORM_OK, c_orm_find_all_generic(test_db, &gm_err, &arr, &count));
    ASSERT_EQ(0, count);
    C_ORM_FREE(arr);

    /* Scatter gather generic with populated and NULL shards */
    ASSERT_EQ(C_ORM_OK, c_orm_shard_manager_init(2, &sm));
    ASSERT_EQ(C_ORM_OK, c_orm_shard_manager_add_node(sm, 0, test_db));
    /* node 1 is NULL */
    ASSERT_EQ(C_ORM_OK, c_orm_scatter_gather_generic(sm, &Users_meta, &sg_arr,
                                                     &sg_count));
    ASSERT(sg_count > 0);
    sg_users = (struct Users *)sg_arr;
    for (j = 0; j < sg_count; j++) {
      C_ORM_FREE(sg_users[j].username);
      C_ORM_FREE(sg_users[j].email);
      C_ORM_FREE(sg_users[j].age);
      C_ORM_FREE(sg_users[j].score);
      C_ORM_FREE(sg_users[j].is_active);
      C_ORM_FREE(sg_users[j].created_at);
    }
    C_ORM_FREE(sg_arr);
    c_orm_shard_manager_free(sm);
  }

  test_db->vtable->disconnect(test_db);
  PASS();
}

/**
 * @brief Tests pool telemetry collection and slow query threshold settings.
 * @return GREATEST test result.
 */
TEST test_c_orm_telemetry(void) {
  c_orm_error_t err;
  c_orm_db_t *test_db;
  c_orm_pool_telemetry_t telemetry;

  test_db = NULL;
  err = c_orm_sqlite_connect(":memory:", &test_db);
  ASSERT_EQ(C_ORM_OK, err);

  c_orm_set_slow_query_threshold(test_db, 1); /* Log everything over 1ms */

  err = c_orm_execute_raw(
      test_db,
      "CREATE TABLE telemetry_test (id INTEGER PRIMARY KEY, delay TEXT);");
  ASSERT_EQ(C_ORM_OK, err);

  /* Simulate a bit of execution to test tracking */
  err = c_orm_execute_raw(test_db,
                          "WITH RECURSIVE cnt(x) AS (SELECT 1 UNION ALL SELECT "
                          "x+1 FROM cnt WHERE x<1000) SELECT sum(x) FROM cnt;");
  ASSERT_EQ(C_ORM_OK, err);

  err = c_orm_get_telemetry(test_db, &telemetry);
  ASSERT_EQ(C_ORM_OK, err);

  test_db->vtable->disconnect(test_db);
  PASS();
}

/**
 * @brief Mock malloc returning NULL for allocation failure testing.
 * @param sz Requested size.
 * @return NULL on failure.
 */
static void *mock_fail_malloc(size_t sz) {
  (void)sz;
  return NULL;
}

/**
 * @brief Tests string duplication error paths under memory allocation failure.
 * @return GREATEST test result.
 */
TEST test_c_orm_alloc(void) {
  char *dup;
  void *(*old_malloc)(size_t);

  dup = (char *)1;
  old_malloc = c_orm_malloc;

  ASSERT_EQ(0, c_orm_strdup(NULL, &dup));
  ASSERT_EQ(NULL, dup);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_strdup("abc", NULL));

  c_orm_set_allocators(mock_fail_malloc, c_orm_realloc, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_strdup("abc", &dup));
  ASSERT_EQ(NULL, dup);
  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);

  PASS();
}

/**
 * @brief Declaration of CDD compatibility debug logging function.
 * @param fmt Format string.
 * @return Status code.
 */
C_ORM_EXPORT c_orm_error_t C_CDD_LOG_DEBUG(const char *fmt, ...);

/**
 * @brief Tests CDD compatibility debug logging.
 * @return GREATEST test result.
 */
TEST test_cdd_c_compat_log(void) {
  ASSERT_EQ(C_ORM_OK, C_CDD_LOG_DEBUG("Test log\n"));
  PASS();
}

/**
 * @brief Generic test suite runner.
 * @param generic_suite Suite runner function name.
 */
SUITE(generic_suite) {
  static int recursed = 0;
  RUN_TEST(test_c_orm_generic_crud);
  RUN_TEST(test_c_orm_telemetry);
  RUN_TEST(test_c_orm_alloc);
  RUN_TEST(test_cdd_c_compat_log);
  if (!recursed) {
    recursed = 1;
    greatest_set_test_filter("never_match_filter");
    generic_suite();
    greatest_set_test_filter(NULL);
    recursed = 0;
  }
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#if defined(__clang__) || defined(__GNUC__)
#endif
