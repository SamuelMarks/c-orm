/**
 * @file c_orm_query_sql.c
 * @brief Dialect Translation & Execution implementation.
 */

/* clang-format off */
#include "c_orm_ast.h"
#include "c_orm_string_builder.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
/* clang-format on */

C_ORM_EXPORT void c_orm_query_params_init(c_orm_query_params_t *params) {
  if (params) {
    params->params = NULL;
    params->count = 0;
    params->capacity = 0;
  }
}

C_ORM_EXPORT void c_orm_query_params_cleanup(c_orm_query_params_t *params) {
  if (params) {
    if (params->params) {
      free(params->params);
    }
    params->params = NULL;
    params->count = 0;
    params->capacity = 0;
  }
}

C_ORM_EXPORT int c_orm_query_params_add(c_orm_query_params_t *params,
                                        const char *value, int is_string) {
  if (!params)
    return 1;

  if (params->count >= params->capacity) {
    size_t new_cap = params->capacity == 0 ? 8 : params->capacity * 2;
    c_orm_query_param_t *new_arr = (c_orm_query_param_t *)realloc(
        params->params, sizeof(c_orm_query_param_t) * new_cap);
    if (!new_arr)
      return 1;
    params->params = new_arr;
    params->capacity = new_cap;
  }

  params->params[params->count].value = value;
  params->params[params->count].is_string = is_string;
  params->count++;
  return 0;
}

