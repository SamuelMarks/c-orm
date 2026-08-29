#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file c_orm_query_fluent.c
 * @brief Implementation of fluent AST-based query builder.
 */

/* clang-format off */
#include "c_orm_ast.h"
#include "c_orm_db.h"
#include "c_orm_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
/* clang-format on */

/**
 * @brief Escapes a literal value.
 *
 * @param arena Memory arena.
 * @param val Value to escape.
 * @return Escaped string or NULL on error.
 */
static const char *c_orm_escape_literal(c_orm_arena_t *arena, const char *val) {
  size_t len;
  size_t count = 0;
  size_t i, j;
  char *escaped;

  LOG_DEBUG("c_orm_escape_literal: entry");
  if (!val) {
    LOG_DEBUG("c_orm_escape_literal: val is null");
    return NULL;
  }
  len = strlen(val);

  for (i = 0; i < len; i++) {
    if (val[i] == '\'')
      count++;
  }

  if (count == 0) {
    LOG_DEBUG("c_orm_escape_literal: exit without escaping");
    return val;
  }

  if (c_orm_arena_alloc(arena, len + count + 1, (void **)&escaped) != 0) {
    LOG_DEBUG("c_orm_escape_literal: OOM");
    return NULL;
  }

  j = 0;
  for (i = 0; i < len; i++) {
    if (val[i] == '\'') {
      escaped[j++] = '\'';
      escaped[j++] = '\'';
    } else {
      escaped[j++] = val[i];
    }
  }
  escaped[j] = '\0';

  LOG_DEBUG("c_orm_escape_literal: exit");
  return escaped;
}

/**
 * @brief Creates a raw AST node.
 *
 * @param q Query structure.
 * @param sql Raw SQL string.
 * @return The AST node or NULL on error.
 */
static c_orm_ast_node_t *c_orm_query_raw_impl(c_orm_query_t *q,
                                              const char *sql) {
  c_orm_ast_raw_t *node;
  LOG_DEBUG("c_orm_query_raw_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_raw_impl: invalid state");
    return NULL;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_raw_t), (void **)&node) !=
      0) {
    LOG_DEBUG("c_orm_query_raw_impl: OOM");
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_RAW;
  node->base.next = NULL;
  node->sql = sql;
  LOG_DEBUG("c_orm_query_raw_impl: exit");
  return (c_orm_ast_node_t *)node;
}

/**
 * @brief Creates a column AST node.
 *
 * @param q Query structure.
 * @param name Column name.
 * @return The AST node or NULL on error.
 */
static c_orm_ast_node_t *c_orm_query_col_impl(c_orm_query_t *q,
                                              const char *name) {
  c_orm_ast_column_t *node;
  LOG_DEBUG("c_orm_query_col_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_col_impl: invalid state");
    return NULL;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_column_t), (void **)&node) !=
      0) {
    LOG_DEBUG("c_orm_query_col_impl: OOM");
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_COLUMN;
  node->base.next = NULL;
  node->name = name;
  LOG_DEBUG("c_orm_query_col_impl: exit");
  return (c_orm_ast_node_t *)node;
}

/**
 * @brief Creates a literal AST node.
 *
 * @param q Query structure.
 * @param val Literal value.
 * @param is_string True if value is a string.
 * @return The AST node or NULL on error.
 */
static c_orm_ast_node_t *c_orm_query_lit_impl(c_orm_query_t *q, const char *val,
                                              int is_string) {
  c_orm_ast_literal_t *node;
  LOG_DEBUG("c_orm_query_lit_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_lit_impl: invalid state");
    return NULL;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_literal_t),
                        (void **)&node) != 0) {
    LOG_DEBUG("c_orm_query_lit_impl: OOM");
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_LITERAL;
  node->base.next = NULL;
  node->is_string = is_string;
  if (is_string) {
    node->value = c_orm_escape_literal(q->arena, val);
    if (!node->value && val) {
      LOG_DEBUG("c_orm_query_lit_impl: escape failed");
      q->error = 1;
      return NULL;
    }
  } else {
    node->value = val;
  }
  LOG_DEBUG("c_orm_query_lit_impl: exit");
  return (c_orm_ast_node_t *)node;
}

/**
 * @brief Creates an operator AST node.
 *
 * @param q Query structure.
 * @param op Operator string.
 * @param left Left operand.
 * @param right Right operand.
 * @return The AST node or NULL on error.
 */
static c_orm_ast_node_t *c_orm_query_op_impl(c_orm_query_t *q, const char *op,
                                             c_orm_ast_node_t *left,
                                             c_orm_ast_node_t *right) {
  c_orm_ast_operator_t *node;
  LOG_DEBUG("c_orm_query_op_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_op_impl: invalid state");
    return NULL;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_operator_t),
                        (void **)&node) != 0) {
    LOG_DEBUG("c_orm_query_op_impl: OOM");
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_OPERATOR;
  node->base.next = NULL;
  node->op = op;
  node->left = left;
  node->right = right;
  LOG_DEBUG("c_orm_query_op_impl: exit");
  return (c_orm_ast_node_t *)node;
}

/** @brief Helper for EQUALS. */
static c_orm_ast_node_t *c_orm_query_eq_impl(c_orm_query_t *q, const char *col,
                                             const char *val, int is_string) {
  c_orm_ast_node_t *ret;
  LOG_DEBUG("c_orm_query_eq_impl: entry");
  ret = c_orm_query_op_impl(q, "=", c_orm_query_col_impl(q, col),
                            c_orm_query_lit_impl(q, val, is_string));
  LOG_DEBUG("c_orm_query_eq_impl: exit");
  return ret;
}

