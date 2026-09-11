#if defined(__clang__) || defined(__GNUC__)
#endif
/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_db.h"
#include "c_orm_oauth2.h"
#include "c_orm_sqlite.h"
#include "c_orm_postgres.h"
#include "c_orm_mysql.h"
#include "cfs/cfs.h"
#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32) && !defined(_MSC_VER)
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#else
#include <direct.h>
#endif
/* clang-format on */

static int oom_countdown = -1;
static int oom_active = 0;

static void *m_mock_malloc(size_t size) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      return NULL;
    }
    oom_countdown--;
  }
  return malloc(size);
}
static void m_mock_free(void *ptr) { free(ptr); }

TEST test_oauth2_json_edge_cases(void) {
  c_orm_oauth2_token_t token;
  memset(&token, 0, sizeof(token));

  /* NULL checks */
  c_orm_oauth2_token_parse_json(NULL, &token);
  c_orm_oauth2_token_parse_json("{}", NULL);

  /* JSON edge cases */
  c_orm_oauth2_token_parse_json("{\"access_token\":\"123\\\"456\"}", &token);
  if (token.access_token)
    free(token.access_token);
  token.access_token = NULL;

  c_orm_oauth2_token_parse_json("{\"refresh_token\":\"abc\"}", &token);
  if (token.refresh_token)
    free(token.refresh_token);
  token.refresh_token = NULL;

  c_orm_oauth2_token_parse_json("{\"token_type\":\"bearer\"}", &token);
  if (token.token_type)
    free(token.token_type);
  token.token_type = NULL;

  c_orm_oauth2_token_parse_json("{\"expires_in\":3600}", &token);

  /* Some bad formatting */
  c_orm_oauth2_token_parse_json("{\"access_token\": ", &token);
  c_orm_oauth2_token_parse_json("{\"access_token\": 123", &token);
  c_orm_oauth2_token_parse_json("{\"access_token\": \"123", &token);
  c_orm_oauth2_token_parse_json("{\"expires_in\": \"3600\"}", &token);
  c_orm_oauth2_token_parse_json("{\"expires_in\": abc}", &token);
  c_orm_oauth2_token_parse_json("{\"unknown_key\": \"val\"}", &token);

  PASS();
}

TEST test_oauth2_flat_json(void) {
  c_orm_oauth2_token_t t;
  c_orm_error_t err;

  /* Use mock json payload */
  err = c_orm_oauth2_token_parse_json(
      "{\"access_token\":\"abc\", \"refresh_token\": \"def\", \"token_type\": "
      "\"Bearer\", \"expires_in\": 3600, \"unknown\": 123}",
      &t);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_STR_EQ("abc", t.access_token);
  ASSERT_STR_EQ("def", t.refresh_token);
  ASSERT_STR_EQ("Bearer", t.token_type);
  ASSERT_EQ(3600, t.expires_in);

  if (t.access_token)
    C_ORM_FREE(t.access_token);
  if (t.refresh_token)
    C_ORM_FREE(t.refresh_token);
  if (t.token_type)
    C_ORM_FREE(t.token_type);

  /* Error paths / edge cases in JSON */
  c_orm_oauth2_token_parse_json("{\"escaped\\\"\": \"val\\\"\"}", &t);
  c_orm_oauth2_token_parse_json("{\"esc\\\\\": \"val\\\\\"}", &t);
  c_orm_oauth2_token_parse_json("{\"expires_in\": -123}", &t);
  c_orm_oauth2_token_parse_json("{\",\"}", &t);
  c_orm_oauth2_token_parse_json("{ \\ }", &t);   /* backslash outside string */
  c_orm_oauth2_token_parse_json("{\"key\\", &t); /* trailing backslash in key */
  c_orm_oauth2_token_parse_json("{\"key\": \"val\\",
                                &t); /* trailing backslash in value */

  PASS();
}

