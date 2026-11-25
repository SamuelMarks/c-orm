/**
 * @file test_ast.c
 * @brief Unit tests for C-ORM AST generation, fluent chaining, and depth
 * limits.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_api.h"
#include "c_orm_ast.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include "test_assert_zero_branch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Tests AST fluent querying, node chaining, cloning, and literal
 * escaping.
 * @return GREATEST test result.
 */
TEST test_c_orm_ast_fluent(void) {
  c_orm_ast_query_t *q = NULL;
  c_orm_ast_query_t *q_clone = NULL;
  c_orm_error_t err;
  c_orm_ast_node_t *n1 = NULL;
  c_orm_ast_node_t *n2 = NULL;
  c_orm_ast_node_t *n3 = NULL;

  err = c_orm_query_new(&q);
  ASSERT_EQ_FMT(0, err, "%d");
  ASSERT(q != NULL);

  /* Test fluent API chaining and macros */
  q->select_(q, "id, name, email");
  q->from(q, "users");
  q->order_by(q, "created_at", 1);
  q->limit(q, 10);
  q->offset(q, 20);

  C_ORM_EQ_NUM("id", "1", &n1);
  C_ORM_WHERE(n1);
  C_ORM_GT_NUM("age", "18", &n2);
  C_ORM_AND_WHERE(n2);
  C_ORM_EQ("status", "active", &n3);
  C_ORM_OR_WHERE(n3);

  ASSERT_EQ_FMT(0, q->error, "%d");

  /* Verify the WHERE node and its operations */
  ASSERT(q->ast_head != NULL);
  ASSERT_EQ_FMT((int)C_ORM_AST_NODE_WHERE, (int)q->ast_head->type, "%d");
  {
    c_orm_ast_where_t *w = (c_orm_ast_where_t *)q->ast_head;
    c_orm_ast_operator_t *op_or = (c_orm_ast_operator_t *)w->condition;
    c_orm_ast_operator_t *op_and;
    c_orm_ast_operator_t *op_eq_status;

    ASSERT_EQ_FMT((int)C_ORM_AST_NODE_OPERATOR, (int)op_or->base.type, "%d");
    ASSERT_STR_EQ("OR", op_or->op);

    op_and = (c_orm_ast_operator_t *)op_or->left;
    ASSERT_EQ_FMT((int)C_ORM_AST_NODE_OPERATOR, (int)op_and->base.type, "%d");
    ASSERT_STR_EQ("AND", op_and->op);

    op_eq_status = (c_orm_ast_operator_t *)op_or->right;
    ASSERT_EQ_FMT((int)C_ORM_AST_NODE_OPERATOR, (int)op_eq_status->base.type,
                  "%d");
    ASSERT_STR_EQ("=", op_eq_status->op);
  }

  /* Test cloning */
  err = q->clone(q, &q_clone);
  ASSERT_EQ_FMT(0, err, "%d");
  ASSERT(q_clone != NULL);
  ASSERT(q_clone->arena != q->arena); /* Must be deep copied into new arena */
  ASSERT(q_clone->ast_head != NULL);
  ASSERT_EQ_FMT((int)C_ORM_AST_NODE_WHERE, (int)q_clone->ast_head->type, "%d");

  /* Test literal escaping */
  {
    c_orm_ast_node_t *lit = NULL;
    q->lit(q, "user\x27s name", 1, &lit);
    ASSERT(lit != NULL);
    ASSERT_STR_EQ("user\x27\x27s name", ((c_orm_ast_literal_t *)lit)->value);
  }

  c_orm_query_free(q);
  c_orm_query_free(q_clone);
  PASS();
}

/**
 * @brief Tests AST to SQL serialization for SQLite dialect.
 * @return GREATEST test result.
 */
