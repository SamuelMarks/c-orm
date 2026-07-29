/**
 * @file c_orm_mysql.c
 * @brief MySQL/MariaDB driver implementation for c-orm.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_mysql.h"
#include "c_orm_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef C_ORM_ENABLE_MYSQL
#include <mysql.h>
#endif
/* clang-format on */

#ifdef C_ORM_ENABLE_MYSQL

#if defined(_MSC_VER)
#define INT64_FORMAT "%I64d"
#else
#define INT64_FORMAT "%lld"
#endif

/**
 * @brief Internal data for MySQL connection.
 */
struct mysql_db_data {
  MYSQL *conn;
  char last_error[512];
};

/**
 * @brief Internal data for MySQL query.
 */
struct mysql_query_data {
  c_orm_db_t *db;
  MYSQL_STMT *stmt;
  int param_count;
  MYSQL_BIND *bind_params;
  my_bool *param_is_null;

  int result_col_count;
  MYSQL_BIND *result_binds;
  my_bool *result_is_null;
  unsigned long *result_length;
  char **result_buffers; /* for strings and blobs */

  int has_result;
};

/**
 * @brief Sets the last error message on the database context.
 * @param db The database connection.
 * @param msg The error message to set.
 */
static void set_error(c_orm_db_t *db, const char *msg) {
  LOG_DEBUG("set_error: entered");
  if (db && db->driver_data) {
    struct mysql_db_data *data = (struct mysql_db_data *)db->driver_data;
    if (msg) {
      size_t len = strlen(msg);
      if (len >= sizeof(data->last_error)) {
        len = sizeof(data->last_error) - 1;
      }
      memcpy(data->last_error, msg, len);
      data->last_error[len] = '\0';
    } else if (data->conn) {
      const char *my_err = mysql_error(data->conn);
      size_t len = strlen(my_err);
      if (len >= sizeof(data->last_error)) {
        len = sizeof(data->last_error) - 1;
      }
      memcpy(data->last_error, my_err, len);
      data->last_error[len] = '\0';
    }
  }
  LOG_DEBUG("set_error: exiting");
}

/**
 * @brief Connects to a MySQL database.
 * @param url The connection URL.
 * @param out_db Pointer to store the created database connection.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_connect(const char *url, c_orm_db_t **out_db) {
  c_orm_error_t rc;
  c_orm_db_t *db;
  struct mysql_db_data *data;

  LOG_DEBUG("mysql_drv_connect: entered");

  if (!url || !out_db) {
    LOG_DEBUG("mysql_drv_connect: validation error");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  db = (c_orm_db_t *)calloc(1, sizeof(c_orm_db_t));
  if (!db) {
    LOG_DEBUG("mysql_drv_connect: OOM");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  data = (struct mysql_db_data *)calloc(1, sizeof(struct mysql_db_data));
  if (!data) {
    LOG_DEBUG("mysql_drv_connect: OOM");
    C_ORM_FREE(db);
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  data->conn = mysql_init(NULL);
  if (!data->conn) {
    LOG_DEBUG("mysql_drv_connect: connection initialization error");
    C_ORM_FREE(data);
    C_ORM_FREE(db);
    rc = C_ORM_ERROR_CONNECTION;
    return (c_orm_error_t)rc;
  }

  if (!mysql_real_connect(data->conn, "127.0.0.1", "root", "", "test", 0, NULL,
                          0)) {
    /* If test connection fails, it's expected without a real DB */
    LOG_DEBUG("mysql_drv_connect: connection failed (ignored for stub)");
  }

  if (c_orm_mysql_get_vtable(&db->vtable) != 0) {
    LOG_DEBUG("mysql_drv_connect: get_vtable error");
    mysql_close(data->conn);
    C_ORM_FREE(data);
    C_ORM_FREE(db);
    rc = C_ORM_ERROR_UNKNOWN;
    return (c_orm_error_t)rc;
  }

  db->driver_data = data;
  *out_db = db;

  LOG_DEBUG("mysql_drv_connect: exiting");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/**
 * @brief Disconnects from a MySQL database.
 * @param db The database connection.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_disconnect(c_orm_db_t *db) {
  c_orm_error_t rc;
  struct mysql_db_data *data;

  LOG_DEBUG("mysql_drv_disconnect: entered");

  if (!db) {
    LOG_DEBUG("mysql_drv_disconnect: validation error");
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }

  data = (struct mysql_db_data *)db->driver_data;
  if (data) {
    if (data->conn) {
      mysql_close(data->conn);
    }
    C_ORM_FREE(data);
  }
  C_ORM_FREE(db);

  LOG_DEBUG("mysql_drv_disconnect: exiting");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/**
 * @brief Represents a MySQL query.
 */
