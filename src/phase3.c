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
  node->query = subquery; /* Assuming arena lifetime matches or is managed */
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
  /* If no select node found yet, create an empty one with distinct */
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