TEST test_c_orm_ast_to_sql(void) {
  c_orm_ast_query_t *q = NULL;
  char *sql = NULL;
  c_orm_query_params_t params;
  c_orm_error_t err;
  c_orm_ast_node_t *eq_n = NULL;

  err = c_orm_query_new(&q);
  ASSERT_EQ_FMT(0, err, "%d");

  q->select_(q, "id, name");
  q->from(q, "users");
  q->eq(q, "status", "active", 1, &eq_n);
  q->where(q, eq_n);
  q->order_by(q, "id", 0);
  q->limit(q, 10);

  c_orm_query_params_init(&params);
  err = c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &params);
  ASSERT_EQ_FMT(0, err, "%d");
  ASSERT_STR_EQ(
      "SELECT id, name FROM users WHERE status = ? ORDER BY id ASC LIMIT 10",
      sql);
  ASSERT_EQ_FMT(1, (int)params.count, "%d");
  ASSERT_STR_EQ("active", params.params[0].value);
  ASSERT_EQ_FMT(1, params.params[0].is_string, "%d");

  c_orm_system_free(sql);
  c_orm_query_params_cleanup(&params);
  c_orm_query_free(q);
  PASS();
}

/**
 * @brief Tests AST to SQL serialization for PostgreSQL dialect.
 * @return GREATEST test result.
 */
TEST test_c_orm_ast_to_sql_postgres(void) {
  c_orm_ast_query_t *q = NULL;
  char *sql = NULL;
  c_orm_query_params_t params;
  c_orm_error_t err;
  c_orm_ast_node_t *eq_n = NULL;
  c_orm_ast_node_t *gt_n = NULL;

  err = c_orm_query_new(&q);
  ASSERT_EQ_FMT(0, err, "%d");

  q->select_(q, "*");
  q->from(q, "orders");
  q->eq(q, "user_id", "42", 0, &eq_n);
  q->where(q, eq_n);
  q->gt(q, "amount", "100", 0, &gt_n);
  q->and_where(q, gt_n);

  c_orm_query_params_init(&params);
  err = c_orm_query_to_sql(q, C_ORM_DIALECT_POSTGRES, &sql, &params);
  ASSERT_EQ_FMT(0, err, "%d");
  ASSERT_STR_EQ("SELECT * FROM orders WHERE user_id = $1 AND amount > $2", sql);
  ASSERT_EQ_FMT(2, (int)params.count, "%d");
  ASSERT_STR_EQ("42", params.params[0].value);
  ASSERT_EQ_FMT(0, params.params[0].is_string, "%d");
  ASSERT_STR_EQ("100", params.params[1].value);
  ASSERT_EQ_FMT(0, params.params[1].is_string, "%d");

  c_orm_system_free(sql);
  c_orm_query_params_cleanup(&params);
  c_orm_query_free(q);
  PASS();
}

/**
 * @brief Tests AST recursion depth limiting and depth threshold adjustment.
 * @return GREATEST test result.
 */
TEST test_c_orm_ast_depth_limit(void) {
  c_orm_ast_query_t *q = NULL;
  char *sql = NULL;
  c_orm_query_params_t params;
  c_orm_error_t err;
  int i;
  unsigned int old_depth = cdd_c_sql_parser_max_depth;

  err = c_orm_query_new(&q);
  ASSERT_EQ_FMT(0, err, "%d");

  q->select_(q, "*");
  q->from(q, "users");

  for (i = 0; i < 110; i++) {
    c_orm_ast_node_t *eq_n = NULL;
    q->eq(q, "id", "1", 0, &eq_n);
    q->and_where(q, eq_n);
  }

  c_orm_query_params_init(&params);

  /* Should fail due to max depth = 100 */
  cdd_c_sql_parser_max_depth = 100;
  err = c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &params);
  ASSERT(err != C_ORM_OK);

  /* Should succeed if max depth is increased */
  cdd_c_sql_parser_max_depth = 200;
  err = c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &params);
  ASSERT_EQ_FMT(0, err, "%d");

  cdd_c_sql_parser_max_depth = old_depth;
  c_orm_system_free(sql);
  c_orm_query_params_cleanup(&params);
  c_orm_query_free(q);
  PASS();
}

