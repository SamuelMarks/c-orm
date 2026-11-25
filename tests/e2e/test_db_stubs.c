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
#include "c_orm_sqlite.h"
#include "c_orm_codegen.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include "test_assert_zero_branch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

#ifndef __EMSCRIPTEN__

/**
 * @brief Executes test_postgres_stubs_edge_cases.
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
  ASSERT(db == NULL);
  err = c_orm_postgres_connect("fake", NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);

  err = c_orm_postgres_get_vtable(&vt);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);
  ASSERT(vt == NULL);

  err = c_orm_postgres_get_vtable(NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);

  err = c_orm_postgres_lo_create(NULL, &oid);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);
  ASSERT_EQ(0U, oid);
  err = c_orm_postgres_lo_create(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);

  err = c_orm_postgres_lo_open(NULL, 0, 0, &fd);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);
  ASSERT(fd == NULL);
  err = c_orm_postgres_lo_open(NULL, 0, 0, NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);

  err = c_orm_postgres_lo_read(NULL, NULL, NULL, 0, &read_len);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);
  ASSERT_EQ(0U, read_len);
  err = c_orm_postgres_lo_read(NULL, NULL, NULL, 0, NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);

  err = c_orm_postgres_lo_write(NULL, NULL, NULL, 0, &written_len);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);
  ASSERT_EQ(0U, written_len);
  err = c_orm_postgres_lo_write(NULL, NULL, NULL, 0, NULL);
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
  ASSERT(db == NULL);
  err = c_orm_mysql_connect("fake", NULL);
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
 * @return C_ORM_OK.
 */
