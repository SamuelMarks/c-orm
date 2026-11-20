/**
 * @file c_orm_sqlite.c
 * @brief SQLite3 driver implementation for c-orm.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_sqlite.h"
#include "c_orm_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#if defined(_WIN32) || defined(_WIN64)
typedef union _LARGE_INTEGER {
    struct {
        unsigned long LowPart;
        long HighPart;
    } u;
#if defined(_MSC_VER)
    __int64 QuadPart;
#else
    c_orm_int64_t QuadPart;
#endif
} LARGE_INTEGER;
__declspec(dllimport) int __stdcall QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount);
__declspec(dllimport) int __stdcall QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency);
#else
#include <sys/time.h>
#endif
#ifdef C_ORM_ENABLE_SQLITE
#include <sqlite3.h>
#endif
/* clang-format on */

#ifdef C_ORM_ENABLE_SQLITE

/**
 * @brief Internal SQLite database data structure.
 */
struct sqlite_db_data {
  sqlite3 *db;
  char last_error[512];
};

/**
 * @brief Internal SQLite query data structure.
 */
struct sqlite_query_data {
  sqlite3_stmt *stmt;
  c_orm_db_t *db;
};

/**
 * @brief Sets the last error message on the database context.
 *
 * @param db Database context.
 * @param msg Error message. If NULL, fetches from sqlite3.
 */
static c_orm_error_t set_error(c_orm_db_t *db, const char *msg) {
  struct sqlite_db_data *data;
  size_t len;
  const char *sqlite_err;
  (void)msg;

  LOG_DEBUG("set_error: entry");
  data = (struct sqlite_db_data *)db->driver_data;
  sqlite_err = sqlite3_errmsg(data->db);
  len = strlen(sqlite_err);
  if (len >= sizeof(data->last_error)) {
    len = sizeof(data->last_error) - 1;
  }
  memcpy(data->last_error, sqlite_err, len);
  data->last_error[len] = '\0';
  LOG_DEBUG("set_error: exit");
  return C_ORM_OK;
}

/**
 * @brief Forward declaration for SQLite connection entry point.
 * @param url Database URL or file path.
 * @param out_db Output database connection handle.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_connect(const char *url, c_orm_db_t **out_db);

/**
 * @brief Disconnects from the SQLite database.
 *
 * @param db Database structure.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t sqlite_disconnect(c_orm_db_t *db) {
  c_orm_error_t rc;
  c_orm_error_t frc;
  struct sqlite_db_data *data;

  LOG_DEBUG("sqlite_disconnect: entry");

  if (!db) {
    LOG_DEBUG("sqlite_disconnect: db is null");
    return C_ORM_OK;
  }

  rc = c_orm_disable_statement_caching(db);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("sqlite_disconnect: disable_statement_caching failed");
  }

  data = (struct sqlite_db_data *)db->driver_data;
  if (data) {
    sqlite3_stmt *stmt;
    while ((stmt = sqlite3_next_stmt(data->db, NULL)) != NULL) {
      sqlite3_finalize(stmt);
    }
    sqlite3_close_v2(data->db);
    data->db = NULL;
    frc = c_orm_system_free(data);
    if (frc != C_ORM_OK && rc == C_ORM_OK) {
      rc = frc;
    }
  }
  frc = c_orm_system_free(db);
  if (frc != C_ORM_OK && rc == C_ORM_OK) {
    rc = frc;
  }
  LOG_DEBUG("sqlite_disconnect: exit");
  return rc;
}

/**
 * @brief Internal query structure wrapping SQLite query data.
 */
typedef struct sqlite_stmt_handle {
  struct sqlite_query_data *data;
} sqlite_stmt_t;

