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
  do {                                                                         \
    c_orm_error_t _app_rc = c_orm_string_builder_append(sb, (str));            \
    if (_app_rc != C_ORM_OK)                                                   \
      return _app_rc;                                                          \
  } while (0)

#define APPEND_SQL(str)                                                        \
  do {                                                                         \
    rc = c_orm_string_builder_append(sb, (str));                               \
    if (rc != C_ORM_OK) {                                                      \
      goto fail_to_sql;                                                        \
    }                                                                          \
  } while (0)

/**
 * @brief Resolves the SQL dialect for an active database connection.
 * @param db Database connection handle.
 * @param out_dialect Output pointer receiving the resolved SQL dialect.
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN if db is NULL.
 */
static c_orm_error_t resolve_db_dialect(const c_orm_db_t *db,
                                        c_orm_dialect_t *out_dialect) {
  if (!db) {
    return C_ORM_ERROR_UNKNOWN;
  }
  if (db->driver_name) {
    if (strcmp(db->driver_name, "postgres") == 0) {
      *out_dialect = C_ORM_DIALECT_POSTGRES;
      return C_ORM_OK;
    }
    if (strcmp(db->driver_name, "mysql") == 0) {
      *out_dialect = C_ORM_DIALECT_MYSQL;
      return C_ORM_OK;
    }
    if (strcmp(db->driver_name, "db2") == 0) {
      *out_dialect = C_ORM_DIALECT_DB2;
      return C_ORM_OK;
    }
    if (strcmp(db->driver_name, "odbc") == 0) {
      *out_dialect = C_ORM_DIALECT_ODBC;
      return C_ORM_OK;
    }
  }
  *out_dialect = C_ORM_DIALECT_SQLITE;
  return C_ORM_OK;
}

/**
 * @brief Appends a single SQL identifier or expression using ANSI double-quotes
 * on Db2 and ODBC dialects while preserving raw expressions.
 * @param sb Destination string builder.
 * @param dialect Target SQL dialect.
 * @param ident Identifier or expression text.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t append_dialect_ident(c_orm_string_builder_t *sb,
                                          c_orm_dialect_t dialect,
                                          const char *ident) {
  const char *p;
  size_t len;

  if (!ident) {
    return C_ORM_ERROR_UNKNOWN;
  }
  if (dialect != C_ORM_DIALECT_DB2 && dialect != C_ORM_DIALECT_ODBC) {
    return c_orm_string_builder_append(sb, ident);
  }
  len = strlen(ident);
  if (len == 0 || (len == 1 && ident[0] == '*')) {
    return c_orm_string_builder_append(sb, ident);
  }
  if (len >= 2 && ident[0] == '"' && ident[len - 1] == '"') {
    return c_orm_string_builder_append(sb, ident);
  }
  for (p = ident; *p != '\0'; ++p) {
    if (strchr(" \t(),'+-/=<>", *p) != NULL) {
      return c_orm_string_builder_append(sb, ident);
    }
  }
  return c_orm_quote_qualified_identifier(sb, ident, '"');
}

/**
 * @brief Appends a comma-separated list of column identifiers or expressions,
 * quoting simple/qualified identifiers on Db2 and ODBC dialects.
 * @param sb Destination string builder.
 * @param dialect Target SQL dialect.
 * @param csv Comma-separated column list string.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t append_dialect_ident_csv(c_orm_string_builder_t *sb,
                                              c_orm_dialect_t dialect,
                                              const char *csv) {
  const char *p;
  const char *seg_start;
  int paren_depth = 0;
  int first = 1;
  c_orm_error_t rc;

  if (!csv) {
    return C_ORM_ERROR_UNKNOWN;
  }
  if (dialect != C_ORM_DIALECT_DB2 && dialect != C_ORM_DIALECT_ODBC) {
    return c_orm_string_builder_append(sb, csv);
  }

  seg_start = csv;
  for (p = csv;; ++p) {
    if (*p == '(') {
      paren_depth++;
    } else if (*p == ')' && paren_depth > 0) {
      paren_depth--;
    }
    if ((*p == ',' && paren_depth == 0) || *p == '\0') {
      const char *s = seg_start;
      const char *e = p;
      size_t item_len;
      char *item_buf = NULL;

      while (s < e && (*s == ' ' || *s == '\t')) {
        s++;
      }
      while (e > s && (e[-1] == ' ' || e[-1] == '\t')) {
        e--;
      }
      if (!first) {
        rc = c_orm_string_builder_append(sb, ", ");
        if (rc != C_ORM_OK) {
          return rc;
        }
      }
      first = 0;

      item_len = (size_t)(e - s);
      rc = c_orm_system_malloc(item_len + 1, (void **)&item_buf);
      if (rc != C_ORM_OK) {
        return C_ORM_ERROR_MEMORY;
      }
      if (item_len > 0) {
        memcpy(item_buf, s, item_len);
      }
      item_buf[item_len] = '\0';
      rc = append_dialect_ident(sb, dialect, item_buf);
      (void)c_orm_system_free(item_buf);
      if (rc != C_ORM_OK) {
        return rc;
      }
      if (*p == '\0') {
        break;
      }
      seg_start = p + 1;
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Binds a single AST parameter with its appropriate SQL type.
 * @param db Database connection handle.
 * @param stmt Prepared statement handle.
 * @param index 1-based parameter index.
 * @param param AST parameter descriptor.
 * @return C_ORM_OK on success, or driver bind error code.
 */
