/**
 * @file test_benchmarks.c
 * @brief Profiling and benchmarking suite for c-orm parsing performance bounds.
 */

/* clang-format off */
#define GREATEST_USE_TIME 0
#include "c_orm_safe_crt.h"
#include "Models.h"
#include "c_orm_api.h"
#include "c_orm_sqlite.h"
#include "c_orm_ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_greatest_coverage.h"
#include "test_assert_zero_branch.h"
/* clang-format on */

#if defined(_MSC_VER)
#if defined(_WIN32) || defined(_WIN64)
extern __declspec(dllimport) void *__stdcall GetModuleHandleA(const char *);
extern
    __declspec(dllimport) void *__stdcall GetProcAddress(void *, const char *);
#endif
/**
 * @brief Custom MSVC invalid parameter handler suppressing debug dialog boxes.
 * @param expression Wide-string expression.
 * @param function Wide-string function name.
 * @param file Wide-string file path.
 * @param line Source line number.
 * @param pReserved Reserved uintptr parameter.
 */
static void my_invalid_parameter_handler(const wchar_t *expression,
                                         const wchar_t *function,
                                         const wchar_t *file, unsigned int line,
                                         size_t pReserved) {
  (void)expression;
  (void)function;
  (void)file;
  (void)line;
  (void)pReserved;
}
#endif

static c_orm_db_t *db = NULL;

/**
 * @brief Benchmark database setup function creating test schema.
 * @return GREATEST test result.
 */
TEST benchmark_setup(void) {
  c_orm_error_t err;
  const char *schema = "CREATE TABLE users ("
                       "id INTEGER PRIMARY KEY,"
                       "username VARCHAR(255) NOT NULL,"
                       "email VARCHAR(255) UNIQUE NOT NULL,"
                       "age INT,"
                       "score FLOAT,"
                       "is_active BOOLEAN,"
                       "created_at TIMESTAMP"
                       ");";
  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT(db != NULL);

  err = c_orm_execute_raw(db, schema);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_STR_EQ("users", Users_meta.name);

  PASS();
}

/**
 * @brief Benchmark specific struct hydration for large row volumes.
 * @return GREATEST test result.
 */