static c_orm_error_t render_node(c_orm_ast_node_t *node,
                                 c_orm_dialect_t dialect,
                                 c_orm_string_builder_t *sb,
                                 c_orm_query_params_t *params) {
  c_orm_error_t err;

  if (!node)
    return C_ORM_OK;

  switch (node->type) {
  case C_ORM_AST_NODE_COLUMN: {
    c_orm_ast_column_t *col = (c_orm_ast_column_t *)node;
    c_orm_string_builder_append(sb, col->name);
    break;
  }
  case C_ORM_AST_NODE_LITERAL: {
    c_orm_ast_literal_t *lit = (c_orm_ast_literal_t *)node;
    if (params) {
      if (c_orm_query_params_add(params, lit->value, lit->is_string) != 0)
        return C_ORM_ERROR_MEMORY;
      if (dialect == C_ORM_DIALECT_POSTGRES) {
        char ph[32];
#if defined(_MSC_VER)
        sprintf_s(ph, sizeof(ph), "$%u", (unsigned int)params->count);
#else
        sprintf(ph, "$%u", (unsigned int)params->count);
#endif
        c_orm_string_builder_append(sb, ph);
      } else {
        c_orm_string_builder_append(sb, "?");
      }
    } else {
      if (lit->is_string)
        c_orm_string_builder_append(sb, "'");
      c_orm_string_builder_append(sb, lit->value);
      if (lit->is_string)
        c_orm_string_builder_append(sb, "'");
    }
    break;
  }
  case C_ORM_AST_NODE_RAW: {
    c_orm_ast_raw_t *raw = (c_orm_ast_raw_t *)node;
    c_orm_string_builder_append(sb, raw->sql);
    break;
  }
  case C_ORM_AST_NODE_OPERATOR: {
    c_orm_ast_operator_t *op = (c_orm_ast_operator_t *)node;
    err = render_node(op->left, dialect, sb, params);
    if (err != C_ORM_OK)
      return err;
    c_orm_string_builder_append(sb, " ");
    c_orm_string_builder_append(sb, op->op);
    if (op->right) {
      c_orm_string_builder_append(sb, " ");
      err = render_node(op->right, dialect, sb, params);
      if (err != C_ORM_OK)
        return err;
    }
    break;
  }
  case C_ORM_AST_NODE_GROUP: {
    c_orm_ast_group_t *grp = (c_orm_ast_group_t *)node;
    c_orm_string_builder_append(sb, "(");
    err = render_node(grp->expr, dialect, sb, params);
    if (err != C_ORM_OK)
      return err;
    c_orm_string_builder_append(sb, ")");
    break;
  }
  case C_ORM_AST_NODE_CAST: {
    c_orm_ast_cast_t *cast = (c_orm_ast_cast_t *)node;
    c_orm_string_builder_append(sb, "CAST(");
    c_orm_string_builder_append(sb, cast->col);
    c_orm_string_builder_append(sb, " AS ");
    c_orm_string_builder_append(sb, cast->type);
    c_orm_string_builder_append(sb, ")");
    break;
  }
  case C_ORM_AST_NODE_FUNCTION: {
    c_orm_ast_function_t *func = (c_orm_ast_function_t *)node;
    c_orm_string_builder_append(sb, func->name);
    c_orm_string_builder_append(sb, "(");
    c_orm_string_builder_append(sb, func->args);
    c_orm_string_builder_append(sb, ")");
    if (func->alias && func->alias[0] != '\0') {
      c_orm_string_builder_append(sb, " AS ");
      c_orm_string_builder_append(sb, func->alias);
    }
    break;
  }
  case C_ORM_AST_NODE_BETWEEN: {
    c_orm_ast_between_t *bw = (c_orm_ast_between_t *)node;
    c_orm_string_builder_append(sb, bw->col);
    c_orm_string_builder_append(sb, " BETWEEN ");
    if (params) {
      if (c_orm_query_params_add(params, bw->low, bw->is_string) != 0)
        return C_ORM_ERROR_MEMORY;
      if (dialect == C_ORM_DIALECT_POSTGRES) {
        char ph[32];
#if defined(_MSC_VER)
        sprintf_s(ph, sizeof(ph), "$%u", (unsigned int)params->count);
#else
        sprintf(ph, "$%u", (unsigned int)params->count);
#endif
        c_orm_string_builder_append(sb, ph);
      } else {
        c_orm_string_builder_append(sb, "?");
      }
      c_orm_string_builder_append(sb, " AND ");
      if (c_orm_query_params_add(params, bw->high, bw->is_string) != 0)
        return C_ORM_ERROR_MEMORY;
      if (dialect == C_ORM_DIALECT_POSTGRES) {
        char ph[32];
#if defined(_MSC_VER)
        sprintf_s(ph, sizeof(ph), "$%u", (unsigned int)params->count);
#else
        sprintf(ph, "$%u", (unsigned int)params->count);
#endif
        c_orm_string_builder_append(sb, ph);
      } else {
        c_orm_string_builder_append(sb, "?");
      }
    } else {
      if (bw->is_string)
        c_orm_string_builder_append(sb, "'");
      c_orm_string_builder_append(sb, bw->low);
      if (bw->is_string)
        c_orm_string_builder_append(sb, "'");
      c_orm_string_builder_append(sb, " AND ");
      if (bw->is_string)
        c_orm_string_builder_append(sb, "'");
      c_orm_string_builder_append(sb, bw->high);
      if (bw->is_string)
        c_orm_string_builder_append(sb, "'");
    }
    break;
  }
  case C_ORM_AST_NODE_EXISTS: {
    c_orm_ast_exists_t *ex = (c_orm_ast_exists_t *)node;
    char *subsql = NULL;
    if (ex->is_not)
      c_orm_string_builder_append(sb, "NOT ");
    c_orm_string_builder_append(sb, "EXISTS (");
    err = c_orm_query_to_sql(ex->query, dialect, &subsql, params);
    if (err != 0)
      return C_ORM_ERROR_SQL;
    c_orm_string_builder_append(sb, subsql);
    c_orm_string_builder_append(sb, ")");
    free(subsql);
    break;
  }
  case C_ORM_AST_NODE_SUBQUERY: {
    c_orm_ast_subquery_t *sq = (c_orm_ast_subquery_t *)node;
    char *subsql = NULL;
    c_orm_string_builder_append(sb, "(");
    err = c_orm_query_to_sql(sq->query, dialect, &subsql, params);
    if (err != 0)
      return C_ORM_ERROR_SQL;
    c_orm_string_builder_append(sb, subsql);
    c_orm_string_builder_append(sb, ")");
    if (sq->alias && sq->alias[0] != '\0') {
      c_orm_string_builder_append(sb, " AS ");
      c_orm_string_builder_append(sb, sq->alias);
    }
    free(subsql);
    break;
  }
  case C_ORM_AST_NODE_WINDOW: {
    c_orm_ast_window_t *win = (c_orm_ast_window_t *)node;
    c_orm_string_builder_append(sb, win->func_name);
    c_orm_string_builder_append(sb, " OVER (");
    if (win->partition_by && win->partition_by[0] != '\0') {
      c_orm_string_builder_append(sb, "PARTITION BY ");
      c_orm_string_builder_append(sb, win->partition_by);
      if (win->order_by && win->order_by[0] != '\0') {
        c_orm_string_builder_append(sb, " ");
      }
    }
    if (win->order_by && win->order_by[0] != '\0') {
      c_orm_string_builder_append(sb, "ORDER BY ");
      c_orm_string_builder_append(sb, win->order_by);
    }
    c_orm_string_builder_append(sb, ")");
    if (win->alias && win->alias[0] != '\0') {
      c_orm_string_builder_append(sb, " AS ");
      c_orm_string_builder_append(sb, win->alias);
    }
    break;
  }
  default:
    break;
  }
  return C_ORM_OK;
}