static c_orm_error_t test_hook(c_orm_db_t *db, const char *sql,
                               void *user_data) {
  (void)db;
  (void)sql;
  if (user_data) {
    *(int *)user_data += 1;
  }
  return C_ORM_OK;
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
 * @return C_ORM_OK.
 */
static c_orm_error_t test_log_cb(const char *msg, void *user_data) {
  (void)msg;
  if (user_data) {
    *(int *)user_data += 1;
  }
  return C_ORM_OK;
}

/**
 * @brief Test expiration callback.
 * @param db Database handle.
 * @param meta Table metadata.
 * @param obj Object pointer.
 * @param user_data Context pointer.
 * @return C_ORM_OK.
 */
static c_orm_error_t test_expire_cb(c_orm_db_t *db,
                                    const c_orm_table_meta_t *meta, void *obj,
                                    void *user_data) {
  (void)db;
  (void)meta;
  (void)obj;
  if (user_data) {
    *(int *)user_data += 1;
  }
  return C_ORM_OK;
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
  {
    int cb_cnt = 0;
    ASSERT_EQ(C_ORM_OK, test_hook(NULL, NULL, NULL));
    ASSERT_EQ(C_ORM_OK, test_hook(NULL, NULL, &cb_cnt));
    ASSERT_EQ(1, cb_cnt);
    rc = crypto_enc_hook(NULL, 0, NULL, NULL, NULL);
    ASSERT_EQ(C_ORM_OK, rc);
    rc = crypto_dec_hook(NULL, 0, NULL, NULL, NULL);
    ASSERT_EQ(C_ORM_OK, rc);
    ASSERT_EQ(C_ORM_OK, test_log_cb(NULL, NULL));
    ASSERT_EQ(C_ORM_OK, test_log_cb(NULL, &cb_cnt));
    ASSERT_EQ(2, cb_cnt);
    ASSERT_EQ(C_ORM_OK, test_expire_cb(NULL, NULL, NULL, NULL));
    ASSERT_EQ(C_ORM_OK, test_expire_cb(NULL, NULL, NULL, &cb_cnt));
    ASSERT_EQ(3, cb_cnt);
  }

  /* Verify c_orm_error_to_string across all error codes */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_error_to_string(C_ORM_OK, NULL));
  {
    const c_orm_error_t all_errs[] = {C_ORM_OK,
                                      C_ORM_ERROR_MEMORY,
                                      C_ORM_ERROR_CONNECTION,
                                      C_ORM_ERROR_SQL,
                                      C_ORM_ERROR_BIND,
                                      C_ORM_ERROR_STEP,
                                      C_ORM_ERROR_TYPE_MISMATCH,
                                      C_ORM_ERROR_NOT_FOUND,
                                      C_ORM_ERROR_NOT_IMPLEMENTED,
                                      C_ORM_ERROR_UNKNOWN,
                                      C_ORM_ERROR_EXPIRED,
                                      C_ORM_ERROR_VALIDATION,
                                      C_ORM_ERROR_RECURSION,
                                      C_ORM_ERROR_READ_ONLY,
                                      C_ORM_ERROR_NULL_POINTER,
                                      C_ORM_ERROR_IO,
                                      C_ORM_ERROR_PARSE,
                                      C_ORM_ERROR_OVERFLOW,
                                      C_ORM_ERROR_CYCLE,
                                      C_ORM_ERROR_BUSY,
                                      C_ORM_ERROR_CRYPTO,
                                      (c_orm_error_t)9999};
    size_t ei;
    for (ei = 0; ei < sizeof(all_errs) / sizeof(all_errs[0]); ++ei) {
      msg = NULL;
      ASSERT_EQ(C_ORM_OK, c_orm_error_to_string(all_errs[ei], &msg));
      ASSERT(msg != NULL);
      ASSERT(msg[0] == 'C');
    }
  }

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
  ASSERT_EQ(C_ORM_OK, c_orm_set_log_callback(NULL, NULL, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_set_log_callback(&db, test_log_cb, NULL));
  ASSERT(db.log_cb == test_log_cb);

  ASSERT_EQ(C_ORM_OK, c_orm_set_slow_query_threshold(NULL, 100));
  ASSERT_EQ(C_ORM_OK, c_orm_set_slow_query_threshold(&db, 100));
  ASSERT_EQ(100U, db.slow_query_threshold_ms);

  rc = c_orm_get_telemetry(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_get_telemetry(&db, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  db.telemetry.active_connections = 7;
  rc = c_orm_get_telemetry(&db, &tel);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ((size_t)7, tel.active_connections);

  ASSERT_EQ(C_ORM_OK, c_orm_set_expire_callback(NULL, NULL, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_set_expire_callback(&db, test_expire_cb, NULL));
  ASSERT(db.expire_cb == test_expire_cb);

  rc = c_orm_db_attach_identity_map(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = c_orm_db_attach_identity_map(&db, NULL);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = c_orm_register_query_interceptor(NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_register_query_interceptor(&db, test_hook, NULL);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(db.query_interceptor == test_hook);

  rc = c_orm_register_hydration_interceptor(NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_register_hydration_interceptor(&db, test_hook, NULL);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(db.hydration_interceptor == test_hook);

  rc = c_orm_register_crypto_hooks(NULL, NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_register_crypto_hooks(&db, crypto_enc_hook, crypto_dec_hook, NULL);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(db.encrypt_hook == crypto_enc_hook);
  ASSERT(db.decrypt_hook == crypto_dec_hook);

  tz.offset_minutes = -300;
  ASSERT_EQ(C_ORM_OK, c_orm_set_timezone(NULL, tz));
  ASSERT_EQ(C_ORM_OK, c_orm_set_timezone(&db, tz));
  ASSERT_EQ(-300, db.timezone.offset_minutes);

  PASS();
}

/**
 * @brief Async query completion callback for testing.
 * @param err Error enum.
 * @param ctx Context pointer.
 * @return C_ORM_OK, or C_ORM_ERROR_VALIDATION if ctx points to -1.
 */
static c_orm_error_t test_async_cb(c_orm_error_t err, void *ctx) {
  if (ctx) {
    int *flag = (int *)ctx;
    if (*flag == -1) {
      return C_ORM_ERROR_VALIDATION;
    }
    *flag = (int)err + 100;
  }
  return C_ORM_OK;
}

/**
 * @brief Custom modality executor submit callback for async tests.
 * @param task_fn Task function pointer.
 * @param task_ctx Task payload context.
 * @param user_data Counter pointer incremented on submit.
 * @return Task return code.
 */
static c_orm_error_t test_executor_submit(c_orm_async_task_fn task_fn,
                                          void *task_ctx, void *user_data) {
  if (user_data) {
    (*(int *)user_data)++;
  }
  return task_fn(task_ctx);
}

/** @brief Mock OOM flag for test_db_stubs. */
static int stubs_oom_fail = 0;

/**
 * @brief Mock malloc callback for OOM testing in test_db_stubs.
 * @param size Requested byte size.
 * @param out_ptr Output allocated pointer.
 * @return C_ORM_OK or C_ORM_ERROR_MEMORY when stubs_oom_fail is set.
 */
static c_orm_error_t stubs_mock_malloc(size_t size, void **out_ptr) {
  if (stubs_oom_fail) {
    *out_ptr = NULL;
    return C_ORM_ERROR_MEMORY;
  }
  return c_orm_default_malloc(size, out_ptr);
}

/**
 * @brief Test async CRUD APIs and error handling.
 * @return GREATEST test result.
 */
TEST test_c_orm_async_coverage(void) {
  c_orm_db_t db;
  c_orm_table_meta_t meta;
  int obj;
  int cb_state;
  c_orm_error_t rc;

  cb_state = 0;
  ASSERT_EQ(C_ORM_OK, test_async_cb(C_ORM_OK, &cb_state));
  ASSERT_EQ(100, cb_state);

  memset(&db, 0, sizeof(db));
  memset(&meta, 0, sizeof(meta));
  obj = 0;

  rc = c_orm_insert_async(NULL, NULL, NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_insert_async(&db, NULL, NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_insert_async(&db, &meta, NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  cb_state = 0;
  rc = c_orm_insert_async(&db, &meta, &obj, test_async_cb, &cb_state);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  ASSERT_EQ(100 + (int)C_ORM_ERROR_MEMORY, cb_state);

  cb_state = -1;
  rc = c_orm_insert_async(&db, &meta, &obj, test_async_cb, &cb_state);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, rc);

  rc = c_orm_find_all_async(NULL, NULL, NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_find_all_async(&db, NULL, NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_find_all_async(&db, &meta, NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  cb_state = 0;
  rc = c_orm_find_all_async(&db, &meta, &obj, test_async_cb, &cb_state);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  ASSERT_EQ(100 + (int)C_ORM_ERROR_MEMORY, cb_state);

  rc = c_orm_insert_async(&db, &meta, &obj, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_find_all_async(&db, &meta, &obj, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  /* Also verify real in-memory SQLite async insert + callback error percolation
   */
#ifdef C_ORM_ENABLE_SQLITE
  {
    c_orm_db_t *real_db = NULL;
    c_orm_column_meta_t col;
    struct {
      int32_t id;
    } row_in;
    struct {
      void *data;
      size_t length;
      size_t capacity;
    } arr_out;
    memset(&col, 0, sizeof(col));
    col.name = "id";
    col.type = C_ORM_TYPE_INT32;
    col.offset = 0;
    col.is_pk = 1;
    memset(&meta, 0, sizeof(meta));
    meta.name = "t_async";
    meta.columns = &col;
    meta.num_columns = 1;
    meta.struct_size = sizeof(row_in);
    meta.query_insert = "INSERT INTO t_async (id) VALUES (?)";
    meta.query_select_all = "SELECT id FROM t_async";

    ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &real_db));
    ASSERT_EQ(C_ORM_OK,
              c_orm_execute_raw(
                  real_db, "CREATE TABLE t_async (id INTEGER PRIMARY KEY)"));
    row_in.id = 42;
    cb_state = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_insert_async(real_db, &meta, &row_in,
                                           test_async_cb, &cb_state));
    ASSERT_EQ(100, cb_state);

    row_in.id = 43;
    cb_state = -1;
    ASSERT_EQ(
        C_ORM_ERROR_VALIDATION,
        c_orm_insert_async(real_db, &meta, &row_in, test_async_cb, &cb_state));

    memset(&arr_out, 0, sizeof(arr_out));
    cb_state = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_find_all_async(real_db, &meta, &arr_out,
                                             test_async_cb, &cb_state));
    ASSERT_EQ(100, cb_state);
    ASSERT_EQ((size_t)2, arr_out.length);
    c_orm_system_free(arr_out.data);

    memset(&arr_out, 0, sizeof(arr_out));
    cb_state = -1;
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_find_all_async(real_db, &meta, &arr_out, test_async_cb,
                                   &cb_state));
    c_orm_system_free(arr_out.data);

    /* Validate NULL argument checks on find_by_id_int32_async, update_async,
     * delete_async, execute_raw_async */
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_find_by_id_int32_async(
                                      NULL, &meta, 1, &row_in, NULL, NULL));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_find_by_id_int32_async(
                                      real_db, NULL, 1, &row_in, NULL, NULL));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_find_by_id_int32_async(
                                      real_db, &meta, 1, NULL, NULL, NULL));

    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              c_orm_update_async(NULL, &meta, &row_in, NULL, NULL));
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              c_orm_update_async(real_db, NULL, &row_in, NULL, NULL));
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              c_orm_update_async(real_db, &meta, NULL, NULL, NULL));

    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              c_orm_delete_async(NULL, &meta, &row_in, NULL, NULL));
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              c_orm_delete_async(real_db, NULL, &row_in, NULL, NULL));
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              c_orm_delete_async(real_db, &meta, NULL, NULL, NULL));

    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              c_orm_execute_raw_async(NULL, "SELECT 1", NULL, NULL));
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              c_orm_execute_raw_async(real_db, NULL, NULL, NULL));

    meta.query_select_by_pk = "SELECT id FROM t_async WHERE id = ?";
    meta.query_update = "UPDATE t_async SET id = ? WHERE id = ?";
    meta.query_delete_by_pk = "DELETE FROM t_async WHERE id = ?";

    /* Custom executor modality dispatch + NULL submit validation */
    {
      int submits = 0;
      c_orm_async_executor_t exec;
      exec.submit = NULL;
      exec.user_data = &submits;
      ASSERT_EQ(C_ORM_OK,
                c_orm_set_modality(real_db, C_ORM_MODALITY_ASYNC, &exec));
      ASSERT_EQ(C_ORM_ERROR_VALIDATION,
                c_orm_execute_raw_async(real_db, "SELECT 1", NULL, NULL));

      exec.submit = test_executor_submit;
      row_in.id = 0;
      cb_state = 0;
      ASSERT_EQ(C_ORM_OK,
                c_orm_find_by_id_int32_async(real_db, &meta, 42, &row_in,
                                             test_async_cb, &cb_state));
      ASSERT_EQ(42, row_in.id);
      ASSERT_EQ(1, submits);

      ASSERT_EQ(C_ORM_OK,
                c_orm_update_async(real_db, &meta, &row_in, NULL, NULL));
      ASSERT_EQ(C_ORM_OK,
                c_orm_execute_raw_async(real_db, "SELECT 1", NULL, NULL));
      ASSERT_EQ(C_ORM_OK,
                c_orm_delete_async(real_db, &meta, &row_in, NULL, NULL));
      ASSERT_EQ(4, submits);
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_execute_raw_async(real_db, "INVALID SQL", NULL, NULL));

      {
        c_orm_malloc_fn_t orig_m = c_orm_malloc;
        ASSERT_EQ(C_ORM_OK, test_async_cb(C_ORM_OK, NULL));
        exec.user_data = NULL;
        ASSERT_EQ(C_ORM_OK,
                  c_orm_execute_raw_async(real_db, "SELECT 1", NULL, NULL));
        exec.user_data = &submits;
        ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(stubs_mock_malloc,
                                                 c_orm_realloc, c_orm_free));
        stubs_oom_fail = 0;
        ASSERT_EQ(C_ORM_OK,
                  c_orm_execute_raw_async(real_db, "SELECT 1", NULL, NULL));
        stubs_oom_fail = 1;
        ASSERT_EQ(C_ORM_ERROR_MEMORY,
                  c_orm_execute_raw_async(real_db, "SELECT 1", NULL, NULL));
        stubs_oom_fail = 0;
        ASSERT_EQ(C_ORM_OK,
                  c_orm_set_allocators(orig_m, c_orm_realloc, c_orm_free));
      }
    }

    /* THREAD_POOL modality without external executor (including mutex error
     * branches) */
    ASSERT_EQ(C_ORM_OK,
              c_orm_set_modality(real_db, C_ORM_MODALITY_THREAD_POOL, NULL));
    ASSERT_EQ(C_ORM_OK,
              c_orm_execute_raw_async(real_db, "SELECT 1", NULL, NULL));
    ASSERT_NEQ(C_ORM_OK, c_orm_execute_raw_async(real_db, "INVALID SQL SYNTAX",
                                                 NULL, NULL));

    c_orm_mock_cache_destroy_fail = 1;
    ASSERT_EQ(C_ORM_ERROR_BUSY,
              c_orm_set_modality(real_db, C_ORM_MODALITY_SYNC, NULL));
    c_orm_mock_cache_destroy_fail = 0;
    real_db->modality_lock = NULL;

    ASSERT_EQ(C_ORM_OK, c_orm_set_modality(real_db, C_ORM_MODALITY_SYNC, NULL));
    ASSERT_EQ(C_ORM_OK,
              c_orm_set_modality(real_db, C_ORM_MODALITY_THREAD_POOL, NULL));
    c_orm_mock_cache_init_fail = 1;
    ASSERT_EQ(C_ORM_ERROR_BUSY,
              c_orm_execute_raw_async(real_db, "SELECT 1", NULL, NULL));
    c_orm_mock_cache_init_fail = 0;

    c_orm_mock_cache_lock_fail = 1;
    ASSERT_EQ(C_ORM_ERROR_BUSY,
              c_orm_execute_raw_async(real_db, "SELECT 1", NULL, NULL));
    c_orm_mock_cache_lock_fail = 0;

    c_orm_mock_cache_unlock_fail = 1;
    ASSERT_EQ(C_ORM_ERROR_BUSY,
              c_orm_execute_raw_async(real_db, "SELECT 1", NULL, NULL));
    ASSERT_EQ(C_ORM_ERROR_BUSY, c_orm_execute_raw_async(
                                    real_db, "INVALID SQL SYNTAX", NULL, NULL));
    c_orm_mock_cache_unlock_fail = 0;

    ASSERT_EQ(C_ORM_OK, c_orm_set_modality(real_db, C_ORM_MODALITY_SYNC, NULL));
    ASSERT_EQ(C_ORM_OK, real_db->vtable->disconnect(real_db));
  }