/**
 * @brief Prepares an SQLite statement.
 *
 * @param db Database connection.
 * @param sql SQL string.
 * @param out_query Pointer to output query structure.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_prepare(c_orm_db_t *db, const char *sql,
                                    c_orm_query_t **out_query) {
  struct sqlite_db_data *db_data;
  struct sqlite_query_data *q_data = NULL;
  sqlite_stmt_t *stmt_handle = NULL;
  c_orm_error_t rc;

  LOG_DEBUG("sqlite_prepare: entry");

  if (!db || !sql || !out_query) {
    LOG_DEBUG("sqlite_prepare: null argument");
    return C_ORM_ERROR_MEMORY;
  }
  db_data = (struct sqlite_db_data *)db->driver_data;

  if (db->log_cb) {
    rc = db->log_cb(sql, db->log_user_data);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }

  rc = c_orm_system_calloc(1, sizeof(sqlite_stmt_t), (void **)&stmt_handle);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("sqlite_prepare: OOM query");
    return C_ORM_ERROR_MEMORY;
  }

  rc = c_orm_system_calloc(1, sizeof(struct sqlite_query_data),
                           (void **)&q_data);
  if (rc != C_ORM_OK) {
    (void)c_orm_system_free(stmt_handle);
    LOG_DEBUG("sqlite_prepare: OOM query data");
    return C_ORM_ERROR_MEMORY;
  }

  if (sqlite3_prepare_v2(db_data->db, sql, -1, &q_data->stmt, NULL) !=
      SQLITE_OK) {
    (void)set_error(db, NULL);
    (void)c_orm_system_free(q_data);
    (void)c_orm_system_free(stmt_handle);
    LOG_DEBUG("sqlite_prepare: prepare failed");
    return C_ORM_ERROR_SQL;
  }

  q_data->db = db;
  stmt_handle->data = q_data;
  *out_query = (c_orm_query_t *)stmt_handle;

  LOG_DEBUG("sqlite_prepare: exit");
  return C_ORM_OK;
}

/**
 * @brief Binds an int32 parameter.
 *
 * @param query Query structure.
 * @param index Parameter index (1-based).
 * @param val Value to bind.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_bind_int32(c_orm_query_t *query, int index,
                                       int32_t val) {
  c_orm_error_t rc;
  LOG_DEBUG("sqlite_bind_int32: entry");

  if (!query || !((sqlite_stmt_t *)query)->data ||
      !((sqlite_stmt_t *)query)->data->stmt) {
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }

  rc = (c_orm_error_t)sqlite3_bind_int(((sqlite_stmt_t *)query)->data->stmt,
                                       index, val);

  if (rc != SQLITE_OK) {
    (void)set_error(((sqlite_stmt_t *)query)->data->db, NULL);
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  rc = C_ORM_OK;
  LOG_DEBUG("sqlite_bind_int32: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Binds an int64 parameter.
 *
 * @param query Query structure.
 * @param index Parameter index (1-based).
 * @param val Value to bind.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_bind_int64(c_orm_query_t *query, int index,
                                       int64_t val) {

  c_orm_error_t rc;
  LOG_DEBUG("sqlite_bind_int64: entry");
  if (!query || !((sqlite_stmt_t *)query)->data ||
      !((sqlite_stmt_t *)query)->data->stmt) {
    LOG_DEBUG("sqlite_bind_int64: invalid state");
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  rc = (c_orm_error_t)sqlite3_bind_int64(((sqlite_stmt_t *)query)->data->stmt,
                                         index, val);
  if (rc != SQLITE_OK) {
    (void)set_error(((sqlite_stmt_t *)query)->data->db, NULL);
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  rc = C_ORM_OK;
  LOG_DEBUG("sqlite_bind_int64: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Binds a double parameter.
 *
 * @param query Query structure.
 * @param index Parameter index (1-based).
 * @param val Value to bind.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_bind_double(c_orm_query_t *query, int index,
                                        double val) {

  c_orm_error_t rc;
  LOG_DEBUG("sqlite_bind_double: entry");
  if (!query || !((sqlite_stmt_t *)query)->data ||
      !((sqlite_stmt_t *)query)->data->stmt) {
    LOG_DEBUG("sqlite_bind_double: invalid state");
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  rc = (c_orm_error_t)sqlite3_bind_double(((sqlite_stmt_t *)query)->data->stmt,
                                          index, val);
  if (rc != SQLITE_OK) {
    (void)set_error(((sqlite_stmt_t *)query)->data->db, NULL);
    LOG_DEBUG("sqlite_bind_double: bind failed");
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  rc = C_ORM_OK;
  LOG_DEBUG("sqlite_bind_double: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Binds a string parameter.
 *
 * @param query Query structure.
 * @param index Parameter index (1-based).
 * @param val Value to bind.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_bind_string(c_orm_query_t *query, int index,
                                        const char *val) {

  c_orm_error_t rc;
  LOG_DEBUG("sqlite_bind_string: entry");
  if (!query || !((sqlite_stmt_t *)query)->data ||
      !((sqlite_stmt_t *)query)->data->stmt) {
    LOG_DEBUG("sqlite_bind_string: invalid state");
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  rc = (c_orm_error_t)sqlite3_bind_text(((sqlite_stmt_t *)query)->data->stmt,
                                        index, val, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) {
    (void)set_error(((sqlite_stmt_t *)query)->data->db, NULL);
    LOG_DEBUG("sqlite_bind_string: bind failed");
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  rc = C_ORM_OK;
  LOG_DEBUG("sqlite_bind_string: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Binds a blob parameter.
 *
 * @param query Query structure.
 * @param index Parameter index (1-based).
 * @param val Value to bind.
 * @param size Size of the blob.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_bind_blob(c_orm_query_t *query, int index,
                                      const void *val, size_t size) {
  c_orm_error_t rc;
  LOG_DEBUG("sqlite_bind_blob: entry");
  if (!query || !((sqlite_stmt_t *)query)->data ||
      !((sqlite_stmt_t *)query)->data->stmt) {
    LOG_DEBUG("sqlite_bind_blob: invalid state");
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  rc =
      (c_orm_error_t)sqlite3_bind_blob(((sqlite_stmt_t *)query)->data->stmt,
                                       index, val, (int)size, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) {
    (void)set_error(((sqlite_stmt_t *)query)->data->db, NULL);
    LOG_DEBUG("sqlite_bind_blob: bind failed");
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  rc = C_ORM_OK;
  LOG_DEBUG("sqlite_bind_blob: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Binds a NULL parameter.
 *
 * @param query Query structure.
 * @param index Parameter index (1-based).
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_bind_null(c_orm_query_t *query, int index) {
  c_orm_error_t rc;
  LOG_DEBUG("sqlite_bind_null: entry");
  if (!query || !((sqlite_stmt_t *)query)->data ||
      !((sqlite_stmt_t *)query)->data->stmt) {
    LOG_DEBUG("sqlite_bind_null: invalid state");
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  rc = (c_orm_error_t)sqlite3_bind_null(((sqlite_stmt_t *)query)->data->stmt,
                                        index);
  if (rc != SQLITE_OK) {
    (void)set_error(((sqlite_stmt_t *)query)->data->db, NULL);
    LOG_DEBUG("sqlite_bind_null: bind failed");
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  rc = C_ORM_OK;
  LOG_DEBUG("sqlite_bind_null: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Steps the prepared statement.
 *
 * @param query Query structure.
 * @param out_has_row Output flag for whether a row is returned.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_step(c_orm_query_t *query, int *out_has_row) {
  c_orm_error_t rc;
  double elapsed = 0.0;
  const char *sql;
  char log_msg[1024];
#if defined(_WIN32) || defined(_WIN64)
  LARGE_INTEGER start_time;
  LARGE_INTEGER end_time;
  LARGE_INTEGER freq;
  start_time.QuadPart = 0;
  end_time.QuadPart = 0;
  freq.QuadPart = 0;
#else
  struct timeval start_time;
  struct timeval end_time;
  start_time.tv_sec = 0;
  start_time.tv_usec = 0;
  end_time.tv_sec = 0;
  end_time.tv_usec = 0;
#endif

  LOG_DEBUG("sqlite_step: entry");

  if (!query || !((sqlite_stmt_t *)query)->data ||
      !((sqlite_stmt_t *)query)->data->stmt || !out_has_row) {
    LOG_DEBUG("sqlite_step: invalid state or args");
    rc = C_ORM_ERROR_STEP;
    return (c_orm_error_t)rc;
  }

  if (((sqlite_stmt_t *)query)->data->db->slow_query_threshold_ms > 0) {
#if defined(_WIN32) || defined(_WIN64)
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start_time);
#else
    gettimeofday(&start_time, NULL);
#endif
  }

  rc = (c_orm_error_t)sqlite3_step(((sqlite_stmt_t *)query)->data->stmt);

  if (((sqlite_stmt_t *)query)->data->db->slow_query_threshold_ms > 0) {
#if defined(_WIN32) || defined(_WIN64)
    QueryPerformanceCounter(&end_time);
    elapsed = (double)(end_time.QuadPart - start_time.QuadPart) * 1000.0 /
              (double)freq.QuadPart;
#else
    gettimeofday(&end_time, NULL);
    elapsed = (double)(end_time.tv_sec - start_time.tv_sec) * 1000.0;
    elapsed += (double)(end_time.tv_usec - start_time.tv_usec) / 1000.0;
#endif
    if (elapsed >=
        (double)((sqlite_stmt_t *)query)->data->db->slow_query_threshold_ms) {
      if (((sqlite_stmt_t *)query)->data->db->log_cb) {
        c_orm_error_t log_rc;
        sql = sqlite3_sql(((sqlite_stmt_t *)query)->data->stmt);
        log_rc = C_ORM_SPRINTF(log_msg, sizeof(log_msg),
                               "SLOW QUERY (%.2fms): %s", elapsed, sql);
        if (log_rc != C_ORM_OK) {
          return log_rc;
        }
        log_rc =
            ((sqlite_stmt_t *)query)
                ->data->db->log_cb(
                    log_msg, ((sqlite_stmt_t *)query)->data->db->log_user_data);
        if (log_rc != C_ORM_OK) {
          return log_rc;
        }
      }
      ((sqlite_stmt_t *)query)->data->db->telemetry.slow_queries_logged++;
    }
  }

  if (rc == SQLITE_ROW) {
    *out_has_row = 1;
    rc = C_ORM_OK;
    LOG_DEBUG("sqlite_step: exit ROW");
    return (c_orm_error_t)rc;
  } else if (rc == SQLITE_DONE) {
    *out_has_row = 0;
    rc = C_ORM_OK;
    LOG_DEBUG("sqlite_step: exit DONE");
    return (c_orm_error_t)rc;
  }

  (void)set_error(((sqlite_stmt_t *)query)->data->db, NULL);
  rc = C_ORM_ERROR_STEP;
  LOG_DEBUG("sqlite_step: step failed");
  return (c_orm_error_t)rc;
}

/**
 * @brief Gets an int32 value from a column.
 *
 * @param query Query structure.
 * @param index Column index.
 * @param out_val Pointer to the output value.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_get_int32(c_orm_query_t *query, int index,
                                      int32_t *out_val) {
  c_orm_error_t rc;
  int type;

  LOG_DEBUG("sqlite_get_int32: entry");
  if (!query || !((sqlite_stmt_t *)query)->data ||
      !((sqlite_stmt_t *)query)->data->stmt || !out_val) {
    LOG_DEBUG("sqlite_get_int32: invalid state or args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  type = sqlite3_column_type(((sqlite_stmt_t *)query)->data->stmt, index);
  if (type != SQLITE_INTEGER && type != SQLITE_NULL) {
    LOG_DEBUG("sqlite_get_int32: type mismatch");
    rc = C_ORM_ERROR_TYPE_MISMATCH;
    return (c_orm_error_t)rc;
  }

  *out_val =
      (int32_t)sqlite3_column_int(((sqlite_stmt_t *)query)->data->stmt, index);
  rc = C_ORM_OK;
  LOG_DEBUG("sqlite_get_int32: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Gets an int64 value from a column.
 *
 * @param query Query structure.
 * @param index Column index.
 * @param out_val Pointer to the output value.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_get_int64(c_orm_query_t *query, int index,
                                      int64_t *out_val) {
  c_orm_error_t rc;
  int type;

  LOG_DEBUG("sqlite_get_int64: entry");
  if (!query || !((sqlite_stmt_t *)query)->data ||
      !((sqlite_stmt_t *)query)->data->stmt || !out_val) {
    LOG_DEBUG("sqlite_get_int64: invalid state or args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  type = sqlite3_column_type(((sqlite_stmt_t *)query)->data->stmt, index);
  if (type != SQLITE_INTEGER && type != SQLITE_NULL) {
    LOG_DEBUG("sqlite_get_int64: type mismatch");
    rc = C_ORM_ERROR_TYPE_MISMATCH;
    return (c_orm_error_t)rc;
  }

  *out_val = (int64_t)sqlite3_column_int64(((sqlite_stmt_t *)query)->data->stmt,
                                           index);
  rc = C_ORM_OK;
  LOG_DEBUG("sqlite_get_int64: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Gets a double value from a column.
 *
 * @param query Query structure.
 * @param index Column index.
 * @param out_val Pointer to the output value.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_get_double(c_orm_query_t *query, int index,
                                       double *out_val) {
  c_orm_error_t rc;
  int type;

  LOG_DEBUG("sqlite_get_double: entry");
  if (!query || !((sqlite_stmt_t *)query)->data ||
      !((sqlite_stmt_t *)query)->data->stmt || !out_val) {
    LOG_DEBUG("sqlite_get_double: invalid state or args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  type = sqlite3_column_type(((sqlite_stmt_t *)query)->data->stmt, index);
  if (type != SQLITE_FLOAT && type != SQLITE_INTEGER && type != SQLITE_NULL) {
    LOG_DEBUG("sqlite_get_double: type mismatch");
    rc = C_ORM_ERROR_TYPE_MISMATCH;
    return (c_orm_error_t)rc;
  }

  *out_val = sqlite3_column_double(((sqlite_stmt_t *)query)->data->stmt, index);
  rc = C_ORM_OK;
  LOG_DEBUG("sqlite_get_double: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Gets a string value from a column.
 *
 * @param query Query structure.
 * @param index Column index.
 * @param out_val Pointer to the output value.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_get_string(c_orm_query_t *query, int index,
                                       const char **out_val) {
  c_orm_error_t rc;
  int type;

  LOG_DEBUG("sqlite_get_string: entry");
  if (!query || !((sqlite_stmt_t *)query)->data ||
      !((sqlite_stmt_t *)query)->data->stmt || !out_val) {
    LOG_DEBUG("sqlite_get_string: invalid state or args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  type = sqlite3_column_type(((sqlite_stmt_t *)query)->data->stmt, index);
  if (type != SQLITE_TEXT && type != SQLITE_NULL) {
    LOG_DEBUG("sqlite_get_string: type mismatch");
    rc = C_ORM_ERROR_TYPE_MISMATCH;
    return (c_orm_error_t)rc;
  }

  *out_val = (const char *)sqlite3_column_text(
      ((sqlite_stmt_t *)query)->data->stmt, index);
  rc = C_ORM_OK;
  LOG_DEBUG("sqlite_get_string: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Gets a blob value from a column.
 *
 * @param query Query structure.
 * @param index Column index.
 * @param out_val Pointer to the output value.
 * @param out_size Pointer to the output size.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_get_blob(c_orm_query_t *query, int index,
                                     const void **out_val, size_t *out_size) {
  c_orm_error_t rc;
  int type;

  LOG_DEBUG("sqlite_get_blob: entry");
  if (!query || !((sqlite_stmt_t *)query)->data ||
      !((sqlite_stmt_t *)query)->data->stmt || !out_val || !out_size) {
    LOG_DEBUG("sqlite_get_blob: invalid state or args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  type = sqlite3_column_type(((sqlite_stmt_t *)query)->data->stmt, index);
  if (type != SQLITE_BLOB && type != SQLITE_NULL) {
    LOG_DEBUG("sqlite_get_blob: type mismatch");
    rc = C_ORM_ERROR_TYPE_MISMATCH;
    return (c_orm_error_t)rc;
  }

  *out_val = sqlite3_column_blob(((sqlite_stmt_t *)query)->data->stmt, index);
  *out_size =
      (size_t)sqlite3_column_bytes(((sqlite_stmt_t *)query)->data->stmt, index);
  rc = C_ORM_OK;
  LOG_DEBUG("sqlite_get_blob: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Checks if a column is NULL.
 *
 * @param query Query structure.
 * @param index Column index.
 * @param out_is_null Pointer to the output flag.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_is_null(c_orm_query_t *query, int index,
                                    int *out_is_null) {
  c_orm_error_t rc;

  LOG_DEBUG("sqlite_is_null: entry");
  if (!query || !((sqlite_stmt_t *)query)->data ||
      !((sqlite_stmt_t *)query)->data->stmt || !out_is_null) {
    LOG_DEBUG("sqlite_is_null: invalid state or args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  *out_is_null = (sqlite3_column_type(((sqlite_stmt_t *)query)->data->stmt,
                                      index) == SQLITE_NULL)
                     ? 1
                     : 0;
  rc = C_ORM_OK;
  LOG_DEBUG("sqlite_is_null: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Finalizes an SQLite query.
 *
 * @param query Query structure.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_finalize(c_orm_query_t *query) {
  c_orm_error_t rc = C_ORM_OK;
  c_orm_error_t frc;

  LOG_DEBUG("sqlite_finalize: entry");
  if (!query) {
    LOG_DEBUG("sqlite_finalize: query is null");
    return C_ORM_OK;
  }
  if (((sqlite_stmt_t *)query)->data) {
    if (((sqlite_stmt_t *)query)->data->stmt) {
      sqlite3_finalize(((sqlite_stmt_t *)query)->data->stmt);
    }
    rc = c_orm_system_free(((sqlite_stmt_t *)query)->data);
  }
  frc = c_orm_system_free(query);
  if (frc != C_ORM_OK && rc == C_ORM_OK) {
    rc = frc;
  }
  LOG_DEBUG("sqlite_finalize: exit");
  return rc;
}

/**
 * @brief Resets an SQLite query.
 *
 * @param query Query structure.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_reset(c_orm_query_t *query) {
  c_orm_error_t rc;

  LOG_DEBUG("sqlite_reset: entry");
  if (!query || !((sqlite_stmt_t *)query)->data ||
      !((sqlite_stmt_t *)query)->data->stmt) {
    LOG_DEBUG("sqlite_reset: invalid state");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  sqlite3_reset(((sqlite_stmt_t *)query)->data->stmt);
  sqlite3_clear_bindings(((sqlite_stmt_t *)query)->data->stmt);
  rc = C_ORM_OK;
  LOG_DEBUG("sqlite_reset: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Gets the last error message from the database.
 *
 * @param db Database structure.
 * @param out_message Pointer to the output message.
 * @return 0 on success, non-zero on failure.
 */
