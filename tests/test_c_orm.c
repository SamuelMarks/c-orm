/* clang-format off */
#include "greatest.h"

#include <c_orm/c_orm.h>

#include <stdlib.h>

#include <string.h>
/* clang-format on */

TEST test_c_orm_connect_and_disconnect(void) {
  c_orm_db_t *db = NULL;
  int res;

#ifdef C_ORM_HAVE_SQLITE
  res = c_orm_connect(&db, C_ORM_DIALECT_SQLITE, "file.db");
  ASSERT_EQ(0, res);
  ASSERT_NEQ(NULL, db);

  /* Disconnect is void currently but needs coverage */
  c_orm_disconnect(db);
#endif

  /* Null check */
  c_orm_disconnect(NULL);

  res = c_orm_connect(NULL, C_ORM_DIALECT_SQLITE, "file.db");
  ASSERT_EQ(-1, res);

  res = c_orm_connect(&db, C_ORM_DIALECT_SQLITE, NULL);
  ASSERT_EQ(-1, res);

  PASS();
}

TEST test_c_orm_postgres_connect(void) {
  c_orm_db_t *db = NULL;
  int res;

#ifdef C_ORM_HAVE_POSTGRES
  res = c_orm_connect(&db, C_ORM_DIALECT_POSTGRES, "test_mock_success");
  ASSERT_EQ(0, res);
  ASSERT_NEQ(NULL, db);
  c_orm_disconnect(db);
#else
  res = c_orm_connect(&db, C_ORM_DIALECT_POSTGRES, "test_mock_success");
  ASSERT_EQ(-4, res);
#endif
  PASS();
}

TEST test_c_orm_mysql_connect(void) {
  c_orm_db_t *db = NULL;
  int res;

#ifdef C_ORM_HAVE_MYSQL
  res = c_orm_connect(&db, C_ORM_DIALECT_MYSQL, "test_mock_success");
  ASSERT_EQ(0, res);
  ASSERT_NEQ(NULL, db);
  c_orm_disconnect(db);
#else
  res = c_orm_connect(&db, C_ORM_DIALECT_MYSQL, "test_mock_success");
  ASSERT_EQ(-4, res);
#endif
  PASS();
}

TEST test_c_orm_unsupported_dialect(void) {
  c_orm_db_t *db = NULL;
  int res = c_orm_connect(&db, C_ORM_DIALECT_UNKNOWN, "file.db");
  ASSERT_EQ(-1, res);
  PASS();
}

/* Helpers for tests to use a valid dialect regardless of build flags */
static c_orm_dialect_t get_active_dialect(void) {
#ifdef C_ORM_HAVE_SQLITE
  return C_ORM_DIALECT_SQLITE;
#elif defined(C_ORM_HAVE_POSTGRES)
  return C_ORM_DIALECT_POSTGRES;
#elif defined(C_ORM_HAVE_MYSQL)
  return C_ORM_DIALECT_MYSQL;
#else
  return C_ORM_DIALECT_UNKNOWN;
#endif
}

TEST test_c_orm_migrate(void) {
  c_orm_db_t *db = NULL;
  c_orm_dialect_t dialect = get_active_dialect();
  int version;
  int res;
  if (dialect == C_ORM_DIALECT_UNKNOWN)
    SKIP();

  res = c_orm_connect(&db, dialect, "file.db");
  ASSERT_EQ(0, res);

  /* Initial state should be 0 */
  res = c_orm_migrate_current_version(db, &version);
  ASSERT_EQ(0, res);
  ASSERT_EQ(0, version);

  /* Apply migrations up */
  res = c_orm_migrate(db, "migrations");
  ASSERT_EQ(0, res);

  res = c_orm_migrate_current_version(db, &version);
  ASSERT_EQ(0, res);
  ASSERT_EQ(1, version);

  /* Apply another migration up */
  res = c_orm_migrate(db, "migrations");
  ASSERT_EQ(0, res);

  res = c_orm_migrate_current_version(db, &version);
  ASSERT_EQ(0, res);
  ASSERT_EQ(2, version);

  /* Rollback 1 version */
  res = c_orm_migrate_rollback(db, "migrations");
  ASSERT_EQ(0, res);

  res = c_orm_migrate_current_version(db, &version);
  ASSERT_EQ(0, res);
  ASSERT_EQ(1, version);

  /* Test null args */
  res = c_orm_migrate(NULL, "migrations");
  ASSERT_EQ(-1, res);

  res = c_orm_migrate(db, NULL);
  ASSERT_EQ(-1, res);

  res = c_orm_migrate_rollback(NULL, "migrations");
  ASSERT_EQ(-1, res);

  res = c_orm_migrate_rollback(db, NULL);
  ASSERT_EQ(-1, res);

  res = c_orm_migrate_current_version(NULL, &version);
  ASSERT_EQ(-1, res);

  res = c_orm_migrate_current_version(db, NULL);
  ASSERT_EQ(-1, res);

  c_orm_disconnect(db);
  PASS();
}

