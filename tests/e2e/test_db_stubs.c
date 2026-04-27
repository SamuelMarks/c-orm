/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_db.h"
#include "c_orm_mysql.h"
#include "c_orm_postgres.h"
#include "greatest.h"
#include <stdio.h>
#include <string.h>

TEST test_postgres_stubs_edge_cases(void) {
  c_orm_db_t *db = NULL;
  const c_orm_driver_vtable_t *vt = NULL;
  c_orm_error_t err;
  unsigned int oid;
  void *fd = NULL;
  size_t read_len = 0;
  size_t written_len = 0;

  err = c_orm_postgres_connect("fake", &db);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);

  err = c_orm_postgres_get_vtable(&vt);
  ASSERT_EQ(1, err);
  ASSERT(vt == NULL);

  err = c_orm_postgres_get_vtable(NULL);
  ASSERT_EQ(1, err);

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

TEST test_mysql_stubs_edge_cases(void) {
  c_orm_db_t *db = NULL;
  const c_orm_driver_vtable_t *vt = NULL;
  c_orm_error_t err;

  err = c_orm_mysql_connect("fake", &db);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);

  err = c_orm_mysql_get_vtable(&vt);
  ASSERT_EQ(1, err);
  ASSERT(vt == NULL);

  err = c_orm_mysql_get_vtable(NULL);
  ASSERT_EQ(1, err);

  PASS();
}

static void test_hook(c_orm_db_t *db, const char *sql, void *user_data) { (void)db; (void)sql; (void)user_data; }
static c_orm_error_t crypto_enc_hook(const void *in, size_t in_len, void *ctx,
                                     void **out, size_t *out_len) { (void)in; (void)in_len; (void)ctx; (void)out; (void)out_len; 
  return C_ORM_OK;
}
static c_orm_error_t crypto_dec_hook(const void *in, size_t in_len, void *ctx,
                                     void **out, size_t *out_len) { (void)in; (void)in_len; (void)ctx; (void)out; (void)out_len; 
  return C_ORM_OK;
}
static void test_log_cb(const char *msg, void *user_data) { (void)msg; (void)user_data; }
static void test_expire_cb(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                           void *obj, void *user_data) { (void)db; (void)meta; (void)obj; (void)user_data; }

static int get_last_err_mock(c_orm_db_t *db, const char **out) { (void)db; (void)out; 
  *out = "mock";
  return 0;
}
static int get_last_trace_mock(c_orm_db_t *db, const char **out) { (void)db; (void)out; 
  *out = "trace";
  return 0;
}

C_ORM_EXPORT c_orm_error_t
c_orm_db_attach_identity_map(c_orm_db_t *db, c_orm_identity_map_t *map);
C_ORM_EXPORT void c_orm_register_query_interceptor(c_orm_db_t *db,
                                                   c_orm_interceptor_cb hook,
                                                   void *context);
C_ORM_EXPORT void
c_orm_register_hydration_interceptor(c_orm_db_t *db, c_orm_interceptor_cb hook,
                                     void *context);

