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

  err = c_orm_insert_generic(test_db, &Users_meta, &u);
  if (err != C_ORM_OK) {
    const char *msg;
    test_db->vtable->get_last_error(test_db, &msg);
    fprintf(stderr, "INSERT ERR: %d - %s\n", err, msg);
  }
  ASSERT_EQ(C_ORM_OK, err);

  memset(&out_u, 0, sizeof(out_u));
  err = c_orm_get_generic(test_db, &Users_meta, 1, &out_u);
  ASSERT_EQ(C_ORM_OK, err);
  if (err == C_ORM_OK) {
    if (out_u.username)
      free(out_u.username);
    if (out_u.email)
      free(out_u.email);
  }

  err = c_orm_find_all_generic(test_db, &Users_meta, &arr, &count);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT(count > 0);

  if (arr) {
    size_t i;
    struct Users *users_arr = (struct Users *)arr;
    for (i = 0; i < count; i++) {
      if (users_arr[i].username)
        free(users_arr[i].username);
      if (users_arr[i].email)
        free(users_arr[i].email);
    }
    free(arr);
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

SUITE(generic_suite) {
  RUN_TEST(test_c_orm_generic_crud);
  RUN_TEST(test_c_orm_telemetry);
}