/** @brief Helper for NOT EQUALS. */
static c_orm_ast_node_t *c_orm_query_neq_impl(c_orm_query_t *q, const char *col,
                                              const char *val, int is_string) {
  c_orm_ast_node_t *ret;
  LOG_DEBUG("c_orm_query_neq_impl: entry");
  ret = c_orm_query_op_impl(q, "!=", c_orm_query_col_impl(q, col),
                            c_orm_query_lit_impl(q, val, is_string));
  LOG_DEBUG("c_orm_query_neq_impl: exit");
  return ret;
}

/** @brief Helper for GREATER THAN. */
static c_orm_ast_node_t *c_orm_query_gt_impl(c_orm_query_t *q, const char *col,
                                             const char *val, int is_string) {
  c_orm_ast_node_t *ret;
  LOG_DEBUG("c_orm_query_gt_impl: entry");
  ret = c_orm_query_op_impl(q, ">", c_orm_query_col_impl(q, col),
                            c_orm_query_lit_impl(q, val, is_string));
  LOG_DEBUG("c_orm_query_gt_impl: exit");
  return ret;
}

/** @brief Helper for LESS THAN. */
static c_orm_ast_node_t *c_orm_query_lt_impl(c_orm_query_t *q, const char *col,
                                             const char *val, int is_string) {
  c_orm_ast_node_t *ret;
  LOG_DEBUG("c_orm_query_lt_impl: entry");
  ret = c_orm_query_op_impl(q, "<", c_orm_query_col_impl(q, col),
                            c_orm_query_lit_impl(q, val, is_string));
  LOG_DEBUG("c_orm_query_lt_impl: exit");
  return ret;
}

/** @brief Helper for LIKE. */
static c_orm_ast_node_t *
c_orm_query_like_impl(c_orm_query_t *q, const char *col, const char *val) {
  c_orm_ast_node_t *ret;
  LOG_DEBUG("c_orm_query_like_impl: entry");
  ret = c_orm_query_op_impl(q, "LIKE", c_orm_query_col_impl(q, col),
                            c_orm_query_lit_impl(q, val, 1));
  LOG_DEBUG("c_orm_query_like_impl: exit");
  return ret;
}

/** @brief Helper for IN. */
static c_orm_ast_node_t *c_orm_query_in_impl(c_orm_query_t *q, const char *col,
                                             const char *val_list) {
  c_orm_ast_node_t *ret;
  LOG_DEBUG("c_orm_query_in_impl: entry");
  ret = c_orm_query_op_impl(q, "IN", c_orm_query_col_impl(q, col),
                            c_orm_query_raw_impl(q, val_list));
  LOG_DEBUG("c_orm_query_in_impl: exit");
  return ret;
}

/**
 * @brief Clones an AST node.
 *
 * @param arena Arena for allocation.
 * @param node Node to clone.
 * @return Cloned node.
 */