static c_orm_error_t bind_ast_param(c_orm_db_t *db, c_orm_query_t *stmt,
                                    int index,
                                    const c_orm_query_param_t *param) {
  const char *val = param->value;
  if (param->param_type == C_ORM_TYPE_DECIMAL) {
    if (db->vtable->bind_decimal) {
      return db->vtable->bind_decimal(stmt, index, &param->decimal_val);
    }
    {
      char dec_buf[64];
      size_t dec_len = 0;
      c_orm_error_t drc = c_orm_decimal_to_str(&param->decimal_val, dec_buf,
                                               sizeof(dec_buf), &dec_len);
      if (drc != C_ORM_OK) {
        return drc;
      }
      return db->vtable->bind_string(stmt, index, dec_buf);
    }
  }
  if (!val) {
    if (db->vtable->bind_null) {
      return db->vtable->bind_null(stmt, index);
    }
    return db->vtable->bind_string(stmt, index, "");
  }
  if (!param->is_string) {
    char *endptr = NULL;
    if ((strcmp(val, "NULL") == 0 || strcmp(val, "null") == 0) &&
        db->vtable->bind_null) {
      return db->vtable->bind_null(stmt, index);
    }
    if ((strcmp(val, "TRUE") == 0 || strcmp(val, "true") == 0) &&
        db->vtable->bind_int64) {
      return db->vtable->bind_int64(stmt, index, 1);
    }
    if ((strcmp(val, "FALSE") == 0 || strcmp(val, "false") == 0) &&
        db->vtable->bind_int64) {
      return db->vtable->bind_int64(stmt, index, 0);
    }
    if (val[0] != '\0') {
      const char *endptr_c = NULL;
      int64_t iv = 0;
      if (c_orm_strtoint64(val, &endptr_c, 10, &iv) == C_ORM_OK &&
          *endptr_c == '\0' && db->vtable->bind_int64) {
        return db->vtable->bind_int64(stmt, index, iv);
      }
      if (db->vtable->bind_decimal && strchr(val, 'e') == NULL &&
          strchr(val, 'E') == NULL) {
        c_orm_decimal_t parsed_dec;
        if (c_orm_decimal_from_str(val, &parsed_dec) == C_ORM_OK) {
          return db->vtable->bind_decimal(stmt, index, &parsed_dec);
        }
      }
      endptr = NULL;
      {
        double dv = strtod(val, &endptr);
        if (*endptr == '\0' && db->vtable->bind_double) {
          return db->vtable->bind_double(stmt, index, dv);
        }
      }
    }
  }
  return db->vtable->bind_string(stmt, index, val);
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
  c_orm_error_t rc = C_ORM_OK;
  LOG_DEBUG("c_orm_query_params_cleanup: entry");
  if (!params)
    return C_ORM_ERROR_UNKNOWN;
  if (params->params)
    rc = c_orm_system_free(params->params);
  params->params = NULL;
  params->count = 0;
  params->capacity = 0;
  LOG_DEBUG("c_orm_query_params_cleanup: exit");
  return rc;
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
  c_orm_query_param_t *new_arr = NULL;

  LOG_DEBUG("c_orm_query_params_add: entry");

  if (!params) {
    LOG_DEBUG("c_orm_query_params_add: params is NULL");
    rc = C_ORM_ERROR_UNKNOWN;
    return rc;
  }

  if (params->count >= params->capacity) {
    new_cap = params->capacity == 0 ? 8 : params->capacity * 2;
    if (params->params == NULL) {
      rc = c_orm_system_malloc(sizeof(c_orm_query_param_t) * new_cap,
                               (void **)&new_arr);
    } else {
      rc = c_orm_system_realloc(params->params,
                                sizeof(c_orm_query_param_t) * new_cap,
                                (void **)&new_arr);
    }
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_query_params_add: OOM");
      rc = C_ORM_ERROR_UNKNOWN;
      return rc;
    }
    params->params = new_arr;
    params->capacity = new_cap;
  }

  params->params[params->count].value = value;
  params->params[params->count].is_string = is_string;
  params->params[params->count].param_type = C_ORM_TYPE_UNKNOWN;
  memset(&params->params[params->count].decimal_val, 0,
         sizeof(c_orm_decimal_t));
  params->count++;
  rc = C_ORM_OK;
  LOG_DEBUG("c_orm_query_params_add: exit");
  return rc;
}

