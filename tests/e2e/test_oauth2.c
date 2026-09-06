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
  c_orm_db_t *db = NULL;
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
  if (token.access_token) {
    free(token.access_token);
    token.access_token = NULL;
  }

  c_orm_oauth2_token_parse_json("{\"access_token\": 123", &token);
  if (token.access_token) {
    free(token.access_token);
    token.access_token = NULL;
  }

  c_orm_oauth2_token_parse_json("{\"access_token\": \"123", &token);
  if (token.access_token) {
    free(token.access_token);
    token.access_token = NULL;
  }

  c_orm_oauth2_token_parse_json("{\"expires_in\": \"3600\"}", &token);
  c_orm_oauth2_token_parse_json("{\"expires_in\": abc}", &token);
  c_orm_oauth2_token_parse_json("{\"unknown_key\": \"val\"}", &token);

  if (db && db->vtable && db->vtable->disconnect) {
    db->vtable->disconnect(db);
    db = NULL;
  }
  PASS();
}

TEST test_oauth2_flat_json(void) {
  c_orm_db_t *db = NULL;
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

  if (db && db->vtable && db->vtable->disconnect) {
    db->vtable->disconnect(db);
    db = NULL;
  }
  PASS();
}

TEST test_oauth2_crypto(void) {
  c_orm_db_t *db = NULL;
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

  if (db && db->vtable && db->vtable->disconnect) {
    db->vtable->disconnect(db);
    db = NULL;
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
  if (fail_sql == 99 && strstr(sql, "COMMIT"))
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
  c_orm_db_t *db = NULL;
  int is_valid;
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

  if (db && db->vtable && db->vtable->disconnect) {
    db->vtable->disconnect(db);
    db = NULL;
  }
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

  memset(&t, 0, sizeof(t));
  t.access_token = "atk";
  t.refresh_token = "rtk";
  t.token_type = "Bearer";
  t.user_id = NULL;
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
  c_orm_db_t *db = NULL;
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

  if (db && db->vtable && db->vtable->disconnect) {
    db->vtable->disconnect(db);
    db = NULL;
  }
  PASS();
}

static c_orm_error_t dummy_prep(c_orm_db_t *db_v, const char *sql,
                                c_orm_query_t **out_query) {
  (void)db_v;
  (void)sql;
  if (out_query)
    *out_query = NULL;

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
  c_orm_db_t *db = NULL;
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

  for (fail_sql = 1; fail_sql <= 4; fail_sql++) {
    c_orm_oauth2_create_tables(&db_dummy);
  }
  fail_sql = 0;
  err = c_orm_oauth2_create_tables(&db_dummy);

  dummy_finalize(NULL);

  if (db && db->vtable && db->vtable->disconnect) {
    db->vtable->disconnect(db);
    db = NULL;
  }
  PASS();
}

TEST test_oauth2_null_args(void) {
  c_orm_db_t *db = NULL;
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

  if (db && db->vtable && db->vtable->disconnect) {
    db->vtable->disconnect(db);
    db = NULL;
  }
  PASS();
}

TEST test_oauth2_valid_token(void) {
  c_orm_db_t *db = NULL;
  c_orm_oauth2_token_t t;
  int i;
  memset(&t, 0, sizeof(t));
  t.created_at = 2000000000L;
  t.expires_in = 3600;
  c_orm_oauth2_is_token_valid(&t, 0, &i);

  if (db && db->vtable && db->vtable->disconnect) {
    db->vtable->disconnect(db);
    db = NULL;
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

  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, old_free);
}

#if defined(__clang__) || defined(__GNUC__)
#endif