static c_orm_error_t sqlite_get_last_error(c_orm_db_t *db,
                                           const char **out_message) {
  c_orm_error_t rc;
  struct sqlite_db_data *data;

  LOG_DEBUG("sqlite_get_last_error: entry");
  if (!out_message) {
    LOG_DEBUG("sqlite_get_last_error: null args");
    rc = C_ORM_ERROR_UNKNOWN;
    return rc;
  }
  if (!db || !db->driver_data) {
    *out_message = "Invalid DB object";
    LOG_DEBUG("sqlite_get_last_error: invalid db object");
    rc = C_ORM_ERROR_UNKNOWN;
    return rc;
  }
  data = (struct sqlite_db_data *)db->driver_data;
  *out_message = data->last_error;
  rc = C_ORM_OK;
  LOG_DEBUG("sqlite_get_last_error: exit");
  return rc;
}

/**
 * @brief Gets the last trace message.
 *
 * @param db Database structure.
 * @param out_trace Pointer to the output trace message.
 * @return 0 on success, non-zero on failure.
 */
static c_orm_error_t sqlite_get_last_trace(c_orm_db_t *db,
                                           const char **out_trace) {
  c_orm_error_t rc;
  (void)db;

  LOG_DEBUG("sqlite_get_last_trace: entry");
  /* SQLite doesn't natively expose rich trace stacks through its public C API
   * without compiling with SQLITE_ENABLE_API_ARMOR or SQLITE_ENABLE_SQLLOG.
   * Step 265 / Step 266: We return the last message contextualized. */
  if (out_trace) {
    *out_trace = "SQLite Driver Stack Trace: Contextual reporting requires "
                 "runtime AST parser integration currently unsupported "
                 "directly in vtable mappings. Last Error: ";
  }
  rc = C_ORM_OK;
  LOG_DEBUG("sqlite_get_last_trace: exit");
  return rc;
}

