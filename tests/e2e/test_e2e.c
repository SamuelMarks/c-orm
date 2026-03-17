/* clang-format off */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c_orm_api.h"
#include "c_orm_sqlite.h"
#include "c_orm_postgres.h"
#include "c_orm_mysql.h"
#include "greatest.h"

/* The generated models */
#include "Models.h"
/* clang-format on */

static c_orm_db_t *db = NULL;

TEST test_e2e_connect(void) {
  c_orm_error_t err;
  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT(db != NULL);
  PASS();
}

TEST test_e2e_generate_schema(void) {
  c_orm_error_t err;
  const char *schema = "CREATE TABLE users ("
                       "id INTEGER PRIMARY KEY,"
                       "username VARCHAR(255) NOT NULL,"
                       "email VARCHAR(255) UNIQUE NOT NULL,"
                       "age INTEGER,"
                       "score FLOAT,"
                       "is_active BOOLEAN,"
                       "created_at TIMESTAMP"
                       ");";

  err = c_orm_execute_raw(db, schema);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_execute_raw(db, "CREATE TABLE posts ("
                              "id INTEGER PRIMARY KEY,"
                              "user_id INTEGER NOT NULL,"
                              "title VARCHAR(255) NOT NULL,"
                              "content TEXT,"
                              "views BIGINT,"
                              "published_date DATE,"
                              "FOREIGN KEY (user_id) REFERENCES users(id)"
                              ");");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  PASS();
}

TEST test_e2e_insert_user(void) {
  struct Users u;
  c_orm_error_t err;
  int32_t age = 30;
  float score = 9.5f;
  bool is_active = true;
  char *created_at = "2026-03-14 12:00:00";

  memset(&u, 0, sizeof(u));
  u.id = 1;
  u.username = "smarks";
  u.email = "samuel@example.com";
  u.age = &age;
  u.score = &score;
  u.is_active = &is_active;
  u.created_at = created_at;

  err = c_orm_insert(db, &Users_meta, &u);
  if (err != C_ORM_OK) {
    const char *msg = NULL;
    c_orm_get_last_error_message(db, &msg);
    printf("Error: %s\n", msg);
  }
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  PASS();
}

TEST test_e2e_fetch_user(void) {
  struct Users u;
  c_orm_error_t err;

  memset(&u, 0, sizeof(u));
  err = c_orm_find_by_id_int32(db, &Users_meta, 1, &u);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  ASSERT_STR_EQ("smarks", u.username);
  ASSERT_STR_EQ("samuel@example.com", u.email);
  ASSERT(u.age != NULL);
  ASSERT_EQ(30, *u.age);
  ASSERT(u.score != NULL);
  /* Double precision check */
  printf("score: %f\n", *u.score);
  ASSERT(*u.score > 9.4 && *u.score < 9.6);
  ASSERT(u.is_active != NULL);
  ASSERT_EQ(1, *u.is_active);

  Users_free(&u);
  PASS();
}

TEST test_e2e_fetch_all(void) {
  struct Users_Array arr;
  c_orm_error_t err;

  memset(&arr, 0, sizeof(arr));
  err = c_orm_find_all(db, &Users_meta, &arr);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  ASSERT_EQ(1, arr.length);
  ASSERT_STR_EQ("smarks", arr.data[0].username);

  Users_Array_free(&arr);
  PASS();
}

TEST test_e2e_hydrate_all_direct(void) {
  struct Users_Array arr;
  c_orm_query_t *query;
  c_orm_error_t err;

  err = db->vtable->prepare(db, "SELECT * FROM users", &query);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  memset(&arr, 0, sizeof(arr));
  err = c_orm_hydrate_all(db, query, &Users_meta, &arr);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  ASSERT_EQ(1, arr.length);
  ASSERT_STR_EQ("smarks", arr.data[0].username);

  Users_Array_free(&arr);
  db->vtable->finalize(query);

  PASS();
}