TEST test_c_orm_db_coverage(void) {
  c_orm_db_t db;
  c_orm_driver_vtable_t vt;
  const char *msg = NULL;
  int rc;
  c_orm_pool_telemetry_t tel;
  c_orm_timezone_t tz;

  memset(&db, 0, sizeof(db));
  memset(&vt, 0, sizeof(vt));
  memset(&tel, 0, sizeof(tel));
  memset(&tz, 0, sizeof(tz));

  /* c_orm_get_last_error_message */
  rc = c_orm_get_last_error_message(NULL, NULL);
  ASSERT_EQ(1, rc);

  rc = c_orm_get_last_error_message(NULL, &msg);
  ASSERT_EQ(0, rc);
  ASSERT_STR_EQ("Unknown Error (No DB context)", msg);

  rc = c_orm_get_last_error_message(&db, &msg);
  ASSERT_EQ(0, rc);

  db.vtable = &vt;
  rc = c_orm_get_last_error_message(&db, &msg);
  ASSERT_EQ(0, rc);

  vt.get_last_error = get_last_err_mock;
  rc = c_orm_get_last_error_message(&db, &msg);
  ASSERT_EQ(0, rc);
  ASSERT_STR_EQ("mock", msg);

  /* c_orm_get_last_error_trace */
  rc = c_orm_get_last_error_trace(NULL, NULL);
  ASSERT_EQ(1, rc);

  rc = c_orm_get_last_error_trace(NULL, &msg);
  ASSERT_EQ(1, rc);

  db.vtable = NULL;
  rc = c_orm_get_last_error_trace(&db, &msg);
  ASSERT_EQ(1, rc);

  db.vtable = &vt;
  rc = c_orm_get_last_error_trace(&db, &msg);
  ASSERT_EQ(1, rc);

  vt.get_last_trace = get_last_trace_mock;
  rc = c_orm_get_last_error_trace(&db, &msg);
  ASSERT_EQ(0, rc);
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
  if (rc != C_ORM_OK) {
    printf("CODEGEN ERROR: %d\n", rc);
  }
  ASSERT_EQ(C_ORM_OK, rc);

  c_orm_set_expire_callback(NULL, NULL, NULL);
  c_orm_set_expire_callback(&db, test_expire_cb, NULL);

  rc = c_orm_db_attach_identity_map(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = c_orm_db_attach_identity_map(&db, NULL);
  if (rc != C_ORM_OK) {
    printf("CODEGEN ERROR: %d\n", rc);
  }
  ASSERT_EQ(C_ORM_OK, rc);

  c_orm_register_query_interceptor(NULL, NULL, NULL);
  c_orm_register_query_interceptor(&db, test_hook, NULL);

  c_orm_register_hydration_interceptor(NULL, NULL, NULL);
  c_orm_register_hydration_interceptor(&db, test_hook, NULL);

  c_orm_register_crypto_hooks(NULL, NULL, NULL, NULL);
  c_orm_register_crypto_hooks(&db, crypto_enc_hook, crypto_dec_hook, NULL);

  c_orm_set_timezone(NULL, tz);
  c_orm_set_timezone(&db, tz);

  PASS();
}

extern c_orm_error_t c_orm_insert_async(c_orm_db_t *db,
                                        const c_orm_table_meta_t *meta,
                                        const void *in_struct,
                                        void (*cb)(c_orm_error_t, void *),
                                        void *ctx);
extern c_orm_error_t c_orm_find_all_async(c_orm_db_t *db,
                                          const c_orm_table_meta_t *meta,
                                          void *out_array,
                                          void (*cb)(c_orm_error_t, void *),
                                          void *ctx);

static void async_cb(c_orm_error_t err, void *ctx) {
  (void)err;
  (void)ctx;
}

TEST test_c_orm_async_coverage(void) {
  c_orm_db_t db;
  c_orm_table_meta_t meta;
  int obj;
  int rc;

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

#include "c_orm_codegen.h"
/* clang-format on */

TEST test_codegen_coverage(void) {
  int rc;
  const char *schema_path;
  FILE *f;

  rc = c_orm_codegen_generate(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  rc = c_orm_codegen_generate("fake.sql", NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  rc = c_orm_codegen_generate("fake.sql", "fake_dir");
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  schema_path = "tests/e2e/schema.sql";
  f = fopen(schema_path, "r");
  if (!f) {
    schema_path = "../tests/e2e/schema.sql";
    f = fopen(schema_path, "r");
    if (!f) {
      schema_path = "../../tests/e2e/schema.sql";
      f = fopen(schema_path, "r");
      if (!f) {
        schema_path = "../../../tests/e2e/schema.sql";
      } else {
        fclose(f);
      }
    } else {
      fclose(f);
    }
  } else {
    fclose(f);
  }
  rc = c_orm_codegen_generate(schema_path, ".");
  if (rc != C_ORM_OK) {
    printf("CODEGEN ERROR: %d\n", rc);
  }
  ASSERT_EQ(C_ORM_OK, rc);

  PASS();
}

TEST test_modality_coverage(void) {
  c_orm_db_t db;
  int rc;

  rc = c_orm_set_modality(NULL, 0, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_set_modality(&db, 1, (void *)0x123);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(1, db.modality);
  ASSERT_EQ((void *)0x123, db.modality_ctx);

  PASS();
}

SUITE(db_stubs_suite) {
  RUN_TEST(test_postgres_stubs_edge_cases);
  RUN_TEST(test_mysql_stubs_edge_cases);
  RUN_TEST(test_c_orm_db_coverage);
  RUN_TEST(test_c_orm_async_coverage);
  RUN_TEST(test_codegen_coverage);
  RUN_TEST(test_modality_coverage);
}