static c_orm_ast_node_t *c_orm_ast_clone_node(c_orm_arena_t *arena,
                                              c_orm_ast_node_t *node) {
  c_orm_ast_node_t *new_node = NULL;
  LOG_DEBUG("c_orm_ast_clone_node: entry");
  if (!node) {
    LOG_DEBUG("c_orm_ast_clone_node: node is null");
    return NULL;
  }

  switch (node->type) {
  case C_ORM_AST_NODE_SELECT:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_select_t),
                          (void **)&new_node) == 0) {
      *(c_orm_ast_select_t *)new_node = *(c_orm_ast_select_t *)node;
    }
    break;
  case C_ORM_AST_NODE_FROM:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_from_t),
                          (void **)&new_node) == 0) {
      *(c_orm_ast_from_t *)new_node = *(c_orm_ast_from_t *)node;
    }
    break;
  case C_ORM_AST_NODE_WHERE:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_where_t),
                          (void **)&new_node) == 0) {
      *(c_orm_ast_where_t *)new_node = *(c_orm_ast_where_t *)node;
      ((c_orm_ast_where_t *)new_node)->condition =
          c_orm_ast_clone_node(arena, ((c_orm_ast_where_t *)node)->condition);
    }
    break;
  case C_ORM_AST_NODE_JOIN:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_join_t),
                          (void **)&new_node) == 0) {
      *(c_orm_ast_join_t *)new_node = *(c_orm_ast_join_t *)node;
      ((c_orm_ast_join_t *)new_node)->on_condition =
          c_orm_ast_clone_node(arena, ((c_orm_ast_join_t *)node)->on_condition);
    }
    break;
  case C_ORM_AST_NODE_GROUP_BY:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_group_by_t),
                          (void **)&new_node) == 0) {
      *(c_orm_ast_group_by_t *)new_node = *(c_orm_ast_group_by_t *)node;
    }
    break;
  case C_ORM_AST_NODE_HAVING:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_having_t),
                          (void **)&new_node) == 0) {
      *(c_orm_ast_having_t *)new_node = *(c_orm_ast_having_t *)node;
      ((c_orm_ast_having_t *)new_node)->condition =
          c_orm_ast_clone_node(arena, ((c_orm_ast_having_t *)node)->condition);
    }
    break;
  case C_ORM_AST_NODE_ORDER_BY:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_order_by_t),
                          (void **)&new_node) == 0) {
      *(c_orm_ast_order_by_t *)new_node = *(c_orm_ast_order_by_t *)node;
    }
    break;
  case C_ORM_AST_NODE_LIMIT:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_limit_t),
                          (void **)&new_node) == 0) {
      *(c_orm_ast_limit_t *)new_node = *(c_orm_ast_limit_t *)node;
    }
    break;
  case C_ORM_AST_NODE_OFFSET:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_offset_t),
                          (void **)&new_node) == 0) {
      *(c_orm_ast_offset_t *)new_node = *(c_orm_ast_offset_t *)node;
    }
    break;
  case C_ORM_AST_NODE_LITERAL:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_literal_t),
                          (void **)&new_node) == 0) {
      *(c_orm_ast_literal_t *)new_node = *(c_orm_ast_literal_t *)node;
    }
    break;
  case C_ORM_AST_NODE_OPERATOR:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_operator_t),
                          (void **)&new_node) == 0) {
      *(c_orm_ast_operator_t *)new_node = *(c_orm_ast_operator_t *)node;
      ((c_orm_ast_operator_t *)new_node)->left =
          c_orm_ast_clone_node(arena, ((c_orm_ast_operator_t *)node)->left);
      ((c_orm_ast_operator_t *)new_node)->right =
          c_orm_ast_clone_node(arena, ((c_orm_ast_operator_t *)node)->right);
    }
    break;
  case C_ORM_AST_NODE_RAW:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_raw_t), (void **)&new_node) ==
        0) {
      *(c_orm_ast_raw_t *)new_node = *(c_orm_ast_raw_t *)node;
    }
    break;
  case C_ORM_AST_NODE_COLUMN:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_column_t),
                          (void **)&new_node) == 0) {
      *(c_orm_ast_column_t *)new_node = *(c_orm_ast_column_t *)node;
    }
    break;
  case C_ORM_AST_NODE_GROUP:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_group_t),
                          (void **)&new_node) == 0) {
      *(c_orm_ast_group_t *)new_node = *(c_orm_ast_group_t *)node;
      ((c_orm_ast_group_t *)new_node)->expr =
          c_orm_ast_clone_node(arena, ((c_orm_ast_group_t *)node)->expr);
    }
    break;
  case C_ORM_AST_NODE_SUBQUERY:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_subquery_t),
                          (void **)&new_node) == 0) {
      *(c_orm_ast_subquery_t *)new_node = *(c_orm_ast_subquery_t *)node;
    }
    break;
  case C_ORM_AST_NODE_UNION:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_union_t),
                          (void **)&new_node) == 0) {
      *(c_orm_ast_union_t *)new_node = *(c_orm_ast_union_t *)node;
    }
    break;
  case C_ORM_AST_NODE_WITH:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_with_t),
                          (void **)&new_node) == 0) {
      *(c_orm_ast_with_t *)new_node = *(c_orm_ast_with_t *)node;
    }
    break;
  case C_ORM_AST_NODE_FUNCTION:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_function_t),
                          (void **)&new_node) == 0) {
      *(c_orm_ast_function_t *)new_node = *(c_orm_ast_function_t *)node;
    }
    break;
  case C_ORM_AST_NODE_CAST:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_cast_t),
                          (void **)&new_node) == 0) {
      *(c_orm_ast_cast_t *)new_node = *(c_orm_ast_cast_t *)node;
    }
    break;
  case C_ORM_AST_NODE_BETWEEN:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_between_t),
                          (void **)&new_node) == 0) {
      *(c_orm_ast_between_t *)new_node = *(c_orm_ast_between_t *)node;
    }
    break;
  case C_ORM_AST_NODE_EXISTS:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_exists_t),
                          (void **)&new_node) == 0) {
      *(c_orm_ast_exists_t *)new_node = *(c_orm_ast_exists_t *)node;
    }
    break;
  case C_ORM_AST_NODE_WINDOW:
    if (c_orm_arena_alloc(arena, sizeof(c_orm_ast_window_t),
                          (void **)&new_node) == 0) {
      *(c_orm_ast_window_t *)new_node = *(c_orm_ast_window_t *)node;
    }
    break;
  }

  if (new_node) {
    new_node->next = NULL;
  }
  LOG_DEBUG("c_orm_ast_clone_node: exit");
  return new_node;
}

/**
 * @brief Clones a query.
 *
 * @param q Query to clone.
 * @param out_q Pointer to the output query.
 * @return 0 on success, non-zero on error.
 */
static c_orm_error_t c_orm_query_clone(c_orm_query_t *q,
                                       c_orm_query_t **out_q) {
  c_orm_error_t rc;
  c_orm_ast_node_t *curr;
  c_orm_ast_node_t *tail = NULL;
  c_orm_ast_node_t *cloned;

  LOG_DEBUG("c_orm_query_clone: entry");
  if (!q || !out_q) {
    LOG_DEBUG("c_orm_query_clone: null argument");
    rc = C_ORM_ERROR_UNKNOWN;
    return rc;
  }

  rc = c_orm_query_new(out_q);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_clone: c_orm_query_new failed");
    return rc;
  }
  (*out_q)->error = q->error;

  curr = q->ast_head;
  while (curr) {
    cloned = c_orm_ast_clone_node((*out_q)->arena, curr);
    if (!cloned) {
      c_orm_query_free(*out_q);
      *out_q = NULL;
      LOG_DEBUG("c_orm_query_clone: clone node failed");
      rc = C_ORM_ERROR_UNKNOWN;
      return rc;
    }
    if (!tail) {
      (*out_q)->ast_head = cloned;
      tail = cloned;
    } else {
      tail->next = cloned;
      tail = cloned;
    }
    curr = curr->next;
  }
  rc = C_ORM_OK;
  LOG_DEBUG("c_orm_query_clone: exit");
  return rc;
}