TEST test_e2e_transactions(void) {
  c_orm_error_t err;
  err = c_orm_transaction_begin(db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_transaction_commit(db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_transaction_begin(db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_transaction_rollback(db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  PASS();
}

#include "c_orm_oauth2.h"
#include "c_orm_query_builder.h"

TEST test_query_builder_extensions(void) {
  c_orm_select_builder_t *b;
  char *sql = NULL;

  ASSERT_EQ(0, c_orm_select_builder_init(&Users_meta, &b));

  ASSERT_EQ(0, c_orm_select_where_gt_current_timestamp(b, "expires_at"));
  ASSERT_EQ(0, c_orm_select_where_lt_current_timestamp(b, "created_at"));
  ASSERT_EQ(0, c_orm_select_limit(b, 10));
  ASSERT_EQ(0, c_orm_select_offset(b, 5));

  ASSERT_EQ(0, c_orm_select_builder_compile(b, &sql));

  ASSERT_STR_EQ("SELECT * FROM users WHERE expires_at > CURRENT_TIMESTAMP AND "
                "created_at < CURRENT_TIMESTAMP LIMIT 10 OFFSET 5",
                sql);

  free(sql);
  c_orm_select_builder_free(b);
  PASS();
}

TEST test_postgres_stub(void) {
  const c_orm_driver_vtable_t *vtable;
  c_orm_db_t *pdb;
  int res = c_orm_postgres_get_vtable(&vtable);
#ifdef C_ORM_ENABLE_POSTGRESQL
  ASSERT_EQ(0, res);
  ASSERT_NEQ(NULL, vtable);

  /* Coverage for null arg check if there is one */
  res = c_orm_postgres_get_vtable(NULL);
  ASSERT_EQ(1, res);

  /* It should fail to connect with an invalid URL */
  ASSERT_EQ(C_ORM_ERROR_CONNECTION,
            c_orm_postgres_connect("invalid_url", &pdb));
#else
  ASSERT_EQ(1, res);
  ASSERT_EQ(NULL, vtable);

  /* Coverage for null arg check if there is one */
  res = c_orm_postgres_get_vtable(NULL);
  ASSERT_EQ(1, res);

  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, c_orm_postgres_connect("...", &pdb));
#endif
  PASS();
}

TEST test_mysql_stub(void) {
  const c_orm_driver_vtable_t *vtable;
  c_orm_db_t *mdb;
  int res = c_orm_mysql_get_vtable(&vtable);
#ifdef C_ORM_ENABLE_MYSQL
  ASSERT_EQ(0, res);
  ASSERT_NEQ(NULL, vtable);

  /* Coverage for null arg check if there is one */
  res = c_orm_mysql_get_vtable(NULL);
  ASSERT_EQ(1, res);

  /* Depending on system, connecting to 127.0.0.1 might succeed or fail. It
   * might return an error. */
  c_orm_mysql_connect(
      "invalid_url",
      &mdb); /* Call it for coverage, don't strictly assert connection state */
#else
  ASSERT_EQ(1, res);
  ASSERT_EQ(NULL, vtable);

  /* Coverage for null arg check if there is one */
  res = c_orm_mysql_get_vtable(NULL);
  ASSERT_EQ(1, res);

  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, c_orm_mysql_connect("...", &mdb));
#endif
  PASS();
}

TEST test_e2e_string_pk_and_oauth2(void) {
  struct Oauth2_tokens token;
  struct Oauth2_tokens fetched;
  c_orm_error_t err;
  int32_t expires_in = 3600;
  int64_t created_at = 1600000000;

  err = c_orm_execute_raw(db, "CREATE TABLE oauth2_tokens ("
                              "access_token VARCHAR(255) PRIMARY KEY,"
                              "refresh_token VARCHAR(255),"
                              "token_type VARCHAR(50),"
                              "expires_in INT,"
                              "created_at BIGINT"
                              ");");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  memset(&token, 0, sizeof(token));
  token.access_token = "atk_12345";
  token.refresh_token = "rtk_09876";
  token.token_type = "Bearer";
  token.expires_in = &expires_in;
  token.created_at = &created_at;

  err = c_orm_insert(db, &Oauth2_tokens_meta, &token);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  memset(&fetched, 0, sizeof(fetched));
  err = c_orm_find_by_id_string(db, &Oauth2_tokens_meta, "atk_12345", &fetched);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_STR_EQ("atk_12345", fetched.access_token);
  ASSERT_STR_EQ("rtk_09876", fetched.refresh_token);
  ASSERT(fetched.expires_in != NULL);
  ASSERT_EQ(3600, *fetched.expires_in);

  Oauth2_tokens_free(&fetched);

  err = c_orm_delete_by_id_string(db, &Oauth2_tokens_meta, "atk_12345");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  memset(&fetched, 0, sizeof(fetched));
  err = c_orm_find_by_id_string(db, &Oauth2_tokens_meta, "atk_12345", &fetched);
  ASSERT_EQ_FMT(C_ORM_ERROR_NOT_FOUND, err, "%d");

  PASS();
}

TEST test_e2e_find_one_by_string(void) {
  struct Users u;
  c_orm_error_t err;

  memset(&u, 0, sizeof(u));
  err = c_orm_find_one_by_string(db, &Users_meta, "username", "smarks", &u);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  ASSERT_STR_EQ("smarks", u.username);
  Users_free(&u);

  memset(&u, 0, sizeof(u));
  err = c_orm_find_one_by_string(db, &Users_meta, "email",
                                 "notfound@example.com", &u);
  ASSERT_EQ_FMT(C_ORM_ERROR_NOT_FOUND, err, "%d");

  PASS();
}

TEST test_e2e_oauth2_helpers(void) {
  c_orm_oauth2_token_t tok;
  int is_valid;
  c_orm_error_t err;
  char *encrypted = NULL;
  char *decrypted = NULL;
  int64_t t1, t2;

  memset(&tok, 0, sizeof(tok));
  tok.access_token = "abc";
  tok.expires_in = 3600;
  tok.created_at = 1000000000;

  err = c_orm_oauth2_is_token_valid(&tok, 1000001000, &is_valid);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(1, is_valid);

  err = c_orm_oauth2_is_token_valid(&tok, 1000004000, &is_valid);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(0, is_valid);

  err = c_orm_oauth2_encrypt_token("plain", &encrypted);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT(encrypted != NULL);

  err = c_orm_oauth2_decrypt_token(encrypted, &decrypted);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT(decrypted != NULL);
  ASSERT_STR_EQ("plain", decrypted);

  free(encrypted);
  free(decrypted);

  err = c_orm_oauth2_get_current_timestamp(&t1);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT(t1 > 0);

  err = c_orm_oauth2_calculate_expiration(t1, 3600, &t2);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(t1 + 3600, t2);

  err = c_orm_store_token_secure(&tok);
  ASSERT_EQ(C_ORM_OK, err);

  PASS();
}

SUITE(e2e_suite) {
  RUN_TEST(test_e2e_connect);
  RUN_TEST(test_e2e_generate_schema);
  RUN_TEST(test_e2e_insert_user);
  RUN_TEST(test_e2e_fetch_user);
  RUN_TEST(test_e2e_fetch_all);
  RUN_TEST(test_e2e_hydrate_all_direct);
  RUN_TEST(test_e2e_transactions);
  RUN_TEST(test_query_builder_extensions);
  RUN_TEST(test_postgres_stub);
  RUN_TEST(test_mysql_stub);
  RUN_TEST(test_e2e_string_pk_and_oauth2);
  RUN_TEST(test_e2e_find_one_by_string);
  RUN_TEST(test_e2e_oauth2_helpers);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
  GREATEST_MAIN_BEGIN();
  RUN_SUITE(e2e_suite);
  GREATEST_MAIN_END();
}