/**
 * @brief Adds an exact fixed-point decimal parameter to the query parameters
 * list.
 *
 * @param params The query parameters structure.
 * @param dec Exact decimal value to record.
 * @param str_repr Optional canonical string representation.
 * @return C_ORM_OK on success, or error code on invalid arguments/OOM.
 */
C_ORM_EXPORT c_orm_error_t c_orm_query_params_add_decimal(
    c_orm_query_params_t *params, const c_orm_decimal_t *dec,
    const char *str_repr) {
  c_orm_error_t rc;
  if (!params || !dec) {
    return C_ORM_ERROR_VALIDATION;
  }
  rc = c_orm_query_params_add(params, str_repr, 0);
  if (rc != C_ORM_OK) {
    return rc;
  }
  params->params[params->count - 1].param_type = C_ORM_TYPE_DECIMAL;
  params->params[params->count - 1].decimal_val = *dec;
  return C_ORM_OK;
}

/**
 * @brief Renders an AST node into the string builder.
 *
 * @param node The AST node.
 * @param dialect The SQL dialect.
 * @param sb The string builder.
 * @param params The query parameters.
 * @param depth The current recursion depth.
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
    err = append_dialect_ident(sb, dialect, col->name);
    if (err != C_ORM_OK) {
      return err;
    }
    break;
  }
  case C_ORM_AST_NODE_LITERAL: {
    lit = (c_orm_ast_literal_t *)node;
    if (params) {
      c_orm_error_t rc;
      if (lit->is_decimal) {
        rc = c_orm_query_params_add_decimal(params, &lit->decimal_val,
                                            lit->value);
      } else {
        rc = c_orm_query_params_add(params, lit->value, lit->is_string);
      }
      if (rc != C_ORM_OK) {
        LOG_DEBUG("render_node: OOM adding literal");
        return rc;
      }
      rc = c_orm_append_placeholder(
          sb, dialect == C_ORM_DIALECT_POSTGRES ? 1 : 0, params->count);
      if (rc != C_ORM_OK) {
        return rc;
      }
    } else if (lit->is_string) {
      c_orm_error_t rc;
      rc = c_orm_append_string_literal(sb, lit->value ? lit->value : "");
      if (rc != C_ORM_OK) {
        return rc;
      }
    } else {
      APPEND(lit->value ? lit->value : "NULL");
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
    err = append_dialect_ident(sb, dialect, cast->col);
    if (err != C_ORM_OK) {
      return err;
    }
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
      err = append_dialect_ident(sb, dialect, func->alias);
      if (err != C_ORM_OK) {
        return err;
      }
    }
    break;
  }
  case C_ORM_AST_NODE_BETWEEN: {
    bw = (c_orm_ast_between_t *)node;
    err = append_dialect_ident(sb, dialect, bw->col);
    if (err != C_ORM_OK) {
      return err;
    }
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
    APPEND(ex->is_not ? "NOT EXISTS (" : "EXISTS (");
    err = c_orm_query_to_sql(ex->query, dialect, &subsql, params);
    if (err != C_ORM_OK) {
      LOG_DEBUG("render_node: SQL generation failed for exists");
      return err;
    }
    APPEND(subsql);
    APPEND(")");
    (void)c_orm_system_free(subsql);
    break;
  }
  case C_ORM_AST_NODE_SUBQUERY: {
    sq = (c_orm_ast_subquery_t *)node;
    subsql = NULL;
    APPEND("(");
    err = c_orm_query_to_sql(sq->query, dialect, &subsql, params);
    if (err != C_ORM_OK) {
      LOG_DEBUG("render_node: SQL generation failed for subquery");
      return err;
    }
    APPEND(subsql);
    APPEND(")");
    if (sq->alias && sq->alias[0] != '\0') {
      APPEND(" AS ");
      APPEND(sq->alias);
    }
    (void)c_orm_system_free(subsql);
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
c_orm_query_to_sql(c_orm_ast_query_t *q, c_orm_dialect_t dialect,
                   char **out_sql, c_orm_query_params_t *out_params) {
  c_orm_string_builder_t *sb = NULL;
  c_orm_ast_node_t *node;
  c_orm_ast_select_t *sel = NULL;
  c_orm_ast_from_t *frm = NULL;
  c_orm_ast_where_t *whr = NULL;
  c_orm_ast_having_t *hav = NULL;
  c_orm_ast_limit_t *lim = NULL;
  c_orm_ast_offset_t *off = NULL;
  c_orm_ast_insert_t *ins = NULL;
  c_orm_ast_update_t *upd = NULL;
  c_orm_ast_delete_t *del = NULL;
  c_orm_ast_returning_t *ret = NULL;
  c_orm_ast_lock_mode_t iso_mode = C_ORM_AST_LOCK_NONE;
  int for_fetch_only = 0;
  c_orm_ast_node_t *stack_nodes[64];
  c_orm_ast_node_t **ordered_nodes = stack_nodes;
  c_orm_ast_node_t **heap_nodes = NULL;
  size_t total_nodes = 0;
  size_t assign_count = 0;
  size_t i;
  int has_with = 0;
  int has_group_by = 0;
  int has_order_by = 0;
  c_orm_error_t rc;
  char *subsql = NULL;

  LOG_DEBUG("c_orm_query_to_sql: entry");

  if (!q || !out_sql) {
    LOG_DEBUG("c_orm_query_to_sql: null argument");
    rc = C_ORM_ERROR_UNKNOWN;
    return rc;
  }

  for (node = q->ast_head; node != NULL; node = node->next) {
    total_nodes++;
  }

  if (total_nodes > 64) {
    if (c_orm_system_malloc(total_nodes * sizeof(c_orm_ast_node_t *),
                            (void **)&heap_nodes) != C_ORM_OK) {
      return C_ORM_ERROR_MEMORY;
    }
    ordered_nodes = heap_nodes;
  }

  rc = c_orm_string_builder_init(&sb);
  if (rc != C_ORM_OK) {
    if (heap_nodes) {
      (void)c_orm_system_free(heap_nodes);
    }
    LOG_DEBUG("c_orm_query_to_sql: builder init failed");
    return rc;
  }

  i = total_nodes;
  for (node = q->ast_head; node != NULL; node = node->next) {
    ordered_nodes[--i] = node;
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
    case C_ORM_AST_NODE_HAVING:
      hav = (c_orm_ast_having_t *)node;
      break;
    case C_ORM_AST_NODE_LIMIT:
      lim = (c_orm_ast_limit_t *)node;
      break;
    case C_ORM_AST_NODE_OFFSET:
      off = (c_orm_ast_offset_t *)node;
      break;
    case C_ORM_AST_NODE_INSERT:
      ins = (c_orm_ast_insert_t *)node;
      break;
    case C_ORM_AST_NODE_UPDATE:
      upd = (c_orm_ast_update_t *)node;
      break;
    case C_ORM_AST_NODE_DELETE:
      del = (c_orm_ast_delete_t *)node;
      break;
    case C_ORM_AST_NODE_ASSIGN:
      assign_count++;
      break;
    case C_ORM_AST_NODE_RETURNING:
      ret = (c_orm_ast_returning_t *)node;
      break;
    case C_ORM_AST_NODE_LOCK: {
      c_orm_ast_lock_t *lck = (c_orm_ast_lock_t *)node;
      if (lck->mode == C_ORM_AST_LOCK_FOR_FETCH_ONLY) {
        for_fetch_only = 1;
      } else if (lck->mode != C_ORM_AST_LOCK_NONE &&
                 iso_mode == C_ORM_AST_LOCK_NONE) {
        iso_mode = lck->mode;
      }
      break;
    }
    default:
      break;
    }
  }

  if (ins || upd || del) {
    int db2_wrap_returning = (dialect == C_ORM_DIALECT_DB2 && ret != NULL);

    if (db2_wrap_returning) {
      APPEND_SQL("SELECT ");
      rc = append_dialect_ident_csv(sb, dialect, ret->columns);
      if (rc != C_ORM_OK) {
        goto fail_to_sql;
      }
      APPEND_SQL(del ? " FROM OLD TABLE (" : " FROM FINAL TABLE (");
    }

    if (ins) {
      size_t k = 0;
      APPEND_SQL("INSERT INTO ");
      rc = append_dialect_ident(sb, dialect, ins->table);
      if (rc != C_ORM_OK) {
        goto fail_to_sql;
      }
      if (assign_count == 0) {
        APPEND_SQL(" DEFAULT VALUES");
      } else {
        APPEND_SQL(" (");
        for (i = 0; i < total_nodes; i++) {
          if (ordered_nodes[i]->type == C_ORM_AST_NODE_ASSIGN) {
            c_orm_ast_assign_t *asgn = (c_orm_ast_assign_t *)ordered_nodes[i];
            if (k > 0) {
              APPEND_SQL(", ");
            }
            rc = append_dialect_ident(sb, dialect, asgn->column);
            if (rc != C_ORM_OK) {
              goto fail_to_sql;
            }
            k++;
          }
        }
        APPEND_SQL(") VALUES (");
        k = 0;
        for (i = 0; i < total_nodes; i++) {
          if (ordered_nodes[i]->type == C_ORM_AST_NODE_ASSIGN) {
            c_orm_ast_assign_t *asgn = (c_orm_ast_assign_t *)ordered_nodes[i];
            if (k > 0) {
              APPEND_SQL(", ");
            }
            rc = render_node(asgn->value_expr, dialect, sb, out_params, 0);
            if (rc != C_ORM_OK) {
              goto fail_to_sql;
            }
            k++;
          }
        }
        APPEND_SQL(")");
      }
    } else if (upd) {
      size_t k = 0;
      APPEND_SQL("UPDATE ");
      rc = append_dialect_ident(sb, dialect, upd->table);
      if (rc != C_ORM_OK) {
        goto fail_to_sql;
      }
      APPEND_SQL(" SET ");
      for (i = 0; i < total_nodes; i++) {
        if (ordered_nodes[i]->type == C_ORM_AST_NODE_ASSIGN) {
          c_orm_ast_assign_t *asgn = (c_orm_ast_assign_t *)ordered_nodes[i];
          if (k > 0) {
            APPEND_SQL(", ");
          }
          rc = append_dialect_ident(sb, dialect, asgn->column);
          if (rc != C_ORM_OK) {
            goto fail_to_sql;
          }
          APPEND_SQL(" = ");
          rc = render_node(asgn->value_expr, dialect, sb, out_params, 0);
          if (rc != C_ORM_OK) {
            goto fail_to_sql;
          }
          k++;
        }
      }
      if (whr) {
        APPEND_SQL(" WHERE ");
        rc = render_node(whr->condition, dialect, sb, out_params, 0);
        if (rc != C_ORM_OK) {
          goto fail_to_sql;
        }
      }
    } else {
      APPEND_SQL("DELETE FROM ");
      rc = append_dialect_ident(sb, dialect, del->table);
      if (rc != C_ORM_OK) {
        goto fail_to_sql;
      }
      if (whr) {
        APPEND_SQL(" WHERE ");
        rc = render_node(whr->condition, dialect, sb, out_params, 0);
        if (rc != C_ORM_OK) {
          goto fail_to_sql;
        }
      }
    }

    if (db2_wrap_returning) {
      APPEND_SQL(")");
    } else if (ret) {
      APPEND_SQL(" RETURNING ");
      rc = append_dialect_ident_csv(sb, dialect, ret->columns);
      if (rc != C_ORM_OK) {
        goto fail_to_sql;
      }
    }
  } else {
    for (i = 0; i < total_nodes; i++) {
      if (ordered_nodes[i]->type == C_ORM_AST_NODE_WITH) {
        c_orm_ast_with_t *wth = (c_orm_ast_with_t *)ordered_nodes[i];
        subsql = NULL;
        APPEND_SQL(has_with ? ", " : "WITH ");
        has_with = 1;
        rc = append_dialect_ident(sb, dialect, wth->alias);
        if (rc != C_ORM_OK) {
          goto fail_to_sql;
        }
        APPEND_SQL(" AS (");
        rc = c_orm_query_to_sql(wth->query, dialect, &subsql, out_params);
        if (rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_query_to_sql: with query failed");
          goto fail_to_sql;
        }
        rc = c_orm_string_builder_append(sb, subsql);
        (void)c_orm_system_free(subsql);
        subsql = NULL;
        if (rc != C_ORM_OK) {
          goto fail_to_sql;
        }
        APPEND_SQL(")");
      }
    }
    if (has_with) {
      APPEND_SQL(" ");
    }

    if (sel) {
      APPEND_SQL("SELECT ");
      if (sel->is_distinct)
        APPEND_SQL("DISTINCT ");
      rc = append_dialect_ident_csv(sb, dialect, sel->columns);
      if (rc != C_ORM_OK) {
        goto fail_to_sql;
      }
    }

    if (frm) {
      APPEND_SQL(" FROM ");
      rc = append_dialect_ident(sb, dialect, frm->table);
      if (rc != C_ORM_OK) {
        goto fail_to_sql;
      }
      if (frm->alias && frm->alias[0] != '\0') {
        APPEND_SQL(" AS ");
        rc = append_dialect_ident(sb, dialect, frm->alias);
        if (rc != C_ORM_OK) {
          goto fail_to_sql;
        }
      }
    } else if (sel && dialect == C_ORM_DIALECT_DB2) {
      APPEND_SQL(" FROM SYSIBM.SYSDUMMY1");
    }

    for (i = 0; i < total_nodes; i++) {
      if (ordered_nodes[i]->type == C_ORM_AST_NODE_JOIN) {
        c_orm_ast_join_t *jn = (c_orm_ast_join_t *)ordered_nodes[i];
        APPEND_SQL(" ");
        APPEND_SQL(jn->type_str);
        APPEND_SQL(" JOIN ");
        rc = append_dialect_ident(sb, dialect, jn->table);
        if (rc != C_ORM_OK) {
          goto fail_to_sql;
        }
        APPEND_SQL(" ON ");
        rc = render_node(jn->on_condition, dialect, sb, out_params, 0);
        if (rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_query_to_sql: join failed");
          goto fail_to_sql;
        }
      }
    }

    if (whr && whr->condition) {
      APPEND_SQL(" WHERE ");
      rc = render_node(whr->condition, dialect, sb, out_params, 0);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_query_to_sql: where failed");
        goto fail_to_sql;
      }
    }

    for (i = 0; i < total_nodes; i++) {
      if (ordered_nodes[i]->type == C_ORM_AST_NODE_GROUP_BY) {
        c_orm_ast_group_by_t *grp = (c_orm_ast_group_by_t *)ordered_nodes[i];
        APPEND_SQL(has_group_by ? ", " : " GROUP BY ");
        has_group_by = 1;
        rc = append_dialect_ident_csv(sb, dialect, grp->columns);
        if (rc != C_ORM_OK) {
          goto fail_to_sql;
        }
      }
    }

    if (hav && hav->condition) {
      APPEND_SQL(" HAVING ");
      rc = render_node(hav->condition, dialect, sb, out_params, 0);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_query_to_sql: having failed");
        goto fail_to_sql;
      }
    }

    for (i = 0; i < total_nodes; i++) {
      if (ordered_nodes[i]->type == C_ORM_AST_NODE_UNION) {
        c_orm_ast_union_t *uni = (c_orm_ast_union_t *)ordered_nodes[i];
        subsql = NULL;
        APPEND_SQL(uni->is_all ? " UNION ALL " : " UNION ");
        rc = c_orm_query_to_sql(uni->query, dialect, &subsql, out_params);
        if (rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_query_to_sql: union failed");
          goto fail_to_sql;
        }
        rc = c_orm_string_builder_append(sb, subsql);
        (void)c_orm_system_free(subsql);
        subsql = NULL;
        if (rc != C_ORM_OK) {
          goto fail_to_sql;
        }
      }
    }

    for (i = 0; i < total_nodes; i++) {
      if (ordered_nodes[i]->type == C_ORM_AST_NODE_ORDER_BY) {
        c_orm_ast_order_by_t *ord = (c_orm_ast_order_by_t *)ordered_nodes[i];
        APPEND_SQL(has_order_by ? ", " : " ORDER BY ");
        has_order_by = 1;
        rc = append_dialect_ident(sb, dialect, ord->column);
        if (rc != C_ORM_OK) {
          goto fail_to_sql;
        }
        APPEND_SQL(ord->is_desc ? " DESC" : " ASC");
      }
    }

    if (dialect == C_ORM_DIALECT_DB2 || dialect == C_ORM_DIALECT_ODBC) {
      char num_buf[96];
      if (lim && off) {
        rc = C_ORM_SPRINTF(num_buf, sizeof(num_buf),
                           " OFFSET " C_ORM_FMT_SIZE_T
                           " ROWS FETCH NEXT " C_ORM_FMT_SIZE_T " ROWS ONLY",
                           C_ORM_CAST_SIZE_T(off->offset),
                           C_ORM_CAST_SIZE_T(lim->limit));
        if (rc != C_ORM_OK) {
          goto fail_to_sql;
        }
        APPEND_SQL(num_buf);
      } else if (lim) {
        rc = C_ORM_SPRINTF(num_buf, sizeof(num_buf),
                           " FETCH FIRST " C_ORM_FMT_SIZE_T " ROWS ONLY",
                           C_ORM_CAST_SIZE_T(lim->limit));
        if (rc != C_ORM_OK) {
          goto fail_to_sql;
        }
        APPEND_SQL(num_buf);
      } else if (off) {
        rc = C_ORM_SPRINTF(num_buf, sizeof(num_buf),
                           " OFFSET " C_ORM_FMT_SIZE_T " ROWS",
                           C_ORM_CAST_SIZE_T(off->offset));
        if (rc != C_ORM_OK) {
          goto fail_to_sql;
        }
        APPEND_SQL(num_buf);
      }
    } else {
      if (lim) {
        char num_buf[32];
        rc = C_ORM_SPRINTF(num_buf, sizeof(num_buf), " LIMIT %u",
                           (unsigned int)lim->limit);
        if (rc != C_ORM_OK) {
          goto fail_to_sql;
        }
        APPEND_SQL(num_buf);
      }

      if (off) {
        char num_buf[32];
        rc = C_ORM_SPRINTF(num_buf, sizeof(num_buf), " OFFSET %u",
                           (unsigned int)off->offset);
        if (rc != C_ORM_OK) {
          goto fail_to_sql;
        }
        APPEND_SQL(num_buf);
      }
    }

    if (dialect == C_ORM_DIALECT_DB2) {
      if (for_fetch_only) {
        APPEND_SQL(" FOR FETCH ONLY");
      }
      switch (iso_mode) {
      case C_ORM_AST_LOCK_WITH_UR:
        APPEND_SQL(" WITH UR");
        break;
      case C_ORM_AST_LOCK_WITH_CS:
        APPEND_SQL(" WITH CS");
        break;
      case C_ORM_AST_LOCK_WITH_RS:
        APPEND_SQL(" WITH RS");
        break;
      case C_ORM_AST_LOCK_WITH_RR:
        APPEND_SQL(" WITH RR");
        break;
      case C_ORM_AST_LOCK_NONE:
      case C_ORM_AST_LOCK_FOR_FETCH_ONLY:
      default:
        break;
      }
    }
  }

  {
    const char *sql_str = NULL;
    size_t len;
    rc = c_orm_string_builder_get(sb, &sql_str);
    if (rc != C_ORM_OK) {
      goto fail_to_sql;
    }
    len = strlen(sql_str);
    *out_sql = NULL;
    if (c_orm_system_malloc(len + 1, (void **)out_sql) != C_ORM_OK) {
      LOG_DEBUG("c_orm_query_to_sql: OOM sql out");
      rc = C_ORM_ERROR_MEMORY;
      goto fail_to_sql;
    }
    rc = C_ORM_STRCPY(*out_sql, len + 1, sql_str);
    if (rc != C_ORM_OK) {
      (void)c_orm_system_free(*out_sql);
      *out_sql = NULL;
      goto fail_to_sql;
    }
  }

  if (heap_nodes) {
    (void)c_orm_system_free(heap_nodes);
  }
  rc = c_orm_string_builder_free(sb);

  LOG_DEBUG("c_orm_query_to_sql: exit");
  return rc;

fail_to_sql:
  if (heap_nodes) {
    (void)c_orm_system_free(heap_nodes);
  }
  (void)c_orm_string_builder_free(sb);
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
                                               c_orm_ast_query_t *q) {
  char *sql = NULL;
  c_orm_query_params_t params;
  c_orm_stmt_t *stmt = NULL;
  c_orm_dialect_t dialect = C_ORM_DIALECT_SQLITE;
  c_orm_error_t err;
  c_orm_error_t fin_rc = C_ORM_OK;
  c_orm_error_t cln_rc;
  size_t i;
  int has_row;

  LOG_DEBUG("c_orm_query_execute: entry");

  err = resolve_db_dialect(db, &dialect);
  if (err != C_ORM_OK || !q) {
    LOG_DEBUG("c_orm_query_execute: null argument");
    return C_ORM_ERROR_UNKNOWN;
  }

  params.params = NULL;
  params.count = 0;
  params.capacity = 0;
  err = c_orm_query_to_sql(q, dialect, &sql, &params);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_execute: to_sql failed");
    goto cleanup_exec;
  }

  err = c_orm_prepare_cached(db, sql, &stmt);
  (void)c_orm_system_free(sql);
  sql = NULL;
  if (err != C_ORM_OK) {
    stmt = NULL;
    LOG_DEBUG("c_orm_query_execute: prepare failed");
    goto cleanup_exec;
  }

  for (i = 0; i < params.count; i++) {
    err = bind_ast_param(db, stmt, (int)(i + 1), &params.params[i]);
    if (err != C_ORM_OK) {
      LOG_DEBUG("c_orm_query_execute: bind failed");
      goto cleanup_exec;
    }
  }

  err = db->vtable->step(stmt, &has_row);

cleanup_exec:
  if (stmt) {
    fin_rc = c_orm_finalize_cached(db, stmt);
  }
  cln_rc = c_orm_query_params_cleanup(&params);
  if (err != C_ORM_OK) {
    return err;
  }
  if (fin_rc != C_ORM_OK) {
    return fin_rc;
  }
  LOG_DEBUG("c_orm_query_execute: exit");
  return cln_rc;
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
                                                 c_orm_ast_query_t *q,
                                                 const c_orm_table_meta_t *meta,
                                                 void *out_struct) {
  char *sql = NULL;
  c_orm_query_params_t params;
  c_orm_stmt_t *stmt = NULL;
  c_orm_dialect_t dialect = C_ORM_DIALECT_SQLITE;
  c_orm_error_t err;
  c_orm_error_t fin_rc = C_ORM_OK;
  c_orm_error_t cln_rc;
  size_t i;
  int has_row;

  LOG_DEBUG("c_orm_query_fetch_one: entry");

  err = resolve_db_dialect(db, &dialect);
  if (err != C_ORM_OK || !q || !meta || !out_struct) {
    LOG_DEBUG("c_orm_query_fetch_one: null argument");
    return C_ORM_ERROR_UNKNOWN;
  }

  params.params = NULL;
  params.count = 0;
  params.capacity = 0;
  err = c_orm_query_to_sql(q, dialect, &sql, &params);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_fetch_one: to_sql failed");
    goto cleanup_one;
  }

  err = c_orm_prepare_cached(db, sql, &stmt);
  (void)c_orm_system_free(sql);
  sql = NULL;
  if (err != C_ORM_OK) {
    stmt = NULL;
    LOG_DEBUG("c_orm_query_fetch_one: prepare failed");
    goto cleanup_one;
  }

  for (i = 0; i < params.count; i++) {
    err = bind_ast_param(db, stmt, (int)(i + 1), &params.params[i]);
    if (err != C_ORM_OK) {
      LOG_DEBUG("c_orm_query_fetch_one: bind failed");
      goto cleanup_one;
    }
  }

  err = db->vtable->step(stmt, &has_row);
  if (err != C_ORM_OK) {
    goto cleanup_one;
  }
  if (!has_row) {
    err = C_ORM_ERROR_NOT_FOUND;
    goto cleanup_one;
  }

  err = c_orm_hydrate_row(db, stmt, meta, out_struct);

cleanup_one:
  if (stmt) {
    fin_rc = c_orm_finalize_cached(db, stmt);
  }
  cln_rc = c_orm_query_params_cleanup(&params);
  if (err != C_ORM_OK) {
    return err;
  }
  if (fin_rc != C_ORM_OK) {
    return fin_rc;
  }

  LOG_DEBUG("c_orm_query_fetch_one: exit");
  return cln_rc;
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
                                                 c_orm_ast_query_t *q,
                                                 const c_orm_table_meta_t *meta,
                                                 void *out_array) {
  char *sql = NULL;
  c_orm_query_params_t params;
  c_orm_stmt_t *stmt = NULL;
  c_orm_dialect_t dialect = C_ORM_DIALECT_SQLITE;
  c_orm_error_t err;
  c_orm_error_t fin_rc = C_ORM_OK;
  c_orm_error_t cln_rc;
  size_t i;

  LOG_DEBUG("c_orm_query_fetch_all: entry");

  err = resolve_db_dialect(db, &dialect);
  if (err != C_ORM_OK || !q || !meta || !out_array) {
    LOG_DEBUG("c_orm_query_fetch_all: null argument");
    return C_ORM_ERROR_UNKNOWN;
  }

  params.params = NULL;
  params.count = 0;
  params.capacity = 0;
  err = c_orm_query_to_sql(q, dialect, &sql, &params);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_query_fetch_all: to_sql failed");
    goto cleanup_all;
  }

  err = c_orm_prepare_cached(db, sql, &stmt);
  (void)c_orm_system_free(sql);
  sql = NULL;
  if (err != C_ORM_OK) {
    stmt = NULL;
    LOG_DEBUG("c_orm_query_fetch_all: prepare failed");
    goto cleanup_all;
  }

  for (i = 0; i < params.count; i++) {
    err = bind_ast_param(db, stmt, (int)(i + 1), &params.params[i]);
    if (err != C_ORM_OK) {
      LOG_DEBUG("c_orm_query_fetch_all: bind failed");
      goto cleanup_all;
    }
  }

  err = c_orm_hydrate_all(db, stmt, meta, out_array);

cleanup_all:
  if (stmt) {
    fin_rc = c_orm_finalize_cached(db, stmt);
  }
  cln_rc = c_orm_query_params_cleanup(&params);
  if (err != C_ORM_OK) {
    return err;
  }
  if (fin_rc != C_ORM_OK) {
    return fin_rc;
  }
  LOG_DEBUG("c_orm_query_fetch_all: exit");
  return cln_rc;
}