TEST test_c_orm_execute(void) {
  c_orm_db_t *db = NULL;
  c_orm_dialect_t dialect = get_active_dialect();
  int res;
  if (dialect == C_ORM_DIALECT_UNKNOWN)
    SKIP();

  res = c_orm_connect(&db, dialect, "file.db");
  ASSERT_EQ(0, res);

  res = c_orm_execute(db, "SELECT 1");
  ASSERT_EQ(0, res);

  res = c_orm_execute(NULL, "SELECT 1");
  ASSERT_EQ(-1, res);

  res = c_orm_execute(db, NULL);
  ASSERT_EQ(-1, res);

  c_orm_disconnect(db);
  PASS();
}

TEST test_c_orm_execute_params_valid(void) {
  c_orm_db_t *db = NULL;
  c_orm_dialect_t dialect = get_active_dialect();
  int res;
  c_orm_param_t params[5];
  if (dialect == C_ORM_DIALECT_UNKNOWN)
    SKIP();

  res = c_orm_connect(&db, dialect, "file.db");
  ASSERT_EQ(0, res);

  params[0].type = C_ORM_PARAM_INTEGER;
  params[0].value.int_val = 42;

  params[1].type = C_ORM_PARAM_REAL;
  params[1].value.real_val = 3.14;

  params[2].type = C_ORM_PARAM_TEXT;
  params[2].value.text_val = "hello world";

  params[3].type = C_ORM_PARAM_BLOB;
  params[3].value.blob_val.data = "deadbeef";
  params[3].value.blob_val.size = 8;

  params[4].type = C_ORM_PARAM_NULL;

  res = c_orm_execute_params(db, "INSERT INTO test VALUES (?, ?, ?, ?, ?)",
                             params, 5);
  ASSERT_EQ(0, res);

  res = c_orm_execute_params(db, "SELECT 1", NULL, 0);
  ASSERT_EQ(0, res);

  c_orm_disconnect(db);
  PASS();
}

TEST test_c_orm_execute_params_invalid(void) {
  c_orm_db_t *db = NULL;
  c_orm_dialect_t dialect = get_active_dialect();
  int res;
  c_orm_param_t param;
  if (dialect == C_ORM_DIALECT_UNKNOWN)
    SKIP();

  res = c_orm_connect(&db, dialect, "file.db");
  ASSERT_EQ(0, res);

  /* Null text */
  param.type = C_ORM_PARAM_TEXT;
  param.value.text_val = NULL;
  res = c_orm_execute_params(db, "SELECT ?", &param, 1);
  ASSERT_EQ(-2, res);

  /* Invalid blob */
  param.type = C_ORM_PARAM_BLOB;
  param.value.blob_val.data = NULL;
  param.value.blob_val.size = 10;
  res = c_orm_execute_params(db, "SELECT ?", &param, 1);
  ASSERT_EQ(-2, res);

  /* Valid blob with size 0 and null data */
  param.type = C_ORM_PARAM_BLOB;
  param.value.blob_val.data = NULL;
  param.value.blob_val.size = 0;
  res = c_orm_execute_params(db, "SELECT ?", &param, 1);
  ASSERT_EQ(0, res);

  /* Unknown type */
  param.type = (c_orm_param_type_t)999;
  res = c_orm_execute_params(db, "SELECT ?", &param, 1);
  ASSERT_EQ(-3, res);

  /* Null db */
  res = c_orm_execute_params(NULL, "SELECT ?", &param, 1);
  ASSERT_EQ(-1, res);

  /* Null query */
  res = c_orm_execute_params(db, NULL, &param, 1);
  ASSERT_EQ(-1, res);

  /* Null params with count > 0 */
  res = c_orm_execute_params(db, "SELECT ?", NULL, 1);
  ASSERT_EQ(-1, res);

  c_orm_disconnect(db);
  PASS();
}