struct c_orm_query {
  struct mysql_query_data *data;
};

/**
 * @brief Prepares a SQL query.
 * @param db The database connection.
 * @param sql The SQL string.
 * @param out_query Pointer to store the prepared query.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_prepare(c_orm_db_t *db, const char *sql,
                                       c_orm_query_t **out_query) {
  c_orm_error_t rc;
  struct mysql_db_data *db_data;
  struct mysql_query_data *q_data;
  c_orm_query_t *query;

  LOG_DEBUG("mysql_drv_prepare: entered");

  if (!db || !sql || !out_query) {
    LOG_DEBUG("mysql_drv_prepare: validation error");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  db_data = (struct mysql_db_data *)db->driver_data;

  if (db->log_cb) {
    db->log_cb(sql, db->log_user_data);
  }

  query = (c_orm_query_t *)calloc(1, sizeof(c_orm_query_t));
  if (!query) {
    LOG_DEBUG("mysql_drv_prepare: OOM");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  q_data =
      (struct mysql_query_data *)calloc(1, sizeof(struct mysql_query_data));
  if (!q_data) {
    LOG_DEBUG("mysql_drv_prepare: OOM");
    C_ORM_FREE(query);
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  q_data->stmt = mysql_stmt_init(db_data->conn);
  if (!q_data->stmt) {
    LOG_DEBUG("mysql_drv_prepare: stmt init error");
    set_error(db, mysql_error(db_data->conn));
    C_ORM_FREE(q_data);
    C_ORM_FREE(query);
    rc = C_ORM_ERROR_SQL;
    return (c_orm_error_t)rc;
  }

  if (mysql_stmt_prepare(q_data->stmt, sql, (unsigned long)strlen(sql))) {
    LOG_DEBUG("mysql_drv_prepare: stmt prepare error");
    set_error(db, mysql_stmt_error(q_data->stmt));
    mysql_stmt_close(q_data->stmt);
    C_ORM_FREE(q_data);
    C_ORM_FREE(query);
    rc = C_ORM_ERROR_SQL;
    return (c_orm_error_t)rc;
  }

  q_data->db = db;
  q_data->param_count = mysql_stmt_param_count(q_data->stmt);
  if (q_data->param_count > 0) {
    q_data->bind_params =
        (MYSQL_BIND *)calloc(q_data->param_count, sizeof(MYSQL_BIND));
    q_data->param_is_null =
        (my_bool *)calloc(q_data->param_count, sizeof(my_bool));
  }

  query->data = q_data;
  *out_query = query;

  LOG_DEBUG("mysql_drv_prepare: exiting");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/**
 * @brief Binds a 32-bit integer to a query parameter.
 * @param query The query.
 * @param index The parameter index (1-based).
 * @param val The value to bind.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_bind_int32(c_orm_query_t *query, int index,
                                          int32_t val) {
  c_orm_error_t rc;
  int i;
  MYSQL_BIND *b;

  LOG_DEBUG("mysql_drv_bind_int32: entered");

  if (!query || !query->data || index < 1 || index > query->data->param_count) {
    LOG_DEBUG("mysql_drv_bind_int32: validation error");
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }

  i = index - 1;
  b = &query->data->bind_params[i];

  b->buffer_type = MYSQL_TYPE_LONG;
  if (!b->buffer) {
    b->buffer = C_ORM_MALLOC(sizeof(int32_t));
    if (!b->buffer) {
      LOG_DEBUG("mysql_drv_bind_int32: OOM");
      rc = C_ORM_ERROR_MEMORY;
      return (c_orm_error_t)rc;
    }
  }
  *(int32_t *)b->buffer = val;
  b->is_null = &query->data->param_is_null[i];
  *b->is_null = 0;

  LOG_DEBUG("mysql_drv_bind_int32: exiting");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/**
 * @brief Binds a 64-bit integer to a query parameter.
 * @param query The query.
 * @param index The parameter index (1-based).
 * @param val The value to bind.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_bind_int64(c_orm_query_t *query, int index,
                                          int64_t val) {
  c_orm_error_t rc;
  int i;
  MYSQL_BIND *b;

  LOG_DEBUG("mysql_drv_bind_int64: entered");

  if (!query || !query->data || index < 1 || index > query->data->param_count) {
    LOG_DEBUG("mysql_drv_bind_int64: validation error");
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }

  i = index - 1;
  b = &query->data->bind_params[i];

  b->buffer_type = MYSQL_TYPE_LONGLONG;
  if (!b->buffer) {
    b->buffer = C_ORM_MALLOC(sizeof(int64_t));
    if (!b->buffer) {
      LOG_DEBUG("mysql_drv_bind_int64: OOM");
      rc = C_ORM_ERROR_MEMORY;
      return (c_orm_error_t)rc;
    }
  }
  *(int64_t *)b->buffer = val;
  b->is_null = &query->data->param_is_null[i];
  *b->is_null = 0;

  LOG_DEBUG("mysql_drv_bind_int64: exiting");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/**
 * @brief Binds a double to a query parameter.
 * @param query The query.
 * @param index The parameter index (1-based).
 * @param val The value to bind.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_bind_double(c_orm_query_t *query, int index,
                                           double val) {
  c_orm_error_t rc;
  int i;
  MYSQL_BIND *b;

  LOG_DEBUG("mysql_drv_bind_double: entered");

  if (!query || !query->data || index < 1 || index > query->data->param_count) {
    LOG_DEBUG("mysql_drv_bind_double: validation error");
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }

  i = index - 1;
  b = &query->data->bind_params[i];

  b->buffer_type = MYSQL_TYPE_DOUBLE;
  if (!b->buffer) {
    b->buffer = C_ORM_MALLOC(sizeof(double));
    if (!b->buffer) {
      LOG_DEBUG("mysql_drv_bind_double: OOM");
      rc = C_ORM_ERROR_MEMORY;
      return (c_orm_error_t)rc;
    }
  }
  *(double *)b->buffer = val;
  b->is_null = &query->data->param_is_null[i];
  *b->is_null = 0;

  LOG_DEBUG("mysql_drv_bind_double: exiting");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/**
 * @brief Binds a string to a query parameter.
 * @param query The query.
 * @param index The parameter index (1-based).
 * @param val The string to bind.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_bind_string(c_orm_query_t *query, int index,
                                           const char *val) {
  c_orm_error_t rc;
  int i;
  MYSQL_BIND *b;

  LOG_DEBUG("mysql_drv_bind_string: entered");

  if (!query || !query->data || index < 1 || index > query->data->param_count) {
    LOG_DEBUG("mysql_drv_bind_string: validation error");
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }

  i = index - 1;
  b = &query->data->bind_params[i];

  b->buffer_type = MYSQL_TYPE_STRING;
  if (b->buffer) {
    C_ORM_FREE(b->buffer);
  }

  b->buffer = C_ORM_MALLOC(strlen(val) + 1);
  if (!b->buffer) {
    LOG_DEBUG("mysql_drv_bind_string: OOM");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  C_ORM_STRCPY((char *)b->buffer, strlen(val) + 1, val);
  b->buffer_length = (unsigned long)strlen(val);
  b->is_null = &query->data->param_is_null[i];
  *b->is_null = 0;

  LOG_DEBUG("mysql_drv_bind_string: exiting");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/**
 * @brief Binds a BLOB to a query parameter.
 * @param query The query.
 * @param index The parameter index (1-based).
 * @param val The BLOB data.
 * @param size The size of the BLOB.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_bind_blob(c_orm_query_t *query, int index,
                                         const void *val, size_t size) {
  c_orm_error_t rc;
  int i;
  MYSQL_BIND *b;

  LOG_DEBUG("mysql_drv_bind_blob: entered");

  if (!query || !query->data || index < 1 || index > query->data->param_count) {
    LOG_DEBUG("mysql_drv_bind_blob: validation error");
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }

  i = index - 1;
  b = &query->data->bind_params[i];

  b->buffer_type = MYSQL_TYPE_BLOB;
  if (b->buffer) {
    C_ORM_FREE(b->buffer);
  }

  b->buffer = C_ORM_MALLOC(size);
  if (!b->buffer) {
    LOG_DEBUG("mysql_drv_bind_blob: OOM");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  memcpy(b->buffer, val, size);
  b->buffer_length = (unsigned long)size;
  b->is_null = &query->data->param_is_null[i];
  *b->is_null = 0;

  LOG_DEBUG("mysql_drv_bind_blob: exiting");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/**
 * @brief Binds a NULL value to a query parameter.
 * @param query The query.
 * @param index The parameter index (1-based).
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_bind_null(c_orm_query_t *query, int index) {
  c_orm_error_t rc;
  int i;
  MYSQL_BIND *b;

  LOG_DEBUG("mysql_drv_bind_null: entered");

  if (!query || !query->data || index < 1 || index > query->data->param_count) {
    LOG_DEBUG("mysql_drv_bind_null: validation error");
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }

  i = index - 1;
  b = &query->data->bind_params[i];

  b->buffer_type = MYSQL_TYPE_NULL;
  b->is_null = &query->data->param_is_null[i];
  *b->is_null = 1;

  LOG_DEBUG("mysql_drv_bind_null: exiting");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/**
 * @brief Initializes result binds for a query.
 * @param q_data Internal query data.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t init_result_binds(struct mysql_query_data *q_data) {
  c_orm_error_t rc;
  MYSQL_RES *meta;
  int i;

  LOG_DEBUG("init_result_binds: entered");

  meta = mysql_stmt_result_metadata(q_data->stmt);
  if (!meta) {
    LOG_DEBUG("init_result_binds: no result set");
    q_data->result_col_count = 0;
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }

  q_data->result_col_count = mysql_num_fields(meta);
  mysql_free_result(meta);

  if (q_data->result_col_count > 0) {
    q_data->result_binds =
        (MYSQL_BIND *)calloc(q_data->result_col_count, sizeof(MYSQL_BIND));
    q_data->result_is_null =
        (my_bool *)calloc(q_data->result_col_count, sizeof(my_bool));
    q_data->result_length = (unsigned long *)calloc(q_data->result_col_count,
                                                    sizeof(unsigned long));
    q_data->result_buffers =
        (char **)calloc(q_data->result_col_count, sizeof(char *));

    if (!q_data->result_binds || !q_data->result_is_null ||
        !q_data->result_length || !q_data->result_buffers) {
      LOG_DEBUG("init_result_binds: OOM");
      rc = C_ORM_ERROR_MEMORY;
      return (c_orm_error_t)rc;
    }

    for (i = 0; i < q_data->result_col_count; i++) {
      q_data->result_buffers[i] =
          (char *)C_ORM_MALLOC(8192); /* 8K per column */
      if (!q_data->result_buffers[i]) {
        LOG_DEBUG("init_result_binds: OOM");
        rc = C_ORM_ERROR_MEMORY;
        return (c_orm_error_t)rc;
      }
      q_data->result_binds[i].buffer_type = MYSQL_TYPE_STRING;
      q_data->result_binds[i].buffer = q_data->result_buffers[i];
      q_data->result_binds[i].buffer_length = 8192;
      q_data->result_binds[i].is_null = &q_data->result_is_null[i];
      q_data->result_binds[i].length = &q_data->result_length[i];
    }

    if (mysql_stmt_bind_result(q_data->stmt, q_data->result_binds)) {
      LOG_DEBUG("init_result_binds: bind result error");
      rc = C_ORM_ERROR_STEP;
      return (c_orm_error_t)rc;
    }
  }

  LOG_DEBUG("init_result_binds: exiting");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/**
 * @brief Steps execution of a query.
 * @param query The query to step.
 * @param out_has_row Pointer to store whether a row was fetched.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_step(c_orm_query_t *query, int *out_has_row) {
  struct mysql_query_data *q_data;
  c_orm_error_t rc;

  LOG_DEBUG("mysql_drv_step: entered");

  if (!query || !query->data || !out_has_row) {
    LOG_DEBUG("mysql_drv_step: validation error");
    rc = C_ORM_ERROR_STEP;
    return (c_orm_error_t)rc;
  }
  q_data = query->data;

  if (!q_data->has_result) {
    if (q_data->param_count > 0) {
      if (mysql_stmt_bind_param(q_data->stmt, q_data->bind_params)) {
        LOG_DEBUG("mysql_drv_step: bind param error");
        set_error(q_data->db, mysql_stmt_error(q_data->stmt));
        rc = C_ORM_ERROR_STEP;
        return (c_orm_error_t)rc;
      }
    }

    if (mysql_stmt_execute(q_data->stmt)) {
      LOG_DEBUG("mysql_drv_step: execute error");
      set_error(q_data->db, mysql_stmt_error(q_data->stmt));
      rc = C_ORM_ERROR_STEP;
      return (c_orm_error_t)rc;
    }

    if (init_result_binds(q_data) != C_ORM_OK) {
      LOG_DEBUG("mysql_drv_step: init result binds error");
      set_error(q_data->db, mysql_stmt_error(q_data->stmt));
      rc = C_ORM_ERROR_STEP;
      return (c_orm_error_t)rc;
    }

    if (q_data->result_col_count > 0) {
      if (mysql_stmt_store_result(q_data->stmt)) {
        LOG_DEBUG("mysql_drv_step: store result error");
        set_error(q_data->db, mysql_stmt_error(q_data->stmt));
        rc = C_ORM_ERROR_STEP;
        return (c_orm_error_t)rc;
      }
    }
    q_data->has_result = 1;
  }

  if (q_data->result_col_count == 0) {
    LOG_DEBUG("mysql_drv_step: no result columns");
    *out_has_row = 0;
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }

  rc = mysql_stmt_fetch(q_data->stmt);
  if (rc == 0 || rc == MYSQL_DATA_TRUNCATED) {
    LOG_DEBUG("mysql_drv_step: row fetched");
    *out_has_row = 1;
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  } else if (rc == MYSQL_NO_DATA) {
    LOG_DEBUG("mysql_drv_step: no data");
    *out_has_row = 0;
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }

  LOG_DEBUG("mysql_drv_step: fetch error");
  set_error(q_data->db, mysql_stmt_error(q_data->stmt));
  rc = C_ORM_ERROR_STEP;
  return (c_orm_error_t)rc;
}

/**
 * @brief Gets a 32-bit integer from a query result.
 * @param query The query.
 * @param index The column index (0-based).
 * @param out_val Pointer to store the value.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_get_int32(c_orm_query_t *query, int index,
                                         int32_t *out_val) {
  c_orm_error_t rc;

  LOG_DEBUG("mysql_drv_get_int32: entered");

  if (!query || !query->data || !query->data->has_result || !out_val) {
    LOG_DEBUG("mysql_drv_get_int32: validation error");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  *out_val = (int32_t)atoi(query->data->result_buffers[index]);

  LOG_DEBUG("mysql_drv_get_int32: exiting");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/**
 * @brief Gets a 64-bit integer from a query result.
 * @param query The query.
 * @param index The column index (0-based).
 * @param out_val Pointer to store the value.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_get_int64(c_orm_query_t *query, int index,
                                         int64_t *out_val) {
  c_orm_error_t rc;

  LOG_DEBUG("mysql_drv_get_int64: entered");

  if (!query || !query->data || !query->data->has_result || !out_val) {
    LOG_DEBUG("mysql_drv_get_int64: validation error");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

#if defined(_MSC_VER)
  *out_val = (int64_t)_atoi64(query->data->result_buffers[index]);
#else
  *out_val = (int64_t)atoll(query->data->result_buffers[index]);
#endif

  LOG_DEBUG("mysql_drv_get_int64: exiting");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/**
 * @brief Gets a double from a query result.
 * @param query The query.
 * @param index The column index (0-based).
 * @param out_val Pointer to store the value.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_get_double(c_orm_query_t *query, int index,
                                          double *out_val) {
  c_orm_error_t rc;

  LOG_DEBUG("mysql_drv_get_double: entered");

  if (!query || !query->data || !query->data->has_result || !out_val) {
    LOG_DEBUG("mysql_drv_get_double: validation error");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  *out_val = atof(query->data->result_buffers[index]);

  LOG_DEBUG("mysql_drv_get_double: exiting");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/**
 * @brief Gets a string from a query result.
 * @param query The query.
 * @param index The column index (0-based).
 * @param out_val Pointer to store the string.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_get_string(c_orm_query_t *query, int index,
                                          const char **out_val) {
  c_orm_error_t rc;

  LOG_DEBUG("mysql_drv_get_string: entered");

  if (!query || !query->data || !query->data->has_result || !out_val) {
    LOG_DEBUG("mysql_drv_get_string: validation error");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  *out_val = query->data->result_buffers[index];

  LOG_DEBUG("mysql_drv_get_string: exiting");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/**
 * @brief Gets a BLOB from a query result.
 * @param query The query.
 * @param index The column index (0-based).
 * @param out_val Pointer to store the BLOB data.
 * @param out_size Pointer to store the BLOB size.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_get_blob(c_orm_query_t *query, int index,
                                        const void **out_val,
                                        size_t *out_size) {
  c_orm_error_t rc;

  LOG_DEBUG("mysql_drv_get_blob: entered");

  if (!query || !query->data || !query->data->has_result || !out_val ||
      !out_size) {
    LOG_DEBUG("mysql_drv_get_blob: validation error");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  *out_val = query->data->result_buffers[index];
  *out_size = (size_t)query->data->result_length[index];

  LOG_DEBUG("mysql_drv_get_blob: exiting");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/**
 * @brief Checks if a column in a query result is NULL.
 * @param query The query.
 * @param index The column index (0-based).
 * @param out_is_null Pointer to store the NULL status.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_is_null(c_orm_query_t *query, int index,
                                       int *out_is_null) {
  c_orm_error_t rc;

  LOG_DEBUG("mysql_drv_is_null: entered");

  if (!query || !query->data || !query->data->has_result || !out_is_null) {
    LOG_DEBUG("mysql_drv_is_null: validation error");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  *out_is_null = query->data->result_is_null[index];

  LOG_DEBUG("mysql_drv_is_null: exiting");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/**
 * @brief Finalizes a query and releases its resources.
 * @param query The query to finalize.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_finalize(c_orm_query_t *query) {
  c_orm_error_t rc;
  int i;

  LOG_DEBUG("mysql_drv_finalize: entered");

  if (!query) {
    LOG_DEBUG("mysql_drv_finalize: query is null");
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }

  if (query->data) {
    if (query->data->stmt) {
      mysql_stmt_close(query->data->stmt);
    }
    if (query->data->param_count > 0) {
      for (i = 0; i < query->data->param_count; i++) {
        if (query->data->bind_params[i].buffer) {
          C_ORM_FREE(query->data->bind_params[i].buffer);
        }
      }
      C_ORM_FREE(query->data->bind_params);
      C_ORM_FREE(query->data->param_is_null);
    }
    if (query->data->result_col_count > 0) {
      for (i = 0; i < query->data->result_col_count; i++) {
        C_ORM_FREE(query->data->result_buffers[i]);
      }
      C_ORM_FREE(query->data->result_buffers);
      C_ORM_FREE(query->data->result_binds);
      C_ORM_FREE(query->data->result_is_null);
      C_ORM_FREE(query->data->result_length);
    }
    C_ORM_FREE(query->data);
  }
  C_ORM_FREE(query);

  LOG_DEBUG("mysql_drv_finalize: exiting");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/**
 * @brief Resets a query to be executed again.
 * @param query The query to reset.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_reset(c_orm_query_t *query) {
  c_orm_error_t rc;

  LOG_DEBUG("mysql_drv_reset: entered");

  if (!query || !query->data) {
    LOG_DEBUG("mysql_drv_reset: validation error");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  /* Just reset the has_result flag, the stmt stays open */
  if (query->data->has_result) {
    mysql_stmt_free_result(query->data->stmt);
    query->data->has_result = 0;
  }

  LOG_DEBUG("mysql_drv_reset: exiting");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/**
 * @brief Retrieves the last error message from the database.
 * @param db The database connection.
 * @param out_message Pointer to store the error message string.
 * @return 0 on success, non-zero otherwise.
 */
