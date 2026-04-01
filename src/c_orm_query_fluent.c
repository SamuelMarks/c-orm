/**
 * @file c_orm_query_fluent.c
 * @brief Implementation of fluent AST-based query builder.
 */

/* clang-format off */
#include "c_orm_ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
/* clang-format on */

static const char *c_orm_escape_literal(c_orm_arena_t *arena, const char *val) {
  size_t len;
  size_t count = 0;
  size_t i, j;
  char *escaped;

  if (!val)
    return NULL;
  len = strlen(val);

  for (i = 0; i < len; i++) {
    if (val[i] == '\'')
      count++;
  }

  if (count == 0)
    return val;

  if (c_orm_arena_alloc(arena, len + count + 1, (void **)&escaped) != 0)
    return NULL;

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

  return escaped;
}

static c_orm_ast_node_t *c_orm_query_raw_impl(c_orm_query_t *q,
                                              const char *sql) {
  c_orm_ast_raw_t *node;
  if (!q || q->error)
    return NULL;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_raw_t), (void **)&node) !=
      0) {
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_RAW;
  node->base.next = NULL;
  node->sql = sql;
  return (c_orm_ast_node_t *)node;
}

static c_orm_ast_node_t *c_orm_query_col_impl(c_orm_query_t *q,
                                              const char *name) {
  c_orm_ast_column_t *node;
  if (!q || q->error)
    return NULL;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_column_t), (void **)&node) !=
      0) {
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_COLUMN;
  node->base.next = NULL;
  node->name = name;
  return (c_orm_ast_node_t *)node;
}

static c_orm_ast_node_t *c_orm_query_lit_impl(c_orm_query_t *q, const char *val,
                                              int is_string) {
  c_orm_ast_literal_t *node;
  if (!q || q->error)
    return NULL;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_literal_t),
                        (void **)&node) != 0) {
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_LITERAL;
  node->base.next = NULL;
  node->is_string = is_string;
  if (is_string) {
    node->value = c_orm_escape_literal(q->arena, val);
    if (!node->value && val) {
      q->error = 1;
      return NULL;
    }
  } else {
    node->value = val;
  }
  return (c_orm_ast_node_t *)node;
}

static c_orm_ast_node_t *c_orm_query_op_impl(c_orm_query_t *q, const char *op,
                                             c_orm_ast_node_t *left,
                                             c_orm_ast_node_t *right) {
  c_orm_ast_operator_t *node;
  if (!q || q->error)
    return NULL;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_operator_t),
                        (void **)&node) != 0) {
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_OPERATOR;
  node->base.next = NULL;
  node->op = op;
  node->left = left;
  node->right = right;
  return (c_orm_ast_node_t *)node;
}

static c_orm_ast_node_t *c_orm_query_eq_impl(c_orm_query_t *q, const char *col,
                                             const char *val, int is_string) {
  return c_orm_query_op_impl(q, "=", c_orm_query_col_impl(q, col),
                             c_orm_query_lit_impl(q, val, is_string));
}

static c_orm_ast_node_t *c_orm_query_neq_impl(c_orm_query_t *q, const char *col,
                                              const char *val, int is_string) {
  return c_orm_query_op_impl(q, "!=", c_orm_query_col_impl(q, col),
                             c_orm_query_lit_impl(q, val, is_string));
}

static c_orm_ast_node_t *c_orm_query_gt_impl(c_orm_query_t *q, const char *col,
                                             const char *val, int is_string) {
  return c_orm_query_op_impl(q, ">", c_orm_query_col_impl(q, col),
                             c_orm_query_lit_impl(q, val, is_string));
}

static c_orm_ast_node_t *c_orm_query_lt_impl(c_orm_query_t *q, const char *col,
                                             const char *val, int is_string) {
  return c_orm_query_op_impl(q, "<", c_orm_query_col_impl(q, col),
                             c_orm_query_lit_impl(q, val, is_string));
}

