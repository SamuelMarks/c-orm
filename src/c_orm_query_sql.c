#if defined(__clang__) || defined(__GNUC__)
#endif
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

C_ORM_EXPORT unsigned int cdd_c_sql_parser_max_depth = 100;

#define APPEND(str)                                                            \
  for (;;) {                                                                   \
    c_orm_error_t _err = c_orm_string_builder_append(sb, (str));               \
    if (_err != C_ORM_OK) {                                                    \
      return _err;                                                             \
    }                                                                          \
    break;                                                                     \
  }

#define APPEND_SQL(str)                                                        \
  for (;;) {                                                                   \
    c_orm_error_t _err = c_orm_string_builder_append(sb, (str));               \
    if (_err != C_ORM_OK) {                                                    \
      (void)c_orm_string_builder_free(sb);                                     \
      return _err;                                                             \
    }                                                                          \
    break;                                                                     \
  }
/**
 * @brief Initializes query parameters.
 *
 * @param params The query parameters structure to initialize.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_query_params_init(c_orm_query_params_t *params) {
  LOG_DEBUG("c_orm_query_params_init: entry");
  if (!params)
    return C_ORM_ERROR_UNKNOWN;
  params->params = NULL;
  params->capacity = 0;
  params->count = 0;
  LOG_DEBUG("c_orm_query_params_init: exit");
  return C_ORM_OK;
}

/**
 * @brief Cleans up query parameters.
 *
 * @param params The query parameters structure to clean up.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_query_params_cleanup(c_orm_query_params_t *params) {
  LOG_DEBUG("c_orm_query_params_cleanup: entry");
  if (!params)
    return C_ORM_ERROR_UNKNOWN;
  if (params->params)
    C_ORM_FREE(params->params);
  params->params = NULL;
  params->count = 0;
  params->capacity = 0;
  /* Mock failure to allow branch coverage */
  /* if (c_orm_malloc(0) == NULL) return C_ORM_ERROR_MEMORY; */
  LOG_DEBUG("c_orm_query_params_cleanup: exit");
  return C_ORM_OK;
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
    if (params->params == NULL) {
      new_arr = (c_orm_query_param_t *)C_ORM_MALLOC(
          sizeof(c_orm_query_param_t) * new_cap);
    } else {
      new_arr = (c_orm_query_param_t *)C_ORM_REALLOC(
          params->params, sizeof(c_orm_query_param_t) * new_cap);
    }
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
                                 c_orm_query_params_t *params,
                                 unsigned int depth) {
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

  if (depth > cdd_c_sql_parser_max_depth) {
    LOG_DEBUG("render_node: max depth exceeded");
    return C_ORM_ERROR_SQL;
  }

  if (!node) {
    LOG_DEBUG("render_node: node is NULL");
    return C_ORM_OK;
  }

  switch (node->type) {
  case C_ORM_AST_NODE_COLUMN: {
    col = (c_orm_ast_column_t *)node;
    APPEND(col->name);
    break;
  }
  case C_ORM_AST_NODE_LITERAL: {
    lit = (c_orm_ast_literal_t *)node;
    if (params) {
      c_orm_error_t rc;
      rc = c_orm_query_params_add(params, lit->value, lit->is_string);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("render_node: OOM adding literal");
        return rc;
      }
      if (dialect == C_ORM_DIALECT_POSTGRES) {
        char ph[32];
        C_ORM_SPRINTF(ph, sizeof(ph), "$%u", (unsigned int)params->count);
        APPEND(ph);
      } else {
        APPEND("?");
      }
    } else {
      if (lit->is_string)
        APPEND("'");
      APPEND(lit->value);
      if (lit->is_string)
        APPEND("'");
    }
    break;
  }
  case C_ORM_AST_NODE_RAW: {
    raw = (c_orm_ast_raw_t *)node;
    APPEND(raw->sql);
    break;
  }
  case C_ORM_AST_NODE_OPERATOR: {
    op = (c_orm_ast_operator_t *)node;
    err = render_node(op->left, dialect, sb, params, depth + 1);
    if (err != C_ORM_OK) {
      LOG_DEBUG("render_node: error rendering left operand");
      return err;
    }
    APPEND(" ");
    APPEND(op->op);
    if (op->right) {
      APPEND(" ");
      err = render_node(op->right, dialect, sb, params, depth + 1);
      if (err != C_ORM_OK) {
        LOG_DEBUG("render_node: error rendering right operand");
        return err;
      }
    }
    break;
  }
  case C_ORM_AST_NODE_GROUP: {
    grp = (c_orm_ast_group_t *)node;
    APPEND("(");
    err = render_node(grp->expr, dialect, sb, params, depth + 1);
    if (err != C_ORM_OK) {
      LOG_DEBUG("render_node: error rendering group");
      return err;
    }
    APPEND(")");
    break;
  }
  case C_ORM_AST_NODE_CAST: {
    cast = (c_orm_ast_cast_t *)node;
    APPEND("CAST(");
    APPEND(cast->col);
    APPEND(" AS ");
    APPEND(cast->type);
    APPEND(")");
    break;
  }
  case C_ORM_AST_NODE_FUNCTION: {
    func = (c_orm_ast_function_t *)node;
    APPEND(func->name);
    APPEND("(");
    APPEND(func->args);
    APPEND(")");
    if (func->alias && func->alias[0] != '\0') {
      APPEND(" AS ");
      APPEND(func->alias);
    }
    break;
  }
  case C_ORM_AST_NODE_BETWEEN: {
    bw = (c_orm_ast_between_t *)node;
    APPEND(bw->col);
    APPEND(" BETWEEN ");
    if (params) {
      c_orm_error_t rc;
      rc = c_orm_query_params_add(params, bw->low, bw->is_string);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("render_node: OOM adding low");
        return rc;
      }
      if (dialect == C_ORM_DIALECT_POSTGRES) {
        char ph[32];
        C_ORM_SPRINTF(ph, sizeof(ph), "$%u", (unsigned int)params->count);
        APPEND(ph);
      } else {
        APPEND("?");
      }
      APPEND(" AND ");
      rc = c_orm_query_params_add(params, bw->high, bw->is_string);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("render_node: OOM adding high");
        return rc;
      }
      if (dialect == C_ORM_DIALECT_POSTGRES) {
        char ph[32];
        C_ORM_SPRINTF(ph, sizeof(ph), "$%u", (unsigned int)params->count);
        APPEND(ph);
      } else {
        APPEND("?");
      }
    } else {
      if (bw->is_string)
        APPEND("'");
      APPEND(bw->low);
      if (bw->is_string)
        APPEND("'");
      APPEND(" AND ");
      if (bw->is_string)
        APPEND("'");
      APPEND(bw->high);
      if (bw->is_string)
        APPEND("'");
    }
    break;
  }
  case C_ORM_AST_NODE_EXISTS: {
    ex = (c_orm_ast_exists_t *)node;
    subsql = NULL;
    if (ex->is_not)
      APPEND("NOT ");
    APPEND("EXISTS (");
    err = c_orm_query_to_sql(ex->query, dialect, &subsql, params);
    if (err != 0) {
      LOG_DEBUG("render_node: SQL generation failed for exists");
      return err;
    }
    err = c_orm_string_builder_append(sb, subsql);
    if (err == C_ORM_OK)
      err = c_orm_string_builder_append(sb, ")");
    C_ORM_FREE(subsql);
    if (err != C_ORM_OK)
      return err;
    break;
  }
  case C_ORM_AST_NODE_SUBQUERY: {
    sq = (c_orm_ast_subquery_t *)node;
    subsql = NULL;
    APPEND("(");
    err = c_orm_query_to_sql(sq->query, dialect, &subsql, params);
    if (err != 0) {
      LOG_DEBUG("render_node: SQL generation failed for subquery");
      return err;
    }
    err = c_orm_string_builder_append(sb, subsql);
    if (err == C_ORM_OK)
      err = c_orm_string_builder_append(sb, ")");
    if (err == C_ORM_OK && sq->alias && sq->alias[0] != '\0') {
      err = c_orm_string_builder_append(sb, " AS ");
      if (err == C_ORM_OK)
        err = c_orm_string_builder_append(sb, sq->alias);
    }
    C_ORM_FREE(subsql);
    if (err != C_ORM_OK)
      return err;
    break;
  }
  case C_ORM_AST_NODE_WINDOW: {
    win = (c_orm_ast_window_t *)node;
    APPEND(win->func_name);
    APPEND(" OVER (");
    if (win->partition_by && win->partition_by[0] != '\0') {
      APPEND("PARTITION BY ");
      APPEND(win->partition_by);
      if (win->order_by && win->order_by[0] != '\0') {
        APPEND(" ");
      }
    }
    if (win->order_by && win->order_by[0] != '\0') {
      APPEND("ORDER BY ");
      APPEND(win->order_by);
    }
    APPEND(")");
    if (win->alias && win->alias[0] != '\0') {
      APPEND(" AS ");
      APPEND(win->alias);
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
    APPEND_SQL("WITH ");
    APPEND_SQL(wth->alias);
    APPEND_SQL(" AS (");
    rc = c_orm_query_to_sql(wth->query, dialect, &subsql, out_params);
    if (rc != 0) {
      (void)c_orm_string_builder_free(sb);
      LOG_DEBUG("c_orm_query_to_sql: with query failed");
      return rc;
    }
    rc = c_orm_string_builder_append(sb, subsql);
    C_ORM_FREE(subsql);
    if (rc != C_ORM_OK) {
      (void)c_orm_string_builder_free(sb);
      return rc;
    }
    APPEND_SQL(") ");
  }

  if (sel) {
    APPEND_SQL("SELECT ");
    if (sel->is_distinct)
      APPEND_SQL("DISTINCT ");
    APPEND_SQL(sel->columns);
  }

  if (frm) {
    APPEND_SQL(" FROM ");
    APPEND_SQL(frm->table);
    if (frm->alias && frm->alias[0] != '\0') {
      APPEND_SQL(" AS ");
      APPEND_SQL(frm->alias);
    }
  }

  for (i = join_count; i > 0; i--) {
    c_orm_ast_join_t *jn = joins[i - 1];
    APPEND_SQL(" ");
    APPEND_SQL(jn->type_str);
    APPEND_SQL(" JOIN ");
    APPEND_SQL(jn->table);
    APPEND_SQL(" ON ");
    rc = render_node(jn->on_condition, dialect, sb, out_params, 0);
    if (rc != C_ORM_OK) {
      (void)c_orm_string_builder_free(sb);
      LOG_DEBUG("c_orm_query_to_sql: join failed");
      return rc;
    }
  }

  if (whr && whr->condition) {
    APPEND_SQL(" WHERE ");
    rc = render_node(whr->condition, dialect, sb, out_params, 0);
    if (rc != C_ORM_OK) {
      (void)c_orm_string_builder_free(sb);
      LOG_DEBUG("c_orm_query_to_sql: where failed");
      return rc;
    }
  }

  if (grp) {
    APPEND_SQL(" GROUP BY ");
    APPEND_SQL(grp->columns);
  }

  if (hav && hav->condition) {
    APPEND_SQL(" HAVING ");
    rc = render_node(hav->condition, dialect, sb, out_params, 0);
    if (rc != C_ORM_OK) {
      (void)c_orm_string_builder_free(sb);
      LOG_DEBUG("c_orm_query_to_sql: having failed");
      return rc;
    }
  }

  if (uni) {
    subsql = NULL;
    APPEND_SQL(uni->is_all ? " UNION ALL " : " UNION ");
    rc = c_orm_query_to_sql(uni->query, dialect, &subsql, out_params);
    if (rc != 0) {
      (void)c_orm_string_builder_free(sb);
      LOG_DEBUG("c_orm_query_to_sql: union failed");
      return rc;
    }
    rc = c_orm_string_builder_append(sb, subsql);
    C_ORM_FREE(subsql);
    if (rc != C_ORM_OK) {
      (void)c_orm_string_builder_free(sb);
      return rc;
    }
  }

  if (ord) {
    APPEND_SQL(" ORDER BY ");
    APPEND_SQL(ord->column);
    APPEND_SQL(ord->is_desc ? " DESC" : " ASC");
  }

  if (lim) {
    char num_buf[32];
    C_ORM_SPRINTF(num_buf, sizeof(num_buf), " LIMIT %u",
                  (unsigned int)lim->limit);
    APPEND_SQL(num_buf);
  }

  if (off) {
    char num_buf[32];
    C_ORM_SPRINTF(num_buf, sizeof(num_buf), " OFFSET %u",
                  (unsigned int)off->offset);
    APPEND_SQL(num_buf);
  }

  {
    const char *sql_str;
    rc = c_orm_string_builder_get(sb, &sql_str);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }

    {
      size_t len = strlen(sql_str);
      *out_sql = (char *)C_ORM_MALLOC(len + 1);
      if (!*out_sql) {
        c_orm_string_builder_free(sb);
        LOG_DEBUG("c_orm_query_to_sql: OOM sql out");
        return C_ORM_ERROR_MEMORY;
      }
      C_ORM_STRCPY(*out_sql, len + 1, sql_str);
    }
  }

  c_orm_string_builder_free(sb);

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
  c_orm_error_t rc = C_ORM_OK;
  size_t i;
  int has_row;

  LOG_DEBUG("c_orm_query_execute: entry");

  if (!db || !q) {
    LOG_DEBUG("c_orm_query_execute: null argument");
    return rc;
  }

  err = c_orm_query_params_init(&params);
  if (err != C_ORM_OK)
    return err;
  err = c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &params);
  if (err != C_ORM_OK) {
    {
      c_orm_error_t cl_err = c_orm_query_params_cleanup(&params);
      if (cl_err != C_ORM_OK)
        return cl_err;
    }
    LOG_DEBUG("c_orm_query_execute: to_sql failed");
    return err;
  }

  err = c_orm_prepare_cached(db, sql, &stmt);
  if (err != C_ORM_OK) {
    C_ORM_FREE(sql);
    {
      c_orm_error_t cl_err = c_orm_query_params_cleanup(&params);
      if (cl_err != C_ORM_OK)
        return cl_err;
    }
    LOG_DEBUG("c_orm_query_execute: prepare failed");
    return err;
  }
  C_ORM_FREE(sql);

  for (i = 0; i < params.count; i++) {
    err = db->vtable->bind_string(stmt, (int)(i + 1), params.params[i].value);
    if (err != C_ORM_OK) {
      {
        c_orm_error_t _fin = c_orm_finalize_cached(db, stmt);
        c_orm_error_t cl_err = c_orm_query_params_cleanup(&params);
        if (_fin != C_ORM_OK) {
          return _fin;
        }
        if (cl_err != C_ORM_OK)
          return cl_err;
      }
      LOG_DEBUG("c_orm_query_execute: bind failed");
      return err;
    }
  }

  err = db->vtable->step(stmt, &has_row);
  if (err != C_ORM_OK) {
    {
      c_orm_error_t _fin = c_orm_finalize_cached(db, stmt);
      c_orm_error_t _cl = c_orm_query_params_cleanup(&params);
      if (_fin != C_ORM_OK) {
        return _fin;
      }
      if (_cl != C_ORM_OK)
        return _cl;
    }
    return err;
  }
  {
    c_orm_error_t _fin = c_orm_finalize_cached(db, stmt);
    c_orm_error_t cl_err = c_orm_query_params_cleanup(&params);
    if (_fin != C_ORM_OK)
      return _fin;
    if (cl_err != C_ORM_OK)
      return cl_err;
  }
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
  c_orm_error_t rc = C_ORM_OK;
  size_t i;
  int has_row;

  LOG_DEBUG("c_orm_query_fetch_one: entry");

  if (!db || !q || !meta || !out_struct) {
    LOG_DEBUG("c_orm_query_fetch_one: null argument");
    return rc;
  }

  err = c_orm_query_params_init(&params);
  if (err != C_ORM_OK)
    return err;
  err = c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &params);
  if (err != C_ORM_OK) {
    {
      c_orm_error_t cl_err = c_orm_query_params_cleanup(&params);
      if (cl_err != C_ORM_OK)
        return cl_err;
    }
    LOG_DEBUG("c_orm_query_fetch_one: to_sql failed");
    return err;
  }

  err = c_orm_prepare_cached(db, sql, &stmt);
  if (err != C_ORM_OK) {
    C_ORM_FREE(sql);
    {
      c_orm_error_t cl_err = c_orm_query_params_cleanup(&params);
      if (cl_err != C_ORM_OK)
        return cl_err;
    }
    LOG_DEBUG("c_orm_query_fetch_one: prepare failed");
    return err;
  }
  C_ORM_FREE(sql);

  for (i = 0; i < params.count; i++) {
    err = db->vtable->bind_string(stmt, (int)(i + 1), params.params[i].value);
    if (err != C_ORM_OK) {
      {
        c_orm_error_t _fin = c_orm_finalize_cached(db, stmt);
        c_orm_error_t cl_err = c_orm_query_params_cleanup(&params);
        if (_fin != C_ORM_OK) {
          return _fin;
        }
        if (cl_err != C_ORM_OK)
          return cl_err;
      }
      LOG_DEBUG("c_orm_query_fetch_one: bind failed");
      return err;
    }
  }

  err = db->vtable->step(stmt, &has_row);
  if (err != C_ORM_OK) {
    {
      c_orm_error_t _fin = c_orm_finalize_cached(db, stmt);
      c_orm_error_t _cl = c_orm_query_params_cleanup(&params);
      if (_fin != C_ORM_OK)
        return _fin;
      if (_cl != C_ORM_OK)
        return _cl;
    }
    return err;
  }
  if (!has_row) {
    {
      c_orm_error_t _fin = c_orm_finalize_cached(db, stmt);
      c_orm_error_t _cl = c_orm_query_params_cleanup(&params);
      if (_fin != C_ORM_OK)
        return _fin;
      if (_cl != C_ORM_OK)
        return _cl;
    }
    return C_ORM_ERROR_NOT_FOUND;
  }

  err = c_orm_hydrate_row(db, stmt, meta, out_struct);
  if (err != C_ORM_OK) {
    {
      c_orm_error_t _fin = c_orm_finalize_cached(db, stmt);
      c_orm_error_t _cl = c_orm_query_params_cleanup(&params);
      if (_fin != C_ORM_OK)
        return _fin;
      if (_cl != C_ORM_OK)
        return _cl;
    }
    return err;
  }

  {
    c_orm_error_t _fin = c_orm_finalize_cached(db, stmt);
    c_orm_error_t cl_err = c_orm_query_params_cleanup(&params);
    if (_fin != C_ORM_OK)
      return _fin;
    if (cl_err != C_ORM_OK)
      return cl_err;
  }
  LOG_DEBUG("c_orm_query_fetch_one: exit");
  return C_ORM_OK;
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
  c_orm_error_t rc = C_ORM_OK;
  size_t i;

  LOG_DEBUG("c_orm_query_fetch_all: entry");

  if (!db || !q || !meta || !out_array) {
    LOG_DEBUG("c_orm_query_fetch_all: null argument");
    return rc;
  }

  err = c_orm_query_params_init(&params);
  if (err != C_ORM_OK)
    return err;
  err = c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &params);
  if (err != C_ORM_OK) {
    {
      c_orm_error_t cl_err = c_orm_query_params_cleanup(&params);
      if (cl_err != C_ORM_OK)
        return cl_err;
    }
    LOG_DEBUG("c_orm_query_fetch_all: to_sql failed");
    return err;
  }

  err = c_orm_prepare_cached(db, sql, &stmt);
  if (err != C_ORM_OK) {
    C_ORM_FREE(sql);
    {
      c_orm_error_t cl_err = c_orm_query_params_cleanup(&params);
      if (cl_err != C_ORM_OK)
        return cl_err;
    }
    LOG_DEBUG("c_orm_query_fetch_all: prepare failed");
    return err;
  }
  C_ORM_FREE(sql);

  for (i = 0; i < params.count; i++) {
    err = db->vtable->bind_string(stmt, (int)(i + 1), params.params[i].value);
    if (err != C_ORM_OK) {
      {
        c_orm_error_t _fin = c_orm_finalize_cached(db, stmt);
        c_orm_error_t cl_err = c_orm_query_params_cleanup(&params);
        if (_fin != C_ORM_OK) {
          return _fin;
        }
        if (cl_err != C_ORM_OK)
          return cl_err;
      }
      LOG_DEBUG("c_orm_query_fetch_all: bind failed");
      return err;
    }
  }

  err = c_orm_hydrate_all(db, stmt, meta, out_array);
  if (err != C_ORM_OK) {
    {
      c_orm_error_t _fin = c_orm_finalize_cached(db, stmt);
      c_orm_error_t _cl = c_orm_query_params_cleanup(&params);
      if (_fin != C_ORM_OK) {
        return _fin;
      }
      if (_cl != C_ORM_OK)
        return _cl;
    }
    return err;
  }
  {
    c_orm_error_t _fin = c_orm_finalize_cached(db, stmt);
    c_orm_error_t cl_err = c_orm_query_params_cleanup(&params);
    if (_fin != C_ORM_OK)
      return _fin;
    if (cl_err != C_ORM_OK)
      return cl_err;
  }
  LOG_DEBUG("c_orm_query_fetch_all: exit");
  return err;
}

#if defined(__clang__) || defined(__GNUC__)
#endif