/**
 * @brief Tests multiple CTEs, >32 JOINs, multiple GROUP BY, chained UNIONs,
 * and multiple ORDER BY clauses via `c_orm_query_new` /
 * `c_orm_query_free`.
 * @return GREATEST test result.
 */
TEST test_c_orm_ast_multi_clauses(void) {
  c_orm_ast_query_t *q = NULL;
  c_orm_ast_query_t *cte1 = NULL;
  c_orm_ast_query_t *cte2 = NULL;
  c_orm_ast_query_t *u1 = NULL;
  c_orm_ast_query_t *u2 = NULL;
  c_orm_ast_node_t *on_cond = NULL;
  char *sql = NULL;
  c_orm_query_params_t params;
  int j;

  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&cte1));
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&cte2));
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&u1));
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&u2));

  cte1->select_(cte1, "1 AS a");
  cte2->select_(cte2, "2 AS b");
  u1->select_(u1, "id");
  u1->from(u1, "archived_users");
  u2->select_(u2, "id");
  u2->from(u2, "guest_users");

  q->with(q, "c1", cte1);
  q->with(q, "c2", cte2);
  q->select_(q, "u.id");
  q->from_alias(q, "users", "u");

  for (j = 0; j < 36; ++j) {
    char tbl[32];
    C_ORM_SPRINTF(tbl, sizeof(tbl), "t%d", j);
    q->raw(q, "1=1", &on_cond);
    q->join(q, tbl, "INNER", on_cond);
  }

  q->group_by(q, "u.org_id");
  q->group_by(q, "u.dept_id");
  q->union_(q, u1, 0);
  q->union_(q, u2, 1);
  q->order_by(q, "u.id", 0);
  q->order_by(q, "u.created_at", 1);

  ASSERT_EQ(C_ORM_OK, c_orm_query_params_init(&params));
  ASSERT_EQ(C_ORM_OK,
            c_orm_query_to_sql(q, C_ORM_DIALECT_POSTGRES, &sql, &params));
  ASSERT(strstr(sql, "WITH c1 AS (SELECT 1 AS a), c2 AS (SELECT 2 AS b) ") !=
         NULL);
  ASSERT(strstr(sql, "INNER JOIN t0 ON 1=1") != NULL);
  ASSERT(strstr(sql, "INNER JOIN t35 ON 1=1") != NULL);
  ASSERT(strstr(sql, "GROUP BY u.org_id, u.dept_id") != NULL);
  ASSERT(strstr(sql,
                "UNION SELECT id FROM archived_users UNION ALL SELECT id FROM "
                "guest_users") != NULL);
  ASSERT(strstr(sql, "ORDER BY u.id ASC, u.created_at DESC") != NULL);

  c_orm_system_free(sql);
  c_orm_query_params_cleanup(&params);
  ASSERT_EQ(C_ORM_OK, c_orm_query_free(u2));
  ASSERT_EQ(C_ORM_OK, c_orm_query_free(u1));
  ASSERT_EQ(C_ORM_OK, c_orm_query_free(cte2));
  ASSERT_EQ(C_ORM_OK, c_orm_query_free(cte1));
  ASSERT_EQ(C_ORM_OK, c_orm_query_free(q));
  PASS();
}

/**
 * @brief AST test suite runner.
 * @param ast_suite Suite runner function name.
 */
SUITE(ast_suite) {
  RUN_TEST(test_c_orm_ast_fluent);
  RUN_TEST(test_c_orm_ast_to_sql);
  RUN_TEST(test_c_orm_ast_to_sql_postgres);
  RUN_TEST(test_c_orm_ast_depth_limit);
  RUN_TEST(test_c_orm_ast_multi_clauses);
}