/**
 * @brief Adds a SELECT clause.
 *
 * @param q Query structure.
 * @param columns Columns string.
 * @return Query structure.
 */
static c_orm_query_t *c_orm_query_select_impl(c_orm_query_t *q,
                                              const char *columns) {
  c_orm_ast_select_t *node;
  LOG_DEBUG("c_orm_query_select_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_select_impl: invalid state");
    return q;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_select_t), (void **)&node) !=
      0) {
    LOG_DEBUG("c_orm_query_select_impl: OOM");
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_SELECT;
  node->base.next = q->ast_head;
  node->columns = columns;
  node->is_distinct = 0;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_select_impl: exit");
  return q;
}

/**
 * @brief Adds a FROM clause.
 *
 * @param q Query structure.
 * @param table Table name.
 * @return Query structure.
 */
static c_orm_query_t *c_orm_query_from_impl(c_orm_query_t *q,
                                            const char *table) {
  c_orm_ast_from_t *node;
  LOG_DEBUG("c_orm_query_from_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_from_impl: invalid state");
    return q;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_from_t), (void **)&node) !=
      0) {
    LOG_DEBUG("c_orm_query_from_impl: OOM");
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_FROM;
  node->base.next = q->ast_head;
  node->table = table;
  node->alias = NULL;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_from_impl: exit");
  return q;
}

/**
 * @brief Adds a WHERE clause.
 *
 * @param q Query structure.
 * @param condition Condition node.
 * @return Query structure.
 */
static c_orm_query_t *c_orm_query_where_impl(c_orm_query_t *q,
                                             c_orm_ast_node_t *condition) {
  c_orm_ast_where_t *node;
  LOG_DEBUG("c_orm_query_where_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_where_impl: invalid state");
    return q;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_where_t), (void **)&node) !=
      0) {
    LOG_DEBUG("c_orm_query_where_impl: OOM");
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_WHERE;
  node->base.next = q->ast_head;
  node->condition = condition;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_where_impl: exit");
  return q;
}

/**
 * @brief Adds an AND WHERE condition.
 *
 * @param q Query structure.
 * @param condition Condition node.
 * @return Query structure.
 */
static c_orm_query_t *c_orm_query_and_where_impl(c_orm_query_t *q,
                                                 c_orm_ast_node_t *condition) {
  c_orm_ast_node_t *curr;
  c_orm_query_t *ret;
  c_orm_ast_where_t *w;
  LOG_DEBUG("c_orm_query_and_where_impl: entry");
  if (!q || q->error || !condition) {
    LOG_DEBUG("c_orm_query_and_where_impl: invalid state");
    return q;
  }
  curr = q->ast_head;
  while (curr) {
    if (curr->type == C_ORM_AST_NODE_WHERE) {
      w = (c_orm_ast_where_t *)curr;
      if (w->condition) {
        w->condition = c_orm_query_op_impl(q, "AND", w->condition, condition);
      } else {
        w->condition = condition;
      }
      LOG_DEBUG("c_orm_query_and_where_impl: exit updated");
      return q;
    }
    curr = curr->next;
  }
  ret = q->where(q, condition);
  LOG_DEBUG("c_orm_query_and_where_impl: exit created");
  return ret;
}

/**
 * @brief Adds an OR WHERE condition.
 *
 * @param q Query structure.
 * @param condition Condition node.
 * @return Query structure.
 */
static c_orm_query_t *c_orm_query_or_where_impl(c_orm_query_t *q,
                                                c_orm_ast_node_t *condition) {
  c_orm_ast_node_t *curr;
  c_orm_query_t *ret;
  c_orm_ast_where_t *w;
  LOG_DEBUG("c_orm_query_or_where_impl: entry");
  if (!q || q->error || !condition) {
    LOG_DEBUG("c_orm_query_or_where_impl: invalid state");
    return q;
  }
  curr = q->ast_head;
  while (curr) {
    if (curr->type == C_ORM_AST_NODE_WHERE) {
      w = (c_orm_ast_where_t *)curr;
      if (w->condition) {
        w->condition = c_orm_query_op_impl(q, "OR", w->condition, condition);
      } else {
        w->condition = condition;
      }
      LOG_DEBUG("c_orm_query_or_where_impl: exit updated");
      return q;
    }
    curr = curr->next;
  }
  ret = q->where(q, condition);
  LOG_DEBUG("c_orm_query_or_where_impl: exit created");
  return ret;
}

/**
 * @brief Adds an ORDER BY clause.
 *
 * @param q Query structure.
 * @param column Column string.
 * @param is_desc Descending flag.
 * @return Query structure.
 */
static c_orm_query_t *
c_orm_query_order_by_impl(c_orm_query_t *q, const char *column, int is_desc) {
  c_orm_ast_order_by_t *node;
  LOG_DEBUG("c_orm_query_order_by_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_order_by_impl: invalid state");
    return q;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_order_by_t),
                        (void **)&node) != 0) {
    LOG_DEBUG("c_orm_query_order_by_impl: OOM");
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_ORDER_BY;
  node->base.next = q->ast_head;
  node->column = column;
  node->is_desc = is_desc;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_order_by_impl: exit");
  return q;
}

/**
 * @brief Adds a LIMIT clause.
 *
 * @param q Query structure.
 * @param n Limit number.
 * @return Query structure.
 */