TEST test_c_orm_transactions(void) {
  c_orm_db_t *db = NULL;
  c_orm_dialect_t dialect = get_active_dialect();
  int res;
  if (dialect == C_ORM_DIALECT_UNKNOWN)
    SKIP();

  res = c_orm_connect(&db, dialect, "file.db");
  ASSERT_EQ(0, res);

  /* Normal flow */
  res = c_orm_transaction_begin(db);
  ASSERT_EQ(0, res);
  res = c_orm_transaction_commit(db);
  ASSERT_EQ(0, res);

  /* Rollback flow */
  res = c_orm_transaction_begin(db);
  ASSERT_EQ(0, res);
  res = c_orm_transaction_rollback(db);
  ASSERT_EQ(0, res);

  /* Null checks */
  ASSERT_EQ(-1, c_orm_transaction_begin(NULL));
  ASSERT_EQ(-1, c_orm_transaction_commit(NULL));
  ASSERT_EQ(-1, c_orm_transaction_rollback(NULL));

  c_orm_disconnect(db);
  PASS();
}

/* Logging state and callback */
static int g_log_call_count = 0;
static const char *g_last_query = NULL;
static void *g_last_user_data = NULL;
static double g_last_duration = -1.0;

static void test_logger_cb(const char *query, double duration_ms,
                           void *user_data) {
  g_log_call_count++;
  g_last_query =
      query; /* Note: query pointer must remain valid for this to work */
  g_last_user_data = user_data;
  g_last_duration = duration_ms;
}

TEST test_c_orm_logging(void) {
  c_orm_db_t *db = NULL;
  c_orm_dialect_t dialect = get_active_dialect();
  int res;
  int mock_user_data = 42;
  if (dialect == C_ORM_DIALECT_UNKNOWN)
    SKIP();

  res = c_orm_connect(&db, dialect, "file.db");
  ASSERT_EQ(0, res);

  /* Test setting logger */
  res = c_orm_set_logger(db, test_logger_cb, &mock_user_data);
  ASSERT_EQ(0, res);

  res = c_orm_set_logger(NULL, test_logger_cb, &mock_user_data);
  ASSERT_EQ(-1, res);

  g_log_call_count = 0;
  res = c_orm_execute(db, "SELECT 1 FROM log_test");
  ASSERT_EQ(0, res);
  ASSERT_EQ(1, g_log_call_count);
  ASSERT_STR_EQ("SELECT 1 FROM log_test", g_last_query);
  ASSERT_EQ(&mock_user_data, g_last_user_data);
  ASSERT(g_last_duration >= 0.0);

  /* Params logging */
  res = c_orm_execute_params(db, "INSERT INTO X VALUES (?)", NULL, 0);
  ASSERT_EQ(0, res);
  ASSERT_EQ(2, g_log_call_count);
  ASSERT_STR_EQ("INSERT INTO X VALUES (?)", g_last_query);
  ASSERT(g_last_duration >= 0.0);

  /* Disable logging */
  res = c_orm_set_logger(db, NULL, NULL);
  ASSERT_EQ(0, res);
  res = c_orm_execute(db, "SELECT 2");
  ASSERT_EQ(0, res);
  ASSERT_EQ(2, g_log_call_count); /* count shouldn't increase */

  c_orm_disconnect(db);
  PASS();
}

