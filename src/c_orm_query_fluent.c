/**
 * @file c_orm_query_fluent.c
 * @brief Implementation of fluent AST-based query builder.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
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
 * @param out_escaped Output pointer receiving escaped string.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_escape_literal(c_orm_arena_t *arena, const char *val,
                                          const char **out_escaped) {
  size_t len;
  size_t count = 0;
  size_t i, j;
  char *escaped;

  LOG_DEBUG("c_orm_escape_literal: entry");
  if (!val) {
    LOG_DEBUG("c_orm_escape_literal: val is null");
    *out_escaped = NULL;
    return C_ORM_OK;
  }
  len = strlen(val);

  for (i = 0; i < len; i++) {
    if (val[i] == '\x27')
      count++;
  }

  if (count == 0) {
    LOG_DEBUG("c_orm_escape_literal: exit without escaping");
    *out_escaped = val;
    return C_ORM_OK;
  }

  {
    c_orm_error_t rc =
        c_orm_arena_alloc(arena, len + count + 1, (void **)&escaped);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_escape_literal: OOM");
      *out_escaped = NULL;
      return rc;
    }
  }

  j = 0;
  for (i = 0; i < len; i++) {
    if (val[i] == '\x27') {
      escaped[j++] = '\x27';
      escaped[j++] = '\x27';
    } else {
      escaped[j++] = val[i];
    }
  }
  escaped[j] = '\0';

  LOG_DEBUG("c_orm_escape_literal: exit");
  *out_escaped = escaped;
  return C_ORM_OK;
}

/**
 * @brief Creates a raw AST node.
 *
 * @param q Query structure.
 * @param sql Raw SQL string.
 * @param out_node Output pointer receiving the AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_raw_impl(c_orm_ast_query_t *q, const char *sql,
                                          c_orm_ast_node_t **out_node) {
  c_orm_ast_raw_t *node;
  const char *sql_copy = NULL;
  c_orm_error_t err;
  LOG_DEBUG("c_orm_query_raw_impl: entry");
  if (!q || q->error || !out_node) {
    LOG_DEBUG("c_orm_query_raw_impl: invalid state");
    if (out_node)
      *out_node = NULL;
    return C_ORM_ERROR_UNKNOWN;
  }
  err = c_orm_arena_strdup(q->arena, sql, &sql_copy);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_raw_impl: OOM for sql string");
    q->error = 1;
    *out_node = NULL;
    return err;
  }
  err = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_raw_t), (void **)&node);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_raw_impl: OOM");
    q->error = 1;
    *out_node = NULL;
    return err;
  }
  node->base.type = C_ORM_AST_NODE_RAW;
  node->base.next = NULL;
  node->sql = sql_copy;
  *out_node = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_raw_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Creates a column AST node.
 *
 * @param q Query structure.
 * @param name Column name.
 * @param out_node Output pointer receiving the AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_col_impl(c_orm_ast_query_t *q,
                                          const char *name,
                                          c_orm_ast_node_t **out_node) {
  c_orm_ast_column_t *node;
  const char *name_copy = NULL;
  c_orm_error_t err;
  LOG_DEBUG("c_orm_query_col_impl: entry");
  if (!q || q->error || !out_node) {
    LOG_DEBUG("c_orm_query_col_impl: invalid state");
    if (out_node)
      *out_node = NULL;
    return C_ORM_ERROR_UNKNOWN;
  }
  err = c_orm_arena_strdup(q->arena, name, &name_copy);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_col_impl: OOM for name string");
    q->error = 1;
    *out_node = NULL;
    return err;
  }
  err = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_column_t), (void **)&node);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_col_impl: OOM");
    q->error = 1;
    *out_node = NULL;
    return err;
  }
  node->base.type = C_ORM_AST_NODE_COLUMN;
  node->base.next = NULL;
  node->name = name_copy;
  *out_node = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_col_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Creates a literal AST node.
 *
 * @param q Query structure.
 * @param val Literal value.
 * @param is_string True if value is a string.
 * @param out_node Output pointer receiving the AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_lit_impl(c_orm_ast_query_t *q, const char *val,
                                          int is_string,
                                          c_orm_ast_node_t **out_node) {
  c_orm_ast_literal_t *node;
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_query_lit_impl: entry");
  if (!q || q->error || !out_node) {
    LOG_DEBUG("c_orm_query_lit_impl: invalid state");
    if (out_node)
      *out_node = NULL;
    return C_ORM_ERROR_UNKNOWN;
  }
  rc = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_literal_t), (void **)&node);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_lit_impl: OOM");
    q->error = 1;
    *out_node = NULL;
    return rc;
  }
  node->base.type = C_ORM_AST_NODE_LITERAL;
  node->base.next = NULL;
  node->is_string = is_string;
  node->is_decimal = 0;
  memset(&node->decimal_val, 0, sizeof(node->decimal_val));
  if (is_string) {
    rc = c_orm_escape_literal(q->arena, val, &node->value);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_query_lit_impl: escape failed");
      q->error = 1;
      *out_node = NULL;
      return rc;
    }
  } else {
    node->value = val;
  }
  *out_node = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_lit_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Creates an operator AST node.
 *
 * @param q Query structure.
 * @param op Operator string.
 * @param left Left operand.
 * @param right Right operand.
 * @param out_node Output pointer receiving the AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_op_impl(c_orm_ast_query_t *q, const char *op,
                                         c_orm_ast_node_t *left,
                                         c_orm_ast_node_t *right,
                                         c_orm_ast_node_t **out_node) {
  c_orm_ast_operator_t *node;
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_query_op_impl: entry");
  if (!q || q->error || !out_node) {
    LOG_DEBUG("c_orm_query_op_impl: invalid state");
    if (out_node)
      *out_node = NULL;
    return C_ORM_ERROR_UNKNOWN;
  }
  rc =
      c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_operator_t), (void **)&node);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_op_impl: OOM");
    q->error = 1;
    *out_node = NULL;
    return rc;
  }
  node->base.type = C_ORM_AST_NODE_OPERATOR;
  node->base.next = NULL;
  node->op = op;
  node->left = left;
  node->right = right;
  *out_node = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_op_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Shared helper constructing `<col> <op_str> <lit_or_raw>` AST nodes.
 * @param q Query structure.
 * @param op_str SQL binary operator string.
 * @param col Column name.
 * @param val Right-hand value string.
 * @param is_string String flag (or -1 for raw expression list).
 * @param out_node Output pointer receiving the AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_bin_cmp_impl(c_orm_ast_query_t *q,
                                              const char *op_str,
                                              const char *col, const char *val,
                                              int is_string,
                                              c_orm_ast_node_t **out_node) {
  c_orm_ast_node_t *l = NULL;
  c_orm_ast_node_t *r = NULL;
  c_orm_error_t rc;
  if (!out_node) {
    if (q)
      q->error = 1;
    return C_ORM_ERROR_UNKNOWN;
  }
  *out_node = NULL;
  rc = c_orm_query_col_impl(q, col, &l);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = (is_string == -1) ? c_orm_query_raw_impl(q, val, &r)
                         : c_orm_query_lit_impl(q, val, is_string, &r);
  if (rc != C_ORM_OK) {
    return rc;
  }
  return c_orm_query_op_impl(q, op_str, l, r, out_node);
}

/**
 * @brief Helper for EQUALS operator.
 * @param q Query structure.
 * @param col Column name.
 * @param val Value string.
 * @param is_string True if value is string.
 * @param out_node Output pointer receiving the AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_eq_impl(c_orm_ast_query_t *q, const char *col,
                                         const char *val, int is_string,
                                         c_orm_ast_node_t **out_node) {
  return c_orm_query_bin_cmp_impl(q, "=", col, val, is_string, out_node);
}

/**
 * @brief Helper for NOT EQUALS operator.
 * @param q Query structure.
 * @param col Column name.
 * @param val Value string.
 * @param is_string True if value is string.
 * @param out_node Output pointer receiving the AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_neq_impl(c_orm_ast_query_t *q, const char *col,
                                          const char *val, int is_string,
                                          c_orm_ast_node_t **out_node) {
  return c_orm_query_bin_cmp_impl(q, "!=", col, val, is_string, out_node);
}

/**
 * @brief Helper for GREATER THAN operator.
 * @param q Query structure.
 * @param col Column name.
 * @param val Value string.
 * @param is_string True if value is string.
 * @param out_node Output pointer receiving the AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_gt_impl(c_orm_ast_query_t *q, const char *col,
                                         const char *val, int is_string,
                                         c_orm_ast_node_t **out_node) {
  return c_orm_query_bin_cmp_impl(q, ">", col, val, is_string, out_node);
}

/**
 * @brief Helper for LESS THAN operator.
 * @param q Query structure.
 * @param col Column name.
 * @param val Value string.
 * @param is_string True if value is string.
 * @param out_node Output pointer receiving the AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_lt_impl(c_orm_ast_query_t *q, const char *col,
                                         const char *val, int is_string,
                                         c_orm_ast_node_t **out_node) {
  return c_orm_query_bin_cmp_impl(q, "<", col, val, is_string, out_node);
}

/**
 * @brief Helper for LIKE operator.
 * @param q Query structure.
 * @param col Column name.
 * @param val Value pattern.
 * @param out_node Output pointer receiving the AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_like_impl(c_orm_ast_query_t *q,
                                           const char *col, const char *val,
                                           c_orm_ast_node_t **out_node) {
  return c_orm_query_bin_cmp_impl(q, "LIKE", col, val, 1, out_node);
}

/**
 * @brief Helper for IN operator.
 * @param q Query structure.
 * @param col Column name.
 * @param val_list Value list string.
 * @param out_node Output pointer receiving the AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_in_impl(c_orm_ast_query_t *q, const char *col,
                                         const char *val_list,
                                         c_orm_ast_node_t **out_node) {
  return c_orm_query_bin_cmp_impl(q, "IN", col, val_list, -1, out_node);
}

static c_orm_error_t c_orm_ast_clone_node(c_orm_arena_t *arena,
                                          c_orm_ast_node_t *node,
                                          c_orm_ast_node_t **out_node);

/**
 * @brief Deep-clones a child `c_orm_ast_query_t` and its entire AST node list
 * into `arena` so subquery lifetimes are self-contained inside the cloned
 * parent query's arena.
 * @param arena Target arena allocator.
 * @param src Source child query pointer.
 * @param out_q Output pointer receiving arena-allocated cloned query.
 * @return C_ORM_OK on success, or error code on allocation failure.
 */