static c_orm_ast_node_t *
c_orm_query_like_impl(c_orm_query_t *q, const char *col, const char *val) {
  return c_orm_query_op_impl(q, "LIKE", c_orm_query_col_impl(q, col),
                             c_orm_query_lit_impl(q, val, 1));
}

static c_orm_ast_node_t *c_orm_query_in_impl(c_orm_query_t *q, const char *col,
                                             const char *val_list) {
  return c_orm_query_op_impl(q, "IN", c_orm_query_col_impl(q, col),
                             c_orm_query_raw_impl(q, val_list));
}

static c_orm_ast_node_t *c_orm_ast_clone_node(c_orm_arena_t *arena,
                                              c_orm_ast_node_t *node) {
  c_orm_ast_node_t *new_node = NULL;
  if (!node)
    return NULL;

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
  return new_node;
}

static int c_orm_query_clone_impl(c_orm_query_t *q, c_orm_query_t **out_q) {
  c_orm_ast_node_t *curr;
  c_orm_ast_node_t *tail = NULL;

  if (!q || !out_q)
    return 1;

  if (c_orm_query_new(out_q) != 0)
    return 1;
  (*out_q)->error = q->error;

  curr = q->ast_head;
  while (curr) {
    c_orm_ast_node_t *cloned = c_orm_ast_clone_node((*out_q)->arena, curr);
    if (!cloned) {
      c_orm_query_free(*out_q);
      *out_q = NULL;
      return 1;
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
  return 0;
}

static c_orm_query_t *c_orm_query_select_impl(c_orm_query_t *q,
                                              const char *columns) {
  c_orm_ast_select_t *node;
  if (!q || q->error)
    return q;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_select_t), (void **)&node) !=
      0) {
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_SELECT;
  node->base.next = q->ast_head;
  node->columns = columns;
  node->is_distinct = 0;
  q->ast_head = (c_orm_ast_node_t *)node;
  return q;
}

static c_orm_query_t *c_orm_query_from_impl(c_orm_query_t *q,
                                            const char *table) {
  c_orm_ast_from_t *node;
  if (!q || q->error)
    return q;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_from_t), (void **)&node) !=
      0) {
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_FROM;
  node->base.next = q->ast_head;
  node->table = table;
  node->alias = NULL;
  q->ast_head = (c_orm_ast_node_t *)node;
  return q;
}

static c_orm_query_t *c_orm_query_where_impl(c_orm_query_t *q,
                                             c_orm_ast_node_t *condition) {
  c_orm_ast_where_t *node;
  if (!q || q->error)
    return q;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_where_t), (void **)&node) !=
      0) {
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_WHERE;
  node->base.next = q->ast_head;
  node->condition = condition;
  q->ast_head = (c_orm_ast_node_t *)node;
  return q;
}

static c_orm_query_t *c_orm_query_and_where_impl(c_orm_query_t *q,
                                                 c_orm_ast_node_t *condition) {
  c_orm_ast_node_t *curr;
  if (!q || q->error || !condition)
    return q;
  curr = q->ast_head;
  while (curr) {
    if (curr->type == C_ORM_AST_NODE_WHERE) {
      c_orm_ast_where_t *w = (c_orm_ast_where_t *)curr;
      if (w->condition) {
        w->condition = c_orm_query_op_impl(q, "AND", w->condition, condition);
      } else {
        w->condition = condition;
      }
      return q;
    }
    curr = curr->next;
  }
  return q->where(q, condition);
}

static c_orm_query_t *c_orm_query_or_where_impl(c_orm_query_t *q,
                                                c_orm_ast_node_t *condition) {
  c_orm_ast_node_t *curr;
  if (!q || q->error || !condition)
    return q;
  curr = q->ast_head;
  while (curr) {
    if (curr->type == C_ORM_AST_NODE_WHERE) {
      c_orm_ast_where_t *w = (c_orm_ast_where_t *)curr;
      if (w->condition) {
        w->condition = c_orm_query_op_impl(q, "OR", w->condition, condition);
      } else {
        w->condition = condition;
      }
      return q;
    }
    curr = curr->next;
  }
  return q->where(q, condition);
}

