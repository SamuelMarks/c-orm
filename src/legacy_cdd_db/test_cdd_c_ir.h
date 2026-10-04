#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_cdd_c_ir.h
 * @brief Unit tests for CDD C IR construction, SQL parsing, and projections.
 */

#ifndef TEST_CDD_C_IR_H
#define TEST_CDD_C_IR_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "cdd_c_ir.h"
#include "c_orm_safe_crt.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_cdd_c_ir_oom.h"
/* clang-format on */

/**
 * @brief Tests basic CDD C IR initialization, table addition, projection, and
 * cleanup.
 * @return GREATEST test result.
 */
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

/**
 * @brief Tests parsing various SQL statements (DDL, SELECT, INSERT) into IR.
 * @return GREATEST test result.
 */
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

/**
 * @brief Tests CDD C IR projection initialization and assignment.
 * @return GREATEST test result.
 */
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

/**
 * @brief Tests multiple allocations of tables and projections in IR.
 * @return GREATEST test result.
 */
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

/**
 * @brief Tests SQL parse failure handling in IR generator.
 * @return GREATEST test result.
 */
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

/**
 * @brief CDD C IR test suite runner.
 * @param cdd_c_ir_suite Suite runner function name.
 */
SUITE(cdd_c_ir_suite) {
  static int recursed = 0;
  RUN_TEST(test_cdd_c_ir_basic);
  RUN_TEST(test_cdd_c_ir_parse_sql);
  RUN_TEST(test_cdd_c_ir_projection);
  RUN_TEST(test_cdd_c_ir_alloc);
  RUN_TEST(test_cdd_c_ir_parse_sql_failure);
  RUN_TEST(test_cdd_c_ir_oom);
  if (!recursed) {
    recursed = 1;
    greatest_set_test_filter("never_match_filter");
    cdd_c_ir_suite();
    greatest_set_test_filter(NULL);
    recursed = 0;
  }
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* TEST_CDD_C_IR_H */

#if defined(__clang__) || defined(__GNUC__)
#endif
