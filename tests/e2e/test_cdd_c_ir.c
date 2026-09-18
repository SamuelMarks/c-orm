#if defined(__clang__) || defined(__GNUC__)
#endif
/* clang-format off */
#include "cdd_c_ir.h"
#include "c_orm_safe_crt.h"
#include <greatest.h>
#include <errno.h>
#include "test_cdd_c_ir_oom.h"
/* clang-format on */

TEST test_cdd_c_ir_basic(void) {
  cdd_c_ir_t ir;
  struct sql_table_t tbl;
  cdd_c_query_projection_t proj;
  c_orm_error_t rc;

  memset(&tbl, 0, sizeof(tbl));

  rc = cdd_c_ir_init(NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  rc = cdd_c_ir_init(&ir);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = cdd_c_ir_add_table(NULL, &tbl);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  rc = cdd_c_ir_add_table(&ir, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  rc = cdd_c_ir_add_table(&ir, &tbl);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = cdd_c_query_projection_init(&proj);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = cdd_c_ir_add_projection(NULL, &proj);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  rc = cdd_c_ir_add_projection(&ir, NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  rc = cdd_c_ir_add_projection(&ir, &proj);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = cdd_c_ir_free(NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  rc = cdd_c_ir_free(&ir);
  ASSERT_EQ(C_ORM_OK, rc);
  cdd_c_query_projection_free(&proj);

  PASS();
}

TEST test_cdd_c_ir_parse_sql(void) {
  cdd_c_ir_t ir;
  c_orm_error_t rc;

  rc = cdd_c_ir_init(&ir);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = parse_sql_into_ir(NULL, &ir);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  rc = parse_sql_into_ir("invalid", NULL);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  /* basic */
  rc = parse_sql_into_ir("CREATE TABLE x (id INT);", &ir);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(1, ir.n_tables);

  rc = cdd_c_ir_free(&ir);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = cdd_c_ir_init(&ir);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = parse_sql_into_ir("SELECT id FROM x;", &ir);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = cdd_c_ir_free(&ir);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = cdd_c_ir_init(&ir);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = parse_sql_into_ir("INSERT INTO x (id) VALUES (1) RETURNING id;", &ir);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = cdd_c_ir_free(&ir);
  ASSERT_EQ(C_ORM_OK, rc);
  PASS();
}

TEST test_cdd_c_ir_projection(void) {
  cdd_c_ir_t ir;
  cdd_c_query_projection_t proj;
  c_orm_error_t rc;

  rc = cdd_c_query_projection_init(&proj);
  ASSERT_EQ(C_ORM_OK, rc);
  proj.source_table = "test";
  proj.mapping_meta.target_name = "test_map";

  rc = cdd_c_ir_init(&ir);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = cdd_c_ir_add_projection(&ir, &proj);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = cdd_c_ir_free(&ir);
  ASSERT_EQ(C_ORM_OK, rc);
  PASS();
}

TEST test_cdd_c_ir_alloc(void) {
  cdd_c_ir_t ir;
  struct sql_table_t tbl;
  cdd_c_query_projection_t proj;
  c_orm_error_t rc;
  int i;

  memset(&tbl, 0, sizeof(tbl));

  rc = cdd_c_ir_init(&ir);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = cdd_c_query_projection_init(&proj);
  ASSERT_EQ(C_ORM_OK, rc);

  for (i = 0; i < 6; i++) {
    rc = cdd_c_ir_add_table(&ir, &tbl);
    ASSERT_EQ(C_ORM_OK, rc);
    rc = cdd_c_ir_add_projection(&ir, &proj);
    ASSERT_EQ(C_ORM_OK, rc);
  }

  ASSERT_EQ(6, ir.n_tables);
  ASSERT_EQ(6, ir.n_projections);

  rc = cdd_c_ir_free(&ir);
  ASSERT_EQ(C_ORM_OK, rc);
  PASS();
}

TEST test_cdd_c_ir_parse_sql_failure(void) {
  cdd_c_ir_t ir;
  c_orm_error_t rc;

  rc = cdd_c_ir_init(&ir);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = parse_sql_into_ir("select * from not_create_table;", &ir);
  ASSERT_EQ(C_ORM_OK, rc);

  /* parser failure */
  rc = parse_sql_into_ir("CREATE TABLE x (id);", &ir);
  ASSERT(rc != C_ORM_OK);

  rc = cdd_c_ir_free(&ir);
  ASSERT_EQ(C_ORM_OK, rc);
  PASS();
}

SUITE(cdd_c_ir_suite) {
  RUN_TEST(test_cdd_c_ir_basic);
  RUN_TEST(test_cdd_c_ir_parse_sql);
  RUN_TEST(test_cdd_c_ir_projection);
  RUN_TEST(test_cdd_c_ir_alloc);
  RUN_TEST(test_cdd_c_ir_parse_sql_failure);
  RUN_TEST(test_cdd_c_ir_oom);
}

#if defined(__clang__) || defined(__GNUC__)
#endif