static c_orm_query_t *c_orm_query_limit_impl(c_orm_query_t *q, size_t n) {
  c_orm_ast_limit_t *node;
  LOG_DEBUG("c_orm_query_limit_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_limit_impl: invalid state");
    return q;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_limit_t), (void **)&node) !=
      0) {
    LOG_DEBUG("c_orm_query_limit_impl: OOM");
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_LIMIT;
  node->base.next = q->ast_head;
  node->limit = n;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_limit_impl: exit");
  return q;
}

/**
 * @brief Adds an OFFSET clause.
 *
 * @param q Query structure.
 * @param n Offset number.
 * @return Query structure.
 */
static c_orm_query_t *c_orm_query_offset_impl(c_orm_query_t *q, size_t n) {
  c_orm_ast_offset_t *node;
  LOG_DEBUG("c_orm_query_offset_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_offset_impl: invalid state");
    return q;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_offset_t), (void **)&node) !=
      0) {
    LOG_DEBUG("c_orm_query_offset_impl: OOM");
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_OFFSET;
  node->base.next = q->ast_head;
  node->offset = n;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_offset_impl: exit");
  return q;
}

/**
 * @brief Adds a JOIN clause.
 *
 * @param q Query structure.
 * @param table Table name.
 * @param type_str Join type string.
 * @param on_condition On condition node.
 * @return Query structure.
 */
static c_orm_query_t *c_orm_query_join_impl(c_orm_query_t *q, const char *table,
                                            const char *type_str,
                                            c_orm_ast_node_t *on_condition) {
  c_orm_ast_join_t *node;
  LOG_DEBUG("c_orm_query_join_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_join_impl: invalid state");
    return q;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_join_t), (void **)&node) !=
      0) {
    LOG_DEBUG("c_orm_query_join_impl: OOM");
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_JOIN;
  node->base.next = q->ast_head;
  node->table = table;
  node->type_str = type_str;
  node->on_condition = on_condition;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_join_impl: exit");
  return q;
}

/** @brief Adds a LEFT JOIN. */
static c_orm_query_t *
c_orm_query_left_join_impl(c_orm_query_t *q, const char *table,
                           c_orm_ast_node_t *on_condition) {
  c_orm_query_t *ret;
  LOG_DEBUG("c_orm_query_left_join_impl: entry");
  ret = c_orm_query_join_impl(q, table, "LEFT", on_condition);
  LOG_DEBUG("c_orm_query_left_join_impl: exit");
  return ret;
}

/** @brief Adds a RIGHT JOIN. */
static c_orm_query_t *
c_orm_query_right_join_impl(c_orm_query_t *q, const char *table,
                            c_orm_ast_node_t *on_condition) {
  c_orm_query_t *ret;
  LOG_DEBUG("c_orm_query_right_join_impl: entry");
  ret = c_orm_query_join_impl(q, table, "RIGHT", on_condition);
  LOG_DEBUG("c_orm_query_right_join_impl: exit");
  return ret;
}

/**
 * @brief Adds a GROUP BY clause.
 *
 * @param q Query structure.
 * @param columns Columns string.
 * @return Query structure.
 */
static c_orm_query_t *c_orm_query_group_by_impl(c_orm_query_t *q,
                                                const char *columns) {
  c_orm_ast_group_by_t *node;
  LOG_DEBUG("c_orm_query_group_by_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_group_by_impl: invalid state");
    return q;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_group_by_t),
                        (void **)&node) != 0) {
    LOG_DEBUG("c_orm_query_group_by_impl: OOM");
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_GROUP_BY;
  node->base.next = q->ast_head;
  node->columns = columns;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_group_by_impl: exit");
  return q;
}

/**
 * @brief Adds a HAVING clause.
 *
 * @param q Query structure.
 * @param condition Condition node.
 * @return Query structure.
 */
static c_orm_query_t *c_orm_query_having_impl(c_orm_query_t *q,
                                              c_orm_ast_node_t *condition) {
  c_orm_ast_having_t *node;
  LOG_DEBUG("c_orm_query_having_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_having_impl: invalid state");
    return q;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_having_t), (void **)&node) !=
      0) {
    LOG_DEBUG("c_orm_query_having_impl: OOM");
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_HAVING;
  node->base.next = q->ast_head;
  node->condition = condition;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_having_impl: exit");
  return q;
}

/**
 * @brief Adds a WITH clause.
 *
 * @param q Query structure.
 * @param alias Alias string.
 * @param subquery Subquery.
 * @return Query structure.
 */
static c_orm_query_t *c_orm_query_with_impl(c_orm_query_t *q, const char *alias,
                                            c_orm_query_t *subquery) {
  c_orm_ast_with_t *node;
  LOG_DEBUG("c_orm_query_with_impl: entry");
  if (!q || q->error || !subquery) {
    LOG_DEBUG("c_orm_query_with_impl: invalid state");
    return q;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_with_t), (void **)&node) !=
      0) {
    LOG_DEBUG("c_orm_query_with_impl: OOM");
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_WITH;
  node->base.next = q->ast_head;
  node->alias = alias;
  node->query = subquery;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_with_impl: exit");
  return q;
}

/**
 * @brief Adds a UNION clause.
 *
 * @param q Query structure.
 * @param other Other query.
 * @param is_all UNION ALL flag.
 * @return Query structure.
 */
static c_orm_query_t *c_orm_query_union_impl(c_orm_query_t *q,
                                             c_orm_query_t *other, int is_all) {
  c_orm_ast_union_t *node;
  LOG_DEBUG("c_orm_query_union_impl: entry");
  if (!q || q->error || !other) {
    LOG_DEBUG("c_orm_query_union_impl: invalid state");
    return q;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_union_t), (void **)&node) !=
      0) {
    LOG_DEBUG("c_orm_query_union_impl: OOM");
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_UNION;
  node->base.next = q->ast_head;
  node->is_all = is_all;
  node->query = other;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_union_impl: exit");
  return q;
}