static c_orm_error_t
c_orm_ast_clone_query_into_arena(c_orm_arena_t *arena,
                                 const c_orm_ast_query_t *src,
                                 c_orm_ast_query_t **out_q) {
  c_orm_ast_query_t *dst = NULL;
  c_orm_ast_node_t *curr;
  c_orm_ast_node_t *tail = NULL;
  c_orm_error_t rc;

  rc = c_orm_arena_alloc(arena, sizeof(c_orm_ast_query_t), (void **)&dst);
  if (rc != C_ORM_OK) {
    *out_q = NULL;
    return rc;
  }

  memcpy(dst, src, sizeof(c_orm_ast_query_t));
  dst->arena = arena;
  dst->ast_head = NULL;

  curr = src->ast_head;
  while (curr) {
    c_orm_ast_node_t *cloned_child = NULL;
    rc = c_orm_ast_clone_node(arena, curr, &cloned_child);
    if (rc != C_ORM_OK) {
      *out_q = NULL;
      return rc;
    }
    if (!tail) {
      dst->ast_head = cloned_child;
      tail = cloned_child;
    } else {
      tail->next = cloned_child;
      tail = cloned_child;
    }
    curr = curr->next;
  }

  *out_q = dst;
  return C_ORM_OK;
}

/**
 * @brief Clones an AST node.
 *
 * @param arena Arena for allocation.
 * @param node Node to clone.
 * @param out_node Output pointer receiving cloned node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_ast_clone_node(c_orm_arena_t *arena,
                                          c_orm_ast_node_t *node,
                                          c_orm_ast_node_t **out_node) {
  c_orm_ast_node_t *new_node = NULL;
  size_t size = 0;
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_ast_clone_node: entry");
  if (!node) {
    LOG_DEBUG("c_orm_ast_clone_node: node is null");
    *out_node = NULL;
    return C_ORM_OK;
  }

  switch (node->type) {
  case C_ORM_AST_NODE_SELECT:
    size = sizeof(c_orm_ast_select_t);
    break;
  case C_ORM_AST_NODE_FROM:
    size = sizeof(c_orm_ast_from_t);
    break;
  case C_ORM_AST_NODE_WHERE:
    size = sizeof(c_orm_ast_where_t);
    break;
  case C_ORM_AST_NODE_JOIN:
    size = sizeof(c_orm_ast_join_t);
    break;
  case C_ORM_AST_NODE_GROUP_BY:
    size = sizeof(c_orm_ast_group_by_t);
    break;
  case C_ORM_AST_NODE_HAVING:
    size = sizeof(c_orm_ast_having_t);
    break;
  case C_ORM_AST_NODE_ORDER_BY:
    size = sizeof(c_orm_ast_order_by_t);
    break;
  case C_ORM_AST_NODE_LIMIT:
    size = sizeof(c_orm_ast_limit_t);
    break;
  case C_ORM_AST_NODE_OFFSET:
    size = sizeof(c_orm_ast_offset_t);
    break;
  case C_ORM_AST_NODE_LITERAL:
    size = sizeof(c_orm_ast_literal_t);
    break;
  case C_ORM_AST_NODE_OPERATOR:
    size = sizeof(c_orm_ast_operator_t);
    break;
  case C_ORM_AST_NODE_RAW:
    size = sizeof(c_orm_ast_raw_t);
    break;
  case C_ORM_AST_NODE_COLUMN:
    size = sizeof(c_orm_ast_column_t);
    break;
  case C_ORM_AST_NODE_GROUP:
    size = sizeof(c_orm_ast_group_t);
    break;
  case C_ORM_AST_NODE_SUBQUERY:
    size = sizeof(c_orm_ast_subquery_t);
    break;
  case C_ORM_AST_NODE_UNION:
    size = sizeof(c_orm_ast_union_t);
    break;
  case C_ORM_AST_NODE_WITH:
    size = sizeof(c_orm_ast_with_t);
    break;
  case C_ORM_AST_NODE_FUNCTION:
    size = sizeof(c_orm_ast_function_t);
    break;
  case C_ORM_AST_NODE_CAST:
    size = sizeof(c_orm_ast_cast_t);
    break;
  case C_ORM_AST_NODE_BETWEEN:
    size = sizeof(c_orm_ast_between_t);
    break;
  case C_ORM_AST_NODE_EXISTS:
    size = sizeof(c_orm_ast_exists_t);
    break;
  case C_ORM_AST_NODE_WINDOW:
    size = sizeof(c_orm_ast_window_t);
    break;
  case C_ORM_AST_NODE_INSERT:
    size = sizeof(c_orm_ast_insert_t);
    break;
  case C_ORM_AST_NODE_UPDATE:
    size = sizeof(c_orm_ast_update_t);
    break;
  case C_ORM_AST_NODE_DELETE:
    size = sizeof(c_orm_ast_delete_t);
    break;
  case C_ORM_AST_NODE_ASSIGN:
    size = sizeof(c_orm_ast_assign_t);
    break;
  case C_ORM_AST_NODE_RETURNING:
    size = sizeof(c_orm_ast_returning_t);
    break;
  case C_ORM_AST_NODE_LOCK:
    size = sizeof(c_orm_ast_lock_t);
    break;
  }

  rc = c_orm_arena_alloc(arena, size, (void **)&new_node);
  if (rc != C_ORM_OK) {
    *out_node = NULL;
    return rc;
  }

  memcpy(new_node, node, size);
  new_node->next = NULL;

  if (node->type == C_ORM_AST_NODE_SELECT) {
    rc = c_orm_arena_strdup(arena, ((c_orm_ast_select_t *)node)->columns,
                            &((c_orm_ast_select_t *)new_node)->columns);
    if (rc != C_ORM_OK) {
      *out_node = NULL;
      return rc;
    }
  } else if (node->type == C_ORM_AST_NODE_FROM) {
    rc = c_orm_arena_strdup(arena, ((c_orm_ast_from_t *)node)->table,
                            &((c_orm_ast_from_t *)new_node)->table);
    if (rc != C_ORM_OK) {
      *out_node = NULL;
      return rc;
    }
    rc = c_orm_arena_strdup(arena, ((c_orm_ast_from_t *)node)->alias,
                            &((c_orm_ast_from_t *)new_node)->alias);
    if (rc != C_ORM_OK) {
      *out_node = NULL;
      return rc;
    }
  } else if (node->type == C_ORM_AST_NODE_COLUMN) {
    rc = c_orm_arena_strdup(arena, ((c_orm_ast_column_t *)node)->name,
                            &((c_orm_ast_column_t *)new_node)->name);
    if (rc != C_ORM_OK) {
      *out_node = NULL;
      return rc;
    }
  } else if (node->type == C_ORM_AST_NODE_RAW) {
    rc = c_orm_arena_strdup(arena, ((c_orm_ast_raw_t *)node)->sql,
                            &((c_orm_ast_raw_t *)new_node)->sql);
    if (rc != C_ORM_OK) {
      *out_node = NULL;
      return rc;
    }
  } else if (node->type == C_ORM_AST_NODE_WHERE) {
    rc = c_orm_ast_clone_node(arena, ((c_orm_ast_where_t *)node)->condition,
                              &((c_orm_ast_where_t *)new_node)->condition);
    if (rc != C_ORM_OK) {
      *out_node = NULL;
      return rc;
    }
  } else if (node->type == C_ORM_AST_NODE_JOIN) {
    rc = c_orm_ast_clone_node(arena, ((c_orm_ast_join_t *)node)->on_condition,
                              &((c_orm_ast_join_t *)new_node)->on_condition);
    if (rc != C_ORM_OK) {
      *out_node = NULL;
      return rc;
    }
  } else if (node->type == C_ORM_AST_NODE_HAVING) {
    rc = c_orm_ast_clone_node(arena, ((c_orm_ast_having_t *)node)->condition,
                              &((c_orm_ast_having_t *)new_node)->condition);
    if (rc != C_ORM_OK) {
      *out_node = NULL;
      return rc;
    }
  } else if (node->type == C_ORM_AST_NODE_OPERATOR) {
    rc = c_orm_ast_clone_node(arena, ((c_orm_ast_operator_t *)node)->left,
                              &((c_orm_ast_operator_t *)new_node)->left);
    if (rc != C_ORM_OK) {
      *out_node = NULL;
      return rc;
    }
    rc = c_orm_ast_clone_node(arena, ((c_orm_ast_operator_t *)node)->right,
                              &((c_orm_ast_operator_t *)new_node)->right);
    if (rc != C_ORM_OK) {
      *out_node = NULL;
      return rc;
    }
  } else if (node->type == C_ORM_AST_NODE_GROUP) {
    rc = c_orm_ast_clone_node(arena, ((c_orm_ast_group_t *)node)->expr,
                              &((c_orm_ast_group_t *)new_node)->expr);
    if (rc != C_ORM_OK) {
      *out_node = NULL;
      return rc;
    }
  } else if (node->type == C_ORM_AST_NODE_ASSIGN) {
    rc = c_orm_ast_clone_node(arena, ((c_orm_ast_assign_t *)node)->value_expr,
                              &((c_orm_ast_assign_t *)new_node)->value_expr);
    if (rc != C_ORM_OK) {
      *out_node = NULL;
      return rc;
    }
  } else if (node->type == C_ORM_AST_NODE_SUBQUERY) {
    rc = c_orm_arena_strdup(arena, ((c_orm_ast_subquery_t *)node)->alias,
                            &((c_orm_ast_subquery_t *)new_node)->alias);
    if (rc != C_ORM_OK) {
      *out_node = NULL;
      return rc;
    }
    if (((c_orm_ast_subquery_t *)node)->query) {
      rc = c_orm_ast_clone_query_into_arena(
          arena, ((c_orm_ast_subquery_t *)node)->query,
          &((c_orm_ast_subquery_t *)new_node)->query);
      if (rc != C_ORM_OK) {
        *out_node = NULL;
        return rc;
      }
    }
  } else if (node->type == C_ORM_AST_NODE_UNION &&
             ((c_orm_ast_union_t *)node)->query) {
    rc = c_orm_ast_clone_query_into_arena(
        arena, ((c_orm_ast_union_t *)node)->query,
        &((c_orm_ast_union_t *)new_node)->query);
    if (rc != C_ORM_OK) {
      *out_node = NULL;
      return rc;
    }
  } else if (node->type == C_ORM_AST_NODE_WITH) {
    rc = c_orm_arena_strdup(arena, ((c_orm_ast_with_t *)node)->alias,
                            &((c_orm_ast_with_t *)new_node)->alias);
    if (rc != C_ORM_OK) {
      *out_node = NULL;
      return rc;
    }
    if (((c_orm_ast_with_t *)node)->query) {
      rc = c_orm_ast_clone_query_into_arena(
          arena, ((c_orm_ast_with_t *)node)->query,
          &((c_orm_ast_with_t *)new_node)->query);
      if (rc != C_ORM_OK) {
        *out_node = NULL;
        return rc;
      }
    }
  } else if (node->type == C_ORM_AST_NODE_EXISTS &&
             ((c_orm_ast_exists_t *)node)->query) {
    rc = c_orm_ast_clone_query_into_arena(
        arena, ((c_orm_ast_exists_t *)node)->query,
        &((c_orm_ast_exists_t *)new_node)->query);
    if (rc != C_ORM_OK) {
      *out_node = NULL;
      return rc;
    }
  }

  LOG_DEBUG("c_orm_ast_clone_node: exit");
  *out_node = new_node;
  return C_ORM_OK;
}

/**
 * @brief Clones a query.
 *
 * @param q Query to clone.
 * @param out_q Pointer to the output query.
 * @return 0 on success, non-zero on error.
 */
