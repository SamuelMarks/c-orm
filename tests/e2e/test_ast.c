/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_ast.h"
#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
/* clang-format on */

TEST test_c_orm_ast_fluent(void) {
  c_orm_query_t *q = NULL;
  c_orm_query_t *q_clone = NULL;
  int err;

  err = c_orm_query_new(&q);
  ASSERT_EQ_FMT(0, err, "%d");
  ASSERT(q != NULL);

  /* Test fluent API chaining and macros */
  q->select_(q, "id, name, email")
      ->from(q, "users")
      ->order_by(q, "created_at", 1)
      ->limit(q, 10)
      ->offset(q, 20);

  C_ORM_WHERE(C_ORM_EQ_NUM("id", "1"));
  C_ORM_AND_WHERE(C_ORM_GT_NUM("age", "18"));
  C_ORM_OR_WHERE(C_ORM_EQ("status", "active"));

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
    c_orm_ast_node_t *lit = q->lit(q, "user's name", 1);
    ASSERT(lit != NULL);
    ASSERT_STR_EQ("user''s name", ((c_orm_ast_literal_t *)lit)->value);
  }

  c_orm_query_free(q);
  c_orm_query_free(q_clone);
  PASS();
}

TEST test_c_orm_ast_to_sql(void) {
  c_orm_query_t *q = NULL;
  char *sql = NULL;
  c_orm_query_params_t params;
  int err;

  err = c_orm_query_new(&q);
  ASSERT_EQ_FMT(0, err, "%d");

  q->select_(q, "id, name")
      ->from(q, "users")
      ->where(q, q->eq(q, "status", "active", 1))
      ->order_by(q, "id", 0)
      ->limit(q, 10);

  c_orm_query_params_init(&params);
  err = c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &params);
  ASSERT_EQ_FMT(0, err, "%d");
  ASSERT_STR_EQ(
      "SELECT id, name FROM users WHERE status = ? ORDER BY id ASC LIMIT 10",
      sql);
  ASSERT_EQ_FMT(1, (int)params.count, "%d");
  ASSERT_STR_EQ("active", params.params[0].value);
  ASSERT_EQ_FMT(1, params.params[0].is_string, "%d");

  free(sql);
  c_orm_query_params_cleanup(&params);
  c_orm_query_free(q);
  PASS();
}

TEST test_c_orm_ast_to_sql_postgres(void) {
  c_orm_query_t *q = NULL;
  char *sql = NULL;
  c_orm_query_params_t params;
  int err;

  err = c_orm_query_new(&q);
  ASSERT_EQ_FMT(0, err, "%d");

  q->select_(q, "*")
      ->from(q, "orders")
      ->where(q, q->eq(q, "user_id", "42", 0))
      ->and_where(q, q->gt(q, "amount", "100", 0));

  c_orm_query_params_init(&params);
  err = c_orm_query_to_sql(q, C_ORM_DIALECT_POSTGRES, &sql, &params);
  ASSERT_EQ_FMT(0, err, "%d");
  ASSERT_STR_EQ("SELECT * FROM orders WHERE user_id = $1 AND amount > $2", sql);
  ASSERT_EQ_FMT(2, (int)params.count, "%d");
  ASSERT_STR_EQ("42", params.params[0].value);
  ASSERT_EQ_FMT(0, params.params[0].is_string, "%d");
  ASSERT_STR_EQ("100", params.params[1].value);
  ASSERT_EQ_FMT(0, params.params[1].is_string, "%d");

  free(sql);
  c_orm_query_params_cleanup(&params);
  c_orm_query_free(q);
  PASS();
}

SUITE(ast_suite) {
  RUN_TEST(test_c_orm_ast_fluent);
  RUN_TEST(test_c_orm_ast_to_sql);
  RUN_TEST(test_c_orm_ast_to_sql_postgres);
}