/**
 * @brief Sets DISTINCT flag on query.
 *
 * @param q Query structure.
 * @return Query structure.
 */
static c_orm_query_t *c_orm_query_distinct_impl(c_orm_query_t *q) {
  c_orm_ast_node_t *curr;
  LOG_DEBUG("c_orm_query_distinct_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_distinct_impl: invalid state");
    return q;
  }
  curr = q->ast_head;
  while (curr) {
    if (curr->type == C_ORM_AST_NODE_SELECT) {
      ((c_orm_ast_select_t *)curr)->is_distinct = 1;
      LOG_DEBUG("c_orm_query_distinct_impl: exit modified existing");
      return q;
    }
    curr = curr->next;
  }
  q->select_(q, "*");
  if (q->ast_head && q->ast_head->type == C_ORM_AST_NODE_SELECT) {
    ((c_orm_ast_select_t *)q->ast_head)->is_distinct = 1;
  }
  LOG_DEBUG("c_orm_query_distinct_impl: exit new select");
  return q;
}

/**
 * @brief Adds FROM clause with alias.
 *
 * @param q Query structure.
 * @param table Table name.
 * @param alias Alias name.
 * @return Query structure.
 */
static c_orm_query_t *c_orm_query_from_alias_impl(c_orm_query_t *q,
                                                  const char *table,
                                                  const char *alias) {
  c_orm_ast_from_t *node;
  LOG_DEBUG("c_orm_query_from_alias_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_from_alias_impl: invalid state");
    return q;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_from_t), (void **)&node) !=
      0) {
    LOG_DEBUG("c_orm_query_from_alias_impl: OOM");
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_FROM;
  node->base.next = q->ast_head;
  node->table = table;
  node->alias = alias;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_from_alias_impl: exit");
  return q;
}

/**
 * @brief Eager loads a relation.
 *
 * @param q Query structure.
 * @param meta Table metadata.
 * @param relation_name Relation name.
 * @return Query structure.
 */
static c_orm_query_t *
c_orm_query_eager_load_impl(c_orm_query_t *q, const c_orm_table_meta_t *meta,
                            const char *relation_name) {
  size_t i;
  const c_orm_relation_meta_t *rel = NULL;
  char *on_cond;
  char *columns;
  c_orm_ast_node_t *curr;
  char *p;
  size_t col_i;
  int first;
  int w;

  LOG_DEBUG("c_orm_query_eager_load_impl: entry");

  if (!q || q->error || !meta || !relation_name) {
    LOG_DEBUG("c_orm_query_eager_load_impl: invalid state or args");
    return q;
  }

  for (i = 0; i < meta->num_relations; i++) {
    if (strcmp(meta->relations[i].field_name, relation_name) == 0) {
      rel = &meta->relations[i];
      break;
    }
  }

  if (!rel || !rel->target_meta) {
    LOG_DEBUG("c_orm_query_eager_load_impl: relation not found");
    q->error = 1;
    return q;
  }

  on_cond = (char *)C_ORM_MALLOC(128);
  if (!on_cond) {
    LOG_DEBUG("c_orm_query_eager_load_impl: OOM");
    q->error = 1;
    return q;
  }
#if defined(_MSC_VER)
  sprintf_s(on_cond, 128, "%s.%s = %s.%s", meta->name, rel->local_key,
            rel->target_meta->name, rel->foreign_key);
#else
  sprintf(on_cond, "%s.%s = %s.%s", meta->name, rel->local_key,
          rel->target_meta->name, rel->foreign_key);
#endif

  q->left_join(q, rel->target_meta->name, q->raw(q, on_cond));
  C_ORM_FREE(on_cond);

  /* Expand SELECT columns dynamically to add child columns with prefix
   * `relation_name_` */
  if (c_orm_arena_alloc(q->arena, 4096, (void **)&columns) == 0) {
    p = columns;
    first = 1;

    /* We MUST include parent columns explicitly too to avoid ambiguity if we
     * replace '*' */
    for (col_i = 0; col_i < meta->num_columns; col_i++) {
      if (!first) {
        *p++ = ',';
        *p++ = ' ';
      }
      first = 0;
#if defined(_MSC_VER)
      w = sprintf_s(p, (size_t)(4096 - (size_t)(p - columns)), "%s.%s",
                    meta->name, meta->columns[col_i].name);
#else
      w = sprintf(p, "%s.%s", meta->name, meta->columns[col_i].name);
#endif

      p += w;
    }

    for (col_i = 0; col_i < rel->target_meta->num_columns; col_i++) {
      if (!first) {
        *p++ = ',';
        *p++ = ' ';
      }
      first = 0;
#if defined(_MSC_VER)
      w = sprintf_s(p, (size_t)(4096 - (size_t)(p - columns)), "%s.%s AS %s_%s",
                    rel->target_meta->name,
                    rel->target_meta->columns[col_i].name, relation_name,
                    rel->target_meta->columns[col_i].name);
#else
      w = sprintf(p, "%s.%s AS %s_%s", rel->target_meta->name,
                  rel->target_meta->columns[col_i].name, relation_name,
                  rel->target_meta->columns[col_i].name);
#endif

      p += w;
    }

    /* Find existing SELECT and replace it */
    curr = q->ast_head;
    while (curr) {
      if (curr->type == C_ORM_AST_NODE_SELECT) {
        c_orm_ast_select_t *s = (c_orm_ast_select_t *)curr;
        s->columns = columns;
        break;
      }
      curr = curr->next;
    }
  }

  LOG_DEBUG("c_orm_query_eager_load_impl: exit");
  return q;
}