static c_orm_query_t *
c_orm_query_order_by_impl(c_orm_query_t *q, const char *column, int is_desc) {
  c_orm_ast_order_by_t *node;
  if (!q || q->error)
    return q;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_order_by_t),
                        (void **)&node) != 0) {
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_ORDER_BY;
  node->base.next = q->ast_head;
  node->column = column;
  node->is_desc = is_desc;
  q->ast_head = (c_orm_ast_node_t *)node;
  return q;
}

static c_orm_query_t *c_orm_query_limit_impl(c_orm_query_t *q, size_t n) {
  c_orm_ast_limit_t *node;
  if (!q || q->error)
    return q;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_limit_t), (void **)&node) !=
      0) {
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_LIMIT;
  node->base.next = q->ast_head;
  node->limit = n;
  q->ast_head = (c_orm_ast_node_t *)node;
  return q;
}

static c_orm_query_t *c_orm_query_offset_impl(c_orm_query_t *q, size_t n) {
  c_orm_ast_offset_t *node;
  if (!q || q->error)
    return q;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_offset_t), (void **)&node) !=
      0) {
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_OFFSET;
  node->base.next = q->ast_head;
  node->offset = n;
  q->ast_head = (c_orm_ast_node_t *)node;
  return q;
}

static c_orm_query_t *c_orm_query_join_impl(c_orm_query_t *q, const char *table,
                                            const char *type_str,
                                            c_orm_ast_node_t *on_condition) {
  c_orm_ast_join_t *node;
  if (!q || q->error)
    return q;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_join_t), (void **)&node) !=
      0) {
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_JOIN;
  node->base.next = q->ast_head;
  node->table = table;
  node->type_str = type_str;
  node->on_condition = on_condition;
  q->ast_head = (c_orm_ast_node_t *)node;
  return q;
}

static c_orm_query_t *
c_orm_query_left_join_impl(c_orm_query_t *q, const char *table,
                           c_orm_ast_node_t *on_condition) {
  return c_orm_query_join_impl(q, table, "LEFT", on_condition);
}

static c_orm_query_t *
c_orm_query_right_join_impl(c_orm_query_t *q, const char *table,
                            c_orm_ast_node_t *on_condition) {
  return c_orm_query_join_impl(q, table, "RIGHT", on_condition);
}

static c_orm_query_t *c_orm_query_group_by_impl(c_orm_query_t *q,
                                                const char *columns) {
  c_orm_ast_group_by_t *node;
  if (!q || q->error)
    return q;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_group_by_t),
                        (void **)&node) != 0) {
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_GROUP_BY;
  node->base.next = q->ast_head;
  node->columns = columns;
  q->ast_head = (c_orm_ast_node_t *)node;
  return q;
}

static c_orm_query_t *c_orm_query_having_impl(c_orm_query_t *q,
                                              c_orm_ast_node_t *condition) {
  c_orm_ast_having_t *node;
  if (!q || q->error)
    return q;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_having_t), (void **)&node) !=
      0) {
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_HAVING;
  node->base.next = q->ast_head;
  node->condition = condition;
  q->ast_head = (c_orm_ast_node_t *)node;
  return q;
}

static c_orm_query_t *c_orm_query_with_impl(c_orm_query_t *q, const char *alias,
                                            c_orm_query_t *subquery) {
  c_orm_ast_with_t *node;
  if (!q || q->error || !subquery)
    return q;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_with_t), (void **)&node) !=
      0) {
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_WITH;
  node->base.next = q->ast_head;
  node->alias = alias;
  node->query = subquery;
  q->ast_head = (c_orm_ast_node_t *)node;
  return q;
}

static c_orm_query_t *c_orm_query_union_impl(c_orm_query_t *q,
                                             c_orm_query_t *other, int is_all) {
  c_orm_ast_union_t *node;
  if (!q || q->error || !other)
    return q;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_union_t), (void **)&node) !=
      0) {
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_UNION;
  node->base.next = q->ast_head;
  node->is_all = is_all;
  node->query = other;
  q->ast_head = (c_orm_ast_node_t *)node;
  return q;
}