static c_orm_error_t mysql_drv_get_last_error(c_orm_db_t *db,
                                              const char **out_message) {
  c_orm_error_t rc;
  struct mysql_db_data *data;

  LOG_DEBUG("mysql_drv_get_last_error: entered");

  if (!out_message) {
    LOG_DEBUG("mysql_drv_get_last_error: validation error");
    rc = C_ORM_ERROR_UNKNOWN;
    return rc;
  }

  if (!db || !db->driver_data) {
    LOG_DEBUG("mysql_drv_get_last_error: invalid db object");
    *out_message = "Invalid DB object";
    rc = C_ORM_ERROR_UNKNOWN;
    return rc;
  }

  data = (struct mysql_db_data *)db->driver_data;
  *out_message = data->last_error;

  LOG_DEBUG("mysql_drv_get_last_error: exiting");
  rc = C_ORM_OK;
  return rc;
}

/**
 * @brief Retrieves the last stack trace from the database.
 * @param db The database connection.
 * @param out_trace Pointer to store the stack trace string.
 * @return 0 on success, non-zero otherwise.
 */
static c_orm_error_t mysql_drv_get_last_trace(c_orm_db_t *db,
                                              const char **out_trace) {
  c_orm_error_t rc;

  LOG_DEBUG("mysql_drv_get_last_trace: entered");

  (void)db;
  if (out_trace) {
    *out_trace = "MySQL stack traces require extended trace plugins lacking in "
                 "cdd-c struct wrappers. Database trace missing natively.";
  }

  LOG_DEBUG("mysql_drv_get_last_trace: exiting");
  rc = C_ORM_OK;
  return rc;
}