/**
 * @brief Creates a group AST node.
 *
 * @param q Query structure.
 * @param expr Expression node.
 * @return Group AST node.
 */
static c_orm_ast_node_t *c_orm_query_group_node_impl(c_orm_query_t *q,
                                                     c_orm_ast_node_t *expr) {
  c_orm_ast_group_t *node;
  LOG_DEBUG("c_orm_query_group_node_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_group_node_impl: invalid state");
    return NULL;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_group_t), (void **)&node) !=
      0) {
    LOG_DEBUG("c_orm_query_group_node_impl: OOM");
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_GROUP;
  node->base.next = NULL;
  node->expr = expr;
  LOG_DEBUG("c_orm_query_group_node_impl: exit");
  return (c_orm_ast_node_t *)node;
}

/**
 * @brief Creates a subquery AST node.
 *
 * @param q Query structure.
 * @param subq Subquery.
 * @param alias Alias.
 * @return Subquery AST node.
 */
static c_orm_ast_node_t *c_orm_query_subquery_node_impl(c_orm_query_t *q,
                                                        c_orm_query_t *subq,
                                                        const char *alias) {
  c_orm_ast_subquery_t *node;
  LOG_DEBUG("c_orm_query_subquery_node_impl: entry");
  if (!q || q->error || !subq) {
    LOG_DEBUG("c_orm_query_subquery_node_impl: invalid state");
    return NULL;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_subquery_t),
                        (void **)&node) != 0) {
    LOG_DEBUG("c_orm_query_subquery_node_impl: OOM");
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_SUBQUERY;
  node->base.next = NULL;
  node->query = subq;
  node->alias = alias;
  LOG_DEBUG("c_orm_query_subquery_node_impl: exit");
  return (c_orm_ast_node_t *)node;
}

/**
 * @brief Creates a function AST node.
 *
 * @param q Query structure.
 * @param name Function name.
 * @param args Arguments string.
 * @param alias Alias.
 * @return Function AST node.
 */
static c_orm_ast_node_t *c_orm_query_func_impl(c_orm_query_t *q,
                                               const char *name,
                                               const char *args,
                                               const char *alias) {
  c_orm_ast_function_t *node;
  LOG_DEBUG("c_orm_query_func_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_func_impl: invalid state");
    return NULL;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_function_t),
                        (void **)&node) != 0) {
    LOG_DEBUG("c_orm_query_func_impl: OOM");
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_FUNCTION;
  node->base.next = NULL;
  node->name = name;
  node->args = args;
  node->alias = alias;
  LOG_DEBUG("c_orm_query_func_impl: exit");
  return (c_orm_ast_node_t *)node;
}

/**
 * @brief Creates a CAST AST node.
 *
 * @param q Query structure.
 * @param col Column name.
 * @param type Cast type.
 * @return CAST AST node.
 */
static c_orm_ast_node_t *
c_orm_query_cast_impl(c_orm_query_t *q, const char *col, const char *type) {
  c_orm_ast_cast_t *node;
  LOG_DEBUG("c_orm_query_cast_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_cast_impl: invalid state");
    return NULL;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_cast_t), (void **)&node) !=
      0) {
    LOG_DEBUG("c_orm_query_cast_impl: OOM");
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_CAST;
  node->base.next = NULL;
  node->col = col;
  node->type = type;
  LOG_DEBUG("c_orm_query_cast_impl: exit");
  return (c_orm_ast_node_t *)node;
}

/**
 * @brief Creates an IS NULL AST node.
 *
 * @param q Query structure.
 * @param col Column name.
 * @param is_not NOT flag.
 * @return IS NULL AST node.
 */
static c_orm_ast_node_t *c_orm_query_is_null_impl(c_orm_query_t *q,
                                                  const char *col, int is_not) {
  c_orm_ast_operator_t *node;
  LOG_DEBUG("c_orm_query_is_null_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_is_null_impl: invalid state");
    return NULL;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_operator_t),
                        (void **)&node) != 0) {
    LOG_DEBUG("c_orm_query_is_null_impl: OOM");
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_OPERATOR;
  node->base.next = NULL;
  node->op = is_not ? "IS NOT NULL" : "IS NULL";
  node->left = q->col(q, col);
  node->right = NULL;
  LOG_DEBUG("c_orm_query_is_null_impl: exit");
  return (c_orm_ast_node_t *)node;
}

/**
 * @brief Creates a BETWEEN AST node.
 *
 * @param q Query structure.
 * @param col Column name.
 * @param low Low value.
 * @param high High value.
 * @param is_string String flag.
 * @return BETWEEN AST node.
 */
static c_orm_ast_node_t *
c_orm_query_between_impl(c_orm_query_t *q, const char *col, const char *low,
                         const char *high, int is_string) {
  c_orm_ast_between_t *node;
  LOG_DEBUG("c_orm_query_between_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_between_impl: invalid state");
    return NULL;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_between_t),
                        (void **)&node) != 0) {
    LOG_DEBUG("c_orm_query_between_impl: OOM");
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_BETWEEN;
  node->base.next = NULL;
  node->col = col;
  node->low = is_string ? c_orm_escape_literal(q->arena, low) : low;
  node->high = is_string ? c_orm_escape_literal(q->arena, high) : high;
  node->is_string = is_string;
  LOG_DEBUG("c_orm_query_between_impl: exit");
  return (c_orm_ast_node_t *)node;
}

