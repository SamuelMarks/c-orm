/* clang-format off */
#include "cdd_c_ir.h"
#include "c_orm_safe_crt.h"
#include <greatest.h>
#include <errno.h>
/* clang-format on */

TEST test_cdd_c_ir_basic(void) {
  cdd_c_ir_t ir;
  struct sql_table_t tbl;
  cdd_c_query_projection_t proj;

  memset(&tbl, 0, sizeof(tbl));

  ASSERT_EQ(-1, (int)cdd_c_ir_init(NULL));
  ASSERT_EQ(0, (int)cdd_c_ir_init(&ir));

  ASSERT_EQ(-1, (int)cdd_c_ir_add_table(NULL, &tbl));
  ASSERT_EQ(-1, (int)cdd_c_ir_add_table(&ir, NULL));
  ASSERT_EQ(0, (int)cdd_c_ir_add_table(&ir, &tbl));

  ASSERT_EQ(0, (int)cdd_c_query_projection_init(&proj));
  ASSERT_EQ(-1, (int)cdd_c_ir_add_projection(NULL, &proj));
  ASSERT_EQ(-1, (int)cdd_c_ir_add_projection(&ir, NULL));
  ASSERT_EQ(0, (int)cdd_c_ir_add_projection(&ir, &proj));

  ASSERT_EQ(-1, (int)cdd_c_ir_free(NULL));
  ASSERT_EQ(0, (int)cdd_c_ir_free(&ir));
  cdd_c_query_projection_free(&proj);

  PASS();
}

TEST test_cdd_c_ir_parse_sql(void) {
  cdd_c_ir_t ir;
  cdd_c_ir_init(&ir);

  ASSERT_EQ(-1, (int)parse_sql_into_ir(NULL, &ir));
  ASSERT_EQ(-1, (int)parse_sql_into_ir("invalid", NULL));

  /* basic */
  ASSERT_EQ(0, (int)parse_sql_into_ir("CREATE TABLE x (id INT);", &ir));
  ASSERT_EQ(1, ir.n_tables);

  cdd_c_ir_free(&ir);
  cdd_c_ir_init(&ir);
  parse_sql_into_ir("SELECT id FROM x;", &ir);

  cdd_c_ir_free(&ir);
  cdd_c_ir_init(&ir);
  parse_sql_into_ir("INSERT INTO x (id) VALUES (1) RETURNING id;", &ir);

  cdd_c_ir_free(&ir);
  PASS();
}

TEST test_cdd_c_ir_projection(void) {
  cdd_c_ir_t ir;
  cdd_c_query_projection_t proj;
  cdd_c_query_projection_init(&proj);
  proj.source_table = "test";
  proj.mapping_meta.target_name = "test_map";

  cdd_c_ir_init(&ir);
  ASSERT_EQ(0, (int)cdd_c_ir_add_projection(&ir, &proj));

  cdd_c_ir_free(&ir);
  PASS();
}

TEST test_cdd_c_ir_alloc(void) {
  cdd_c_ir_t ir;
  struct sql_table_t tbl;
  cdd_c_query_projection_t proj;
  int i;

  memset(&tbl, 0, sizeof(tbl));

  cdd_c_ir_init(&ir);
  cdd_c_query_projection_init(&proj);

  for (i = 0; i < 6; i++) {
    cdd_c_ir_add_table(&ir, &tbl);
    cdd_c_ir_add_projection(&ir, &proj);
  }

  ASSERT_EQ(6, ir.n_tables);
  ASSERT_EQ(6, ir.n_projections);

  cdd_c_ir_free(&ir);
  PASS();
}

TEST test_cdd_c_ir_parse_sql_failure(void) {
  cdd_c_ir_t ir;
  cdd_c_ir_init(&ir);

  ASSERT_EQ(0, (int)parse_sql_into_ir("select * from not_create_table;", &ir));

  cdd_c_ir_free(&ir);
  PASS();
}

SUITE(cdd_c_ir_suite) {
  RUN_TEST(test_cdd_c_ir_basic);
  RUN_TEST(test_cdd_c_ir_parse_sql);
  RUN_TEST(test_cdd_c_ir_projection);
  RUN_TEST(test_cdd_c_ir_alloc);
  RUN_TEST(test_cdd_c_ir_parse_sql_failure);
}