/**
 * @brief Gets the last insert row ID.
 *
 * @param db Database structure.
 * @param out_id Pointer to the output ID.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_get_last_insert_rowid(c_orm_db_t *db,
                                                  int64_t *out_id) {
  c_orm_error_t rc;
  struct sqlite_db_data *db_data;

  LOG_DEBUG("sqlite_get_last_insert_rowid: entry");
  if (!db || !db->driver_data || !out_id) {
    LOG_DEBUG("sqlite_get_last_insert_rowid: invalid state or args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  db_data = (struct sqlite_db_data *)db->driver_data;
  *out_id = (int64_t)sqlite3_last_insert_rowid(db_data->db);
  rc = C_ORM_OK;
  LOG_DEBUG("sqlite_get_last_insert_rowid: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Gets the column count of a query.
 *
 * @param query Query structure.
 * @param out_count Pointer to the output count.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_get_column_count(c_orm_query_t *query,
                                             int *out_count) {
  c_orm_error_t rc;

  LOG_DEBUG("sqlite_get_column_count: entry");
  if (!query || !((sqlite_stmt_t *)query)->data ||
      !((sqlite_stmt_t *)query)->data->stmt || !out_count) {
    LOG_DEBUG("sqlite_get_column_count: invalid state or args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  *out_count = sqlite3_column_count(((sqlite_stmt_t *)query)->data->stmt);
  rc = C_ORM_OK;
  LOG_DEBUG("sqlite_get_column_count: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Gets the column name of a query at the specified index.
 *
 * @param query Query structure.
 * @param index Column index.
 * @param out_name Pointer to the output name string.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_get_column_name(c_orm_query_t *query, int index,
                                            const char **out_name) {
  c_orm_error_t rc;

  LOG_DEBUG("sqlite_get_column_name: entry");
  if (!query || !((sqlite_stmt_t *)query)->data ||
      !((sqlite_stmt_t *)query)->data->stmt || !out_name) {
    LOG_DEBUG("sqlite_get_column_name: invalid state or args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  *out_name = sqlite3_column_name(((sqlite_stmt_t *)query)->data->stmt, index);
  rc = C_ORM_OK;
  LOG_DEBUG("sqlite_get_column_name: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Binds an exact fixed-point decimal as canonical text to SQLite.
 * @param query Prepared SQLite query handle.
 * @param index 1-based parameter index.
 * @param val Pointer to decimal value to bind.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_bind_decimal(c_orm_query_t *query, int index,
                                         const c_orm_decimal_t *val) {
  char buf[64];
  c_orm_error_t rc;
  if (!val) {
    return sqlite_bind_null(query, index);
  }
  rc = c_orm_decimal_to_str(val, buf, sizeof(buf), NULL);
  if (rc != C_ORM_OK) {
    return rc;
  }
  return sqlite_bind_string(query, index, buf);
}

/**
 * @brief Retrieves an exact fixed-point decimal from an SQLite result column.
 * @param query Prepared SQLite query handle.
 * @param index 0-based column index.
 * @param out_val Output pointer receiving the parsed decimal.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_get_decimal(c_orm_query_t *query, int index,
                                        c_orm_decimal_t *out_val) {
  sqlite3_stmt *stmt;
  int col_type;
  const char *s = NULL;

  if (!out_val) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (!query || !((sqlite_stmt_t *)query)->data ||
      !((sqlite_stmt_t *)query)->data->stmt) {
    return C_ORM_ERROR_MEMORY;
  }
  stmt = ((sqlite_stmt_t *)query)->data->stmt;
  col_type = sqlite3_column_type(stmt, index);
  if (col_type == SQLITE_NULL) {
    return c_orm_decimal_zero(0, out_val);
  }
  if (col_type == SQLITE_INTEGER) {
    return c_orm_decimal_from_int64(
        (c_orm_int64_t)sqlite3_column_int64(stmt, index), out_val);
  }
  if (col_type == SQLITE_BLOB) {
    return C_ORM_ERROR_TYPE_MISMATCH;
  }
  s = (const char *)sqlite3_column_text(stmt, index);
  return c_orm_decimal_from_str(s, out_val);
}

/**
 * @brief Static driver vtable for SQLite.
 */