TEST test_oauth2_crypto(void) {
  char *out = NULL;
  int i;
  c_orm_oauth2_token_t t;
  memset(&t, 0, sizeof(t));
  t.access_token = "abc";
  t.refresh_token = "def";

  for (i = 0; i < 5; i++) {
    oom_active = 1;
    oom_countdown = i;
    c_orm_oauth2_encrypt_token("plain", &out);
    oom_active = 0;
    if (out) {
      C_ORM_FREE(out);
      out = NULL;
    }
  }

  for (i = 0; i < 5; i++) {
    oom_active = 1;
    oom_countdown = i;
    c_orm_oauth2_decrypt_token("cipher", &out);
    oom_active = 0;
    if (out) {
      C_ORM_FREE(out);
      out = NULL;
    }
  }

  for (i = 0; i < 5; i++) {
    oom_active = 1;
    oom_countdown = i;
    c_orm_store_token_secure(&t);
    oom_active = 0;
  }

  c_orm_store_token_secure(NULL);

  /* hex parsing 'A'-'F' and '0'-'9' */
  c_orm_oauth2_decrypt_token("ABCDEF", &out);
  if (out) {
    C_ORM_FREE(out);
    out = NULL;
  }
  c_orm_oauth2_decrypt_token("0123456789", &out);
  if (out) {
    C_ORM_FREE(out);
    out = NULL;
  }
  c_orm_oauth2_decrypt_token("!@#$zZgG  ", &out);
  if (out) {
    C_ORM_FREE(out);
    out = NULL;
  }

  c_orm_oauth2_get_current_timestamp(NULL);
  {
    int64_t ts;
    c_orm_oauth2_get_current_timestamp(&ts);
    c_orm_oauth2_calculate_expiration(ts, 3600, &ts);
  }

  /* file open error */
  {
    cfs_path p;
    cfs_size_t rm_out = 0;
    cfs_errc cfs_rc;
    remove("c_orm_token.dat");
    cfs_rc = cfs_path_init_str(&p, "c_orm_token.dat");
    (void)cfs_rc;
    cfs_rc = cfs_remove_all(&p, &rm_out, NULL);
    (void)cfs_rc;
    cfs_rc = cfs_create_directory(&p, NULL);
    (void)cfs_rc;
    c_orm_store_token_secure(&t);
    cfs_rc = cfs_remove_all(&p, &rm_out, NULL);
    (void)cfs_rc;
    cfs_path_destroy(&p);
    remove("c_orm_token.dat");
  }

  PASS();
}

static int fail_sql = 0;
static c_orm_error_t (*orig_prep)(c_orm_db_t *, const char *, c_orm_query_t **);
static c_orm_error_t my_oauth2_prep(c_orm_db_t *db_v, const char *sql,
                                    c_orm_query_t **out_query) {
  if (fail_sql == 1 && strstr(sql, "CREATE TABLE IF NOT EXISTS users"))
    return C_ORM_ERROR_SQL;
  if (fail_sql == 2 && strstr(sql, "CREATE TABLE IF NOT EXISTS tokens"))
    return C_ORM_ERROR_SQL;
  if (fail_sql == 3 && strstr(sql, "CREATE TABLE IF NOT EXISTS clients"))
    return C_ORM_ERROR_SQL;
  if (fail_sql == 4 && strstr(sql, "CREATE TABLE IF NOT EXISTS auth_codes"))
    return C_ORM_ERROR_SQL;
  if (fail_sql == 5 && strstr(sql, "SELECT"))
    return C_ORM_ERROR_SQL;
  if (fail_sql == 6 && strstr(sql, "DELETE"))
    return C_ORM_ERROR_SQL;
  return orig_prep(db_v, sql, out_query);
}

static c_orm_error_t (*orig_step)(c_orm_query_t *, int *);
static c_orm_error_t my_oauth2_step(c_orm_query_t *query, int *out_has_row) {
  if (fail_sql == 7) /* INSERT / DELETE returning NOT_FOUND */
    return C_ORM_ERROR_NOT_FOUND;
  if (fail_sql == 8)
    return C_ORM_ERROR_NOT_FOUND;
  return orig_step(query, out_has_row);
}