TEST test_c_orm_pool(void) {
  c_orm_pool_t *pool = NULL;
  c_orm_db_t *db1 = NULL;
  c_orm_db_t *db2 = NULL;
  c_orm_db_t *db3 = NULL;
  c_orm_db_t *fake_db = NULL;
  int res;

  c_orm_dialect_t dialect = get_active_dialect();
  if (dialect == C_ORM_DIALECT_UNKNOWN)
    SKIP();

  /* Bad args */
  res = c_orm_pool_create(NULL, dialect, "file.db", 2);
  ASSERT_EQ(-1, res);
  res = c_orm_pool_create(&pool, dialect, "file.db", 0);
  ASSERT_EQ(-1, res);

  /* Test massive allocation failure for coverage */
  res = c_orm_pool_create(&pool, dialect, "file.db", (size_t)-1);
  ASSERT_EQ(-1, res);

  res = c_orm_pool_create(&pool, dialect, "file.db", 2);
  ASSERT_EQ(0, res);
  /* Create valid pool of size 2 */
  res = c_orm_pool_create(&pool, dialect, "file.db", 2);
  ASSERT_EQ(0, res);
  ASSERT_NEQ(NULL, pool);

  /* Acquire 1 */
  res = c_orm_pool_acquire(pool, &db1);
  ASSERT_EQ(0, res);
  ASSERT_NEQ(NULL, db1);

  /* Acquire 2 */
  res = c_orm_pool_acquire(pool, &db2);
  ASSERT_EQ(0, res);
  ASSERT_NEQ(NULL, db2);

  /* Acquire 3 (exhausted) */
  res = c_orm_pool_acquire(pool, &db3);
  ASSERT_EQ(-2, res);
  ASSERT_EQ(NULL, db3);

  /* Release 1 */
  res = c_orm_pool_release(pool, db1);
  ASSERT_EQ(0, res);

  /* Acquire 3 should now succeed */
  res = c_orm_pool_acquire(pool, &db3);
  ASSERT_EQ(0, res);
  ASSERT_NEQ(NULL, db3);
  /* Should be the same pointer as db1 was reused */
  ASSERT_EQ(db1, db3);

  /* Release bad args */
  res = c_orm_pool_release(NULL, db3);
  ASSERT_EQ(-1, res);
  res = c_orm_pool_release(pool, NULL);
  ASSERT_EQ(-1, res);

  /* Release an unknown connection */
  c_orm_connect(&fake_db, dialect, "other.db");
  res = c_orm_pool_release(pool, fake_db);
  ASSERT_EQ(-2, res);
  c_orm_disconnect(fake_db);

  /* Destroy */
  res = c_orm_pool_destroy(pool);
  ASSERT_EQ(0, res);

  res = c_orm_pool_destroy(NULL);
  ASSERT_EQ(-1, res);

  PASS();
}

TEST test_c_orm_fluent_query(void) {
  c_orm_db_t *db = NULL;
  c_orm_query_t *q = NULL;
  int res;
  char *sql = NULL;

  c_orm_dialect_t dialect = get_active_dialect();
  if (dialect == C_ORM_DIALECT_UNKNOWN)
    SKIP();

  res = c_orm_connect(&db, dialect, "file.db");
  ASSERT_EQ(0, res);

  /* Invalid create */
  res = c_orm_query_create(NULL, db, "users");
  ASSERT_EQ(-1, res);
  res = c_orm_query_create(&q, NULL, "users");
  ASSERT_EQ(-1, res);
  res = c_orm_query_create(&q, db, NULL);
  ASSERT_EQ(-1, res);

  /* Valid create */
  res = c_orm_query_create(&q, db, "users");
  ASSERT_EQ(0, res);
  ASSERT_NEQ(NULL, q);

  /* Basic build test (select * from users) */
  res = c_orm_query_build(q, &sql);
  ASSERT_EQ(0, res);
  ASSERT_STR_EQ("SELECT * FROM users", sql);
  free(sql);
  sql = NULL;

  /* Add clauses */
  res = c_orm_query_select(q, "id, name");
  ASSERT_EQ(0, res);

  res = c_orm_query_where(q, "age > 18");
  ASSERT_EQ(0, res);

  res = c_orm_query_order_by(q, "name ASC");
  ASSERT_EQ(0, res);

  res = c_orm_query_limit(q, 10);
  ASSERT_EQ(0, res);

  /* Build full query */
  res = c_orm_query_build(q, &sql);
  ASSERT_EQ(0, res);
  ASSERT_STR_EQ(
      "SELECT id, name FROM users WHERE age > 18 ORDER BY name ASC LIMIT 10",
      sql);
  free(sql);
  sql = NULL;

  /* Re-adding clauses replaces them */
  res = c_orm_query_select(q, "count(*)");
  ASSERT_EQ(0, res);
  res = c_orm_query_where(q, "id = 1");
  ASSERT_EQ(0, res);
  res = c_orm_query_order_by(q, "id DESC");
  ASSERT_EQ(0, res);

  res = c_orm_query_build(q, &sql);
  ASSERT_EQ(0, res);
  ASSERT_STR_EQ(
      "SELECT count(*) FROM users WHERE id = 1 ORDER BY id DESC LIMIT 10", sql);
  free(sql);
  sql = NULL;

  /* Execute the query */
  res = c_orm_query_execute(q);
  ASSERT_EQ(0, res);

  /* Null arg checks for clauses */
  ASSERT_EQ(-1, c_orm_query_select(NULL, "id"));
  ASSERT_EQ(-1, c_orm_query_select(q, NULL));
  ASSERT_EQ(-1, c_orm_query_where(NULL, "id=1"));
  ASSERT_EQ(-1, c_orm_query_where(q, NULL));
  ASSERT_EQ(-1, c_orm_query_order_by(NULL, "id"));
  ASSERT_EQ(-1, c_orm_query_order_by(q, NULL));
  ASSERT_EQ(-1, c_orm_query_limit(NULL, 10));
  ASSERT_EQ(-1, c_orm_query_build(NULL, &sql));
  ASSERT_EQ(-1, c_orm_query_build(q, NULL));
  ASSERT_EQ(-1, c_orm_query_execute(NULL));
  ASSERT_EQ(-1, c_orm_query_destroy(NULL));

  /* Destroy */
  res = c_orm_query_destroy(q);
  ASSERT_EQ(0, res);

  c_orm_disconnect(db);
  PASS();
}

