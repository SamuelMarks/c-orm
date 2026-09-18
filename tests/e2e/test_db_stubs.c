#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_db_stubs.c
 * @brief Unit tests covering database driver stubs, async APIs, and hooks.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_api.h"
#include "c_orm_db.h"
#include "c_orm_mysql.h"
#include "c_orm_postgres.h"
#include "c_orm_codegen.h"
#include "greatest.h"
#include <stdio.h>
#include <string.h>
/* clang-format on */

#ifndef __EMSCRIPTEN__
/**
 * @brief Test PostgreSQL driver stub functions and unimplemented error codes.
 * @return GREATEST test result.
 */
TEST test_postgres_stubs_edge_cases(void) {
  c_orm_db_t *db;
  const c_orm_driver_vtable_t *vt;
  c_orm_error_t err;
  unsigned int oid;
  void *fd;
  size_t read_len;
  size_t written_len;

  db = NULL;
  vt = NULL;
  fd = NULL;
  read_len = 0;
  written_len = 0;

  err = c_orm_postgres_connect("fake", &db);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);

  err = c_orm_postgres_get_vtable(&vt);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);
  ASSERT(vt == NULL);

  err = c_orm_postgres_get_vtable(NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);

  err = c_orm_postgres_lo_create(NULL, &oid);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);

  err = c_orm_postgres_lo_open(NULL, 0, 0, &fd);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);

  err = c_orm_postgres_lo_read(NULL, NULL, NULL, 0, &read_len);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);

  err = c_orm_postgres_lo_write(NULL, NULL, NULL, 0, &written_len);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);

  err = c_orm_postgres_lo_close(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);

  PASS();
}

/**
 * @brief Test MySQL driver stub functions and unimplemented error codes.
 * @return GREATEST test result.
 */
TEST test_mysql_stubs_edge_cases(void) {
  c_orm_db_t *db;
  const c_orm_driver_vtable_t *vt;
  c_orm_error_t err;

  db = NULL;
  vt = NULL;

  err = c_orm_mysql_connect("fake", &db);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);

  err = c_orm_mysql_get_vtable(&vt);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);
  ASSERT(vt == NULL);

  err = c_orm_mysql_get_vtable(NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);

  PASS();
}
#endif

/**
 * @brief Test database hook callback.
 * @param db Database handle.
 * @param sql Executed SQL.
 * @param user_data Context pointer.
 */
static void test_hook(c_orm_db_t *db, const char *sql, void *user_data) {
  (void)db;
  (void)sql;
  (void)user_data;
}

/**
 * @brief Mock encryption hook.
 * @param in Input buffer.
 * @param in_len Input length.
 * @param ctx Context pointer.
 * @param out Output buffer pointer.
 * @param out_len Output length pointer.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t crypto_enc_hook(const void *in, size_t in_len, void *ctx,
                                     void **out, size_t *out_len) {
  (void)in;
  (void)in_len;
  (void)ctx;
  (void)out;
  (void)out_len;
  return C_ORM_OK;
}

/**
 * @brief Mock decryption hook.
 * @param in Input buffer.
 * @param in_len Input length.
 * @param ctx Context pointer.
 * @param out Output buffer pointer.
 * @param out_len Output length pointer.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t crypto_dec_hook(const void *in, size_t in_len, void *ctx,
                                     void **out, size_t *out_len) {
  (void)in;
  (void)in_len;
  (void)ctx;
  (void)out;
  (void)out_len;
  return C_ORM_OK;
}

/**
 * @brief Test logging callback.
 * @param msg Log message.
 * @param user_data Context pointer.
 */
static void test_log_cb(const char *msg, void *user_data) {
  (void)msg;
  (void)user_data;
}

/**
 * @brief Test expiration callback.
 * @param db Database handle.
 * @param meta Table metadata.
 * @param obj Object pointer.
 * @param user_data Context pointer.
 */