TEST test_oauth2_init(void) {
  c_orm_db_t *db = NULL;
  c_orm_db_t db_pg, db_my;
  c_orm_driver_vtable_t mock_vt;
  const c_orm_driver_vtable_t *pg_vt;
  const c_orm_driver_vtable_t *my_vt;
  c_orm_error_t err;
  c_orm_driver_vtable_t my_pg_vt, my_my_vt;
  (void)my_pg_vt;
  (void)my_my_vt;

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  mock_vt = *(c_orm_driver_vtable_t *)db->vtable;
  orig_prep = mock_vt.prepare;
  mock_vt.prepare = my_oauth2_prep;
  orig_step = mock_vt.step;
  mock_vt.step = my_oauth2_step;

  err = c_orm_oauth2_create_tables(db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  c_orm_postgres_get_vtable(&pg_vt);
  c_orm_mysql_get_vtable(&my_vt);

  /* Mutate the static stubs in .bss to use our mock prep so they don't crash
   * when c_orm_execute_raw calls them! */
#ifdef C_ORM_HAVE_POSTGRES
  if (pg_vt) {
    c_orm_driver_vtable_t *m = (c_orm_driver_vtable_t *)pg_vt;
    *m = mock_vt;
    m->prepare = my_oauth2_prep;
  }
#endif
#ifdef C_ORM_HAVE_MYSQL
  if (my_vt) {
    c_orm_driver_vtable_t *m = (c_orm_driver_vtable_t *)my_vt;
    *m = mock_vt;
    m->prepare = my_oauth2_prep;
  }
#endif

  /* Also mutate sqlite vt for coverage */

  db_pg = *db;
  db_pg.vtable = pg_vt;
  c_orm_oauth2_create_tables(&db_pg);

  db_my = *db;
  db_my.vtable = my_vt;
  c_orm_oauth2_create_tables(&db_my);

  db_pg.vtable = NULL;
  c_orm_oauth2_create_tables(&db_pg); /* NOT IMPLEMENTED */

  /* simulate failure for sqlite using OOM */
  {
    int i;
    for (i = 0; i < 20; i++) {
      oom_active = 1;
      oom_countdown = i;
      c_orm_oauth2_create_tables(db);
      oom_active = 0;
    }
  }

  /* simulate failure for generic db */
  {
    c_orm_db_t db_generic;
    c_orm_driver_vtable_t generic_vt;
    db_generic = *db;
    generic_vt = mock_vt;
    generic_vt.prepare = my_oauth2_prep;
    db_generic.vtable = &generic_vt;

    for (fail_sql = 1; fail_sql <= 4; fail_sql++) {
      c_orm_oauth2_create_tables(&db_generic);
    }
    fail_sql = 0;
    c_orm_oauth2_create_tables(&db_generic);
  }
  fail_sql = 0;

  if (db && db->vtable && db->vtable->disconnect) {
    db->vtable->disconnect(db);
    db = NULL;
  }
  PASS();
}

TEST test_oauth2_client(void) {
  c_orm_db_t *db = NULL;
  int is_valid;
  c_orm_user_t user;
  c_orm_oauth2_client_t client;
  c_orm_driver_vtable_t mock_vt;
  c_orm_error_t err;

  c_orm_sqlite_connect(":memory:", &db);
  c_orm_oauth2_create_tables(db);

  memset(&client, 0, sizeof(client));
  client.id = "no";
  client.client_secret = "no";
  client.redirect_uris = "x";
  client.grant_types = "x";
  client.scopes = "x";
  ASSERT_EQ(C_ORM_OK,
            c_orm_insert_generic(db, &c_orm_oauth2_client_meta, &client));

  is_valid = 0;
  err = c_orm_oauth2_verify_client(db, "no", "no", &is_valid);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_EQ(1, is_valid);

  is_valid = 0;
  c_orm_oauth2_verify_client(db, "does_not_exist", "no", &is_valid);
  ASSERT_EQ(0, is_valid);

  c_orm_user_verify_credentials(db, "does_not_exist", "p", &is_valid);
  ASSERT_EQ(0, is_valid);

  /* public client */
  memset(&client, 0, sizeof(client));
  client.id = "pub";
  client.client_secret = NULL;
  ASSERT_EQ(C_ORM_OK,
            c_orm_insert_generic(db, &c_orm_oauth2_client_meta, &client));
  is_valid = 0;
  err = c_orm_oauth2_verify_client(db, "pub", NULL, &is_valid);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_EQ(1, is_valid);

  memset(&user, 0, sizeof(user));
  user.id = "u";
  user.username = "u";
  user.password_hash = "p";
  user.salt = "s";
  c_orm_insert_generic(db, &c_orm_user_meta, &user);

  is_valid = 0;
  c_orm_user_verify_credentials(db, "u", "p", &is_valid);
  ASSERT_EQ(1, is_valid);

  fail_sql = 5; /* trigger error in c_orm_find_one_by_string */
  mock_vt = *(c_orm_driver_vtable_t *)db->vtable;
  mock_vt.prepare = my_oauth2_prep;
  mock_vt.step = my_oauth2_step;
  db->vtable = &mock_vt;
  c_orm_user_verify_credentials(db, "u2", "p", &is_valid);
  c_orm_oauth2_verify_client(db, "no", "no", &is_valid);
  fail_sql = 0;

  if (db && db->vtable && db->vtable->disconnect) {
    db->vtable->disconnect(db);
    db = NULL;
  }
  PASS();
}

TEST test_oauth2_scopes(void) {
  int is_valid = 0;
  int i;
  c_orm_oauth2_validate_scope("a b c", "a b", &is_valid);
  ASSERT(is_valid);
  for (i = 0; i < 5; i++) {
    oom_active = 1;
    oom_countdown = i;
    c_orm_oauth2_validate_scope("a b c", "a b", &is_valid);
    oom_active = 0;
  }
  c_orm_oauth2_validate_scope("a b c", "d", &is_valid);
  ASSERT(!is_valid);
  c_orm_oauth2_validate_scope(NULL, "a b", &is_valid);
  c_orm_oauth2_validate_scope("a b c", NULL, &is_valid);
  c_orm_oauth2_validate_scope("a b c", "", &is_valid);
  c_orm_oauth2_validate_scope("a b c", "  ", &is_valid);

  c_orm_oauth2_is_token_valid(NULL, 0, NULL);
  c_orm_oauth2_calculate_expiration(0, 3600, NULL);

  PASS();
}
TEST test_oauth2_auth_code(void) {
  c_orm_db_t *db = NULL;
  c_orm_oauth2_auth_code_t ac;
  c_orm_oauth2_auth_code_t out;
  c_orm_driver_vtable_t mock_vt;
  int i;
  c_orm_sqlite_connect(":memory:", &db);
  c_orm_oauth2_create_tables(db);

  memset(&ac, 0, sizeof(ac));
  ac.code = "123";
  ac.client_id = "client";
  ac.redirect_uri = "http";
  ac.user_id = "user";
  ac.scopes = "scopes";
  ac.expires_at = 999;

  c_orm_oauth2_save_auth_code(db, &ac);
  c_orm_oauth2_consume_auth_code(db, "123", &out);
  if (out.code) {
    C_ORM_FREE(out.code);
    out.code = NULL;
  }
  if (out.client_id) {
    C_ORM_FREE(out.client_id);
    out.client_id = NULL;
  }
  if (out.redirect_uri) {
    C_ORM_FREE(out.redirect_uri);
    out.redirect_uri = NULL;
  }
  if (out.user_id) {
    C_ORM_FREE(out.user_id);
    out.user_id = NULL;
  }
  if (out.scopes) {
    C_ORM_FREE(out.scopes);
    out.scopes = NULL;
  }
  c_orm_oauth2_consume_auth_code(db, "bad", &out);

  for (i = 0; i < 20; i++) {
    c_orm_oauth2_auth_code_t ac_oom = ac;
    oom_active = 1;
    oom_countdown = i;
    c_orm_oauth2_save_auth_code(db, &ac_oom);
    oom_active = 0;
  }
  for (i = 0; i < 20; i++) {
    oom_active = 1;
    oom_countdown = i;
    c_orm_oauth2_consume_auth_code(db, "123", &out);
    oom_active = 0;
    if (out.code) {
      C_ORM_FREE(out.code);
      out.code = NULL;
    }
    if (out.client_id) {
      C_ORM_FREE(out.client_id);
      out.client_id = NULL;
    }
    if (out.redirect_uri) {
      C_ORM_FREE(out.redirect_uri);
      out.redirect_uri = NULL;
    }
    if (out.user_id) {
      C_ORM_FREE(out.user_id);
      out.user_id = NULL;
    }
    if (out.scopes) {
      C_ORM_FREE(out.scopes);
      out.scopes = NULL;
    }
  }
  for (i = 0; i < 20; i++) {
    oom_active = 1;
    oom_countdown = i;
    c_orm_oauth2_cleanup_expired_tokens(db, 1000);
    oom_active = 0;
  }

  /* fail sql branches */
  mock_vt = *(c_orm_driver_vtable_t *)db->vtable;
  mock_vt.prepare = my_oauth2_prep;
  mock_vt.step = my_oauth2_step;
  db->vtable = &mock_vt;

  fail_sql = 7;
  c_orm_oauth2_save_auth_code(db, &ac); /* returns NOT_FOUND */
  fail_sql = 5;
  c_orm_oauth2_consume_auth_code(db, "123", &out);
  fail_sql = 6;
  c_orm_oauth2_consume_auth_code(db, "123", &out);
  fail_sql = 6;
  c_orm_oauth2_cleanup_expired_tokens(db, 1000);
  fail_sql = 8;
  c_orm_oauth2_cleanup_expired_tokens(db, 1000); /* returns NOT_FOUND */
  fail_sql = 0;
  ac.client_id = NULL;
  ac.redirect_uri = NULL;
  ac.user_id = NULL;
  ac.scopes = NULL;
  c_orm_oauth2_save_auth_code(db, &ac);

  if (db && db->vtable && db->vtable->disconnect) {
    db->vtable->disconnect(db);
    db = NULL;
  }
  PASS();
}

TEST test_oauth2_token(void) {
  c_orm_db_t *db = NULL;
  c_orm_oauth2_token_t t;
  c_orm_oauth2_token_t out;
  c_orm_driver_vtable_t mock_vt;
  int i;
  c_orm_sqlite_connect(":memory:", &db);
  c_orm_oauth2_create_tables(db);
  c_orm_execute_raw(db,
                    "INSERT INTO users (id, username) VALUES ('u1', 'user1');");

  memset(&t, 0, sizeof(t));
  t.access_token = "atk";
  t.refresh_token = "rtk";
  t.token_type = "Bearer";
  t.user_id = "u1";
  t.scopes = "read";
  t.created_at = 2000000000L;
  t.expires_in = 3600;

  c_orm_oauth2_save_token(db, &t);
  c_orm_oauth2_get_token(db, "atk", &out);
  if (out.access_token) {
    C_ORM_FREE(out.access_token);
    out.access_token = NULL;
  }
  if (out.refresh_token) {
    C_ORM_FREE(out.refresh_token);
    out.refresh_token = NULL;
  }
  if (out.token_type) {
    C_ORM_FREE(out.token_type);
    out.token_type = NULL;
  }
  if (out.user_id) {
    C_ORM_FREE(out.user_id);
    out.user_id = NULL;
  }
  if (out.scopes) {
    C_ORM_FREE(out.scopes);
    out.scopes = NULL;
  }
  c_orm_oauth2_get_token(db, "does_not_exist", &out);

  c_orm_oauth2_is_token_valid(&out, 0, &i);

  c_orm_oauth2_revoke_token(db, "atk");

  for (i = 0; i < 20; i++) {
    c_orm_oauth2_token_t t_oom = t;
    oom_active = 1;
    oom_countdown = i;
    c_orm_oauth2_save_token(db, &t_oom);
    oom_active = 0;
  }
  for (i = 0; i < 20; i++) {
    oom_active = 1;
    oom_countdown = i;
    c_orm_oauth2_get_token(db, "atk", &out);
    oom_active = 0;
    if (out.access_token) {
      C_ORM_FREE(out.access_token);
      out.access_token = NULL;
    }
    if (out.refresh_token) {
      C_ORM_FREE(out.refresh_token);
      out.refresh_token = NULL;
    }
    if (out.token_type) {
      C_ORM_FREE(out.token_type);
      out.token_type = NULL;
    }
    if (out.user_id) {
      C_ORM_FREE(out.user_id);
      out.user_id = NULL;
    }
    if (out.scopes) {
      C_ORM_FREE(out.scopes);
      out.scopes = NULL;
    }
  }
  for (i = 0; i < 20; i++) {
    oom_active = 1;
    oom_countdown = i;
    c_orm_oauth2_revoke_token(db, "atk");
    oom_active = 0;
  }

  /* fail sql branches */
  mock_vt = *(c_orm_driver_vtable_t *)db->vtable;
  mock_vt.prepare = my_oauth2_prep;
  mock_vt.step = my_oauth2_step;
  db->vtable = &mock_vt;

  fail_sql = 7;
  c_orm_oauth2_save_token(db, &t);
  fail_sql = 5;
  c_orm_oauth2_get_token(db, "atk", &out);
  fail_sql = 6;
  c_orm_oauth2_revoke_token(db, "atk");
  fail_sql = 6;
  c_orm_oauth2_cleanup_expired_tokens(db, 1000);
  fail_sql = 0;

  /* Missing fields */
  t.refresh_token = NULL;
  t.token_type = NULL;
  t.user_id = NULL;
  t.scopes = NULL;
  c_orm_oauth2_save_token(db, &t);

  if (db && db->vtable && db->vtable->disconnect) {
    db->vtable->disconnect(db);
    db = NULL;
  }
  PASS();
}

TEST test_oauth2_crypto_fail_open(void) {
  c_orm_oauth2_token_t t;
  cfs_path p;
  cfs_size_t rm_out = 0;
  (void)p;
  (void)rm_out;
  memset(&t, 0, sizeof(t));
  t.access_token = "abc";
  t.refresh_token = "def";

  remove("c_orm_token.dat");
#if !defined(_WIN32)
  mkdir("c_orm_token.dat", 0777);
#else
  _mkdir("c_orm_token.dat");
#endif
  c_orm_store_token_secure(&t);
#if !defined(_WIN32)
  rmdir("c_orm_token.dat");
#else
  _rmdir("c_orm_token.dat");
#endif

  PASS();
}

static c_orm_error_t dummy_prep(c_orm_db_t *db_v, const char *sql,
                                c_orm_query_t **out_query) {
  (void)db_v;
  (void)sql;
  if (out_query)
    *out_query = (c_orm_query_t *)0x1234;

  if (fail_sql == 1 && strstr(sql, "CREATE TABLE IF NOT EXISTS users"))
    return C_ORM_ERROR_SQL;
  if (fail_sql == 2 && strstr(sql, "CREATE TABLE IF NOT EXISTS tokens"))
    return C_ORM_ERROR_SQL;
  if (fail_sql == 3 && strstr(sql, "CREATE TABLE IF NOT EXISTS clients"))
    return C_ORM_ERROR_SQL;
  if (fail_sql == 4 && strstr(sql, "CREATE TABLE IF NOT EXISTS auth_codes"))
    return C_ORM_ERROR_SQL;

  return C_ORM_OK;
}

static c_orm_error_t dummy_step(c_orm_query_t *query, int *out_has_row) {
  (void)query;
  if (out_has_row)
    *out_has_row = 0;
  return C_ORM_OK;
}

static c_orm_error_t dummy_finalize(c_orm_query_t *query) {
  (void)query;
  return C_ORM_OK;
}

TEST test_oauth2_init_non_sqlite(void) {
  c_orm_db_t db_dummy;
  c_orm_driver_vtable_t dummy_vt;
  c_orm_error_t err;
  (void)err;

  memset(&db_dummy, 0, sizeof(db_dummy));
  memset(&dummy_vt, 0, sizeof(dummy_vt));

  dummy_vt.prepare = dummy_prep;
  dummy_vt.step = dummy_step;
  dummy_vt.finalize = dummy_finalize;
  db_dummy.vtable = &dummy_vt;
  c_orm_disable_statement_caching(&db_dummy);

  for (fail_sql = 1; fail_sql <= 4; fail_sql++) {
    c_orm_oauth2_create_tables(&db_dummy);
  }
  fail_sql = 0;
  err = c_orm_oauth2_create_tables(&db_dummy);

  dummy_finalize(NULL);
  PASS();
}

TEST test_oauth2_null_args(void) {
  c_orm_oauth2_save_token(NULL, NULL);
  c_orm_oauth2_get_token(NULL, NULL, NULL);
  c_orm_oauth2_revoke_token(NULL, NULL);
  c_orm_oauth2_create_tables(NULL);
  c_orm_oauth2_verify_client(NULL, NULL, NULL, NULL);
  c_orm_oauth2_verify_client((void *)1, NULL, NULL, NULL);
  c_orm_oauth2_save_auth_code(NULL, NULL);
  c_orm_oauth2_consume_auth_code(NULL, NULL, NULL);
  c_orm_oauth2_validate_scope(NULL, NULL, NULL);
  c_orm_oauth2_validate_scope("a", NULL, NULL);
  c_orm_oauth2_validate_scope(NULL, "b", NULL);
  c_orm_oauth2_encrypt_token(NULL, NULL);
  c_orm_oauth2_decrypt_token(NULL, NULL);
  c_orm_oauth2_get_current_timestamp(NULL);
  c_orm_oauth2_calculate_expiration(0, 0, NULL);
  c_orm_store_token_secure(NULL);
  c_orm_oauth2_is_token_valid(NULL, 0, NULL);
  c_orm_oauth2_token_parse_json(NULL, NULL);
  c_orm_user_verify_credentials(NULL, NULL, NULL, NULL);
  c_orm_oauth2_cleanup_expired_tokens(NULL, 0);

  PASS();
}

TEST test_oauth2_valid_token(void) {
  c_orm_oauth2_token_t t;
  int i;
  memset(&t, 0, sizeof(t));
  t.created_at = 2000000000L;
  t.expires_in = 3600;
  c_orm_oauth2_is_token_valid(&t, 0, &i);

  PASS();
}

static c_orm_error_t mock_oauth_step_fail(c_orm_query_t *q, int *has_row) {
  (void)q;
  (void)has_row;
  return C_ORM_ERROR_STEP;
}

TEST test_oauth2_all_branches(void) {
  c_orm_db_t *db = NULL;
  c_orm_oauth2_token_t tok;
  c_orm_oauth2_token_t out_tok;
  c_orm_oauth2_auth_code_t ac;
  c_orm_oauth2_auth_code_t out_ac;
  c_orm_oauth2_client_t client;
  char *enc = NULL;
  char *plain = NULL;
  int is_valid = 0;
  char huge_json[350];
  c_orm_driver_vtable_t fail_vt;
  const c_orm_driver_vtable_t *old_vt;

  memset(&tok, 0, sizeof(tok));
  memset(&out_tok, 0, sizeof(out_tok));
  memset(&ac, 0, sizeof(ac));
  memset(&out_ac, 0, sizeof(out_ac));
  memset(&client, 0, sizeof(client));

  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &db));
  ASSERT_EQ(C_ORM_OK, c_orm_oauth2_create_tables(db));

  /* 1. JSON edge cases: unmapped key, long key >=256 chars, escaped quote in
   * key, OOM */
  ASSERT_EQ(C_ORM_OK, c_orm_oauth2_token_parse_json(
                          "{\"unknown_key\":\"val\",\"access_token\":\"tok1\"}",
                          &out_tok));
  if (out_tok.access_token) {
    c_orm_free(out_tok.access_token);
    out_tok.access_token = NULL;
  }

  memset(huge_json, 'a', sizeof(huge_json));
  huge_json[0] = '{';
  huge_json[1] = '"';
  huge_json[270] = '"';
  huge_json[271] = ':';
  huge_json[272] = '"';
  huge_json[273] = 'v';
  huge_json[274] = '"';
  huge_json[275] = '}';
  huge_json[276] = '\0';
  ASSERT_EQ(C_ORM_OK, c_orm_oauth2_token_parse_json(huge_json, &out_tok));

  ASSERT_EQ(C_ORM_OK,
            c_orm_oauth2_token_parse_json(
                "{\"my\\\"key\":\"val\",\"access_token\":\"tok2\"}", &out_tok));
  if (out_tok.access_token) {
    c_orm_free(out_tok.access_token);
    out_tok.access_token = NULL;
  }

  oom_active = 1;
  oom_countdown = 0;
  c_orm_oauth2_token_parse_json("{\"access_token\":\"val\"}", &out_tok);
  oom_active = 0;

  /* 2. Token validity checks */
  tok.created_at = 1000;
  tok.expires_in = 3600;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_oauth2_is_token_valid(NULL, 2000, &is_valid));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_oauth2_is_token_valid(&tok, 2000, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_oauth2_is_token_valid(&tok, 2000, &is_valid));
  ASSERT_EQ(1, is_valid);

  /* 3. Crypto NULL args and token with NULL access_token */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_oauth2_encrypt_token(NULL, &enc));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_oauth2_encrypt_token("tok", NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_oauth2_decrypt_token(NULL, &plain));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_oauth2_decrypt_token("enc", NULL));

  /* Encrypt with NULL access token */
  tok.access_token = NULL;
  c_orm_oauth2_encrypt_token(tok.access_token, &enc);
  c_orm_store_token_secure(&tok);

  /* 4. verify_user parameter checks and nonexistent user */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_user_verify_credentials(NULL, "admin", "pass", &is_valid));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_user_verify_credentials(db, NULL, "pass", &is_valid));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_user_verify_credentials(db, "admin", NULL, &is_valid));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_user_verify_credentials(db, "admin", "pass", NULL));

  /* Nonexistent user */
  ASSERT_EQ(C_ORM_OK, c_orm_user_verify_credentials(db, "nonexistent", "pass",
                                                    &is_valid));
  ASSERT_EQ(0, is_valid);

  /* User with NULL password_hash and NULL salt */
  c_orm_execute_raw(db, "INSERT INTO users (id, username, password_hash, salt) "
                        "VALUES ('u_null', 'null_user', NULL, NULL);");
  ASSERT_EQ(C_ORM_OK,
            c_orm_user_verify_credentials(db, "null_user", "pass", &is_valid));
  ASSERT_EQ(0, is_valid);

  /* 5. verify_client parameter checks and various secret checks */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_oauth2_verify_client(NULL, "cid", "sec", &is_valid));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_oauth2_verify_client(db, NULL, "sec", &is_valid));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_oauth2_verify_client(db, "cid", "sec", NULL));

  /* Nonexistent client */
  ASSERT_EQ(C_ORM_OK,
            c_orm_oauth2_verify_client(db, "nonexistent", "sec", &is_valid));
  ASSERT_EQ(0, is_valid);

  /* Insert client with secret */
  client.id = "c1";
  client.client_secret = "secret123";
  c_orm_insert(db, &c_orm_oauth2_client_meta, &client);

  /* Correct secret */
  ASSERT_EQ(C_ORM_OK,
            c_orm_oauth2_verify_client(db, "c1", "secret123", &is_valid));
  ASSERT_EQ(1, is_valid);

  /* NULL secret arg */
  ASSERT_EQ(C_ORM_OK, c_orm_oauth2_verify_client(db, "c1", NULL, &is_valid));
  ASSERT_EQ(0, is_valid);

  /* Wrong secret */
  ASSERT_EQ(C_ORM_OK, c_orm_oauth2_verify_client(db, "c1", "wrong", &is_valid));
  ASSERT_EQ(0, is_valid);

  /* Insert client with empty secret */
  client.id = "c2";
  client.client_secret = "";
  c_orm_insert(db, &c_orm_oauth2_client_meta, &client);
  ASSERT_EQ(C_ORM_OK, c_orm_oauth2_verify_client(db, "c2", "any", &is_valid));
  ASSERT_EQ(1, is_valid);

  /* Insert client with NULL secret */
  c_orm_execute_raw(db, "INSERT INTO clients (id, client_secret, "
                        "redirect_uris, scopes, grant_types) VALUES ('c_pub', "
                        "NULL, 'http://localhost', 'read', 'code');");
  ASSERT_EQ(C_ORM_OK, c_orm_oauth2_verify_client(db, "c_pub", NULL, &is_valid));
  ASSERT_EQ(1, is_valid);

  /* 6. save_token NULL checks and user_id non-null branch */
  c_orm_execute_raw(
      db, "INSERT INTO users (id, username) VALUES ('user_456', 'u456');");
  tok.access_token = "tok_acc";
  tok.refresh_token = "tok_ref";
  tok.token_type = "Bearer";
  tok.user_id = "user_456";
  tok.scopes = "read write";
  tok.expires_in = 3600;
  tok.created_at = 2000000000L;

  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_oauth2_save_token(NULL, &tok));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_oauth2_save_token(db, NULL));
  {
    c_orm_oauth2_token_t tok_no_acc;
    memset(&tok_no_acc, 0, sizeof(tok_no_acc));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_oauth2_save_token(db, &tok_no_acc));
  }
  ASSERT_EQ(C_ORM_OK, c_orm_oauth2_save_token(db, &tok));

  /* 7. get_token NULL checks */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_oauth2_get_token(NULL, "tok_acc", &out_tok));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_oauth2_get_token(db, NULL, &out_tok));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_oauth2_get_token(db, "tok_acc", NULL));
  {
    c_orm_error_t get_tok_rc = c_orm_oauth2_get_token(db, "tok_acc", &out_tok);
    ASSERT_EQ_FMT(C_ORM_OK, get_tok_rc, "%d");
  }
  if (out_tok.access_token)
    c_orm_free(out_tok.access_token);
  if (out_tok.refresh_token)
    c_orm_free(out_tok.refresh_token);
  if (out_tok.token_type)
    c_orm_free(out_tok.token_type);
  if (out_tok.user_id)
    c_orm_free(out_tok.user_id);
  if (out_tok.scopes)
    c_orm_free(out_tok.scopes);

  /* 8. revoke_token NULL checks */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_oauth2_revoke_token(NULL, "tok_acc"));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_oauth2_revoke_token(db, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_oauth2_revoke_token(db, "tok_acc"));

  /* 9. save_auth_code NULL checks */
  ac.code = "code_123";
  ac.client_id = "c1";
  ac.redirect_uri = "https://example.com";
  ac.user_id = "user_456";
  ac.expires_at = 100000;
  ac.scopes = "read";

  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_oauth2_save_auth_code(NULL, &ac));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_oauth2_save_auth_code(db, NULL));
  {
    c_orm_oauth2_auth_code_t ac_no_code;
    memset(&ac_no_code, 0, sizeof(ac_no_code));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_oauth2_save_auth_code(db, &ac_no_code));
  }
  ASSERT_EQ(C_ORM_OK, c_orm_oauth2_save_auth_code(db, &ac));

  /* 10. consume_auth_code NULL checks */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_oauth2_consume_auth_code(NULL, "code_123", &out_ac));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_oauth2_consume_auth_code(db, NULL, &out_ac));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_oauth2_consume_auth_code(db, "code_123", NULL));
  c_orm_oauth2_consume_auth_code(db, "code_123", &out_ac);
  if (out_ac.code)
    c_orm_free(out_ac.code);
  if (out_ac.client_id)
    c_orm_free(out_ac.client_id);
  if (out_ac.redirect_uri)
    c_orm_free(out_ac.redirect_uri);
  if (out_ac.user_id)
    c_orm_free(out_ac.user_id);
  if (out_ac.scopes)
    c_orm_free(out_ac.scopes);

  /* 11. cleanup_expired_tokens error path */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_oauth2_cleanup_expired_tokens(NULL, 0));

  old_vt = db->vtable;
  memcpy(&fail_vt, old_vt, sizeof(fail_vt));
  fail_vt.step = mock_oauth_step_fail;
  db->vtable = &fail_vt;
  ASSERT_EQ(C_ORM_ERROR_STEP, c_orm_oauth2_cleanup_expired_tokens(db, 200000));
  db->vtable = old_vt;

  if (db && db->vtable && db->vtable->disconnect) {
    db->vtable->disconnect(db);
  }
  PASS();
}

SUITE(oauth2_suite) {
  void *(*old_malloc)(size_t) = c_orm_malloc;
  void (*old_free)(void *) = c_orm_free;

  c_orm_set_allocators(m_mock_malloc, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, m_mock_free);

  RUN_TEST(test_oauth2_flat_json);
  RUN_TEST(test_oauth2_json_edge_cases);
  RUN_TEST(test_oauth2_crypto);
  RUN_TEST(test_oauth2_crypto_fail_open);
  RUN_TEST(test_oauth2_init);
  RUN_TEST(test_oauth2_init_non_sqlite);
  RUN_TEST(test_oauth2_client);
  RUN_TEST(test_oauth2_scopes);
  RUN_TEST(test_oauth2_auth_code);
  RUN_TEST(test_oauth2_token);
  RUN_TEST(test_oauth2_null_args);

  RUN_TEST(test_oauth2_valid_token);
  RUN_TEST(test_oauth2_all_branches);

  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, old_free);
}

#if defined(__clang__) || defined(__GNUC__)
#endif