static c_orm_error_t c_orm_query_clone(c_orm_ast_query_t *q,
                                       c_orm_ast_query_t **out_q) {
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
    rc = c_orm_ast_clone_node((*out_q)->arena, curr, &cloned);
    if (rc != C_ORM_OK) {
      c_orm_query_free(*out_q);
      *out_q = NULL;
      LOG_DEBUG("c_orm_query_clone: clone node failed");
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
  LOG_DEBUG("c_orm_query_clone: exit");
  return C_ORM_OK;
}

/**
 * @brief Adds a SELECT clause.
 *
 * @param q Query structure.
 * @param columns Columns string.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_select_impl(c_orm_ast_query_t *q,
                                             const char *columns) {
  c_orm_ast_select_t *node;
  const char *cols_copy = NULL;
  c_orm_error_t err;
  LOG_DEBUG("c_orm_query_select_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_select_impl: invalid state");
    return C_ORM_ERROR_UNKNOWN;
  }
  err = c_orm_arena_strdup(q->arena, columns, &cols_copy);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_select_impl: OOM for columns string");
    q->error = 1;
    return err;
  }
  err = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_select_t), (void **)&node);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_select_impl: OOM");
    q->error = 1;
    return err;
  }
  node->base.type = C_ORM_AST_NODE_SELECT;
  node->base.next = q->ast_head;
  node->columns = cols_copy;
  node->is_distinct = 0;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_select_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Adds a FROM clause.
 *
 * @param q Query structure.
 * @param table Table name.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_from_impl(c_orm_ast_query_t *q,
                                           const char *table) {
  c_orm_ast_from_t *node;
  const char *tbl_copy = NULL;
  c_orm_error_t err;
  LOG_DEBUG("c_orm_query_from_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_from_impl: invalid state");
    return C_ORM_ERROR_UNKNOWN;
  }
  err = c_orm_arena_strdup(q->arena, table, &tbl_copy);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_from_impl: OOM for table string");
    q->error = 1;
    return err;
  }
  err = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_from_t), (void **)&node);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_from_impl: OOM");
    q->error = 1;
    return err;
  }
  node->base.type = C_ORM_AST_NODE_FROM;
  node->base.next = q->ast_head;
  node->table = tbl_copy;
  node->alias = NULL;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_from_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Adds a WHERE clause.
 *
 * @param q Query structure.
 * @param condition Condition node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_where_impl(c_orm_ast_query_t *q,
                                            c_orm_ast_node_t *condition) {
  c_orm_ast_where_t *node;
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_query_where_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_where_impl: invalid state");
    return C_ORM_ERROR_UNKNOWN;
  }
  rc = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_where_t), (void **)&node);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_where_impl: OOM");
    q->error = 1;
    return rc;
  }
  node->base.type = C_ORM_AST_NODE_WHERE;
  node->base.next = q->ast_head;
  node->condition = condition;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_where_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Adds an AND WHERE condition.
 *
 * @param q Query structure.
 * @param condition Condition node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_and_where_impl(c_orm_ast_query_t *q,
                                                c_orm_ast_node_t *condition) {
  c_orm_ast_node_t *curr;
  c_orm_error_t ret;
  c_orm_ast_where_t *w;
  LOG_DEBUG("c_orm_query_and_where_impl: entry");
  if (!q || q->error || !condition) {
    LOG_DEBUG("c_orm_query_and_where_impl: invalid state");
    return C_ORM_ERROR_UNKNOWN;
  }
  curr = q->ast_head;
  while (curr) {
    if (curr->type == C_ORM_AST_NODE_WHERE) {
      w = (c_orm_ast_where_t *)curr;
      if (w->condition) {
        ret = c_orm_query_op_impl(q, "AND", w->condition, condition,
                                  &w->condition);
        if (ret != C_ORM_OK) {
          return ret;
        }
      } else {
        w->condition = condition;
      }
      LOG_DEBUG("c_orm_query_and_where_impl: exit updated");
      return C_ORM_OK;
    }
    curr = curr->next;
  }
  ret = q->where(q, condition);
  if (ret != C_ORM_OK) {
    return ret;
  }
  LOG_DEBUG("c_orm_query_and_where_impl: exit created");
  return C_ORM_OK;
}

/**
 * @brief Adds an OR WHERE condition.
 *
 * @param q Query structure.
 * @param condition Condition node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_or_where_impl(c_orm_ast_query_t *q,
                                               c_orm_ast_node_t *condition) {
  c_orm_ast_node_t *curr;
  c_orm_error_t ret;
  c_orm_ast_where_t *w;
  LOG_DEBUG("c_orm_query_or_where_impl: entry");
  if (!q || q->error || !condition) {
    LOG_DEBUG("c_orm_query_or_where_impl: invalid state");
    return C_ORM_ERROR_UNKNOWN;
  }
  curr = q->ast_head;
  while (curr) {
    if (curr->type == C_ORM_AST_NODE_WHERE) {
      w = (c_orm_ast_where_t *)curr;
      if (w->condition) {
        ret = c_orm_query_op_impl(q, "OR", w->condition, condition,
                                  &w->condition);
        if (ret != C_ORM_OK) {
          return ret;
        }
      } else {
        w->condition = condition;
      }
      LOG_DEBUG("c_orm_query_or_where_impl: exit updated");
      return C_ORM_OK;
    }
    curr = curr->next;
  }
  ret = q->where(q, condition);
  if (ret != C_ORM_OK) {
    return ret;
  }
  LOG_DEBUG("c_orm_query_or_where_impl: exit created");
  return C_ORM_OK;
}

/**
 * @brief Adds an ORDER BY clause.
 *
 * @param q Query structure.
 * @param column Column string.
 * @param is_desc Descending flag.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_order_by_impl(c_orm_ast_query_t *q,
                                               const char *column,
                                               int is_desc) {
  c_orm_ast_order_by_t *node;
  const char *col_copy = NULL;
  c_orm_error_t err;
  LOG_DEBUG("c_orm_query_order_by_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_order_by_impl: invalid state");
    return C_ORM_ERROR_UNKNOWN;
  }
  err = c_orm_arena_strdup(q->arena, column, &col_copy);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_order_by_impl: OOM for column string");
    q->error = 1;
    return err;
  }
  err =
      c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_order_by_t), (void **)&node);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_order_by_impl: OOM");
    q->error = 1;
    return err;
  }
  node->base.type = C_ORM_AST_NODE_ORDER_BY;
  node->base.next = q->ast_head;
  node->column = col_copy;
  node->is_desc = is_desc;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_order_by_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Adds a LIMIT clause.
 *
 * @param q Query structure.
 * @param n Limit number.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_limit_impl(c_orm_ast_query_t *q, size_t n) {
  c_orm_ast_limit_t *node;
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_query_limit_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_limit_impl: invalid state");
    return C_ORM_ERROR_UNKNOWN;
  }
  rc = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_limit_t), (void **)&node);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_limit_impl: OOM");
    q->error = 1;
    return rc;
  }
  node->base.type = C_ORM_AST_NODE_LIMIT;
  node->base.next = q->ast_head;
  node->limit = n;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_limit_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Adds an OFFSET clause.
 *
 * @param q Query structure.
 * @param n Offset number.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_offset_impl(c_orm_ast_query_t *q, size_t n) {
  c_orm_ast_offset_t *node;
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_query_offset_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_offset_impl: invalid state");
    return C_ORM_ERROR_UNKNOWN;
  }
  rc = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_offset_t), (void **)&node);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_offset_impl: OOM");
    q->error = 1;
    return rc;
  }
  node->base.type = C_ORM_AST_NODE_OFFSET;
  node->base.next = q->ast_head;
  node->offset = n;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_offset_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Adds a JOIN clause.
 *
 * @param q Query structure.
 * @param table Table name.
 * @param type_str Join type string.
 * @param on_condition On condition node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_join_impl(c_orm_ast_query_t *q,
                                           const char *table,
                                           const char *type_str,
                                           c_orm_ast_node_t *on_condition) {
  c_orm_ast_join_t *node;
  const char *tbl_copy = NULL;
  const char *type_copy = NULL;
  c_orm_error_t err;
  LOG_DEBUG("c_orm_query_join_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_join_impl: invalid state");
    return C_ORM_ERROR_UNKNOWN;
  }
  err = c_orm_arena_strdup(q->arena, table, &tbl_copy);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_join_impl: OOM for table string");
    q->error = 1;
    return err;
  }
  err = c_orm_arena_strdup(q->arena, type_str, &type_copy);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_join_impl: OOM for type string");
    q->error = 1;
    return err;
  }
  err = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_join_t), (void **)&node);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_join_impl: OOM");
    q->error = 1;
    return err;
  }
  node->base.type = C_ORM_AST_NODE_JOIN;
  node->base.next = q->ast_head;
  node->table = tbl_copy;
  node->type_str = type_copy;
  node->on_condition = on_condition;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_join_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Adds a LEFT JOIN.
 * @param q Query structure.
 * @param table Target table name.
 * @param on_condition ON join condition AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t
c_orm_query_left_join_impl(c_orm_ast_query_t *q, const char *table,
                           c_orm_ast_node_t *on_condition) {
  c_orm_error_t ret;
  LOG_DEBUG("c_orm_query_left_join_impl: entry");
  ret = c_orm_query_join_impl(q, table, "LEFT", on_condition);
  if (ret != C_ORM_OK) {
    return ret;
  }
  LOG_DEBUG("c_orm_query_left_join_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Adds a RIGHT JOIN.
 * @param q Query structure.
 * @param table Target table name.
 * @param on_condition ON join condition AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t
c_orm_query_right_join_impl(c_orm_ast_query_t *q, const char *table,
                            c_orm_ast_node_t *on_condition) {
  c_orm_error_t ret;
  LOG_DEBUG("c_orm_query_right_join_impl: entry");
  ret = c_orm_query_join_impl(q, table, "RIGHT", on_condition);
  if (ret != C_ORM_OK) {
    return ret;
  }
  LOG_DEBUG("c_orm_query_right_join_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Adds a GROUP BY clause.
 *
 * @param q Query structure.
 * @param columns Columns string.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_group_by_impl(c_orm_ast_query_t *q,
                                               const char *columns) {
  c_orm_ast_group_by_t *node;
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_query_group_by_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_group_by_impl: invalid state");
    return C_ORM_ERROR_UNKNOWN;
  }
  rc =
      c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_group_by_t), (void **)&node);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_group_by_impl: OOM");
    q->error = 1;
    return rc;
  }
  node->base.type = C_ORM_AST_NODE_GROUP_BY;
  node->base.next = q->ast_head;
  node->columns = columns;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_group_by_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Adds a HAVING clause.
 *
 * @param q Query structure.
 * @param condition Condition node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_having_impl(c_orm_ast_query_t *q,
                                             c_orm_ast_node_t *condition) {
  c_orm_ast_having_t *node;
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_query_having_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_having_impl: invalid state");
    return C_ORM_ERROR_UNKNOWN;
  }
  rc = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_having_t), (void **)&node);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_having_impl: OOM");
    q->error = 1;
    return rc;
  }
  node->base.type = C_ORM_AST_NODE_HAVING;
  node->base.next = q->ast_head;
  node->condition = condition;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_having_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Adds a WITH clause.
 *
 * @param q Query structure.
 * @param alias Alias string.
 * @param subquery Subquery.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_with_impl(c_orm_ast_query_t *q,
                                           const char *alias,
                                           c_orm_ast_query_t *subquery) {
  c_orm_ast_with_t *node;
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_query_with_impl: entry");
  if (!q || q->error || !subquery) {
    LOG_DEBUG("c_orm_query_with_impl: invalid state");
    return C_ORM_ERROR_UNKNOWN;
  }
  rc = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_with_t), (void **)&node);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_with_impl: OOM");
    q->error = 1;
    return rc;
  }
  node->base.type = C_ORM_AST_NODE_WITH;
  node->base.next = q->ast_head;
  node->alias = alias;
  node->query = subquery;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_with_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Adds a UNION clause.
 *
 * @param q Query structure.
 * @param other Other query.
 * @param is_all UNION ALL flag.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_union_impl(c_orm_ast_query_t *q,
                                            c_orm_ast_query_t *other,
                                            int is_all) {
  c_orm_ast_union_t *node;
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_query_union_impl: entry");
  if (!q || q->error || !other) {
    LOG_DEBUG("c_orm_query_union_impl: invalid state");
    return C_ORM_ERROR_UNKNOWN;
  }
  rc = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_union_t), (void **)&node);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_union_impl: OOM");
    q->error = 1;
    return rc;
  }
  node->base.type = C_ORM_AST_NODE_UNION;
  node->base.next = q->ast_head;
  node->is_all = is_all;
  node->query = other;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_union_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Sets DISTINCT flag on query.
 *
 * @param q Query structure.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_distinct_impl(c_orm_ast_query_t *q) {
  c_orm_ast_node_t *curr;
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_query_distinct_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_distinct_impl: invalid state");
    return C_ORM_ERROR_UNKNOWN;
  }
  curr = q->ast_head;
  while (curr) {
    if (curr->type == C_ORM_AST_NODE_SELECT) {
      ((c_orm_ast_select_t *)curr)->is_distinct = 1;
      LOG_DEBUG("c_orm_query_distinct_impl: exit modified existing");
      return C_ORM_OK;
    }
    curr = curr->next;
  }
  rc = q->select_(q, "*");
  if (rc != C_ORM_OK) {
    return rc;
  }
  ((c_orm_ast_select_t *)q->ast_head)->is_distinct = 1;
  LOG_DEBUG("c_orm_query_distinct_impl: exit new select");
  return C_ORM_OK;
}

/**
 * @brief Adds FROM clause with alias.
 *
 * @param q Query structure.
 * @param table Table name.
 * @param alias Alias name.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_from_alias_impl(c_orm_ast_query_t *q,
                                                 const char *table,
                                                 const char *alias) {
  c_orm_ast_from_t *node;
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_query_from_alias_impl: entry");
  if (!q || q->error) {
    LOG_DEBUG("c_orm_query_from_alias_impl: invalid state");
    return C_ORM_ERROR_UNKNOWN;
  }
  rc = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_from_t), (void **)&node);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_from_alias_impl: OOM");
    q->error = 1;
    return rc;
  }
  node->base.type = C_ORM_AST_NODE_FROM;
  node->base.next = q->ast_head;
  node->table = table;
  node->alias = alias;
  q->ast_head = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_from_alias_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Formats `<lhs_tbl>.<lhs_col> = <rhs_tbl>.<rhs_col>` dynamically and
 * appends a `LEFT JOIN <join_tbl>` AST node.
 * @param q Target query AST builder.
 * @param lhs_tbl Left table identifier.
 * @param lhs_col Left column identifier.
 * @param rhs_tbl Right table identifier.
 * @param rhs_col Right column identifier.
 * @param join_tbl Joined table identifier.
 * @return C_ORM_OK on success, or error code on OOM/free failure.
 */
static c_orm_error_t
emit_eager_left_join(c_orm_ast_query_t *q, const char *lhs_tbl,
                     const char *lhs_col, const char *rhs_tbl,
                     const char *rhs_col, const char *join_tbl) {
  const char *lc = lhs_col ? lhs_col : "id";
  const char *rc_s = rhs_col ? rhs_col : "id";
  size_t l_tlen = strlen(lhs_tbl);
  size_t l_clen = strlen(lc);
  size_t r_tlen = strlen(rhs_tbl);
  size_t r_clen = strlen(rc_s);
  size_t total_len = l_tlen + 1 + l_clen + 3 + r_tlen + 1 + r_clen + 1;
  char *on_cond = NULL;
  char *w;
  c_orm_ast_node_t *raw_node = NULL;
  c_orm_error_t rc;

  rc = c_orm_system_malloc(total_len, (void **)&on_cond);
  if (rc != C_ORM_OK) {
    q->error = 1;
    return rc;
  }

  w = on_cond;
  memcpy(w, lhs_tbl, l_tlen);
  w += l_tlen;
  *w++ = '.';
  memcpy(w, lc, l_clen);
  w += l_clen;
  memcpy(w, " = ", 3);
  w += 3;
  memcpy(w, rhs_tbl, r_tlen);
  w += r_tlen;
  *w++ = '.';
  memcpy(w, rc_s, r_clen);
  w += r_clen;
  *w = '\0';

  rc = q->raw(q, on_cond, &raw_node);
  if (rc != C_ORM_OK) {
    c_orm_system_free(on_cond);
    q->error = 1;
    return rc;
  }
  rc = q->left_join(q, join_tbl, raw_node);
  if (rc != C_ORM_OK) {
    c_orm_system_free(on_cond);
    q->error = 1;
    return rc;
  }
  rc = c_orm_system_free(on_cond);
  if (rc != C_ORM_OK) {
    q->error = 1;
    return rc;
  }
  return C_ORM_OK;
}

/**
 * @brief Eager loads a relation (`LEFT JOIN` + aliased `SELECT` projection),
 * including double-`LEFT JOIN` for `MANY_TO_MANY`.
 *
 * @param q Query structure.
 * @param meta Table metadata.
 * @param relation_name Relation name.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_eager_load_impl(c_orm_ast_query_t *q,
                                                 const c_orm_table_meta_t *meta,
                                                 const char *relation_name) {
  size_t i;
  const c_orm_relation_meta_t *rel = NULL;
  char *columns;
  c_orm_ast_node_t *curr;
  char *p;
  size_t col_i;
  size_t cols_cap;
  int first;
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_query_eager_load_impl: entry");

  if (!q || q->error || !meta || !relation_name) {
    LOG_DEBUG("c_orm_query_eager_load_impl: invalid state or args");
    return C_ORM_ERROR_UNKNOWN;
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
    return C_ORM_ERROR_NOT_FOUND;
  }

  if (rel->type == C_ORM_RELATION_MANY_TO_MANY) {
    const char *parent_key = rel->local_key;
    const char *target_key = rel->foreign_key;
    if (!parent_key) {
      for (col_i = 0; col_i < meta->num_columns; col_i++) {
        if (meta->columns[col_i].is_pk) {
          parent_key = meta->columns[col_i].name;
          break;
        }
      }
    }
    if (!target_key) {
      for (col_i = 0; col_i < rel->target_meta->num_columns; col_i++) {
        if (rel->target_meta->columns[col_i].is_pk) {
          target_key = rel->target_meta->columns[col_i].name;
          break;
        }
      }
    }
    if (!rel->join_table || !rel->join_local_key || !rel->join_foreign_key ||
        !parent_key || !target_key) {
      q->error = 1;
      return C_ORM_ERROR_VALIDATION;
    }
    rc = emit_eager_left_join(q, meta->name, parent_key, rel->join_table,
                              rel->join_local_key, rel->join_table);
    if (rc != C_ORM_OK) {
      return rc;
    }
    rc = emit_eager_left_join(q, rel->join_table, rel->join_foreign_key,
                              rel->target_meta->name, target_key,
                              rel->target_meta->name);
    if (rc != C_ORM_OK) {
      return rc;
    }
  } else {
    rc = emit_eager_left_join(q, meta->name, rel->local_key,
                              rel->target_meta->name, rel->foreign_key,
                              rel->target_meta->name);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }

  /* Compute exact dynamic capacity for expanded SELECT projection */
  cols_cap = 512;
  {
    size_t m_nlen = strlen(meta->name);
    size_t t_nlen = strlen(rel->target_meta->name);
    size_t r_nlen = strlen(relation_name);
    for (col_i = 0; col_i < meta->num_columns; col_i++) {
      cols_cap += m_nlen + 1 + strlen(meta->columns[col_i].name) + 2;
    }
    for (col_i = 0; col_i < rel->target_meta->num_columns; col_i++) {
      size_t c_nlen = strlen(rel->target_meta->columns[col_i].name);
      cols_cap += t_nlen + 1 + c_nlen + 4 + r_nlen + 1 + c_nlen + 2;
    }
  }

  /* Expand SELECT columns dynamically to add child columns with prefix
   * `relation_name_` */
  rc = c_orm_arena_alloc(q->arena, cols_cap, (void **)&columns);
  if (rc != C_ORM_OK) {
    q->error = 1;
    return rc;
  }
  {
    size_t m_nlen = strlen(meta->name);
    size_t t_nlen = strlen(rel->target_meta->name);
    size_t r_nlen = strlen(relation_name);
    p = columns;
    first = 1;

    for (col_i = 0; col_i < meta->num_columns; col_i++) {
      const char *cn = meta->columns[col_i].name;
      size_t c_nlen = strlen(cn);
      if (!first) {
        *p++ = ',';
        *p++ = ' ';
      }
      first = 0;
      memcpy(p, meta->name, m_nlen);
      p += m_nlen;
      *p++ = '.';
      memcpy(p, cn, c_nlen);
      p += c_nlen;
    }

    for (col_i = 0; col_i < rel->target_meta->num_columns; col_i++) {
      const char *cn = rel->target_meta->columns[col_i].name;
      size_t c_nlen = strlen(cn);
      if (!first) {
        *p++ = ',';
        *p++ = ' ';
      }
      first = 0;
      memcpy(p, rel->target_meta->name, t_nlen);
      p += t_nlen;
      *p++ = '.';
      memcpy(p, cn, c_nlen);
      p += c_nlen;
      memcpy(p, " AS ", 4);
      p += 4;
      memcpy(p, relation_name, r_nlen);
      p += r_nlen;
      *p++ = '_';
      memcpy(p, cn, c_nlen);
      p += c_nlen;
    }
    *p = '\0';

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
  return q->error ? C_ORM_ERROR_MEMORY : C_ORM_OK;
}

/**
 * @brief Creates a group AST node.
 *
 * @param q Query structure.
 * @param expr Expression node.
 * @param out_node Output pointer receiving the AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_group_node_impl(c_orm_ast_query_t *q,
                                                 c_orm_ast_node_t *expr,
                                                 c_orm_ast_node_t **out_node) {
  c_orm_ast_group_t *node;
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_query_group_node_impl: entry");
  if (!q || q->error || !out_node) {
    LOG_DEBUG("c_orm_query_group_node_impl: invalid state");
    if (out_node)
      *out_node = NULL;
    return C_ORM_ERROR_UNKNOWN;
  }
  rc = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_group_t), (void **)&node);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_group_node_impl: OOM");
    q->error = 1;
    *out_node = NULL;
    return rc;
  }
  node->base.type = C_ORM_AST_NODE_GROUP;
  node->base.next = NULL;
  node->expr = expr;
  *out_node = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_group_node_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Creates a subquery AST node.
 *
 * @param q Query structure.
 * @param subq Subquery.
 * @param alias Alias.
 * @param out_node Output pointer receiving the AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t
c_orm_query_subquery_node_impl(c_orm_ast_query_t *q, c_orm_ast_query_t *subq,
                               const char *alias, c_orm_ast_node_t **out_node) {
  c_orm_ast_subquery_t *node;
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_query_subquery_node_impl: entry");
  if (!q || q->error || !subq || !out_node) {
    LOG_DEBUG("c_orm_query_subquery_node_impl: invalid state");
    if (out_node)
      *out_node = NULL;
    return C_ORM_ERROR_UNKNOWN;
  }
  rc =
      c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_subquery_t), (void **)&node);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_subquery_node_impl: OOM");
    q->error = 1;
    *out_node = NULL;
    return rc;
  }
  node->base.type = C_ORM_AST_NODE_SUBQUERY;
  node->base.next = NULL;
  node->query = subq;
  node->alias = alias;
  *out_node = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_subquery_node_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Creates a function AST node.
 *
 * @param q Query structure.
 * @param name Function name.
 * @param args Arguments string.
 * @param alias Alias.
 * @param out_node Output pointer receiving the AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_func_impl(c_orm_ast_query_t *q,
                                           const char *name, const char *args,
                                           const char *alias,
                                           c_orm_ast_node_t **out_node) {
  c_orm_ast_function_t *node;
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_query_func_impl: entry");
  if (!q || q->error || !out_node) {
    LOG_DEBUG("c_orm_query_func_impl: invalid state");
    if (out_node)
      *out_node = NULL;
    return C_ORM_ERROR_UNKNOWN;
  }
  rc =
      c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_function_t), (void **)&node);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_func_impl: OOM");
    q->error = 1;
    *out_node = NULL;
    return rc;
  }
  node->base.type = C_ORM_AST_NODE_FUNCTION;
  node->base.next = NULL;
  node->name = name;
  node->args = args;
  node->alias = alias;
  *out_node = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_func_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Creates a CAST AST node.
 *
 * @param q Query structure.
 * @param col Column name.
 * @param type Cast type.
 * @param out_node Output pointer receiving the AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_cast_impl(c_orm_ast_query_t *q,
                                           const char *col, const char *type,
                                           c_orm_ast_node_t **out_node) {
  c_orm_ast_cast_t *node;
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_query_cast_impl: entry");
  if (!q || q->error || !out_node) {
    LOG_DEBUG("c_orm_query_cast_impl: invalid state");
    if (out_node)
      *out_node = NULL;
    return C_ORM_ERROR_UNKNOWN;
  }
  rc = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_cast_t), (void **)&node);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_cast_impl: OOM");
    q->error = 1;
    *out_node = NULL;
    return rc;
  }
  node->base.type = C_ORM_AST_NODE_CAST;
  node->base.next = NULL;
  node->col = col;
  node->type = type;
  *out_node = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_cast_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Creates an IS NULL AST node.
 *
 * @param q Query structure.
 * @param col Column name.
 * @param is_not NOT flag.
 * @param out_node Output pointer receiving the AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_is_null_impl(c_orm_ast_query_t *q,
                                              const char *col, int is_not,
                                              c_orm_ast_node_t **out_node) {
  c_orm_ast_operator_t *node;
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_query_is_null_impl: entry");
  if (!q || q->error || !out_node) {
    LOG_DEBUG("c_orm_query_is_null_impl: invalid state");
    if (out_node)
      *out_node = NULL;
    return C_ORM_ERROR_UNKNOWN;
  }
  rc =
      c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_operator_t), (void **)&node);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_is_null_impl: OOM");
    q->error = 1;
    *out_node = NULL;
    return rc;
  }
  node->base.type = C_ORM_AST_NODE_OPERATOR;
  node->base.next = NULL;
  node->op = is_not ? "IS NOT NULL" : "IS NULL";
  node->right = NULL;
  rc = q->col(q, col, &node->left);
  if (rc != C_ORM_OK) {
    q->error = 1;
    *out_node = NULL;
    return rc;
  }
  *out_node = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_is_null_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Creates a BETWEEN AST node.
 *
 * @param q Query structure.
 * @param col Column name.
 * @param low Low value.
 * @param high High value.
 * @param is_string String flag.
 * @param out_node Output pointer receiving the AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_between_impl(c_orm_ast_query_t *q,
                                              const char *col, const char *low,
                                              const char *high, int is_string,
                                              c_orm_ast_node_t **out_node) {
  c_orm_ast_between_t *node;
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_query_between_impl: entry");
  if (!q || q->error || !out_node) {
    LOG_DEBUG("c_orm_query_between_impl: invalid state");
    if (out_node)
      *out_node = NULL;
    return C_ORM_ERROR_UNKNOWN;
  }
  rc = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_between_t), (void **)&node);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_between_impl: OOM");
    q->error = 1;
    *out_node = NULL;
    return rc;
  }
  node->base.type = C_ORM_AST_NODE_BETWEEN;
  node->base.next = NULL;
  node->col = col;
  if (is_string) {
    rc = c_orm_escape_literal(q->arena, low, &node->low);
    if (rc != C_ORM_OK) {
      q->error = 1;
      *out_node = NULL;
      return rc;
    }
    rc = c_orm_escape_literal(q->arena, high, &node->high);
    if (rc != C_ORM_OK) {
      q->error = 1;
      *out_node = NULL;
      return rc;
    }
  } else {
    node->low = low;
    node->high = high;
  }
  node->is_string = is_string;
  *out_node = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_between_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Creates an EXISTS AST node.
 *
 * @param q Query structure.
 * @param subq Subquery.
 * @param is_not NOT flag.
 * @param out_node Output pointer receiving the AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_exists_impl(c_orm_ast_query_t *q,
                                             c_orm_ast_query_t *subq,
                                             int is_not,
                                             c_orm_ast_node_t **out_node) {
  c_orm_ast_exists_t *node;
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_query_exists_impl: entry");
  if (!q || q->error || !subq || !out_node) {
    LOG_DEBUG("c_orm_query_exists_impl: invalid state");
    if (out_node)
      *out_node = NULL;
    return C_ORM_ERROR_UNKNOWN;
  }
  rc = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_exists_t), (void **)&node);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_exists_impl: OOM");
    q->error = 1;
    *out_node = NULL;
    return rc;
  }
  node->base.type = C_ORM_AST_NODE_EXISTS;
  node->base.next = NULL;
  node->query = subq;
  node->is_not = is_not;
  *out_node = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_exists_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Creates a WINDOW function AST node.
 *
 * @param q Query structure.
 * @param func_name Function name.
 * @param partition_by Partition by.
 * @param order_by Order by.
 * @param alias Alias.
 * @param out_node Output pointer receiving the AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t
c_orm_query_window_impl(c_orm_ast_query_t *q, const char *func_name,
                        const char *partition_by, const char *order_by,
                        const char *alias, c_orm_ast_node_t **out_node) {
  c_orm_ast_window_t *node;
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_query_window_impl: entry");
  if (!q || q->error || !out_node) {
    LOG_DEBUG("c_orm_query_window_impl: invalid state");
    if (out_node)
      *out_node = NULL;
    return C_ORM_ERROR_UNKNOWN;
  }
  rc = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_window_t), (void **)&node);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_window_impl: OOM");
    q->error = 1;
    *out_node = NULL;
    return rc;
  }
  node->base.type = C_ORM_AST_NODE_WINDOW;
  node->base.next = NULL;
  node->func_name = func_name;
  node->partition_by = partition_by;
  node->order_by = order_by;
  node->alias = alias;
  *out_node = (c_orm_ast_node_t *)node;
  LOG_DEBUG("c_orm_query_window_impl: exit");
  return C_ORM_OK;
}

/**
 * @brief Adds an INSERT INTO target table AST node.
 *
 * @param q Query structure.
 * @param table Target table name.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_insert_into_impl(c_orm_ast_query_t *q,
                                                  const char *table) {
  c_orm_ast_insert_t *node;
  const char *tbl_copy = NULL;
  c_orm_error_t err;
  if (!q || q->error || !table || table[0] == '\0') {
    return C_ORM_ERROR_VALIDATION;
  }
  err = c_orm_arena_strdup(q->arena, table, &tbl_copy);
  if (err != C_ORM_OK) {
    q->error = 1;
    return err;
  }
  err = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_insert_t), (void **)&node);
  if (err != C_ORM_OK) {
    q->error = 1;
    return err;
  }
  node->base.type = C_ORM_AST_NODE_INSERT;
  node->base.next = q->ast_head;
  node->table = tbl_copy;
  q->ast_head = (c_orm_ast_node_t *)node;
  return C_ORM_OK;
}

/**
 * @brief Adds an UPDATE target table AST node.
 *
 * @param q Query structure.
 * @param table Target table name.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_update_table_impl(c_orm_ast_query_t *q,
                                                   const char *table) {
  c_orm_ast_update_t *node;
  const char *tbl_copy = NULL;
  c_orm_error_t err;
  if (!q || q->error || !table || table[0] == '\0') {
    return C_ORM_ERROR_VALIDATION;
  }
  err = c_orm_arena_strdup(q->arena, table, &tbl_copy);
  if (err != C_ORM_OK) {
    q->error = 1;
    return err;
  }
  err = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_update_t), (void **)&node);
  if (err != C_ORM_OK) {
    q->error = 1;
    return err;
  }
  node->base.type = C_ORM_AST_NODE_UPDATE;
  node->base.next = q->ast_head;
  node->table = tbl_copy;
  q->ast_head = (c_orm_ast_node_t *)node;
  return C_ORM_OK;
}

/**
 * @brief Adds a DELETE FROM target table AST node.
 *
 * @param q Query structure.
 * @param table Target table name.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_delete_from_impl(c_orm_ast_query_t *q,
                                                  const char *table) {
  c_orm_ast_delete_t *node;
  const char *tbl_copy = NULL;
  c_orm_error_t err;
  if (!q || q->error || !table || table[0] == '\0') {
    return C_ORM_ERROR_VALIDATION;
  }
  err = c_orm_arena_strdup(q->arena, table, &tbl_copy);
  if (err != C_ORM_OK) {
    q->error = 1;
    return err;
  }
  err = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_delete_t), (void **)&node);
  if (err != C_ORM_OK) {
    q->error = 1;
    return err;
  }
  node->base.type = C_ORM_AST_NODE_DELETE;
  node->base.next = q->ast_head;
  node->table = tbl_copy;
  q->ast_head = (c_orm_ast_node_t *)node;
  return C_ORM_OK;
}

/**
 * @brief Appends a column-value assignment AST node for INSERT or UPDATE.
 *
 * @param q Query structure.
 * @param column Target column name.
 * @param value_expr Value expression AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_set_col_impl(c_orm_ast_query_t *q,
                                              const char *column,
                                              c_orm_ast_node_t *value_expr) {
  c_orm_ast_assign_t *node;
  const char *col_copy = NULL;
  c_orm_error_t err;
  if (!q || q->error || !column || column[0] == '\0' || !value_expr) {
    return C_ORM_ERROR_VALIDATION;
  }
  err = c_orm_arena_strdup(q->arena, column, &col_copy);
  if (err != C_ORM_OK) {
    q->error = 1;
    return err;
  }
  err = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_assign_t), (void **)&node);
  if (err != C_ORM_OK) {
    q->error = 1;
    return err;
  }
  node->base.type = C_ORM_AST_NODE_ASSIGN;
  node->base.next = q->ast_head;
  node->column = col_copy;
  node->value_expr = value_expr;
  q->ast_head = (c_orm_ast_node_t *)node;
  return C_ORM_OK;
}

/**
 * @brief Adds a RETURNING (or Db2 FINAL/OLD TABLE) projection AST node.
 *
 * @param q Query structure.
 * @param columns Comma-separated column projection list.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_returning_impl(c_orm_ast_query_t *q,
                                                const char *columns) {
  c_orm_ast_returning_t *node;
  const char *cols_copy = NULL;
  c_orm_error_t err;
  if (!q || q->error || !columns || columns[0] == '\0') {
    return C_ORM_ERROR_VALIDATION;
  }
  err = c_orm_arena_strdup(q->arena, columns, &cols_copy);
  if (err != C_ORM_OK) {
    q->error = 1;
    return err;
  }
  err = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_returning_t),
                          (void **)&node);
  if (err != C_ORM_OK) {
    q->error = 1;
    return err;
  }
  node->base.type = C_ORM_AST_NODE_RETURNING;
  node->base.next = q->ast_head;
  node->columns = cols_copy;
  q->ast_head = (c_orm_ast_node_t *)node;
  return C_ORM_OK;
}

/**
 * @brief Adds a Db2 isolation (`WITH UR`/`CS`/`RS`/`RR`) or `FOR FETCH ONLY`
 * lock AST node.
 *
 * @param q Query structure.
 * @param mode Isolation or cursor read-only mode.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_with_lock_impl(c_orm_ast_query_t *q,
                                                c_orm_ast_lock_mode_t mode) {
  c_orm_ast_lock_t *node;
  c_orm_error_t err;
  if (!q || q->error) {
    return C_ORM_ERROR_VALIDATION;
  }
  err = c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_lock_t), (void **)&node);
  if (err != C_ORM_OK) {
    q->error = 1;
    return err;
  }
  node->base.type = C_ORM_AST_NODE_LOCK;
  node->base.next = q->ast_head;
  node->mode = mode;
  q->ast_head = (c_orm_ast_node_t *)node;
  return C_ORM_OK;
}

/**
 * @brief Constructs an exact fixed-point decimal literal AST node.
 *
 * @param q Query structure.
 * @param dec Exact decimal value to embed.
 * @param out_node Output pointer receiving the literal AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_query_lit_decimal_impl(c_orm_ast_query_t *q,
                                                  const c_orm_decimal_t *dec,
                                                  c_orm_ast_node_t **out_node) {
  c_orm_ast_literal_t *node;
  char dec_buf[64];
  size_t dec_len = 0;
  const char *val_copy = NULL;
  c_orm_error_t err;

  if (!q || q->error || !dec || !out_node) {
    if (out_node) {
      *out_node = NULL;
    }
    return C_ORM_ERROR_VALIDATION;
  }
  err = c_orm_decimal_to_str(dec, dec_buf, sizeof(dec_buf), &dec_len);
  if (err != C_ORM_OK) {
    *out_node = NULL;
    return err;
  }
  err = c_orm_arena_strdup(q->arena, dec_buf, &val_copy);
  if (err != C_ORM_OK) {
    q->error = 1;
    *out_node = NULL;
    return err;
  }
  err =
      c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_literal_t), (void **)&node);
  if (err != C_ORM_OK) {
    q->error = 1;
    *out_node = NULL;
    return err;
  }
  node->base.type = C_ORM_AST_NODE_LITERAL;
  node->base.next = NULL;
  node->value = val_copy;
  node->is_string = 0;
  node->is_decimal = 1;
  node->decimal_val = *dec;
  *out_node = (c_orm_ast_node_t *)node;
  return C_ORM_OK;
}

/**
 * @brief Allocates and initializes a new fluent query structure.
 *
 * @param out_query Pointer to the output query.
 * @return C_ORM_OK on success, or error code on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_query_new(c_orm_ast_query_t **out_query) {
  c_orm_error_t rc;
  c_orm_ast_query_t *q;

  LOG_DEBUG("c_orm_query_new: entry");

  if (!out_query) {
    LOG_DEBUG("c_orm_query_new: null argument");
    return C_ORM_ERROR_UNKNOWN;
  }

  q = NULL;
  rc = c_orm_system_malloc(sizeof(c_orm_ast_query_t), (void **)&q);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_new: OOM");
    return rc;
  }

  rc = c_orm_arena_new(&q->arena);
  if (rc != C_ORM_OK) {
    c_orm_error_t free_rc = c_orm_system_free(q);
    LOG_DEBUG("c_orm_query_new: arena OOM");
    if (free_rc != C_ORM_OK) {
      return free_rc;
    }
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
  q->insert_into = c_orm_query_insert_into_impl;
  q->update_table = c_orm_query_update_table_impl;
  q->delete_from = c_orm_query_delete_from_impl;
  q->set_col = c_orm_query_set_col_impl;
  q->returning = c_orm_query_returning_impl;
  q->with_lock = c_orm_query_with_lock_impl;
  q->lit_decimal = c_orm_query_lit_decimal_impl;

  *out_query = q;
  LOG_DEBUG("c_orm_query_new: exit");
  return C_ORM_OK;
}

/**
 * @brief Frees a fluent query structure.
 *
 * @param query Query structure to free.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_query_free(c_orm_ast_query_t *query) {
  c_orm_error_t rc = C_ORM_OK;
  c_orm_error_t free_rc;
  LOG_DEBUG("c_orm_query_free: entry");
  if (query) {
    rc = c_orm_arena_free(query->arena);
    free_rc = c_orm_system_free(query);
    if (free_rc != C_ORM_OK && rc == C_ORM_OK) {
      rc = free_rc;
    }
  }
  LOG_DEBUG("c_orm_query_free: exit");
  return rc;
}