static int g_async_cb_count = 0;
static void test_async_cb(int status, void *user_data) {
  int *marker = (int *)user_data;
  g_async_cb_count++;
  if (marker) {
    *marker = status;
  }
}

TEST test_c_orm_async(void) {
  c_orm_db_t *db = NULL;
  c_orm_dialect_t dialect = get_active_dialect();
  int res;
  int job_marker_1 = -99;
  int job_marker_2 = -99;
  if (dialect == C_ORM_DIALECT_UNKNOWN)
    SKIP();

  res = c_orm_connect(&db, dialect, "file.db");
  ASSERT_EQ(0, res);

  g_async_cb_count = 0;

  /* Queue jobs */
  res = c_orm_execute_async(db, "SELECT 1", test_async_cb, &job_marker_1);
  ASSERT_EQ(0, res);

  res = c_orm_execute_async(db, "SELECT 2", test_async_cb, &job_marker_2);
  ASSERT_EQ(0, res);

  /* Ensure callbacks not fired yet */
  ASSERT_EQ(0, g_async_cb_count);
  ASSERT_EQ(-99, job_marker_1);
  ASSERT_EQ(-99, job_marker_2);

  /* Poll first job */
  res = c_orm_poll_async(db);
  ASSERT_EQ(1, res); /* 1 job processed */
  ASSERT_EQ(1, g_async_cb_count);
  ASSERT_EQ(0, job_marker_1); /* Success status from c_orm_execute */
  ASSERT_EQ(-99, job_marker_2);

  /* Poll second job */
  res = c_orm_poll_async(db);
  ASSERT_EQ(1, res); /* 1 job processed */
  ASSERT_EQ(2, g_async_cb_count);
  ASSERT_EQ(0, job_marker_2); /* Success status from c_orm_execute */

  /* Poll empty queue */
  res = c_orm_poll_async(db);
  ASSERT_EQ(0, res); /* 0 jobs processed */
  ASSERT_EQ(2, g_async_cb_count);

  /* Null arg tests */
  ASSERT_EQ(-1, c_orm_execute_async(NULL, "SELECT 1", test_async_cb, NULL));
  ASSERT_EQ(-1, c_orm_execute_async(db, NULL, test_async_cb, NULL));
  ASSERT_EQ(-1, c_orm_execute_async(db, "SELECT 1", NULL, NULL));
  ASSERT_EQ(-1, c_orm_poll_async(NULL));

  /* Test disconnect cleans up pending jobs cleanly */
  res = c_orm_execute_async(db, "SELECT 3", test_async_cb, NULL);
  ASSERT_EQ(0, res);
  c_orm_disconnect(db); /* Will leak if queue isn't cleaned in disconnect */

  PASS();
}

SUITE(c_orm_suite) {
  RUN_TEST(test_c_orm_connect_and_disconnect);
  RUN_TEST(test_c_orm_postgres_connect);
  RUN_TEST(test_c_orm_mysql_connect);
  RUN_TEST(test_c_orm_unsupported_dialect);
  RUN_TEST(test_c_orm_migrate);
  RUN_TEST(test_c_orm_execute);
  RUN_TEST(test_c_orm_execute_params_valid);
  RUN_TEST(test_c_orm_execute_params_invalid);
  RUN_TEST(test_c_orm_transactions);
  RUN_TEST(test_c_orm_logging);
  RUN_TEST(test_c_orm_pool);
  RUN_TEST(test_c_orm_fluent_query);
  RUN_TEST(test_c_orm_async);
}