#endif

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
  fprintf(f, "%s\n", "CREATE TABLE t_stubs (id INTEGER PRIMARY KEY);");
  fclose(f);
  schema_path = "test_stubs_schema.sql";

  rc = c_orm_codegen_generate(schema_path, "test_out");
  remove("test_stubs_schema.sql");

  ASSERT(rc != C_ORM_ERROR_NOT_IMPLEMENTED);

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
 * @brief Test SQL-lexer-aware PostgreSQL `?` placeholder rewriting and
 * `c_orm_mysql_parse_url` query-parameter parsing.
 * @return GREATEST test result.
 */
TEST test_driver_url_and_query_parsers(void) {
  int pcount = 0;
  char *rewritten = NULL;
  c_orm_mysql_url_info_t info;
  char long_buf[600];

  /* OOM on c_orm_postgres_rewrite_query */
  {
    c_orm_malloc_fn_t old_m = c_orm_malloc;
    c_orm_realloc_fn_t old_r = c_orm_realloc;
    c_orm_free_fn_t old_f = c_orm_free;
    c_orm_set_allocators(stubs_mock_malloc, old_r, old_f);
    stubs_oom_fail = 1;
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              c_orm_postgres_rewrite_query("SELECT ?", &pcount, &rewritten));
    stubs_oom_fail = 0;
    c_orm_set_allocators(old_m, old_r, old_f);
  }

  /* High-byte tag, unclosed quotes/comments/dollar-quotes, $10 then $2 then ?
   */
  ASSERT_EQ(C_ORM_OK,
            c_orm_postgres_rewrite_query(
                "SELECT $Tag\x80_9$body$Tag\x80_9$, $0bad$, $10, $2, ? "
                "-- eof comment without newline",
                &pcount, &rewritten));
  ASSERT_EQ(10, pcount);
  c_orm_system_free(rewritten);

  ASSERT_EQ(C_ORM_OK, c_orm_postgres_rewrite_query("SELECT 'unclosed", &pcount,
                                                   &rewritten));
  c_orm_system_free(rewritten);
  ASSERT_EQ(C_ORM_OK, c_orm_postgres_rewrite_query("SELECT \"unclosed", &pcount,
                                                   &rewritten));
  c_orm_system_free(rewritten);
  ASSERT_EQ(C_ORM_OK, c_orm_postgres_rewrite_query("SELECT /* unclosed",
                                                   &pcount, &rewritten));
  c_orm_system_free(rewritten);
  ASSERT_EQ(C_ORM_OK, c_orm_postgres_rewrite_query("SELECT $tag$unclosed",
                                                   &pcount, &rewritten));
  c_orm_system_free(rewritten);
  ASSERT_EQ(C_ORM_OK, c_orm_postgres_rewrite_query("SELECT $tag!rest", &pcount,
                                                   &rewritten));
  c_orm_system_free(rewritten);
  ASSERT_EQ(C_ORM_OK,
            c_orm_postgres_rewrite_query(
                "SELECT a - b, a / b, /* x / y * z */ $[$ $a:$ $unclosed",
                &pcount, &rewritten));
  c_orm_system_free(rewritten);

  /* Exhaustive c_orm_mysql_parse_url edge cases */
  ASSERT_EQ(C_ORM_OK, c_orm_mysql_parse_url("localhost:3306/db", &info));
  ASSERT_EQ(C_ORM_OK,
            c_orm_mysql_parse_url("mariadb://h/d?socket=/tmp/m.sock", &info));
  ASSERT_EQ(C_ORM_OK,
            c_orm_mysql_parse_url(
                "mysql://h/d?unix_sockXX=1&sockXX=1&ssX=1&ssl-modX=1&sslmodX=1"
                "&ssl=2&ssl=nope&ssl=NOT_REQD&ssl=not_req&ssl=falsy&a=1&",
                &info));
  ASSERT_EQ(C_ORM_OK, c_orm_mysql_parse_url("mysql://?ssl=true", &info));
  ASSERT_EQ(1, info.use_ssl);
  ASSERT_EQ(C_ORM_OK, c_orm_mysql_parse_url("mysql://h/d?ssl=TRUE", &info));
  ASSERT_EQ(1, info.use_ssl);
  ASSERT_EQ(C_ORM_OK, c_orm_mysql_parse_url("mysql://h/d?ssl=require", &info));
  ASSERT_EQ(1, info.use_ssl);
  ASSERT_EQ(C_ORM_OK,
            c_orm_mysql_parse_url("mysql://h/d?ssl=0&other=1", &info));
  ASSERT_EQ(0, info.use_ssl);
  ASSERT_EQ(C_ORM_OK,
            c_orm_mysql_parse_url("mysql://h/d?ssl=UNKNOWN_MODE", &info));
  ASSERT_EQ(C_ORM_OK, c_orm_mysql_parse_url("mysql://h/d?", &info));
  ASSERT_EQ(C_ORM_OK, c_orm_mysql_parse_url("mysql:///onlydb", &info));
  ASSERT_STR_EQ("onlydb", info.dbname);
  ASSERT_EQ(C_ORM_OK, c_orm_mysql_parse_url("mysql://host:/", &info));
  ASSERT_EQ(C_ORM_OK, c_orm_mysql_parse_url(
                          "mysql://host:12345678901234567890/db", &info));
  ASSERT_EQ(C_ORM_OK, c_orm_mysql_parse_url("mysql://host:", &info));
  ASSERT_EQ(C_ORM_OK,
            c_orm_mysql_parse_url("mysql://host:12345678901234567890", &info));
  ASSERT_EQ(C_ORM_OK, c_orm_mysql_parse_url("mysql://u:p@extra@h/d", &info));
  ASSERT_EQ(C_ORM_OK, c_orm_mysql_parse_url("mysql://useronly@", &info));

  /* Overlong credential, host, dbname, and unix_socket truncation guards */
  memset(long_buf, 'x', sizeof(long_buf) - 1);
  long_buf[sizeof(long_buf) - 1] = '\0';
  {
    char url_buf[4096];
    C_ORM_SPRINTF(url_buf, sizeof(url_buf),
                  "mysql://%s:%s@%s:3306/%s?socket=%s", long_buf, long_buf,
                  long_buf, long_buf, long_buf);
    ASSERT_EQ(C_ORM_OK, c_orm_mysql_parse_url(url_buf, &info));

    C_ORM_SPRINTF(url_buf, sizeof(url_buf), "mysql://%s@%s/%s", long_buf,
                  long_buf, long_buf);
    ASSERT_EQ(C_ORM_OK, c_orm_mysql_parse_url(url_buf, &info));

    C_ORM_SPRINTF(url_buf, sizeof(url_buf), "mysql://%s:3306", long_buf);
    ASSERT_EQ(C_ORM_OK, c_orm_mysql_parse_url(url_buf, &info));

    C_ORM_SPRINTF(url_buf, sizeof(url_buf), "mysql://%s", long_buf);
    ASSERT_EQ(C_ORM_OK, c_orm_mysql_parse_url(url_buf, &info));

    C_ORM_SPRINTF(url_buf, sizeof(url_buf), "mysql://h/d?unix_socket=%s",
                  long_buf);
    ASSERT_EQ(C_ORM_OK, c_orm_mysql_parse_url(url_buf, &info));

    C_ORM_SPRINTF(url_buf, sizeof(url_buf), "mysql://%s:%s@h/d", long_buf,
                  long_buf);
    ASSERT_EQ(C_ORM_OK, c_orm_mysql_parse_url(url_buf, &info));
  }

  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_postgres_rewrite_query(NULL, &pcount, &rewritten));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_postgres_rewrite_query("SELECT 1", NULL, &rewritten));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_postgres_rewrite_query("SELECT 1", &pcount, NULL));

  /* Quotes, escaped quotes, comments, dollar-quotes, and ?? JSONB operator */
  ASSERT_EQ(C_ORM_OK,
            c_orm_postgres_rewrite_query(
                "SELECT 'really? it''s?' AS a, \"col?\"\"x?\" AS b, "
                "$$dol?lar$$ AS c, $tag_1$x?$y$?z$tag_1$ AS d, $bad!tag$, "
                "payload ?? ? -- line ? comment\n"
                "/* outer /* inner ? */ ? */ FROM t WHERE id = ? AND x = $4",
                &pcount, &rewritten));
  ASSERT_EQ(4, pcount);
  ASSERT(strstr(rewritten, "'really? it''s?'") != NULL);
  ASSERT(strstr(rewritten, "\"col?\"\"x?\"") != NULL);
  ASSERT(strstr(rewritten, "$$dol?lar$$") != NULL);
  ASSERT(strstr(rewritten, "$tag_1$x?$y$?z$tag_1$") != NULL);
  ASSERT(strstr(rewritten, "payload ? $1") != NULL);
  ASSERT(strstr(rewritten, "WHERE id = $2 AND x = $4") != NULL);
  c_orm_system_free(rewritten);
  rewritten = NULL;

  /* Unterminated dollar tag fallback */
  ASSERT_EQ(C_ORM_OK, c_orm_postgres_rewrite_query("SELECT $unclosed, ?",
                                                   &pcount, &rewritten));
  ASSERT_EQ(1, pcount);
  ASSERT_STR_EQ("SELECT $unclosed, $1", rewritten);
  c_orm_system_free(rewritten);

  /* MySQL URL parser */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_mysql_parse_url(NULL, &info));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_mysql_parse_url("mysql://localhost", NULL));

  ASSERT_EQ(C_ORM_OK, c_orm_mysql_parse_url(
                          "mysql://alice:s3cr3t@db.internal:3307/analytics"
                          "?unix_socket=/var/run/mysqld/mysqld.sock&ssl=1",
                          &info));
  ASSERT_STR_EQ("alice", info.user);
  ASSERT_STR_EQ("s3cr3t", info.password);
  ASSERT_STR_EQ("db.internal", info.host);
  ASSERT_EQ(3307U, info.port);
  ASSERT_STR_EQ("analytics", info.dbname);
  ASSERT_STR_EQ("/var/run/mysqld/mysqld.sock", info.unix_socket);
  ASSERT_EQ(1, info.use_ssl);

  ASSERT_EQ(
      C_ORM_OK,
      c_orm_mysql_parse_url(
          "mariadb://bob@localhost/app?socket=/tmp/my.sock&ssl=false", &info));
  ASSERT_STR_EQ("bob", info.user);
  ASSERT_STR_EQ("", info.password);
  ASSERT_STR_EQ("localhost", info.host);
  ASSERT_STR_EQ("app", info.dbname);
  ASSERT_STR_EQ("/tmp/my.sock", info.unix_socket);
  ASSERT_EQ(0, info.use_ssl);

  ASSERT_EQ(C_ORM_OK, c_orm_mysql_parse_url(
                          "mysql://10.0.0.5:3308?ssl-mode=REQUIRED", &info));
  ASSERT_STR_EQ("10.0.0.5", info.host);
  ASSERT_EQ(3308U, info.port);
  ASSERT_EQ(1, info.use_ssl);

  ASSERT_EQ(C_ORM_OK, c_orm_mysql_parse_url(
                          "mysql://plainhost?sslmode=DISABLED&flag", &info));
  ASSERT_STR_EQ("plainhost", info.host);
  ASSERT_EQ(0U, info.port);
  ASSERT_EQ(0, info.use_ssl);

  PASS();
}

/**
 * @brief Database stubs and hooks test suite runner.
 * @param db_stubs_suite Suite runner function name.
 */
SUITE(db_stubs_suite) {
  static int recursed = 0;
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
  RUN_TEST(test_driver_url_and_query_parsers);
  if (!recursed) {
    recursed = 1;
    greatest_set_test_filter("never_match_filter");
    db_stubs_suite();
    greatest_set_test_filter(NULL);
    recursed = 0;
  }
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