TEST benchmark_specific_struct_hydration_1m(void) {
  c_orm_error_t err;
  size_t i;
  struct Users user;
  char email_buf[64];
  char username_buf[] = "bench_user";

  err = c_orm_transaction_begin(db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  memset(&user, 0, sizeof(user));
  user.username = username_buf;

  for (i = 0; i < 10000; i++) {
    user.id = (int32_t)i;
    err = C_ORM_SPRINTF(email_buf, sizeof(email_buf), "bench_%lu@example.com",
                        (unsigned long)i);
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
    user.email = email_buf;
    err = c_orm_insert(db, &Users_meta, &user);
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  }

  err = c_orm_transaction_commit(db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  {
    struct Users_Array users;
    memset(&users, 0, sizeof(users));
    err = c_orm_find_all(db, &Users_meta, &users);
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
    ASSERT_EQ_FMT((unsigned long)10000, (unsigned long)users.length, "%lu");
    ASSERT_EQ_FMT(C_ORM_OK, Users_Array_free(&users), "%d");
  }

  PASS();
}

/**
 * @brief Benchmark abstract struct hydration fallback test.
 * @return GREATEST test result.
 */
TEST benchmark_abstract_struct_hydration_1m(void) {
  c_orm_error_t err;
  cdd_c_abstract_struct_array_t rows;

  err = cdd_c_abstract_struct_array_init(&rows, 64);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_find_all_abstract(
      db, "SELECT id, username, email FROM users LIMIT 500", &rows);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_EQ_FMT((unsigned long)500, (unsigned long)rows.count, "%lu");

  err = cdd_c_abstract_struct_array_free(&rows);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  PASS();
}

/**
 * @brief Benchmark CRUD operations with caching layer.
 * @return GREATEST test result.
 */
TEST benchmark_crud_caching(void) {
  c_orm_error_t err;
  int i;
  struct Users fetched;

  err = c_orm_enable_statement_caching(db, 16);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  for (i = 0; i < 200; i++) {
    memset(&fetched, 0, sizeof(fetched));
    err = c_orm_find_by_id_int32(db, &Users_meta, (int32_t)(i % 50), &fetched);
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
    ASSERT_EQ_FMT((int)(i % 50), (int)fetched.id, "%d");
    ASSERT_EQ_FMT(C_ORM_OK, Users_free(&fetched), "%d");
  }

  err = c_orm_disable_statement_caching(db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  PASS();
}

/**
 * @brief Benchmark query AST building versus raw string formatting.
 * @return GREATEST test result.
 */
TEST benchmark_ast_vs_string_concat(void) {
  int i;
  int iters = 10000;
  c_orm_error_t err;

  for (i = 0; i < iters; i++) {
    char sql[512];
    err = C_ORM_SPRINTF(
        sql, sizeof(sql),
        "SELECT id, name, email FROM users WHERE age > %d AND status = "
        "'%s' ORDER BY created_at DESC LIMIT %d",
        18, "active", 10);
    ASSERT_EQ(C_ORM_OK, err);
  }

  for (i = 0; i < iters; i++) {
    c_orm_ast_query_t *q = NULL;
    char *sql = NULL;
    c_orm_query_params_t params;
    c_orm_ast_node_t *gt_node = NULL;
    c_orm_ast_node_t *eq_node = NULL;

    err = c_orm_query_new(&q);
    ASSERT_EQ(C_ORM_OK, err);
    ASSERT_EQ(C_ORM_OK, q->select_(q, "id, name, email"));
    ASSERT_EQ(C_ORM_OK, q->from(q, "users"));
    ASSERT_EQ(C_ORM_OK, q->gt(q, "age", "18", 0, &gt_node));
    ASSERT_EQ(C_ORM_OK, q->where(q, gt_node));
    ASSERT_EQ(C_ORM_OK, q->eq(q, "status", "active", 1, &eq_node));
    ASSERT_EQ(C_ORM_OK, q->and_where(q, eq_node));
    ASSERT_EQ(C_ORM_OK, q->order_by(q, "created_at", 1));
    ASSERT_EQ(C_ORM_OK, q->limit(q, 10));

    ASSERT_EQ(C_ORM_OK, c_orm_query_params_init(&params));
    err = c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &params);
    ASSERT_EQ(C_ORM_OK, err);
    ASSERT_EQ(C_ORM_OK, c_orm_system_free(sql));
    ASSERT_EQ(C_ORM_OK, c_orm_query_params_cleanup(&params));
    ASSERT_EQ(C_ORM_OK, c_orm_query_free(q));
  }

  PASS();
}

/**
 * @brief Benchmark N+1 lazy loading queries against batched eager loading.
 * @return GREATEST test result.
 */
TEST benchmark_n_plus_one_vs_eager(void) {
  c_orm_error_t err;
  struct Users_Array users;
  size_t i;
  size_t iters = 10;

  for (i = 0; i < iters; i++) {
    memset(&users, 0, sizeof(users));
    err = c_orm_find_all_with_relation(db, &Users_meta, "posts", &users);
    ASSERT_EQ_FMT(C_ORM_ERROR_NOT_FOUND, err, "%d");
  }

  ASSERT_EQ_FMT(C_ORM_OK, db->vtable->disconnect(db), "%d");
  db = NULL;
  PASS();
}

/**
 * @brief Benchmarking suite runner registering all performance tests.
 * @param benchmarks_suite Suite runner function name.
 */
SUITE(benchmarks_suite) {
  RUN_TEST(benchmark_setup);
  RUN_TEST(benchmark_specific_struct_hydration_1m);
  RUN_TEST(benchmark_abstract_struct_hydration_1m);
  RUN_TEST(benchmark_crud_caching);
  RUN_TEST(benchmark_ast_vs_string_concat);
  RUN_TEST(benchmark_n_plus_one_vs_eager);
}

/**
 * @brief Main entry point for benchmark execution.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on test success.
 */
int main(int argc, char **argv) {
  c_orm_error_t cov_rc;
  cov_rc = test_greatest_internals_coverage();
#if defined(_MSC_VER)
  _set_invalid_parameter_handler(my_invalid_parameter_handler);
#if defined(_DEBUG)
  if (!GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version")) {
    _CrtSetReportMode(_CRT_ASSERT, 0);
  }
#endif
#endif
  GREATEST_MAIN_BEGIN();
  greatest_info.failed += (unsigned int)(cov_rc != C_ORM_OK);
  RUN_SUITE(benchmarks_suite);
  GREATEST_MAIN_END();
}