static c_orm_query_t *c_orm_query_distinct_impl(c_orm_query_t *q) {
  c_orm_ast_node_t *curr;
  if (!q || q->error)
    return q;
  curr = q->ast_head;
  while (curr) {
    if (curr->type == C_ORM_AST_NODE_SELECT) {
      ((c_orm_ast_select_t *)curr)->is_distinct = 1;
      return q;
    }
    curr = curr->next;
  }
  q->select_(q, "*");
  ((c_orm_ast_select_t *)q->ast_head)->is_distinct = 1;
  return q;
}

static c_orm_query_t *c_orm_query_from_alias_impl(c_orm_query_t *q,
                                                  const char *table,
                                                  const char *alias) {
  c_orm_ast_from_t *node;
  if (!q || q->error)
    return q;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_from_t), (void **)&node) !=
      0) {
    q->error = 1;
    return q;
  }
  node->base.type = C_ORM_AST_NODE_FROM;
  node->base.next = q->ast_head;
  node->table = table;
  node->alias = alias;
  q->ast_head = (c_orm_ast_node_t *)node;
  return q;
}

static c_orm_query_t *
c_orm_query_eager_load_impl(c_orm_query_t *q, const c_orm_table_meta_t *meta,
                            const char *relation_name) {
  size_t i;
  const c_orm_relation_meta_t *rel = NULL;
  char *on_cond;
  char *columns;
  c_orm_ast_node_t *curr;

  if (!q || q->error || !meta || !relation_name)
    return q;

  for (i = 0; i < meta->num_relations; i++) {
    if (strcmp(meta->relations[i].field_name, relation_name) == 0) {
      rel = &meta->relations[i];
      break;
    }
  }

  if (!rel || !rel->target_meta) {
    q->error = 1;
    return q;
  }

  on_cond = (char *)malloc(128);
  if (!on_cond) {
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
  free(on_cond);

  /* Expand SELECT columns dynamically to add child columns with prefix
   * `relation_name_` */
  if (c_orm_arena_alloc(q->arena, 4096, (void **)&columns) == 0) {
    char *p = columns;
    size_t col_i;
    int first = 1;

    /* We MUST include parent columns explicitly too to avoid ambiguity if we
     * replace '*' */
    for (col_i = 0; col_i < meta->num_columns; col_i++) {
      int w;
      if (!first) {
        *p++ = ',';
        *p++ = ' ';
      }
      first = 0;
#if defined(_MSC_VER)
      w = sprintf_s(p, 4096 - (p - columns), "%s.%s", meta->name,
                    meta->columns[col_i].name);
#else
      w = sprintf(p, "%s.%s", meta->name, meta->columns[col_i].name);
#endif
      p += w;
    }

    for (col_i = 0; col_i < rel->target_meta->num_columns; col_i++) {
      int w;
      if (!first) {
        *p++ = ',';
        *p++ = ' ';
      }
      first = 0;
#if defined(_MSC_VER)
      w = sprintf_s(p, 4096 - (p - columns), "%s.%s AS %s_%s",
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

  return q;
}

static c_orm_ast_node_t *c_orm_query_group_node_impl(c_orm_query_t *q,
                                                     c_orm_ast_node_t *expr) {
  c_orm_ast_group_t *node;
  if (!q || q->error)
    return NULL;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_group_t), (void **)&node) !=
      0) {
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_GROUP;
  node->base.next = NULL;
  node->expr = expr;
  return (c_orm_ast_node_t *)node;
}

static c_orm_ast_node_t *c_orm_query_subquery_node_impl(c_orm_query_t *q,
                                                        c_orm_query_t *subq,
                                                        const char *alias) {
  c_orm_ast_subquery_t *node;
  if (!q || q->error || !subq)
    return NULL;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_subquery_t),
                        (void **)&node) != 0) {
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_SUBQUERY;
  node->base.next = NULL;
  node->query = subq;
  node->alias = alias;
  return (c_orm_ast_node_t *)node;
}