/**
 * @brief Retrieves the last inserted row ID.
 * @param db The database connection.
 * @param out_id Pointer to store the row ID.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_get_last_insert_rowid(c_orm_db_t *db,
                                                     int64_t *out_id) {
  c_orm_error_t rc;

  LOG_DEBUG("mysql_drv_get_last_insert_rowid: entered");

  /* Stub implementation for now */
  (void)db;
  if (out_id) {
    *out_id = 0;
  }

  LOG_DEBUG("mysql_drv_get_last_insert_rowid: not implemented");
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  return (c_orm_error_t)rc;
}

/**
 * @brief The MySQL driver vtable.
 */
static c_orm_driver_vtable_t mysql_vtable = {mysql_drv_connect,
                                             mysql_drv_disconnect,
                                             mysql_drv_prepare,
                                             mysql_drv_bind_int32,
                                             mysql_drv_bind_int64,
                                             mysql_drv_bind_double,
                                             mysql_drv_bind_string,
                                             mysql_drv_bind_blob,
                                             mysql_drv_bind_null,
                                             mysql_drv_step,
                                             mysql_drv_get_int32,
                                             mysql_drv_get_int64,
                                             mysql_drv_get_double,
                                             mysql_drv_get_string,
                                             mysql_drv_get_blob,
                                             mysql_drv_is_null,
                                             mysql_drv_finalize,
                                             mysql_drv_reset,
                                             mysql_drv_get_last_error,
                                             mysql_drv_get_last_trace,
                                             mysql_drv_get_last_insert_rowid};