/**
 * @brief Creates an EXISTS AST node.
 *
 * @param q Query structure.
 * @param subq Subquery.
 * @param is_not NOT flag.
 * @return EXISTS AST node.
 */
static c_orm_ast_node_t *
c_orm_query_exists_impl(c_orm_query_t *q, c_orm_query_t *subq, int is_not) {
  c_orm_ast_exists_t *node;
  LOG_DEBUG("c_orm_query_exists_impl: entry");
  if (!q || q->error || !subq) {
    LOG_DEBUG("c_orm_query_exists_impl: invalid state");
    return NULL;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_exists_t), (void **)&node) !=
      0) {
    LOG_DEBUG("c_orm_query_exists_impl: OOM");
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_EXISTS;
  node->base.next = NULL;
  node->query = subq;
  node->is_not = is_not;
  LOG_DEBUG("c_orm_query_exists_impl: exit");
  return (c_orm_ast_node_t *)node;
}

/**
 * @brief Creates a WINDOW function AST node.
 *
 * @param q Query structure.
 * @param func_name Function name.
 * @param partition_by Partition by.
 * @param order_by Order by.
 * @param alias Alias.
 * @return WINDOW AST node.
 */
static c_orm_ast_node_t *c_orm_query_window_impl(c_orm_query_t *q,
                                                 const char *func_name,
                                                 const char *partition_by,
                                                 const char *order_by,
                                                 const char *alias) {
  c_orm_ast_window_t *node;
  LOG_DEBUG("c_orm_query_window_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_window_impl: invalid state");
    return NULL;
  }
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_window_t), (void **)&node) !=
      0) {
    LOG_DEBUG("c_orm_query_window_impl: OOM");
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_WINDOW;
  node->base.next = NULL;
  node->func_name = func_name;
  node->partition_by = partition_by;
  node->order_by = order_by;
  node->alias = alias;
  LOG_DEBUG("c_orm_query_window_impl: exit");
  return (c_orm_ast_node_t *)node;
}

/**
 * @brief Allocates and initializes a new fluent query structure.
 *
 * @param out_query Pointer to the output query.
 * @return 0 on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_query_new(c_orm_query_t **out_query) {
  c_orm_error_t rc;
  c_orm_query_t *q;

  LOG_DEBUG("c_orm_query_new: entry");

  if (!out_query) {
    LOG_DEBUG("c_orm_query_new: null argument");
    rc = C_ORM_ERROR_UNKNOWN;
    return rc;
  }

  q = (c_orm_query_t *)C_ORM_MALLOC(sizeof(c_orm_query_t));
  if (!q) {
    LOG_DEBUG("c_orm_query_new: OOM");
    rc = C_ORM_ERROR_UNKNOWN;
    return rc;
  }

  rc = c_orm_arena_new(&q->arena);
  if (rc != C_ORM_OK) {
    C_ORM_FREE(q);
    LOG_DEBUG("c_orm_query_new: arena OOM");
    return rc;
  }

  q->ast_head = NULL;
  q->error = 0;

  q->select_ = c_orm_query_select_impl;
  q->from = c_orm_query_from_impl;
  q->where = c_orm_query_where_impl;
  q->and_where = c_orm_query_and_where_impl;
  q->or_where = c_orm_query_or_where_impl;
  q->order_by = c_orm_query_order_by_impl;
  q->limit = c_orm_query_limit_impl;
  q->offset = c_orm_query_offset_impl;
  q->clone = c_orm_query_clone;

  q->join = c_orm_query_join_impl;
  q->left_join = c_orm_query_left_join_impl;
  q->right_join = c_orm_query_right_join_impl;
  q->group_by = c_orm_query_group_by_impl;
  q->having = c_orm_query_having_impl;
  q->with = c_orm_query_with_impl;
  q->union_ = c_orm_query_union_impl;
  q->distinct = c_orm_query_distinct_impl;
  q->from_alias = c_orm_query_from_alias_impl;
  q->eager_load = c_orm_query_eager_load_impl;

  q->group = c_orm_query_group_node_impl;
  q->subquery = c_orm_query_subquery_node_impl;
  q->func = c_orm_query_func_impl;
  q->cast_ = c_orm_query_cast_impl;
  q->is_null = c_orm_query_is_null_impl;
  q->between = c_orm_query_between_impl;
  q->exists = c_orm_query_exists_impl;
  q->window = c_orm_query_window_impl;
  q->raw = c_orm_query_raw_impl;
  q->eq = c_orm_query_eq_impl;
  q->neq = c_orm_query_neq_impl;
  q->gt = c_orm_query_gt_impl;
  q->lt = c_orm_query_lt_impl;
  q->like = c_orm_query_like_impl;
  q->in = c_orm_query_in_impl;
  q->op = c_orm_query_op_impl;
  q->lit = c_orm_query_lit_impl;
  q->col = c_orm_query_col_impl;

  *out_query = q;
  rc = C_ORM_OK;
  LOG_DEBUG("c_orm_query_new: exit");
  return rc;
}

/**
 * @brief Frees a fluent query structure.
 *
 * @param query Query structure to free.
 */
C_ORM_EXPORT void c_orm_query_free(c_orm_query_t *query) {
  LOG_DEBUG("c_orm_query_free: entry");
  if (query) {
    c_orm_arena_free(query->arena);
    C_ORM_FREE(query);
  }
  LOG_DEBUG("c_orm_query_free: exit");
}

#if defined(__clang__) || defined(__GNUC__)
#endif