C_ORM_EXPORT int c_orm_query_to_sql(c_orm_query_t *q, c_orm_dialect_t dialect,
                                    char **out_sql,
                                    c_orm_query_params_t *out_params) {
  c_orm_string_builder_t *sb;
  c_orm_ast_node_t *node;
  c_orm_ast_select_t *sel = NULL;
  c_orm_ast_from_t *frm = NULL;
  c_orm_ast_where_t *whr = NULL;
  c_orm_ast_group_by_t *grp = NULL;
  c_orm_ast_having_t *hav = NULL;
  c_orm_ast_order_by_t *ord = NULL;
  c_orm_ast_limit_t *lim = NULL;
  c_orm_ast_offset_t *off = NULL;
  c_orm_ast_with_t *wth = NULL;
  c_orm_ast_union_t *uni = NULL;

  c_orm_ast_join_t *joins[32];
  size_t join_count = 0;
  size_t i;

  if (!q || !out_sql)
    return 1;

  if (c_orm_string_builder_init(&sb) != 0)
    return 1;

  node = q->ast_head;
  while (node) {
    switch (node->type) {
    case C_ORM_AST_NODE_SELECT:
      sel = (c_orm_ast_select_t *)node;
      break;
    case C_ORM_AST_NODE_FROM:
      frm = (c_orm_ast_from_t *)node;
      break;
    case C_ORM_AST_NODE_WHERE:
      whr = (c_orm_ast_where_t *)node;
      break;
    case C_ORM_AST_NODE_JOIN:
      if (join_count < 32)
        joins[join_count++] = (c_orm_ast_join_t *)node;
      break;
    case C_ORM_AST_NODE_GROUP_BY:
      grp = (c_orm_ast_group_by_t *)node;
      break;
    case C_ORM_AST_NODE_HAVING:
      hav = (c_orm_ast_having_t *)node;
      break;
    case C_ORM_AST_NODE_ORDER_BY:
      ord = (c_orm_ast_order_by_t *)node;
      break;
    case C_ORM_AST_NODE_LIMIT:
      lim = (c_orm_ast_limit_t *)node;
      break;
    case C_ORM_AST_NODE_OFFSET:
      off = (c_orm_ast_offset_t *)node;
      break;
    case C_ORM_AST_NODE_WITH:
      wth = (c_orm_ast_with_t *)node;
      break;
    case C_ORM_AST_NODE_UNION:
      uni = (c_orm_ast_union_t *)node;
      break;
    default:
      break;
    }
    node = node->next;
  }

  if (wth) {
    char *subsql = NULL;
    c_orm_string_builder_append(sb, "WITH ");
    c_orm_string_builder_append(sb, wth->alias);
    c_orm_string_builder_append(sb, " AS (");
    if (c_orm_query_to_sql(wth->query, dialect, &subsql, out_params) != 0) {
      c_orm_string_builder_free(sb);
      return 1;
    }
    c_orm_string_builder_append(sb, subsql);
    free(subsql);
    c_orm_string_builder_append(sb, ") ");
  }

  if (sel) {
    c_orm_string_builder_append(sb, "SELECT ");
    if (sel->is_distinct)
      c_orm_string_builder_append(sb, "DISTINCT ");
    c_orm_string_builder_append(sb, sel->columns);
  }

  if (frm) {
    c_orm_string_builder_append(sb, " FROM ");
    c_orm_string_builder_append(sb, frm->table);
    if (frm->alias && frm->alias[0] != '\0') {
      c_orm_string_builder_append(sb, " AS ");
      c_orm_string_builder_append(sb, frm->alias);
    }
  }

  for (i = join_count; i > 0; i--) {
    c_orm_ast_join_t *jn = joins[i - 1];
    c_orm_string_builder_append(sb, " ");
    c_orm_string_builder_append(sb, jn->type_str);
    c_orm_string_builder_append(sb, " JOIN ");
    c_orm_string_builder_append(sb, jn->table);
    c_orm_string_builder_append(sb, " ON ");
    if (render_node(jn->on_condition, dialect, sb, out_params) != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return 1;
    }
  }

  if (whr && whr->condition) {
    c_orm_string_builder_append(sb, " WHERE ");
    if (render_node(whr->condition, dialect, sb, out_params) != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return 1;
    }
  }

  if (grp) {
    c_orm_string_builder_append(sb, " GROUP BY ");
    c_orm_string_builder_append(sb, grp->columns);
  }

  if (hav && hav->condition) {
    c_orm_string_builder_append(sb, " HAVING ");
    if (render_node(hav->condition, dialect, sb, out_params) != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return 1;
    }
  }

  if (uni) {
    char *subsql = NULL;
    c_orm_string_builder_append(sb, uni->is_all ? " UNION ALL " : " UNION ");
    if (c_orm_query_to_sql(uni->query, dialect, &subsql, out_params) != 0) {
      c_orm_string_builder_free(sb);
      return 1;
    }
    c_orm_string_builder_append(sb, subsql);
    free(subsql);
  }

  if (ord) {
    c_orm_string_builder_append(sb, " ORDER BY ");
    c_orm_string_builder_append(sb, ord->column);
    c_orm_string_builder_append(sb, ord->is_desc ? " DESC" : " ASC");
  }

  if (lim) {
    char num_buf[32];
#if defined(_MSC_VER)
    sprintf_s(num_buf, sizeof(num_buf), " LIMIT %u", (unsigned int)lim->limit);
#else
    sprintf(num_buf, " LIMIT %u", (unsigned int)lim->limit);
#endif
    c_orm_string_builder_append(sb, num_buf);
  }

  if (off) {
    char num_buf[32];
#if defined(_MSC_VER)
    sprintf_s(num_buf, sizeof(num_buf), " OFFSET %u",
              (unsigned int)off->offset);
#else
    sprintf(num_buf, " OFFSET %u", (unsigned int)off->offset);
#endif
    c_orm_string_builder_append(sb, num_buf);
  }

  {
    const char *sql_str;
    if (c_orm_string_builder_get(sb, &sql_str) == 0) {
#if defined(_MSC_VER)
      size_t len = strlen(sql_str);
      *out_sql = (char *)malloc(len + 1);
      if (*out_sql)
        strcpy_s(*out_sql, len + 1, sql_str);
#else
      *out_sql = strdup(sql_str);
#endif
    } else {
      *out_sql = NULL;
    }
  }

  c_orm_string_builder_free(sb);
  return *out_sql ? 0 : 1;
}