static void test_expire_cb(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                           void *obj, void *user_data) {
  (void)db;
  (void)meta;
  (void)obj;
  (void)user_data;
}

/**
 * @brief Mock get last error callback.
 * @param db Database handle.
 * @param out Pointer to receive error message.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t get_last_err_mock(c_orm_db_t *db, const char **out) {
  (void)db;
  (void)out;
  *out = "mock";
  return C_ORM_OK;
}

/**
 * @brief Mock get last trace callback.
 * @param db Database handle.
 * @param out Pointer to receive trace string.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t get_last_trace_mock(c_orm_db_t *db, const char **out) {
  (void)db;
  (void)out;
  *out = "trace";
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t
c_orm_db_attach_identity_map(c_orm_db_t *db, c_orm_identity_map_t *map);
C_ORM_EXPORT c_orm_error_t c_orm_register_query_interceptor(
    c_orm_db_t *db, c_orm_interceptor_cb hook, void *context);
C_ORM_EXPORT c_orm_error_t c_orm_register_hydration_interceptor(
    c_orm_db_t *db, c_orm_interceptor_cb hook, void *context);

/**
 * @brief Test database handle configuration, hooks, and telemetry.
 * @return GREATEST test result.
 */
TEST test_c_orm_db_coverage(void) {
  c_orm_db_t db;
  c_orm_driver_vtable_t vt;
  const char *msg;
  c_orm_error_t rc;
  c_orm_pool_telemetry_t tel;
  c_orm_timezone_t tz;

  msg = NULL;

  /* call the hooks */
  test_hook(NULL, NULL, NULL);
  rc = crypto_enc_hook(NULL, 0, NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = crypto_dec_hook(NULL, 0, NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_OK, rc);
  test_log_cb(NULL, NULL);
  test_expire_cb(NULL, NULL, NULL, NULL);

  memset(&db, 0, sizeof(db));
  memset(&vt, 0, sizeof(vt));
  memset(&tel, 0, sizeof(tel));
  memset(&tz, 0, sizeof(tz));

  /* c_orm_get_last_error_message */
  rc = c_orm_get_last_error_message(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  rc = c_orm_get_last_error_message(NULL, &msg);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_STR_EQ("Unknown Error (No DB context)", msg);

  rc = c_orm_get_last_error_message(&db, &msg);
  ASSERT_EQ(C_ORM_OK, rc);

  db.vtable = &vt;
  rc = c_orm_get_last_error_message(&db, &msg);
  ASSERT_EQ(C_ORM_OK, rc);

  vt.get_last_error = get_last_err_mock;
  rc = c_orm_get_last_error_message(&db, &msg);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_STR_EQ("mock", msg);

  /* c_orm_get_last_error_trace */
  rc = c_orm_get_last_error_trace(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  rc = c_orm_get_last_error_trace(NULL, &msg);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  db.vtable = NULL;
  rc = c_orm_get_last_error_trace(&db, &msg);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  db.vtable = &vt;
  rc = c_orm_get_last_error_trace(&db, &msg);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  vt.get_last_trace = get_last_trace_mock;
  rc = c_orm_get_last_error_trace(&db, &msg);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_STR_EQ("trace", msg);

  /* getters setters */
  c_orm_set_log_callback(NULL, NULL, NULL);
  c_orm_set_log_callback(&db, test_log_cb, NULL);

  c_orm_set_slow_query_threshold(NULL, 100);
  c_orm_set_slow_query_threshold(&db, 100);

  rc = c_orm_get_telemetry(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_get_telemetry(&db, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_get_telemetry(&db, &tel);
  ASSERT_EQ(C_ORM_OK, rc);

  c_orm_set_expire_callback(NULL, NULL, NULL);
  c_orm_set_expire_callback(&db, test_expire_cb, NULL);

  rc = c_orm_db_attach_identity_map(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = c_orm_db_attach_identity_map(&db, NULL);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = c_orm_register_query_interceptor(NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_register_query_interceptor(&db, test_hook, NULL);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = c_orm_register_hydration_interceptor(NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_register_hydration_interceptor(&db, test_hook, NULL);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = c_orm_register_crypto_hooks(NULL, NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_register_crypto_hooks(&db, crypto_enc_hook, crypto_dec_hook, NULL);
  ASSERT_EQ(C_ORM_OK, rc);

  c_orm_set_timezone(NULL, tz);
  c_orm_set_timezone(&db, tz);

  PASS();
}

/**
 * @brief Async query completion callback.
 * @param err Error enum.
 * @param ctx Context pointer.
 */
static void async_cb(c_orm_error_t err, void *ctx) {
  if (err != C_ORM_OK) {
    /* err handled */
  }
  (void)ctx;
}

/**
 * @brief Test async CRUD APIs and error handling.
 * @return GREATEST test result.
 */
TEST test_c_orm_async_coverage(void) {
  c_orm_db_t db;
  c_orm_table_meta_t meta;
  int obj;
  c_orm_error_t rc;

  memset(&db, 0, sizeof(db));
  memset(&meta, 0, sizeof(meta));
  obj = 0;

  rc = c_orm_insert_async(NULL, NULL, NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_insert_async(&db, NULL, NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_insert_async(&db, &meta, NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_insert_async(&db, &meta, &obj, async_cb, NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, rc);

  rc = c_orm_find_all_async(NULL, NULL, NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_find_all_async(&db, NULL, NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_find_all_async(&db, &meta, NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_find_all_async(&db, &meta, &obj, async_cb, NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, rc);

  rc = c_orm_insert_async(&db, &meta, &obj, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, rc);

  rc = c_orm_find_all_async(&db, &meta, &obj, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, rc);

  PASS();
}

#ifndef __EMSCRIPTEN__
/**
 * @brief Test codegen API error validation and file generation.
 * @return GREATEST test result.
 */
TEST test_codegen_coverage(void) {
  c_orm_error_t rc;
  const char *schema_path;
  FILE *f;

  f = NULL;
  rc = c_orm_codegen_generate(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  rc = c_orm_codegen_generate("fake.sql", NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  rc = c_orm_codegen_generate("fake.sql", "fake_dir");
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  C_ORM_FOPEN(&f, "test_stubs_schema.sql", "w");
  if (f != NULL) {
    fprintf(f, "%s\n", "CREATE TABLE t_stubs (id INTEGER PRIMARY KEY);");
    fclose(f);
  }
  schema_path = "test_stubs_schema.sql";

  rc = c_orm_codegen_generate(schema_path, "test_out");
  remove("test_stubs_schema.sql");

  ASSERT_EQ(C_ORM_OK, rc);

  PASS();
}
#endif

/**
 * @brief Test database modality setting and context inspection.
 * @return GREATEST test result.
 */
TEST test_modality_coverage(void) {
  c_orm_db_t db;
  c_orm_error_t rc;

  memset(&db, 0, sizeof(db));

  rc = c_orm_set_modality(NULL, 0, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_set_modality(&db, 1, (void *)0x123);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(1, db.modality);
  ASSERT_EQ((void *)0x123, db.modality_ctx);

  PASS();
}

/**
 * @brief Database stubs and hooks test suite runner.
 */
SUITE(db_stubs_suite) {
#ifndef __EMSCRIPTEN__
  RUN_TEST(test_postgres_stubs_edge_cases);
  RUN_TEST(test_mysql_stubs_edge_cases);
#endif
  RUN_TEST(test_c_orm_db_coverage);
  RUN_TEST(test_c_orm_async_coverage);
#ifndef __EMSCRIPTEN__
  RUN_TEST(test_codegen_coverage);
#endif
  RUN_TEST(test_modality_coverage);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#if defined(__clang__) || defined(__GNUC__)
#endif
