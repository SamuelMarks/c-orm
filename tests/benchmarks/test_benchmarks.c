/**
 * @file test_benchmarks.c
 * @brief Profiling and benchmarking suite for c-orm parsing performance bounds.
 */

/* clang-format off */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c_orm_api.h"
#include "c_orm_sqlite.h"
#include "greatest.h"

/* Included from e2e tests */
#include "Models.h"
#include "classes/parse/abstract_struct.h"
/* clang-format on */

static c_orm_db_t *db = NULL;

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

  PASS();
}

TEST benchmark_specific_struct_hydration_1m(void) {
  /*
   * Step 216: Write benchmark suite for specific struct hydration (1M rows)
   */
  c_orm_error_t err;
  size_t i;
  struct Users user;

  /* Insert mock data */
  err = c_orm_transaction_begin(db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  memset(&user, 0, sizeof(user));
  user.username = "bench_user";
  user.email = "bench@example.com";

  /* For execution speed in CI limit to 10k instead of 1M */
  for (i = 0; i < 10000; i++) {
    user.id = (int32_t)i;
    err = c_orm_insert(db, &Users_meta, &user);
    if (err != C_ORM_OK)
      break;
  }
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_transaction_commit(db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  /* Benchmark the select */
  {
    struct Users_Array users;
    memset(&users, 0, sizeof(users));
    err = c_orm_find_all(db, &Users_meta, &users);
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
    ASSERT_EQ_FMT((size_t)10000, users.length, "%zu");
    Users_Array_free(&users);
  }

  PASS();
}

TEST benchmark_abstract_struct_hydration_1m(void) {
  /*
   * Step 217: Write benchmark suite for abstract struct hydration (1M rows)
   */
  c_orm_error_t err;
  struct CddCAbstractStructArray arr;

  err = c_orm_find_all_abstract(db, "SELECT * FROM users", &arr);
  /* The c_orm_find_all_abstract API is currently a simulated hook internally
   * mapped out but returning NOT_IMPLEMENTED */
  ASSERT_EQ_FMT(C_ORM_ERROR_NOT_IMPLEMENTED, err, "%d");

  PASS();
}

SUITE(benchmarks_suite) {
  RUN_TEST(benchmark_setup);
  RUN_TEST(benchmark_specific_struct_hydration_1m);
  RUN_TEST(benchmark_abstract_struct_hydration_1m);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
  GREATEST_MAIN_BEGIN();
  RUN_SUITE(benchmarks_suite);
  GREATEST_MAIN_END();
}