C_ORM_EXPORT c_orm_error_t c_orm_query_execute(c_orm_db_t *db,
                                               c_orm_query_t *q) {
  char *sql = NULL;
  c_orm_query_params_t params;
  c_orm_query_t *stmt;
  c_orm_error_t err;
  size_t i;
  int has_row;

  if (!db || !q)
    return C_ORM_ERROR_MEMORY;

  c_orm_query_params_init(&params);
  if (c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &params) != 0) {
    c_orm_query_params_cleanup(&params);
    return C_ORM_ERROR_SQL;
  }

  err = c_orm_prepare_cached(db, sql, &stmt);
  free(sql);
  if (err != C_ORM_OK) {
    c_orm_query_params_cleanup(&params);
    return err;
  }

  for (i = 0; i < params.count; i++) {
    err = db->vtable->bind_string(stmt, (int)(i + 1), params.params[i].value);
    if (err != C_ORM_OK) {
      c_orm_finalize_cached(db, stmt);
      c_orm_query_params_cleanup(&params);
      return err;
    }
  }

  err = db->vtable->step(stmt, &has_row);
  c_orm_finalize_cached(db, stmt);
  c_orm_query_params_cleanup(&params);
  return err;
}

C_ORM_EXPORT c_orm_error_t c_orm_query_fetch_one(c_orm_db_t *db,
                                                 c_orm_query_t *q,
                                                 const c_orm_table_meta_t *meta,
                                                 void *out_struct) {
  char *sql = NULL;
  c_orm_query_params_t params;
  c_orm_query_t *stmt;
  c_orm_error_t err;
  size_t i;
  int has_row;

  if (!db || !q || !meta || !out_struct)
    return C_ORM_ERROR_MEMORY;

  c_orm_query_params_init(&params);
  if (c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &params) != 0) {
    c_orm_query_params_cleanup(&params);
    return C_ORM_ERROR_SQL;
  }

  err = c_orm_prepare_cached(db, sql, &stmt);
  free(sql);
  if (err != C_ORM_OK) {
    c_orm_query_params_cleanup(&params);
    return err;
  }

  for (i = 0; i < params.count; i++) {
    err = db->vtable->bind_string(stmt, (int)(i + 1), params.params[i].value);
    if (err != C_ORM_OK) {
      c_orm_finalize_cached(db, stmt);
      c_orm_query_params_cleanup(&params);
      return err;
    }
  }

  err = db->vtable->step(stmt, &has_row);
  if (err == C_ORM_OK && has_row) {
    err = c_orm_hydrate_row(db, stmt, meta, out_struct);
  } else if (err == C_ORM_OK) {
    err = C_ORM_ERROR_NOT_FOUND;
  }

  c_orm_finalize_cached(db, stmt);
  c_orm_query_params_cleanup(&params);
  return err;
}