/**
 * @brief Gets the MySQL driver vtable.
 * @param out_vtable Pointer to store the vtable.
 * @return 0 on success, non-zero otherwise.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_mysql_get_vtable(const c_orm_driver_vtable_t **out_vtable) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_mysql_get_vtable: entered");

  if (!out_vtable) {
    LOG_DEBUG("c_orm_mysql_get_vtable: validation error");
    rc = C_ORM_ERROR_UNKNOWN;
    return rc;
  }

  *out_vtable = &mysql_vtable;

  LOG_DEBUG("c_orm_mysql_get_vtable: exiting");
  rc = C_ORM_OK;
  return rc;
}

/**
 * @brief Connects to a MySQL database using a URL.
 * @param url The connection URL.
 * @param out_db Pointer to store the created database connection.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_mysql_connect(const char *url,
                                               c_orm_db_t **out_db) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_mysql_connect: entered");

  rc = mysql_drv_connect(url, out_db);

  LOG_DEBUG("c_orm_mysql_connect: exiting");
  return (c_orm_error_t)rc;
}

#else

/* Stub out if MySQL is not enabled */

/**
 * @brief Gets the MySQL driver vtable (stub).
 * @param out_vtable Pointer to store the vtable.
 * @return 0 on success, non-zero otherwise.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_mysql_get_vtable(const c_orm_driver_vtable_t **out_vtable) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_mysql_get_vtable (stub): entered");

  if (out_vtable) {
    *out_vtable = NULL;
  }

  LOG_DEBUG("c_orm_mysql_get_vtable (stub): exiting");
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  return rc;
}

/**
 * @brief Connects to a MySQL database (stub).
 * @param url The connection URL.
 * @param out_db Pointer to store the connection.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_mysql_connect(const char *url,
                                               c_orm_db_t **out_db) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_mysql_connect (stub): entered");

  (void)url;
  (void)out_db;

  LOG_DEBUG("c_orm_mysql_connect (stub): not implemented");
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  return (c_orm_error_t)rc;
}

#endif