static c_orm_ast_node_t *c_orm_query_func_impl(c_orm_query_t *q,
                                               const char *name,
                                               const char *args,
                                               const char *alias) {
  c_orm_ast_function_t *node;
  if (!q || q->error)
    return NULL;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_function_t),
                        (void **)&node) != 0) {
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_FUNCTION;
  node->base.next = NULL;
  node->name = name;
  node->args = args;
  node->alias = alias;
  return (c_orm_ast_node_t *)node;
}

static c_orm_ast_node_t *
c_orm_query_cast_impl(c_orm_query_t *q, const char *col, const char *type) {
  c_orm_ast_cast_t *node;
  if (!q || q->error)
    return NULL;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_cast_t), (void **)&node) !=
      0) {
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_CAST;
  node->base.next = NULL;
  node->col = col;
  node->type = type;
  return (c_orm_ast_node_t *)node;
}

static c_orm_ast_node_t *c_orm_query_is_null_impl(c_orm_query_t *q,
                                                  const char *col, int is_not) {
  c_orm_ast_operator_t *node;
  if (!q || q->error)
    return NULL;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_operator_t),
                        (void **)&node) != 0) {
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_OPERATOR;
  node->base.next = NULL;
  node->op = is_not ? "IS NOT NULL" : "IS NULL";
  node->left = q->col(q, col);
  node->right = NULL;
  return (c_orm_ast_node_t *)node;
}

static c_orm_ast_node_t *
c_orm_query_between_impl(c_orm_query_t *q, const char *col, const char *low,
                         const char *high, int is_string) {
  c_orm_ast_between_t *node;
  if (!q || q->error)
    return NULL;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_between_t),
                        (void **)&node) != 0) {
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_BETWEEN;
  node->base.next = NULL;
  node->col = col;
  node->low = is_string ? c_orm_escape_literal(q->arena, low) : low;
  node->high = is_string ? c_orm_escape_literal(q->arena, high) : high;
  node->is_string = is_string;
  return (c_orm_ast_node_t *)node;
}

static c_orm_ast_node_t *
c_orm_query_exists_impl(c_orm_query_t *q, c_orm_query_t *subq, int is_not) {
  c_orm_ast_exists_t *node;
  if (!q || q->error || !subq)
    return NULL;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_exists_t), (void **)&node) !=
      0) {
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_EXISTS;
  node->base.next = NULL;
  node->query = subq;
  node->is_not = is_not;
  return (c_orm_ast_node_t *)node;
}

static c_orm_ast_node_t *c_orm_query_window_impl(c_orm_query_t *q,
                                                 const char *func_name,
                                                 const char *partition_by,
                                                 const char *order_by,
                                                 const char *alias) {
  c_orm_ast_window_t *node;
  if (!q || q->error)
    return NULL;
  if (c_orm_arena_alloc(q->arena, sizeof(c_orm_ast_window_t), (void **)&node) !=
      0) {
    q->error = 1;
    return NULL;
  }
  node->base.type = C_ORM_AST_NODE_WINDOW;
  node->base.next = NULL;
  node->func_name = func_name;
  node->partition_by = partition_by;
  node->order_by = order_by;
  node->alias = alias;
  return (c_orm_ast_node_t *)node;
}

C_ORM_EXPORT int c_orm_query_new(c_orm_query_t **out_query) {
  c_orm_query_t *q;

  if (!out_query)
    return 1;

  q = (c_orm_query_t *)malloc(sizeof(c_orm_query_t));
  if (!q)
    return 1;

  if (c_orm_arena_new(&q->arena) != 0) {
    free(q);
    return 1;
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
  q->clone = c_orm_query_clone_impl;

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
  return 0;
}

C_ORM_EXPORT void c_orm_query_free(c_orm_query_t *query) {
  if (query) {
    c_orm_arena_free(query->arena);
    free(query);
  }
}