C_ORM_EXPORT c_orm_error_t c_orm_query_fetch_all(c_orm_db_t *db,
                                                 c_orm_query_t *q,
                                                 const c_orm_table_meta_t *meta,
                                                 void *out_array) {
  char *sql = NULL;
  c_orm_query_params_t params;
  c_orm_query_t *stmt;
  c_orm_error_t err;
  size_t i;

  if (!db || !q || !meta || !out_array)
    return C_ORM_ERROR_MEMORY;

  c_orm_query_params_init(&params);
  if (c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &params) != 0) {
    c_orm_query_params_cleanup(&params);
    return C_ORM_ERROR_SQL;
  }

  err = c_orm_prepare_cached(db, sql, &stmt);
  free(sql);
  if (err != C_ORM_OK) {
    c_orm_query_params_cleanup(&params);
    return err;
  }

  for (i = 0; i < params.count; i++) {
    err = db->vtable->bind_string(stmt, (int)(i + 1), params.params[i].value);
    if (err != C_ORM_OK) {
      c_orm_finalize_cached(db, stmt);
      c_orm_query_params_cleanup(&params);
      return err;
    }
  }

  err = c_orm_hydrate_all(db, stmt, meta, out_array);
  c_orm_finalize_cached(db, stmt);
  c_orm_query_params_cleanup(&params);
  return err;
}