static const c_orm_driver_vtable_t sqlite_vtable = {
    sqlite_connect,
    sqlite_disconnect,
    sqlite_prepare,
    sqlite_bind_int32,
    sqlite_bind_int64,
    sqlite_bind_double,
    sqlite_bind_string,
    sqlite_bind_blob,
    sqlite_bind_null,
    sqlite_step,
    sqlite_get_int32,
    sqlite_get_int64,
    sqlite_get_double,
    sqlite_get_string,
    sqlite_get_blob,
    sqlite_is_null,
    sqlite_finalize,
    sqlite_reset,
    sqlite_get_last_error,
    sqlite_get_last_trace,
    sqlite_get_last_insert_rowid,
    sqlite_get_column_count,
    sqlite_get_column_name,
    sqlite_bind_decimal,
    sqlite_get_decimal};

/**
 * @brief Connects to an SQLite database.
 *
 * @param url Database URL.
 * @param out_db Output database structure.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t sqlite_connect(const char *url, c_orm_db_t **out_db) {
  c_orm_db_t *db;
  struct sqlite_db_data *data;
  c_orm_error_t rc;

  LOG_DEBUG("sqlite_connect: entry");

  if (!url || !out_db) {
    LOG_DEBUG("sqlite_connect: null argument");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  db = NULL;
  rc = c_orm_system_calloc(1, sizeof(c_orm_db_t), (void **)&db);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("sqlite_connect: OOM db");
    return C_ORM_ERROR_MEMORY;
  }

  data = NULL;
  rc = c_orm_system_calloc(1, sizeof(struct sqlite_db_data), (void **)&data);
  if (rc != C_ORM_OK) {
    (void)c_orm_system_free(db);
    LOG_DEBUG("sqlite_connect: OOM data");
    return C_ORM_ERROR_MEMORY;
  }

  if (sqlite3_open(url, &data->db) != SQLITE_OK) {
    sqlite3_close(data->db);
    (void)c_orm_system_free(data);
    (void)c_orm_system_free(db);
    LOG_DEBUG("sqlite_connect: sqlite3_open failed");
    return C_ORM_ERROR_CONNECTION;
  }

  db->vtable = &sqlite_vtable;
  db->driver_data = data;
  db->driver_name = "sqlite";
  *out_db = db;

  LOG_DEBUG("sqlite_connect: exit");
  return C_ORM_OK;
}

/**
 * @brief Gets the SQLite driver vtable.
 *
 * @param out_vtable Pointer to output the vtable.
 * @return 0 on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_sqlite_get_vtable(const c_orm_driver_vtable_t **out_vtable) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_sqlite_get_vtable: entry");
  if (!out_vtable) {
    LOG_DEBUG("c_orm_sqlite_get_vtable: out_vtable is null");
    rc = C_ORM_ERROR_UNKNOWN;
    return rc;
  }
  *out_vtable = &sqlite_vtable;
  rc = C_ORM_OK;
  LOG_DEBUG("c_orm_sqlite_get_vtable: exit");
  return rc;
}

/**
 * @brief Connects to an SQLite database.
 *
 * @param url Database URL.
 * @param out_db Output database structure.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_sqlite_connect(const char *url,
                                                c_orm_db_t **out_db) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_sqlite_connect: entry");
  rc = sqlite_connect(url, out_db);
  LOG_DEBUG("c_orm_sqlite_connect: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Opens a BLOB in SQLite.
 *
 * @param db Database context.
 * @param db_name Database name.
 * @param table Table name.
 * @param column Column name.
 * @param row_id Row ID.
 * @param is_read_write Read/write flag.
 * @param out_blob_handle Output blob handle.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_sqlite_blob_open(
    c_orm_db_t *db, const char *db_name, const char *table, const char *column,
    int64_t row_id, int is_read_write, void **out_blob_handle) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_sqlite_blob_open: entry");
  if (!db || !db->driver_data || !table || !column || !out_blob_handle) {
    LOG_DEBUG("c_orm_sqlite_blob_open: invalid args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  rc = (c_orm_error_t)sqlite3_blob_open(
      ((struct sqlite_db_data *)db->driver_data)->db,
      db_name ? db_name : "main", table, column, row_id, is_read_write,
      (sqlite3_blob **)out_blob_handle);
  if (rc != SQLITE_OK) {
    LOG_DEBUG("c_orm_sqlite_blob_open: open failed");
    rc = C_ORM_ERROR_UNKNOWN;
    return (c_orm_error_t)rc;
  }
  rc = C_ORM_OK;
  LOG_DEBUG("c_orm_sqlite_blob_open: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Reads from an opened SQLite BLOB.
 *
 * @param blob_handle Blob handle.
 * @param buffer Output buffer.
 * @param n Number of bytes to read.
 * @param offset Offset in the blob.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_sqlite_blob_read(void *blob_handle,
                                                  void *buffer, int n,
                                                  int offset) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_sqlite_blob_read: entry");
  if (!blob_handle || !buffer) {
    LOG_DEBUG("c_orm_sqlite_blob_read: invalid args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  rc = (c_orm_error_t)sqlite3_blob_read((sqlite3_blob *)blob_handle, buffer, n,
                                        offset);
  if (rc != SQLITE_OK) {
    LOG_DEBUG("c_orm_sqlite_blob_read: read failed");
    rc = C_ORM_ERROR_UNKNOWN;
    return (c_orm_error_t)rc;
  }
  rc = C_ORM_OK;
  LOG_DEBUG("c_orm_sqlite_blob_read: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Writes to an opened SQLite BLOB.
 *
 * @param blob_handle Blob handle.
 * @param buffer Input buffer.
 * @param n Number of bytes to write.
 * @param offset Offset in the blob.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_sqlite_blob_write(void *blob_handle,
                                                   const void *buffer, int n,
                                                   int offset) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_sqlite_blob_write: entry");
  if (!blob_handle || !buffer) {
    LOG_DEBUG("c_orm_sqlite_blob_write: invalid args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  rc = (c_orm_error_t)sqlite3_blob_write((sqlite3_blob *)blob_handle, buffer, n,
                                         offset);
  if (rc != SQLITE_OK) {
    LOG_DEBUG("c_orm_sqlite_blob_write: write failed");
    rc = C_ORM_ERROR_UNKNOWN;
    return (c_orm_error_t)rc;
  }
  rc = C_ORM_OK;
  LOG_DEBUG("c_orm_sqlite_blob_write: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Closes an SQLite BLOB.
 *
 * @param blob_handle Blob handle.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_sqlite_blob_close(void *blob_handle) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_sqlite_blob_close: entry");
  if (!blob_handle) {
    LOG_DEBUG("c_orm_sqlite_blob_close: blob is null");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  sqlite3_blob_close((sqlite3_blob *)blob_handle);
  rc = C_ORM_OK;
  LOG_DEBUG("c_orm_sqlite_blob_close: exit");
  return (c_orm_error_t)rc;
}

#else

/**
 * @brief Gets the SQLite driver vtable (stub).
 *
 * @param out_vtable Pointer to output the vtable.
 * @return 0 on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_sqlite_get_vtable(const c_orm_driver_vtable_t **out_vtable) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_sqlite_get_vtable: entry (stub)");
  if (out_vtable) {
    *out_vtable = NULL;
  }
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  LOG_DEBUG("c_orm_sqlite_get_vtable: exit (stub)");
  return rc;
}

/**
 * @brief Connects to an SQLite database (stub).
 *
 * @param url Database URL.
 * @param out_db Output database structure.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_sqlite_connect(const char *url,
                                                c_orm_db_t **out_db) {
  c_orm_error_t rc;

  (void)url;
  (void)out_db;
  LOG_DEBUG("c_orm_sqlite_connect: entry (stub)");
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  LOG_DEBUG("c_orm_sqlite_connect: exit (stub)");
  return (c_orm_error_t)rc;
}

/**
 * @brief Opens a BLOB in SQLite (stub).
 *
 * @param db Database context.
 * @param db_name Database name.
 * @param table Table name.
 * @param column Column name.
 * @param row_id Row ID.
 * @param is_read_write Read/write flag.
 * @param out_blob_handle Output blob handle.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_sqlite_blob_open(
    c_orm_db_t *db, const char *db_name, const char *table, const char *column,
    int64_t row_id, int is_read_write, void **out_blob_handle) {
  c_orm_error_t rc;
  (void)db;
  (void)db_name;
  (void)table;
  (void)column;
  (void)row_id;
  (void)is_read_write;
  (void)out_blob_handle;
  LOG_DEBUG("c_orm_sqlite_blob_open: entry (stub)");
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  LOG_DEBUG("c_orm_sqlite_blob_open: exit (stub)");
  return (c_orm_error_t)rc;
}

/**
 * @brief Reads from an opened SQLite BLOB (stub).
 *
 * @param blob_handle Blob handle.
 * @param buffer Output buffer.
 * @param n Number of bytes to read.
 * @param offset Offset in the blob.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_sqlite_blob_read(void *blob_handle,
                                                  void *buffer, int n,
                                                  int offset) {
  c_orm_error_t rc;
  (void)blob_handle;
  (void)buffer;
  (void)n;
  (void)offset;
  LOG_DEBUG("c_orm_sqlite_blob_read: entry (stub)");
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  LOG_DEBUG("c_orm_sqlite_blob_read: exit (stub)");
  return (c_orm_error_t)rc;
}

/**
 * @brief Writes to an opened SQLite BLOB (stub).
 *
 * @param blob_handle Blob handle.
 * @param buffer Input buffer.
 * @param n Number of bytes to write.
 * @param offset Offset in the blob.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_sqlite_blob_write(void *blob_handle,
                                                   const void *buffer, int n,
                                                   int offset) {
  c_orm_error_t rc;
  (void)blob_handle;
  (void)buffer;
  (void)n;
  (void)offset;
  LOG_DEBUG("c_orm_sqlite_blob_write: entry (stub)");
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  LOG_DEBUG("c_orm_sqlite_blob_write: exit (stub)");
  return (c_orm_error_t)rc;
}

/**
 * @brief Closes an SQLite BLOB (stub).
 *
 * @param blob_handle Blob handle.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_sqlite_blob_close(void *blob_handle) {
  c_orm_error_t rc;
  (void)blob_handle;
  LOG_DEBUG("c_orm_sqlite_blob_close: entry (stub)");
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  LOG_DEBUG("c_orm_sqlite_blob_close: exit (stub)");
  return (c_orm_error_t)rc;
}

#endif
