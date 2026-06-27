/**
 * @file c_orm_query_sql.c
 * @brief Dialect Translation & Execution implementation.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_ast.h"
#include "c_orm_db.h"
#include "c_orm_log.h"
#include "c_orm_string_builder.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
/* clang-format on */

/**
 * @brief Initializes query parameters.
 *
 * @param params The query parameters structure to initialize.
 */
C_ORM_EXPORT void c_orm_query_params_init(c_orm_query_params_t *params) {
  LOG_DEBUG("c_orm_query_params_init: entry");
  if (params) {
    params->params = NULL;
    params->count = 0;
    params->capacity = 0;
  }
  LOG_DEBUG("c_orm_query_params_init: exit");
}

/**
 * @brief Cleans up query parameters.
 *
 * @param params The query parameters structure to clean up.
 */
C_ORM_EXPORT void c_orm_query_params_cleanup(c_orm_query_params_t *params) {
  LOG_DEBUG("c_orm_query_params_cleanup: entry");
  if (params) {
    if (params->params) {
      C_ORM_FREE(params->params);
    }
    params->params = NULL;
    params->count = 0;
    params->capacity = 0;
  }
  LOG_DEBUG("c_orm_query_params_cleanup: exit");
}

/**
 * @brief Adds a parameter to the query parameters list.
 *
 * @param params The query parameters structure.
 * @param value The value to add.
 * @param is_string Whether the value is a string.
 * @return 0 on success, non-zero on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_query_params_add(c_orm_query_params_t *params,
                                                  const char *value,
                                                  int is_string) {
  c_orm_error_t rc;
  size_t new_cap;
  c_orm_query_param_t *new_arr;

  LOG_DEBUG("c_orm_query_params_add: entry");

  if (!params) {
    LOG_DEBUG("c_orm_query_params_add: params is NULL");
    rc = C_ORM_ERROR_UNKNOWN;
    return rc;
  }

  if (params->count >= params->capacity) {
    new_cap = params->capacity == 0 ? 8 : params->capacity * 2;
    new_arr = (c_orm_query_param_t *)C_ORM_REALLOC(
        params->params, sizeof(c_orm_query_param_t) * new_cap);
    if (!new_arr) {
      LOG_DEBUG("c_orm_query_params_add: OOM");
      rc = C_ORM_ERROR_UNKNOWN;
      return rc;
    }
    params->params = new_arr;
    params->capacity = new_cap;
  }

  params->params[params->count].value = value;
  params->params[params->count].is_string = is_string;
  params->count++;
  rc = C_ORM_OK;
  LOG_DEBUG("c_orm_query_params_add: exit");
  return rc;
}

/**
 * @brief Renders an AST node into the string builder.
 *
 * @param node The AST node.
 * @param dialect The SQL dialect.
 * @param sb The string builder.
 * @param params The query parameters.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t render_node(c_orm_ast_node_t *node,
                                 c_orm_dialect_t dialect,
                                 c_orm_string_builder_t *sb,
                                 c_orm_query_params_t *params) {
  c_orm_error_t err;
  c_orm_ast_column_t *col;
  c_orm_ast_literal_t *lit;
  c_orm_ast_raw_t *raw;
  c_orm_ast_operator_t *op;
  c_orm_ast_group_t *grp;
  c_orm_ast_cast_t *cast;
  c_orm_ast_function_t *func;
  c_orm_ast_between_t *bw;
  c_orm_ast_exists_t *ex;
  c_orm_ast_subquery_t *sq;
  c_orm_ast_window_t *win;
  char *subsql;

  LOG_DEBUG("render_node: entry");

  if (!node) {
    LOG_DEBUG("render_node: node is NULL");
    return C_ORM_OK;
  }

  switch (node->type) {
  case C_ORM_AST_NODE_COLUMN: {
    col = (c_orm_ast_column_t *)node;
    c_orm_string_builder_append(sb, col->name);
    break;
  }
  case C_ORM_AST_NODE_LITERAL: {
    lit = (c_orm_ast_literal_t *)node;
    if (params) {
      if (c_orm_query_params_add(params, lit->value, lit->is_string) != 0) {
        LOG_DEBUG("render_node: OOM adding literal");
        return C_ORM_ERROR_MEMORY;
      }
      if (dialect == C_ORM_DIALECT_POSTGRES) {
        char ph[32];
        C_ORM_SPRINTF(ph, sizeof(ph), "$%u", (unsigned int)params->count);
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
    raw = (c_orm_ast_raw_t *)node;
    c_orm_string_builder_append(sb, raw->sql);
    break;
  }
  case C_ORM_AST_NODE_OPERATOR: {
    op = (c_orm_ast_operator_t *)node;
    err = render_node(op->left, dialect, sb, params);
    if (err != C_ORM_OK) {
      LOG_DEBUG("render_node: error rendering left operand");
      return err;
    }
    c_orm_string_builder_append(sb, " ");
    c_orm_string_builder_append(sb, op->op);
    if (op->right) {
      c_orm_string_builder_append(sb, " ");
      err = render_node(op->right, dialect, sb, params);
      if (err != C_ORM_OK) {
        LOG_DEBUG("render_node: error rendering right operand");
        return err;
      }
    }
    break;
  }
  case C_ORM_AST_NODE_GROUP: {
    grp = (c_orm_ast_group_t *)node;
    c_orm_string_builder_append(sb, "(");
    err = render_node(grp->expr, dialect, sb, params);
    if (err != C_ORM_OK) {
      LOG_DEBUG("render_node: error rendering group");
      return err;
    }
    c_orm_string_builder_append(sb, ")");
    break;
  }
  case C_ORM_AST_NODE_CAST: {
    cast = (c_orm_ast_cast_t *)node;
    c_orm_string_builder_append(sb, "CAST(");
    c_orm_string_builder_append(sb, cast->col);
    c_orm_string_builder_append(sb, " AS ");
    c_orm_string_builder_append(sb, cast->type);
    c_orm_string_builder_append(sb, ")");
    break;
  }
  case C_ORM_AST_NODE_FUNCTION: {
    func = (c_orm_ast_function_t *)node;
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
    bw = (c_orm_ast_between_t *)node;
    c_orm_string_builder_append(sb, bw->col);
    c_orm_string_builder_append(sb, " BETWEEN ");
    if (params) {
      if (c_orm_query_params_add(params, bw->low, bw->is_string) != 0) {
        LOG_DEBUG("render_node: OOM adding low");
        return C_ORM_ERROR_MEMORY;
      }
      if (dialect == C_ORM_DIALECT_POSTGRES) {
        char ph[32];
        C_ORM_SPRINTF(ph, sizeof(ph), "$%u", (unsigned int)params->count);
        c_orm_string_builder_append(sb, ph);
      } else {
        c_orm_string_builder_append(sb, "?");
      }
      c_orm_string_builder_append(sb, " AND ");
      if (c_orm_query_params_add(params, bw->high, bw->is_string) != 0) {
        LOG_DEBUG("render_node: OOM adding high");
        return C_ORM_ERROR_MEMORY;
      }
      if (dialect == C_ORM_DIALECT_POSTGRES) {
        char ph[32];
        C_ORM_SPRINTF(ph, sizeof(ph), "$%u", (unsigned int)params->count);
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
    ex = (c_orm_ast_exists_t *)node;
    subsql = NULL;
    if (ex->is_not)
      c_orm_string_builder_append(sb, "NOT ");
    c_orm_string_builder_append(sb, "EXISTS (");
    err = c_orm_query_to_sql(ex->query, dialect, &subsql, params);
    if (err != 0) {
      LOG_DEBUG("render_node: SQL generation failed for exists");
      return C_ORM_ERROR_SQL;
    }
    c_orm_string_builder_append(sb, subsql);
    c_orm_string_builder_append(sb, ")");
    C_ORM_FREE(subsql);
    break;
  }
  case C_ORM_AST_NODE_SUBQUERY: {
    sq = (c_orm_ast_subquery_t *)node;
    subsql = NULL;
    c_orm_string_builder_append(sb, "(");
    err = c_orm_query_to_sql(sq->query, dialect, &subsql, params);
    if (err != 0) {
      LOG_DEBUG("render_node: SQL generation failed for subquery");
      return C_ORM_ERROR_SQL;
    }
    c_orm_string_builder_append(sb, subsql);
    c_orm_string_builder_append(sb, ")");
    if (sq->alias && sq->alias[0] != '\0') {
      c_orm_string_builder_append(sb, " AS ");
      c_orm_string_builder_append(sb, sq->alias);
    }
    C_ORM_FREE(subsql);
    break;
  }
  case C_ORM_AST_NODE_WINDOW: {
    win = (c_orm_ast_window_t *)node;
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
  LOG_DEBUG("render_node: exit");
  return C_ORM_OK;
}

/**
 * @brief Converts a query to SQL.
 *
 * @param q The query to convert.
 * @param dialect The SQL dialect.
 * @param out_sql Pointer to receive the allocated SQL string.
 * @param out_params Pointer to receive the query parameters.
 * @return 0 on success, non-zero on failure.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_query_to_sql(c_orm_query_t *q, c_orm_dialect_t dialect, char **out_sql,
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
  c_orm_error_t rc;
  char *subsql = NULL;

  LOG_DEBUG("c_orm_query_to_sql: entry");

  if (!q || !out_sql) {
    LOG_DEBUG("c_orm_query_to_sql: null argument");
    rc = C_ORM_ERROR_UNKNOWN;
    return rc;
  }

  rc = c_orm_string_builder_init(&sb);
  if (rc != 0) {
    LOG_DEBUG("c_orm_query_to_sql: builder init failed");
    return rc;
  }

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
    c_orm_string_builder_append(sb, "WITH ");
    c_orm_string_builder_append(sb, wth->alias);
    c_orm_string_builder_append(sb, " AS (");
    rc = c_orm_query_to_sql(wth->query, dialect, &subsql, out_params);
    if (rc != 0) {
      c_orm_string_builder_free(sb);
      LOG_DEBUG("c_orm_query_to_sql: with query failed");
      return rc;
    }
    c_orm_string_builder_append(sb, subsql);
    C_ORM_FREE(subsql);
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
      rc = C_ORM_ERROR_UNKNOWN;
      LOG_DEBUG("c_orm_query_to_sql: join failed");
      return rc;
    }
  }

  if (whr && whr->condition) {
    c_orm_string_builder_append(sb, " WHERE ");
    if (render_node(whr->condition, dialect, sb, out_params) != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      rc = C_ORM_ERROR_UNKNOWN;
      LOG_DEBUG("c_orm_query_to_sql: where failed");
      return rc;
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
      rc = C_ORM_ERROR_UNKNOWN;
      LOG_DEBUG("c_orm_query_to_sql: having failed");
      return rc;
    }
  }

  if (uni) {
    subsql = NULL;
    c_orm_string_builder_append(sb, uni->is_all ? " UNION ALL " : " UNION ");
    rc = c_orm_query_to_sql(uni->query, dialect, &subsql, out_params);
    if (rc != 0) {
      c_orm_string_builder_free(sb);
      LOG_DEBUG("c_orm_query_to_sql: union failed");
      return rc;
    }
    c_orm_string_builder_append(sb, subsql);
    C_ORM_FREE(subsql);
  }

  if (ord) {
    c_orm_string_builder_append(sb, " ORDER BY ");
    c_orm_string_builder_append(sb, ord->column);
    c_orm_string_builder_append(sb, ord->is_desc ? " DESC" : " ASC");
  }

  if (lim) {
    char num_buf[32];
    C_ORM_SPRINTF(num_buf, sizeof(num_buf), " LIMIT %u",
                  (unsigned int)lim->limit);
    c_orm_string_builder_append(sb, num_buf);
  }

  if (off) {
    char num_buf[32];
    C_ORM_SPRINTF(num_buf, sizeof(num_buf), " OFFSET %u",
                  (unsigned int)off->offset);
    c_orm_string_builder_append(sb, num_buf);
  }

  {
    const char *sql_str;
    if (c_orm_string_builder_get(sb, &sql_str) == 0) {
      size_t len = strlen(sql_str);
      *out_sql = (char *)C_ORM_MALLOC(len + 1);
      if (*out_sql) {
        C_ORM_STRCPY(*out_sql, len + 1, sql_str);
      } else {
        c_orm_string_builder_free(sb);
        rc = C_ORM_ERROR_UNKNOWN;
        LOG_DEBUG("c_orm_query_to_sql: OOM sql out");
        return rc;
      }
    }
  }

  c_orm_string_builder_free(sb);
  rc = *out_sql ? C_ORM_OK : C_ORM_ERROR_UNKNOWN;
  LOG_DEBUG("c_orm_query_to_sql: exit");
  return rc;
}

/**
 * @brief Executes a query without returning results.
 *
 * @param db The database connection.
 * @param q The query to execute.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_query_execute(c_orm_db_t *db,
                                               c_orm_query_t *q) {
  char *sql = NULL;
  c_orm_query_params_t params;
  c_orm_query_t *stmt;
  c_orm_error_t err;
  size_t i;
  int has_row;

  LOG_DEBUG("c_orm_query_execute: entry");

  if (!db || !q) {
    LOG_DEBUG("c_orm_query_execute: null argument");
    return C_ORM_ERROR_MEMORY;
  }

  c_orm_query_params_init(&params);
  if (c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &params) != 0) {
    c_orm_query_params_cleanup(&params);
    LOG_DEBUG("c_orm_query_execute: to_sql failed");
    return C_ORM_ERROR_SQL;
  }

  err = c_orm_prepare_cached(db, sql, &stmt);
  C_ORM_FREE(sql);
  if (err != C_ORM_OK) {
    c_orm_query_params_cleanup(&params);
    LOG_DEBUG("c_orm_query_execute: prepare failed");
    return err;
  }

  for (i = 0; i < params.count; i++) {
    err = db->vtable->bind_string(stmt, (int)(i + 1), params.params[i].value);
    if (err != C_ORM_OK) {
      c_orm_finalize_cached(db, stmt);
      c_orm_query_params_cleanup(&params);
      LOG_DEBUG("c_orm_query_execute: bind failed");
      return err;
    }
  }

  err = db->vtable->step(stmt, &has_row);
  c_orm_finalize_cached(db, stmt);
  c_orm_query_params_cleanup(&params);
  LOG_DEBUG("c_orm_query_execute: exit");
  return err;
}

/**
 * @brief Fetches a single row from a query.
 *
 * @param db The database connection.
 * @param q The query to execute.
 * @param meta The table metadata.
 * @param out_struct Pointer to the output structure.
 * @return C_ORM_OK on success, or an error code.
 */
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

  LOG_DEBUG("c_orm_query_fetch_one: entry");

  if (!db || !q || !meta || !out_struct) {
    LOG_DEBUG("c_orm_query_fetch_one: null argument");
    return C_ORM_ERROR_MEMORY;
  }

  c_orm_query_params_init(&params);
  if (c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &params) != 0) {
    c_orm_query_params_cleanup(&params);
    LOG_DEBUG("c_orm_query_fetch_one: to_sql failed");
    return C_ORM_ERROR_SQL;
  }

  err = c_orm_prepare_cached(db, sql, &stmt);
  C_ORM_FREE(sql);
  if (err != C_ORM_OK) {
    c_orm_query_params_cleanup(&params);
    LOG_DEBUG("c_orm_query_fetch_one: prepare failed");
    return err;
  }

  for (i = 0; i < params.count; i++) {
    err = db->vtable->bind_string(stmt, (int)(i + 1), params.params[i].value);
    if (err != C_ORM_OK) {
      c_orm_finalize_cached(db, stmt);
      c_orm_query_params_cleanup(&params);
      LOG_DEBUG("c_orm_query_fetch_one: bind failed");
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
  LOG_DEBUG("c_orm_query_fetch_one: exit");
  return err;
}

/**
 * @brief Fetches all rows from a query.
 *
 * @param db The database connection.
 * @param q The query to execute.
 * @param meta The table metadata.
 * @param out_array Pointer to receive the array of structures.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_query_fetch_all(c_orm_db_t *db,
                                                 c_orm_query_t *q,
                                                 const c_orm_table_meta_t *meta,
                                                 void *out_array) {
  char *sql = NULL;
  c_orm_query_params_t params;
  c_orm_query_t *stmt;
  c_orm_error_t err;
  size_t i;

  LOG_DEBUG("c_orm_query_fetch_all: entry");

  if (!db || !q || !meta || !out_array) {
    LOG_DEBUG("c_orm_query_fetch_all: null argument");
    return C_ORM_ERROR_MEMORY;
  }

  c_orm_query_params_init(&params);
  if (c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &params) != 0) {
    c_orm_query_params_cleanup(&params);
    LOG_DEBUG("c_orm_query_fetch_all: to_sql failed");
    return C_ORM_ERROR_SQL;
  }

  err = c_orm_prepare_cached(db, sql, &stmt);
  C_ORM_FREE(sql);
  if (err != C_ORM_OK) {
    c_orm_query_params_cleanup(&params);
    LOG_DEBUG("c_orm_query_fetch_all: prepare failed");
    return err;
  }

  for (i = 0; i < params.count; i++) {
    err = db->vtable->bind_string(stmt, (int)(i + 1), params.params[i].value);
    if (err != C_ORM_OK) {
      c_orm_finalize_cached(db, stmt);
      c_orm_query_params_cleanup(&params);
      LOG_DEBUG("c_orm_query_fetch_all: bind failed");
      return err;
    }
  }

  err = c_orm_hydrate_all(db, stmt, meta, out_array);
  c_orm_finalize_cached(db, stmt);
  c_orm_query_params_cleanup(&params);
  LOG_DEBUG("c_orm_query_fetch_all: exit");
  return err;
}
